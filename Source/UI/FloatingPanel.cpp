#include "FloatingPanel.h"

namespace fable
{

FloatingPanel::FloatingPanel (const juce::String& titleToUse,
                              std::unique_ptr<juce::Component> contentToUse,
                              bool resizable)
    : title (titleToUse), content (std::move (contentToUse))
{
    addAndMakeVisible (*content);

    closeButton.setColour (juce::TextButton::buttonColourId, colours::titlebar);
    closeButton.onClick = [this]
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    };
    addAndMakeVisible (closeButton);

    constrainer.setMinimumSize (240, 120);
    if (resizable)
    {
        resizer = std::make_unique<juce::ResizableBorderComponent> (this, &constrainer);
        addAndMakeVisible (*resizer);
        resizer->setBorderThickness (juce::BorderSize<int> (0, 0, 6, 6));
    }

    setOpaque (true);
}

void FloatingPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    auto titleArea = getLocalBounds().removeFromTop (kTitleHeight);
    g.setColour (colours::titlebar);
    g.fillRect (titleArea);

    g.setColour (colours::accent);
    g.fillRect (titleArea.removeFromLeft (4));

    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (title, titleArea.reduced (8, 0), juce::Justification::centredLeft);

    g.setColour (colours::outline);
    g.drawRect (getLocalBounds(), 1);
}

void FloatingPanel::resized()
{
    auto area = getLocalBounds();
    auto titleArea = area.removeFromTop (kTitleHeight);
    closeButton.setBounds (titleArea.removeFromRight (kTitleHeight).reduced (4));
    content->setBounds (area.reduced (1));
    if (resizer != nullptr)
        resizer->setBounds (getLocalBounds());
}

void FloatingPanel::mouseDown (const juce::MouseEvent& e)
{
    toFront (true);
    if (e.getPosition().y < kTitleHeight)
        dragger.startDraggingComponent (this, e);
}

void FloatingPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getMouseDownPosition().y < kTitleHeight)
        dragger.dragComponent (this, e, &constrainer);
}

void FloatingPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click title bar: expand to fill the workspace
    if (e.getPosition().y < kTitleHeight && getParentComponent() != nullptr)
        setBounds (getParentComponent()->getLocalBounds().reduced (8));
}

void FloatingPanel::toFrontAndShow()
{
    setVisible (true);
    toFront (true);
}

} // namespace fable
