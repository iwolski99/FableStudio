#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fable
{

// Buttons/sliders that forward right-clicks to an onRightClick callback instead
// of letting the base class swallow them. JUCE mouse events don't bubble to
// parent components, so without these a right-click on a child control (a
// channel's name button, a mixer effect slot, etc.) never reaches the row's own
// context-menu handler.

class RightClickButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
};

class RightClickSlider : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
};

} // namespace fable
