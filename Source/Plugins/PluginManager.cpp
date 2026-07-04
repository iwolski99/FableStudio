#include "PluginManager.h"

namespace fable
{

PluginManager::PluginManager() : juce::Thread ("VST3 Scanner")
{
    formatManager.addFormat (new juce::VST3PluginFormat());
    loadList();

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

juce::String PluginManager::getCurrentlyScannedPlugin() const
{
    const juce::ScopedLock sl (scanNameLock);
    return currentScanName;
}

void PluginManager::run()
{
    auto* format = formatManager.getFormat (0);
    if (format == nullptr)
        return;

    // Plugins listed in the dead-man's file crashed a previous scan: skip them.
    juce::PluginDirectoryScanner scanner (knownPlugins, *format, getSearchPath(),
                                          true /*recursive*/, deadMansFile(),
                                          false /*allowAsync*/);

    juce::String pluginBeingScanned;
    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl (scanNameLock);
            currentScanName = scanner.getNextPluginFileThatWillBeScanned();
        }

        if (! scanner.scanNextFile (true /*dontRescanIfAlreadyInList*/, pluginBeingScanned))
            break;

        scanProgress.store (scanner.getProgress());
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

} // namespace fable
