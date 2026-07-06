#include "ComputerKeyboardPlayer.h"

namespace fable
{

// One octave, FL's classic bottom-row layout: Z X C V B N M are the white
// keys, S D _ G H J the interleaved black keys (no black key between E-F or
// B-C, matching a real keyboard). PageUp/PageDown shift octaves rather than
// extending the row further, so every letter here is otherwise unclaimed by
// any other shortcut in the app.
const std::array<ComputerKeyboardPlayer::KeyNote, 12> ComputerKeyboardPlayer::kNoteKeys { {
    { 'Z', 0 }, { 'S', 1 }, { 'X', 2 }, { 'D', 3 }, { 'C', 4 }, { 'V', 5 },
    { 'G', 6 }, { 'B', 7 }, { 'H', 8 }, { 'N', 9 }, { 'J', 10 }, { 'M', 11 },
} };

ComputerKeyboardPlayer::ComputerKeyboardPlayer (AppContext& ctx) : context (ctx)
{
    startTimerHz (100);
}

ComputerKeyboardPlayer::~ComputerKeyboardPlayer()
{
    releaseAll();
}

void ComputerKeyboardPlayer::setEnabled (bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;
    enabled = shouldBeEnabled;
    if (! enabled)
        releaseAll();
}

void ComputerKeyboardPlayer::releaseAll()
{
    for (size_t i = 0; i < held.size(); ++i)
    {
        if (held[i].pitch >= 0)
            context.engine.auditionNoteOff (held[i].channelId, held[i].pitch);
        held[i] = {};
        keyWasDown[i] = false;
    }
}

void ComputerKeyboardPlayer::timerCallback()
{
    // Never intercept keys while the app isn't the frontmost process, or
    // while the user is typing into a text field anywhere in the app
    // (renaming a channel/pattern, a file save dialog, a fade-time box) -
    // otherwise every "S" or "M" typed there would also fire a note.
    const bool textFieldFocused =
        dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;

    if (! enabled || context.selectedChannelId < 0
        || ! juce::Process::isForegroundProcess() || textFieldFocused)
    {
        releaseAll();
        return;
    }

    // Octave shift: edge-triggered so holding the key down doesn't repeat.
    const bool pgUp   = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::pageUpKey);
    const bool pgDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::pageDownKey);
    if (pgUp && ! pageUpWasDown)     octaveShift = juce::jlimit (-4, 4, octaveShift + 1);
    if (pgDown && ! pageDownWasDown) octaveShift = juce::jlimit (-4, 4, octaveShift - 1);
    pageUpWasDown = pgUp;
    pageDownWasDown = pgDown;

    for (size_t i = 0; i < kNoteKeys.size(); ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) kNoteKeys[i].key);
        if (down == keyWasDown[i])
            continue;
        keyWasDown[i] = down;

        if (down)
        {
            const int pitch = juce::jlimit (0, 127,
                kDefaultRootNote + octaveShift * 12 + kNoteKeys[i].semitoneOffset);
            held[i] = { context.selectedChannelId, pitch };
            context.engine.auditionNoteOn (held[i].channelId, pitch, 0.85f);
        }
        else if (held[i].pitch >= 0)
        {
            context.engine.auditionNoteOff (held[i].channelId, held[i].pitch);
            held[i] = {};
        }
    }
}

} // namespace fable
