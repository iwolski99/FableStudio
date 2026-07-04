#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fable
{

// Original dark theme in the spirit of pattern-based DAWs: near-black panels,
// warm orange accent, green LEDs.

namespace colours
{
    const juce::Colour workspace   { 0xff16181b };
    const juce::Colour panel       { 0xff2b2f34 };
    const juce::Colour panelDark   { 0xff222529 };
    const juce::Colour panelLight  { 0xff383d43 };
    const juce::Colour titlebar    { 0xff1d2023 };
    const juce::Colour outline     { 0xff0e0f11 };
    const juce::Colour text        { 0xffd6d9dc };
    const juce::Colour textDim     { 0xff8a9096 };
    const juce::Colour accent      { 0xffef9636 };   // warm orange
    const juce::Colour led         { 0xff9ae05a };   // green LED
    const juce::Colour playhead    { 0xffefe07a };
    const juce::Colour stepBeat    { 0xffc95f52 };   // step cell, on-beat group tint
    const juce::Colour stepOffBeat { 0xffe0a23f };
}

class FableLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FableLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

} // namespace fable
