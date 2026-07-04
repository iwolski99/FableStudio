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
                      public juce::DragAndDropContainer,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void requestClose (std::function<void()> onConfirmedClose);

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    void timerCallback() override;

    void newProject();
    void openProject();
    void saveProject (bool saveAs, std::function<void (bool)> onComplete = {});
    void exportWav();
    void loadProjectFromFileAndSync (const juce::File& file);
    void openPluginEditor (juce::AudioPluginInstance* instance, const juce::String& title);
    void showStatus (const juce::String& message);
    void updateWindowTitle();

    // Bringing a panel to front should never be a two-click operation: if it's
    // already the frontmost visible panel, the button/shortcut hides it;
    // otherwise it shows (if hidden) and raises it, even if some other panel
    // is currently overlapping it.
    void togglePanel (FloatingPanel& panel);
    bool isPanelFrontmost (const FloatingPanel& panel) const;
    void updatePanelTabStates();

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

    // FL-style tab bar: one button per panel that always raises it to front,
    // rather than the old View-menu checkbox that just hid an already-open
    // panel if it happened to be behind another one.
    juce::TextButton playlistTabButton    { "Playlist" };
    juce::TextButton channelRackTabButton { "Channel Rack" };
    juce::TextButton pianoRollTabButton   { "Piano Roll" };
    juce::TextButton mixerTabButton       { "Mixer" };

    std::vector<std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String statusMessage;
    juce::uint32 statusMessageTime = 0;
    bool initialLayoutDone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace fable
