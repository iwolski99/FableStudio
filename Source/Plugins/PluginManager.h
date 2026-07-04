#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Common.h"

namespace fable
{

// VST3 discovery and instantiation.
//
// - Scans the platform's default VST3 folders plus any user-added folders,
//   on a background thread.
// - Discovery is out-of-process: each candidate plugin is probed by launching
//   this same executable as a child process (see Source/Main.cpp's
//   "--scan-plugin" mode). A crashing or hanging plugin only takes down that
//   throwaway child - the running DAW keeps going - and the offending file is
//   blacklisted so it's skipped on future scans.
// - The discovered plugin list and settings persist in the user app-data dir.

class PluginManager : private juce::Thread
{
public:
    PluginManager();
    ~PluginManager() override;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownPlugins;

    // Default folders for this OS plus user-added ones (persisted).
    juce::FileSearchPath getSearchPath() const;
    juce::StringArray    getUserFolders() const   { return userFolders; }
    void                 setUserFolders (const juce::StringArray& folders);

    void  startScan();
    void  stopScan();
    void  scanSynchronously();   // blocking, in-process scan (headless tools / CI only)
    bool  isScanning() const        { return isThreadRunning(); }
    float getScanProgress() const   { return scanProgress.load(); }
    juce::String getCurrentlyScannedPlugin() const;

    std::function<void()> onScanFinished;   // called on the message thread
    std::function<void()> onListChanged;    // called on the message thread

    // Synchronous, message-thread instantiation.
    std::unique_ptr<juce::AudioPluginInstance>
        createInstance (const juce::PluginDescription& desc,
                        double sampleRate, int blockSize, juce::String& errorOut);

    // Look up a description by KnownPluginList identifier string.
    std::optional<juce::PluginDescription> findByIdentifier (const juce::String& identifier) const;

    juce::Array<juce::PluginDescription> getInstruments() const;
    juce::Array<juce::PluginDescription> getEffects() const;

    // Entry point for the "--scan-plugin <file> --out <resultFile>" child process
    // mode (invoked from Main.cpp before any window/audio device is created).
    // Probes one plugin file in this process and writes descriptions as XML.
    static void runScanChildProcess (const juce::File& pluginFile, const juce::File& outFile);

private:
    void run() override;
    void saveList();
    void loadList();
    void loadBlacklist();
    void saveBlacklist();
    juce::Array<juce::File> findCandidateFiles() const;
    bool probeOneFile (const juce::File& file);   // false if crashed/timed out/blacklisted

    juce::File settingsFile()  const { return getAppDataDir().getChildFile ("plugins.xml"); }
    juce::File blacklistFile() const { return getAppDataDir().getChildFile ("plugin-blacklist.txt"); }

    juce::StringArray userFolders;
    juce::StringArray blacklistedFiles;
    std::atomic<float> scanProgress { 0.0f };
    juce::CriticalSection scanNameLock;
    juce::String currentScanName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};

} // namespace fable
