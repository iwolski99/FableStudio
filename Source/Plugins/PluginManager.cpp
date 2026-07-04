#include "PluginManager.h"

namespace fable
{

namespace
{
    // How long a single plugin gets to answer a probe before we assume it's
    // hung and kill the child. Some plugins (samplers with big libraries) are
    // legitimately slow to load the first time, so this is generous.
    constexpr int kProbeTimeoutMs = 20000;

    void findVst3Candidates (const juce::File& dir, juce::Array<juce::File>& results, int depth)
    {
        if (depth > 8 || ! dir.isDirectory())
            return;

        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*",
                                                                 juce::File::findFilesAndDirectories))
        {
            auto f = entry.getFile();
            if (f.hasFileExtension ("vst3"))
            {
                results.add (f);
                continue;   // a .vst3 bundle's insides aren't further candidates
            }
            if (f.isDirectory())
                findVst3Candidates (f, results, depth + 1);
        }
    }
}

PluginManager::PluginManager() : juce::Thread ("VST3 Scanner")
{
    formatManager.addFormat (new juce::VST3PluginFormat());
    loadList();
    loadBlacklist();

    knownPlugins.addChangeListener (nullptr);   // no-op; changes handled via callbacks below
}

PluginManager::~PluginManager()
{
    stopScan();
}

juce::FileSearchPath PluginManager::getSearchPath() const
{
    juce::FileSearchPath path;

    // JUCE knows the platform default VST3 locations
    // (C:\Program Files\Common Files\VST3, /Library/Audio/Plug-Ins/VST3, ~/.vst3, ...)
    juce::VST3PluginFormat vst3;
    path.addPath (vst3.getDefaultLocationsToSearch());

    for (auto& f : userFolders)
        if (f.isNotEmpty())
            path.addIfNotAlreadyThere (juce::File (f));

    return path;
}

void PluginManager::setUserFolders (const juce::StringArray& folders)
{
    userFolders = folders;
    saveList();
}

void PluginManager::startScan()
{
    if (isThreadRunning())
        return;
    scanProgress.store (0.0f);
    startThread();
}

void PluginManager::stopScan()
{
    signalThreadShouldExit();
    stopThread (5000);
}

void PluginManager::scanSynchronously()
{
    // Simple in-process scan used only by headless tools/CI, where the plugin
    // under test (our own FableTestTone) is known-safe. The real app always
    // goes through the out-of-process run() below.
    auto* format = formatManager.getFormat (0);
    if (format == nullptr)
        return;

    for (auto& file : findCandidateFiles())
    {
        if (knownPlugins.getTypeForFile (file.getFullPathName()) != nullptr)
            continue;
        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, file.getFullPathName());
        for (auto* d : found)
            knownPlugins.addType (*d);
    }
    saveList();
}

juce::String PluginManager::getCurrentlyScannedPlugin() const
{
    const juce::ScopedLock sl (scanNameLock);
    return currentScanName;
}

juce::Array<juce::File> PluginManager::findCandidateFiles() const
{
    juce::Array<juce::File> results;
    auto path = getSearchPath();
    for (int i = 0; i < path.getNumPaths(); ++i)
        findVst3Candidates (path[i], results, 0);
    return results;
}

bool PluginManager::probeOneFile (const juce::File& file)
{
    if (blacklistedFiles.contains (file.getFullPathName()))
        return false;

    const auto resultFile = getAppDataDir().getChildFile ("scan-result-"
        + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
    resultFile.deleteFile();

    juce::ChildProcess child;
    juce::StringArray args;
    args.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
    args.add ("--scan-plugin");
    args.add (file.getFullPathName());
    args.add ("--out");
    args.add (resultFile.getFullPathName());

    bool ok = false;
    if (child.start (args))
    {
        if (child.waitForProcessToFinish (kProbeTimeoutMs) && resultFile.existsAsFile())
        {
            if (auto xml = juce::parseXML (resultFile))
            {
                for (auto* d : xml->getChildIterator())
                {
                    juce::PluginDescription desc;
                    if (desc.loadFromXml (*d))
                    {
                        knownPlugins.addType (desc);
                        ok = true;
                    }
                }
            }
        }
        else
        {
            child.kill();   // hung - don't let it linger as a zombie
        }
    }

    resultFile.deleteFile();

    if (! ok)
    {
        blacklistedFiles.addIfNotAlreadyThere (file.getFullPathName());
        saveBlacklist();
    }
    return ok;
}

void PluginManager::run()
{
    const auto candidates = findCandidateFiles();
    const int total = juce::jmax (1, candidates.size());

    for (int i = 0; i < candidates.size(); ++i)
    {
        if (threadShouldExit())
            break;

        const auto& file = candidates.getReference (i);
        {
            const juce::ScopedLock sl (scanNameLock);
            currentScanName = file.getFullPathName();
        }

        // Skip files already known and unchanged, and previously-blacklisted ones.
        if (knownPlugins.getTypeForFile (file.getFullPathName()) == nullptr)
            probeOneFile (file);

        scanProgress.store ((float) (i + 1) / (float) total);
    }

    scanProgress.store (1.0f);
    {
        const juce::ScopedLock sl (scanNameLock);
        currentScanName.clear();
    }

    juce::MessageManager::callAsync ([this]
    {
        saveList();
        if (onListChanged)  onListChanged();
        if (onScanFinished) onScanFinished();
    });
}

void PluginManager::saveList()
{
    auto root = std::make_unique<juce::XmlElement> ("FABLE_PLUGINS");

    if (auto listXml = knownPlugins.createXml())
        root->addChildElement (listXml.release());

    auto* folders = root->createNewChildElement ("USER_FOLDERS");
    for (auto& f : userFolders)
        folders->createNewChildElement ("FOLDER")->setAttribute ("path", f);

    root->writeTo (settingsFile());
}

void PluginManager::loadList()
{
    auto xml = juce::parseXML (settingsFile());
    if (xml == nullptr)
        return;

    if (auto* listXml = xml->getChildByName ("KNOWNPLUGINS"))
        knownPlugins.recreateFromXml (*listXml);

    if (auto* folders = xml->getChildByName ("USER_FOLDERS"))
        for (auto* f : folders->getChildIterator())
            userFolders.add (f->getStringAttribute ("path"));
}

void PluginManager::loadBlacklist()
{
    auto file = blacklistFile();
    if (file.existsAsFile())
        blacklistedFiles = juce::StringArray::fromLines (file.loadFileAsString());
}

void PluginManager::saveBlacklist()
{
    blacklistFile().replaceWithText (blacklistedFiles.joinIntoString ("\n"));
}

std::unique_ptr<juce::AudioPluginInstance>
PluginManager::createInstance (const juce::PluginDescription& desc,
                               double sampleRate, int blockSize, juce::String& errorOut)
{
    return formatManager.createPluginInstance (desc, sampleRate, blockSize, errorOut);
}

std::optional<juce::PluginDescription>
PluginManager::findByIdentifier (const juce::String& identifier) const
{
    for (auto& d : knownPlugins.getTypes())
        if (d.createIdentifierString() == identifier)
            return d;
    return {};
}

juce::Array<juce::PluginDescription> PluginManager::getInstruments() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument)
            out.add (d);
    return out;
}

juce::Array<juce::PluginDescription> PluginManager::getEffects() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : knownPlugins.getTypes())
        if (! d.isInstrument)
            out.add (d);
    return out;
}

void PluginManager::runScanChildProcess (const juce::File& pluginFile, const juce::File& outFile)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, pluginFile.getFullPathName());

    auto root = std::make_unique<juce::XmlElement> ("SCAN_RESULT");
    for (auto* d : found)
        root->addChildElement (d->createXml().release());
    root->writeTo (outFile);
}

} // namespace fable
