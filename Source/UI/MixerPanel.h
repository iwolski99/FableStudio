#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Mixer: master + insert strips (fader, pan, mute, solo, meters) and an
// effect-slot rack for the selected track (built-in effects or VST3 effects).

class MixerPanel : public juce::Component,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    explicit MixerPanel (AppContext& ctx);
    ~MixerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Strip;
    class SlotRack;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildStrips();

    AppContext& context;
    juce::Viewport stripViewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<Strip>> strips;
    std::unique_ptr<SlotRack> slotRack;

    friend class Strip;
    friend class SlotRack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};

} // namespace fable
