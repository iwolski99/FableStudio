#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <map>
#include <vector>

#include "../Common.h"

namespace fable
{

// ---------------------------------------------------------------------------
// Pure data model, owned by the message thread. The audio engine never touches
// these structs directly; it consumes compiled snapshots (see PlaybackData).
// ---------------------------------------------------------------------------

struct Note
{
    int    startTick  = 0;      // relative to pattern start
    int    lengthTicks = kTicksPerStep;
    int    pitch      = kDefaultRootNote;   // MIDI note number
    float  velocity   = 0.8f;   // 0..1

    bool operator== (const Note& o) const
    {
        return startTick == o.startTick && lengthTicks == o.lengthTicks
            && pitch == o.pitch && juce::approximatelyEqual (velocity, o.velocity);
    }
};

struct Step
{
    bool  on       = false;
    float velocity = 0.8f;
};

// Per-pattern note/step data for one channel.
struct ChannelPatternData
{
    std::vector<Step> steps;    // sized to pattern length in steps
    std::vector<Note> notes;    // piano-roll notes (independent of steps)

    bool isEmpty() const
    {
        if (! notes.empty()) return false;
        for (auto& s : steps) if (s.on) return false;
        return true;
    }
};

struct Pattern
{
    juce::String name;
    int lengthSteps = kStepsPerBar;             // step-grid length
    std::map<int, ChannelPatternData> channelData;  // key: channel id

    ChannelPatternData& dataFor (int channelId)
    {
        auto& d = channelData[channelId];
        if ((int) d.steps.size() != lengthSteps)
            d.steps.resize ((size_t) lengthSteps);
        return d;
    }

    // Total musical length in ticks (piano-roll notes may extend past the step grid).
    int lengthTicks() const
    {
        int len = lengthSteps * kTicksPerStep;
        for (auto& [id, d] : channelData)
            for (auto& n : d.notes)
                len = juce::jmax (len, n.startTick + n.lengthTicks);
        // round up to whole bars so looping stays musical
        return ((len + kTicksPerBar - 1) / kTicksPerBar) * kTicksPerBar;
    }
};

enum class GeneratorType { sampler, synth, plugin };

struct Channel
{
    int              id = 0;
    juce::String     name;
    GeneratorType    type = GeneratorType::sampler;
    juce::String     samplePath;        // sampler: file path ("builtin:kick" etc. for synthesized kits)
    juce::String     pluginIdentifier;  // plugin: KnownPluginList identifier string
    juce::MemoryBlock pluginState;      // plugin: saved state blob
    float            volume = 0.78f;    // 0..1 linear gain
    float            pan    = 0.0f;     // -1..1
    bool             muted  = false;
    int              mixerTrack = 1;    // 1..16 insert, 0 = master
    int              rootNote = kDefaultRootNote;
    float            sampleFadeInMs  = 0.0f;
    float            sampleFadeOutMs = 0.0f;
    juce::Colour     colour { 0xff5a8f5a };
};

struct PlaylistClip
{
    int patternIndex = 0;
    int track        = 0;   // playlist lane
    int startTick    = 0;   // absolute song position
    int lengthTicks  = kTicksPerBar;
    int offsetTicks  = 0;   // position within the pattern's own loop where this clip's content begins (slicing)
    bool muted       = false;

    int endTick() const { return startTick + lengthTicks; }
};

struct AudioClip
{
    juce::String filePath;
    juce::String name;
    float gain       = 1.0f;
    int mixerTrack  = 0;   // 0 = master, 1..16 = inserts
    int track       = 0;   // playlist lane
    int startTick   = 0;   // absolute song position
    int lengthTicks = kTicksPerBar;
    int sourceOffsetTicks = 0;   // ticks into the source file where playback begins (slicing)
    bool muted      = false;

    int endTick() const { return startTick + lengthTicks; }
};

enum class EffectType { none, reverb, delay, eq3, limiter, plugin };

struct EffectSlot
{
    EffectType        type = EffectType::none;
    juce::String      pluginIdentifier;
    juce::MemoryBlock pluginState;
    bool              enabled = true;
    // builtin effect parameters, meaning depends on type
    std::array<float, 8> params { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
};

struct MixerTrackModel
{
    juce::String name;
    float volume = 0.8f;
    float pan    = 0.0f;
    bool  muted  = false;
    bool  solo   = false;
    std::array<EffectSlot, kNumEffectSlots> slots;
};

struct Project
{
    juce::String name { "Untitled" };
    double bpm = 140.0;
    int    swing = 0;                       // 0..100 (%), applied to off-beat steps

    std::vector<Channel>  channels;
    std::vector<Pattern>  patterns;
    std::vector<PlaylistClip> clips;
    std::vector<AudioClip>    audioClips;
    std::array<MixerTrackModel, kNumMixerTracks> mixerTracks;

    int nextChannelId = 1;

    Project();

    Channel*       channelById (int id);
    const Channel* channelById (int id) const;
    int            indexOfChannel (int id) const;

    // Returns the new channel's id. (Not a reference: the vector reallocates.)
    int  addChannel (GeneratorType type, const juce::String& name);
    void removeChannel (int id);

    Pattern& addPattern();
    int      songLengthTicks() const;
};

// Creates the default startup project: synthesized drum kit + synth channel,
// a demo beat in pattern 1 and a short arrangement.
Project createDefaultProject();

} // namespace fable
