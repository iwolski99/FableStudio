#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Top strip: play/stop, pattern/song switch, draggable BPM, position readout,
// master level + CPU meter.

class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar (AppContext& ctx);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // A label whose value drags vertically (like a DAW BPM display).
    class DragValue : public juce::Component
    {
    public:
        std::function<double()> getValue;
        std::function<void (double)> setValue;
        double step = 1.0, fineStep = 0.1;
        juce::String suffix;

        void paint (juce::Graphics& g) override
        {
            g.setColour (colours::panelDark);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
            g.setColour (colours::outline);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (colours::accent);
            g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
            if (getValue)
                g.drawText (juce::String (getValue(), getValue() == (int) getValue() ? 0 : 1) + suffix,
                            getLocalBounds(), juce::Justification::centred);
        }
        void mouseDown (const juce::MouseEvent&) override { startValue = getValue ? getValue() : 0.0; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const double delta = -e.getDistanceFromDragStartY()
                                 * (e.mods.isShiftDown() ? fineStep : step) * 0.25;
            if (setValue)
                setValue (startValue + delta);
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (onEditRequest)
                onEditRequest();
        }
        std::function<void()> onEditRequest;
    private:
        double startValue = 0.0;
    };

    AppContext& context;
    juce::TextButton playButton { juce::String::fromUTF8 ("\xe2\x96\xb6") },
                     stopButton { juce::String::fromUTF8 ("\xe2\x96\xa0") };
    juce::TextButton patButton { "PAT" }, songButton { "SONG" };
    DragValue bpmValue;
    juce::Label positionLabel;
    float displayedLevel[2] { 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace fable
