#include "FableLookAndFeel.h"

namespace fable
{

FableLookAndFeel::FableLookAndFeel()
{
    auto scheme = getDarkColourScheme();
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground, colours::panel);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground, colours::panelDark);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuBackground,   colours::panelDark);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::outline,          colours::outline);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText,      colours::text);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill,      colours::accent);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedText,  juce::Colours::white);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedFill,  colours::accent.withAlpha (0.6f));
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuText,         colours::text);
    setColourScheme (scheme);

    setColour (juce::ResizableWindow::backgroundColourId, colours::panel);
    setColour (juce::TextButton::buttonColourId,   colours::panelLight);
    setColour (juce::TextButton::buttonOnColourId, colours::accent.darker (0.2f));
    setColour (juce::TextButton::textColourOffId,  colours::text);
    setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, colours::panelDark);
    setColour (juce::ComboBox::outlineColourId,    colours::outline);
    setColour (juce::Label::textColourId,          colours::text);
    setColour (juce::Slider::backgroundColourId,   colours::panelDark);
    setColour (juce::Slider::trackColourId,        colours::accent.withAlpha (0.75f));
    setColour (juce::Slider::thumbColourId,        colours::text);
    setColour (juce::Slider::rotarySliderFillColourId,    colours::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colours::panelDark.darker (0.3f));
    setColour (juce::PopupMenu::backgroundColourId, colours::panelDark);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.35f));
    setColour (juce::ScrollBar::thumbColourId,     colours::panelLight);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelDark);
    setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    setColour (juce::TextEditor::backgroundColourId, colours::panelDark);
    setColour (juce::TextEditor::outlineColourId,    colours::outline);
    setColour (juce::ListBox::backgroundColourId,    colours::panelDark);
    setColour (juce::TreeView::backgroundColourId,   colours::panelDark);
}

void FableLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float arcThickness = juce::jmax (2.0f, radius * 0.18f);

    // body
    g.setColour (colours::panelDark.darker (0.2f));
    g.fillEllipse (centre.x - radius * 0.72f, centre.y - radius * 0.72f, radius * 1.44f, radius * 1.44f);
    g.setColour (colours::outline);
    g.drawEllipse (centre.x - radius * 0.72f, centre.y - radius * 0.72f, radius * 1.44f, radius * 1.44f, 1.0f);

    // value arc
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - arcThickness * 0.5f, radius - arcThickness * 0.5f,
                       0.0f, rotaryStartAngle, angle, true);
    g.setColour (slider.isEnabled() ? findColour (juce::Slider::rotarySliderFillColourId)
                                    : colours::textDim);
    g.strokePath (arc, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -radius * 0.68f, 3.0f, radius * 0.4f, 1.0f);
    g.setColour (colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void FableLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        // mixer-style fader
        const auto track = juce::Rectangle<float> ((float) x + (float) width * 0.5f - 2.0f, (float) y,
                                                   4.0f, (float) height);
        g.setColour (colours::panelDark.darker (0.35f));
        g.fillRoundedRectangle (track, 2.0f);

        g.setColour (findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (track.withTop (sliderPos), 2.0f);

        const float thumbH = 14.0f, thumbW = juce::jmin ((float) width - 2.0f, 26.0f);
        juce::Rectangle<float> thumb (0, 0, thumbW, thumbH);
        thumb.setCentre ((float) x + (float) width * 0.5f, sliderPos);
        g.setColour (colours::panelLight.brighter (0.15f));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (thumb, 3.0f, 1.0f);
        g.setColour (colours::accent);
        g.fillRect (thumb.withSizeKeepingCentre (thumbW - 6.0f, 2.0f));
        return;
    }

    juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                            minSliderPos, maxSliderPos, style, slider);
}

void FableLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;
    if (down)             colour = colour.darker (0.25f);
    else if (highlighted) colour = colour.brighter (0.12f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

juce::Font FableLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f));
}

juce::Font FableLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f));
}

} // namespace fable
