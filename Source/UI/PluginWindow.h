#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FableLookAndFeel.h"

namespace fable
{

// A floating native window hosting a VST3 editor (or a generic parameter
// editor when the plugin has no UI).

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& instance, const juce::String& title,
                  std::function<void (PluginWindow*)> onCloseToUse)
        : juce::DocumentWindow (title, colours::titlebar, juce::DocumentWindow::closeButton),
          plugin (instance), onClose (std::move (onCloseToUse))
    {
        // A JUCE-drawn title bar (not native) guarantees the window is draggable
        // by its title bar on every platform - native-title-bar plugin windows
        // could end up stuck/immovable depending on the window manager.
        setUsingNativeTitleBar (false);
        setTitleBarHeight (26);

        juce::AudioProcessorEditor* editor = plugin.hasEditor()
            ? plugin.createEditorIfNeeded()
            : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin);

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        // Keep at least the title bar on-screen so it can always be grabbed.
        setConstrainer (&constrainer);
        constrainer.setMinimumOnscreenAmounts (26, 48, 26, 48);
        centreWithSize (juce::jmax (300, editor->getWidth()),
                        juce::jmax (150, editor->getHeight()) + 26);
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose (this);   // owner deletes us
    }

    juce::AudioPluginInstance& getPlugin() const { return plugin; }

private:
    juce::AudioPluginInstance& plugin;
    std::function<void (PluginWindow*)> onClose;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace fable
