#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Common.h"

namespace fable
{

// VST3 discovery and instantiation.
//
// - Scans the platform's default VST3 folders plus any user-added folders,
//   on a background thread.
// - A "dead man's pedal" file records the plugin currently being probed; if a
//   plugin crashes the app during a scan, it is blacklisted on the next run.
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

private:
    void run() override;
    void saveList();
    void loadList();

    juce::File settingsFile()  const { return getAppDataDir().getChildFile ("plugins.xml"); }
    juce::File deadMansFile()  const { return getAppDataDir().getChildFile ("scan-in-progress.txt"); }

    juce::StringArray userFolders;
    std::atomic<float> scanProgress { 0.0f };
    juce::CriticalSection scanNameLock;
    juce::String currentScanName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};

} // namespace fable
