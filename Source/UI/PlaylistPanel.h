#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Playlist: arrange pattern clips on tracks along a bar timeline.
// Left-click paints the selected pattern, drag moves, right edge resizes,
// right-click deletes. Playhead shown in song mode.

class PlaylistPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit PlaylistPanel (AppContext& ctx);
    ~PlaylistPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ClipArea;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    AppContext& context;
    juce::Label hintLabel;
    juce::ComboBox snapBox;
    juce::Viewport viewport;
    std::unique_ptr<ClipArea> clipArea;

    friend class ClipArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistPanel)
};

} // namespace fable
