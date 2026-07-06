#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Piano Roll: draw/move/resize/delete notes for the selected channel in the
// selected pattern, with a keyboard column, snap grid and a velocity lane.

class PianoRollPanel : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit PianoRollPanel (AppContext& ctx);
    ~PianoRollPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class NoteGrid;
    class VelocityLane;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshHeader();
    bool isPitchInHighlightedScale (int pitch) const;
    bool isScaleActive() const;
    bool isRootPitch (int pitch) const;

    std::vector<Note>* currentNotes();
    Pattern* currentPattern() { return context.selectedPattern(); }

    AppContext& context;
    juce::ComboBox channelBox, snapBox, keyBox, scaleBox;
    juce::Label hintLabel;
    juce::Viewport viewport;
    std::unique_ptr<NoteGrid> grid;
    std::unique_ptr<VelocityLane> velocityLane;

    friend class NoteGrid;
    friend class VelocityLane;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
};

} // namespace fable
