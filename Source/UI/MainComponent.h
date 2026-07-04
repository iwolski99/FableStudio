#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppContext.h"
#include "BrowserPanel.h"
#include "ChannelRackPanel.h"
#include "FloatingPanel.h"
#include "MixerPanel.h"
#include "PianoRollPanel.h"
#include "PlaylistPanel.h"
#include "PluginWindow.h"
#include "TransportBar.h"

namespace fable
{

// The whole application: menu bar + transport on top, browser dock on the
// left, and the floating Channel Rack / Piano Roll / Playlist / Mixer panels
// over the workspace.

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    void timerCallback() override;

    void newProject();
    void openProject();
    void saveProject (bool saveAs);
    void exportWav();
    void loadProjectFromFileAndSync (const juce::File& file);
    void openPluginEditor (juce::AudioPluginInstance* instance, const juce::String& title);
    void showStatus (const juce::String& message);
    void updateWindowTitle();

    AppContext context;
    FableLookAndFeel lookAndFeel;

    juce::MenuBarComponent menuBar;
    TransportBar transport { context };
    BrowserPanel browser { context };
    juce::Component workspace;

    FloatingPanel channelRackPanel { "Channel Rack", std::make_unique<ChannelRackPanel> (context) };
    FloatingPanel pianoRollPanel   { "Piano Roll",   std::make_unique<PianoRollPanel> (context) };
    FloatingPanel playlistPanel    { "Playlist",     std::make_unique<PlaylistPanel> (context) };
    FloatingPanel mixerPanel       { "Mixer",        std::make_unique<MixerPanel> (context) };

    std::vector<std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String statusMessage;
    juce::uint32 statusMessageTime = 0;
    bool initialLayoutDone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace fable
