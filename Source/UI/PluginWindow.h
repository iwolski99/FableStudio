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
        setUsingNativeTitleBar (true);

        juce::AudioProcessorEditor* editor = plugin.hasEditor()
            ? plugin.createEditorIfNeeded()
            : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin);

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (juce::jmax (300, editor->getWidth()),
                        juce::jmax (150, editor->getHeight()));
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace fable
