#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Playlist: arrange pattern clips and audio clips on tracks along a bar timeline.
// A toolbar selects between Draw (paint/move/resize), Paint (drag to fill
// consecutive cells), Slice (split a clip at the click point) and Mute
// (toggle a clip on/off without removing it) - FL Studio's classic playlist
// tool set. Right-click still deletes / opens the clip menu regardless of the
// active tool. Browser-dragged samples become audio clips.
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

    enum class Tool { draw, paint, slice, mute };

private:
    class ClipArea;
    class PatternRenameMouseListener;
    class WaveformCache;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshHeader();
    void setTool (Tool t);
    void refreshToolButtons();
    void renameSelectedPattern();
    void showPatternMenu();

    AppContext& context;
    juce::Label hintLabel;
    juce::ComboBox patternBox, snapBox;
    juce::TextButton drawToolButton { "Draw" }, paintToolButton { "Paint" },
                     sliceToolButton { "Slice" }, muteToolButton { "Mute" };
    juce::TextButton addPatternButton { "+" };
    std::unique_ptr<PatternRenameMouseListener> patternRenameListener;
    juce::Viewport viewport;
    std::unique_ptr<ClipArea> clipArea;
    std::unique_ptr<WaveformCache> waveformCache;
    Tool currentTool = Tool::draw;
    bool dragActive = false;
    juce::Point<int> dragPosition;

    friend class ClipArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistPanel)
};

} // namespace fable
