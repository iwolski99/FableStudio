#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "AppContext.h"
#include "FableLookAndFeel.h"
#include "../Engine/InstrumentParams.h"

namespace fable
{

// A generic, analog-style editor panel for the native instruments (FableSynth
// and the Kick designer). It builds a grid of labelled rotary knobs / choice
// boxes straight from a ParamSpec table, reads/writes the channel's synthParams
// map, pushes edits to the engine live, and saves/loads JSON presets.

class InstrumentEditor : public juce::Component
{
public:
    InstrumentEditor (AppContext& ctx, int channelId, juce::String title,
                      std::vector<ParamSpec> specs, juce::String presetSubdir);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::String getTitleText() const { return title; }

private:
    struct Control
    {
        ParamSpec spec;
        juce::Label name;
        std::unique_ptr<juce::Slider>   knob;
        std::unique_ptr<juce::ComboBox> combo;
    };

    Channel* channel() { return context.project.channelById (channelId); }
    void buildControls();
    void loadFromModel();
    void writeParam (const juce::String& key, float value);
    void savePreset();
    void loadPreset();
    void initPatch();

    AppContext& context;
    int channelId;
    juce::String title;
    std::vector<ParamSpec> specs;
    juce::String presetSubdir;

    std::vector<std::unique_ptr<Control>> controls;
    juce::TextButton initButton { "Init" }, saveButton { "Save preset" }, loadButton { "Load preset" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentEditor)
};

// Floating window that hosts an InstrumentEditor, mirroring PluginWindow's
// close handling so MainComponent can own/track it in a list.
class InstrumentEditorWindow : public juce::DocumentWindow
{
public:
    InstrumentEditorWindow (AppContext& ctx, int channelIdToUse, const juce::String& title,
                            std::vector<ParamSpec> specs, const juce::String& presetSubdir,
                            std::function<void (InstrumentEditorWindow*)> onCloseToUse)
        : juce::DocumentWindow (title, colours::titlebar, juce::DocumentWindow::closeButton),
          channelId (channelIdToUse), onClose (std::move (onCloseToUse))
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (26);
        setContentOwned (new InstrumentEditor (ctx, channelIdToUse, title, std::move (specs), presetSubdir), true);
        setResizable (false, false);
        constrainer.setMinimumOnscreenAmounts (26, 48, 26, 48);
        setConstrainer (&constrainer);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override { if (onClose) onClose (this); }

    int getChannelId() const { return channelId; }

private:
    int channelId;
    std::function<void (InstrumentEditorWindow*)> onClose;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentEditorWindow)
};

} // namespace fable
