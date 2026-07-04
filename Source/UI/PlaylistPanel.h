#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Playlist: arrange pattern clips and audio clips on tracks along a bar timeline.
// Left-click paints the selected pattern, drag moves, right edge resizes,
// right-click deletes, and browser-dragged samples become audio clips.
// Playhead shown in song mode.

class PlaylistPanel : public juce::Component,
                      public juce::DragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit PlaylistPanel (AppContext& ctx);
    ~PlaylistPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInDragSource (const SourceDetails& dragSourceDetails) override;
    void itemDragEnter (const SourceDetails& dragSourceDetails) override;
    void itemDragMove (const SourceDetails& dragSourceDetails) override;
    void itemDragExit (const SourceDetails& dragSourceDetails) override;
    void itemDropped (const SourceDetails& dragSourceDetails) override;

private:
    class ClipArea;
    class WaveformCache;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshHeader();

    AppContext& context;
    juce::Label hintLabel;
    juce::ComboBox patternBox, snapBox;
    juce::Viewport viewport;
    std::unique_ptr<ClipArea> clipArea;
    std::unique_ptr<WaveformCache> waveformCache;
    bool dragActive = false;
    juce::Point<int> dragPosition;

    friend class ClipArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistPanel)
};

} // namespace fable
