#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Model/Project.h"

namespace fable
{

// ---------------------------------------------------------------------------
// The model is compiled on the message thread into an immutable PlaybackData
// snapshot; the audio thread swaps snapshots atomically and never touches the
// model. Steps and piano-roll notes both become plain note events.
// ---------------------------------------------------------------------------

struct SeqEvent
{
    int   tick = 0;          // relative to pattern start
    int   lengthTicks = kTicksPerStep;
    int   channelId = 0;
    int   pitch = kDefaultRootNote;
    float velocity = 0.8f;
};

struct CompiledPattern
{
    int lengthTicks = kTicksPerBar;
    std::vector<SeqEvent> events;      // sorted by tick
};

struct CompiledClip
{
    int startTick = 0, lengthTicks = 0, patternIndex = 0;
    int endTick() const { return startTick + lengthTicks; }
};

struct CompiledAudioClip
{
    juce::String name, filePath;
    int startTick = 0, lengthTicks = 0, track = 0, mixerTrack = 0;
    double sourceRate = 44100.0;
    juce::AudioBuffer<float> audio;

    int endTick() const { return startTick + lengthTicks; }
};

struct PlaybackData
{
    std::vector<CompiledPattern> patterns;
    std::vector<CompiledClip>    clips;     // sorted by startTick
    std::vector<CompiledAudioClip> audioClips;
    int songLengthTicks = kTicksPerBar;     // rounded up to a whole bar
};

// Builds a snapshot from the project (applies swing, merges steps + notes).
std::shared_ptr<const PlaybackData> compilePlayback (const Project& p);
std::shared_ptr<const PlaybackData> compilePlayback (const Project& p,
                                                     juce::AudioFormatManager& formats);

// ---------------------------------------------------------------------------
// Audio-thread sequencer state. Emits MIDI into per-channel buffers with
// sample-accurate offsets and tracks pending note-offs across blocks.
// ---------------------------------------------------------------------------

class MidiSink
{
public:
    virtual ~MidiSink() = default;
    virtual void addNoteOn  (int channelId, int pitch, float velocity, int sampleOffset) = 0;
    virtual void addNoteOff (int channelId, int pitch, int sampleOffset) = 0;
};

class Sequencer
{
public:
    struct Transport
    {
        bool   songMode = false;
        int    patternIndex = 0;
        double bpm = 140.0;
    };

    void reset (double startTick = 0.0);

    // Advances by numSamples, emitting events. Returns the new position in ticks.
    double process (const PlaybackData& data, const Transport& t,
                    double sampleRate, int numSamples, MidiSink& sink);

    // Flush all held notes (on stop / snapshot change with fewer channels).
    void allNotesOff (MidiSink& sink);

    double getPositionTicks() const { return positionTicks; }
    void   setPositionTicks (double t) { positionTicks = t; }

private:
    void emitRange (const PlaybackData& data, const Transport& t,
                    double fromTick, double toTick, double ticksPerSample,
                    double blockStartTick, MidiSink& sink);
    void scheduleOff (int channelId, int pitch, double samplesFromNow);

    struct PendingOff
    {
        int channelId = 0, pitch = 0;
        double samplesRemaining = 0;
        bool active = false;
    };

    static constexpr int kMaxPendingOffs = 1024;
    std::array<PendingOff, kMaxPendingOffs> pendingOffs;
    double positionTicks = 0.0;
};

} // namespace fable
