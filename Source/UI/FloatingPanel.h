#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FableLookAndFeel.h"

namespace fable
{

// A draggable, resizable window that lives *inside* the workspace component —
// the classic pattern-DAW feel of Playlist/Channel Rack/Mixer floating over
// the desktop area.

class FloatingPanel : public juce::Component
{
public:
    FloatingPanel (const juce::String& title, std::unique_ptr<juce::Component> contentToUse,
                   bool resizable = true);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    juce::Component* getContent() const { return content.get(); }
    void toFrontAndShow();

    std::function<void()> onClosed;

    static constexpr int kTitleHeight = 26;

private:
    juce::String title;
    std::unique_ptr<juce::Component> content;
    std::unique_ptr<juce::ResizableBorderComponent> resizer;
    juce::ComponentBoundsConstrainer constrainer;
    juce::ComponentDragger dragger;
    juce::TextButton closeButton { "x" };
    juce::Rectangle<int> restoredBounds;
    bool maximized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingPanel)
};

} // namespace fable
