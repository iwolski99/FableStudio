#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"

namespace fable
{

// FL-style "typing keyboard to piano keyboard": lets the PC keyboard play
// notes on the currently selected channel - the built-in synth/sampler or a
// hosted VST3 instrument - including while a plugin editor window has
// focus. That last part is why this polls physical key state on a Timer
// rather than hooking JUCE's focus-routed keyPressed: a hosted VST3 editor
// is a separate top-level window, so our own components never see key
// events for it, but the whole app (main window + plugin windows) is still
// one process, and isKeyCurrentlyDown()/isForegroundProcess() work across
// all of it.
class ComputerKeyboardPlayer : private juce::Timer
{
public:
    explicit ComputerKeyboardPlayer (AppContext& ctx);
    ~ComputerKeyboardPlayer() override;

    void setEnabled (bool shouldBeEnabled);
    bool isEnabled() const { return enabled; }

private:
    void timerCallback() override;
    void releaseAll();

    struct KeyNote { char key; int semitoneOffset; };
    static const std::array<KeyNote, 12> kNoteKeys;

    struct HeldNote { int channelId = -1; int pitch = -1; };

    AppContext& context;
    bool enabled = true;
    int octaveShift = 0;
    std::array<bool, 12> keyWasDown {};
    std::array<HeldNote, 12> held {};
    bool pageUpWasDown = false, pageDownWasDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComputerKeyboardPlayer)
};

} // namespace fable
