#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Settings: audio device selection (driver/device/rate/buffer) and VST3 folder
// management with a scan progress display.

class SettingsDialog : public juce::Component, private juce::Timer
{
public:
    explicit SettingsDialog (AppContext& ctx);
    ~SettingsDialog() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    static void show (AppContext& ctx);   // opens as a DialogWindow

private:
    void timerCallback() override;
    void refreshFolderList();

    AppContext& context;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // plugins tab content
    class PluginsTab;
    PluginsTab* pluginsTab = nullptr;   // owned by tabs

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};

} // namespace fable
