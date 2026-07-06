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

    // Latch whether the drag started on the title bar. We must NOT re-test the
    // mouse-down Y on every drag event: getMouseDownPosition() is relative to
    // the panel's *current* position, so as the panel moves up while dragging,
    // that Y drifts past kTitleHeight and the drag would cut out - which is
    // exactly why dragging a panel upward used to stop after a few pixels.
    draggingTitle = e.getPosition().y < kTitleHeight;
    if (draggingTitle)
        dragger.startDraggingComponent (this, e);
}

void FloatingPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingTitle)
        dragger.dragComponent (this, e, &constrainer);
}

void FloatingPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.getPosition().y >= kTitleHeight || getParentComponent() == nullptr)
        return;

    if (! maximized)
    {
        restoredBounds = getBounds();
        setBounds (getParentComponent()->getLocalBounds().reduced (8));
        maximized = true;
    }
    else
    {
        if (! restoredBounds.isEmpty())
            setBounds (restoredBounds);
        maximized = false;
    }
}

void FloatingPanel::toFrontAndShow()
{
    setVisible (true);
    toFront (true);
}

} // namespace fable
