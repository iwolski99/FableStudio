#include "Project.h"

namespace fable
{

Project::Project()
{
    mixerTracks[0].name = "Master";
    for (int i = 1; i < kNumMixerTracks; ++i)
        mixerTracks[(size_t) i].name = "Insert " + juce::String (i);
}

Channel* Project::channelById (int id)
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

const Channel* Project::channelById (int id) const
{
    for (auto& c : channels)
        if (c.id == id)
            return &c;
    return nullptr;
}

int Project::indexOfChannel (int id) const
{
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].id == id)
            return (int) i;
    return -1;
}

int Project::addChannel (GeneratorType type, const juce::String& name)
{
    Channel c;
    c.id   = nextChannelId++;
    c.name = name;
    c.type = type;

    static const juce::Colour palette[] = {
        juce::Colour (0xff58a05e), juce::Colour (0xffc9803c), juce::Colour (0xff5f86c0),
        juce::Colour (0xffb05fb0), juce::Colour (0xffc0c05f), juce::Colour (0xff5fb0a8),
    };
    c.colour = palette[(size_t) ((c.id - 1) % (int) std::size (palette))];

    channels.push_back (std::move (c));
    return channels.back().id;
}

void Project::removeChannel (int id)
{
    channels.erase (std::remove_if (channels.begin(), channels.end(),
                                    [id] (const Channel& c) { return c.id == id; }),
                    channels.end());
    for (auto& p : patterns)
        p.channelData.erase (id);
}

Pattern& Project::addPattern()
{
    Pattern p;
    p.name = "Pattern " + juce::String ((int) patterns.size() + 1);
    patterns.push_back (std::move (p));
    return patterns.back();
}

int Project::songLengthTicks() const
{
    int len = 0;
    for (auto& c : clips)
        len = juce::jmax (len, c.endTick());
    for (auto& c : audioClips)
        len = juce::jmax (len, c.endTick());
    return len;
}

Project createDefaultProject()
{
    Project p;
    p.bpm = 140.0;

    const int kickId  = p.addChannel (GeneratorType::sampler, "Kick");
    const int clapId  = p.addChannel (GeneratorType::sampler, "Clap");
    const int hatId   = p.addChannel (GeneratorType::sampler, "Hat");
    const int snareId = p.addChannel (GeneratorType::sampler, "Snare");
    const int synthId = p.addChannel (GeneratorType::synth,   "FableSynth");

    p.channelById (kickId)->samplePath  = "builtin:kick";
    p.channelById (clapId)->samplePath  = "builtin:clap";
    p.channelById (hatId)->samplePath   = "builtin:hat";
    p.channelById (snareId)->samplePath = "builtin:snare";

    p.channelById (kickId)->mixerTrack  = 1;
    p.channelById (clapId)->mixerTrack  = 2;
    p.channelById (hatId)->mixerTrack   = 3;
    p.channelById (snareId)->mixerTrack = 4;
    p.channelById (synthId)->mixerTrack = 5;

    auto& pat = p.addPattern();

    // A simple, instantly-audible demo groove.
    auto& kickData = pat.dataFor (kickId);
    for (int i : { 0, 4, 8, 12 })          kickData.steps[(size_t) i].on = true;
    auto& clapData = pat.dataFor (clapId);
    for (int i : { 4, 12 })                clapData.steps[(size_t) i].on = true;
    auto& hatData = pat.dataFor (hatId);
    for (int i = 2; i < 16; i += 4)        hatData.steps[(size_t) i].on = true;

    // A little minor-key synth motif in the piano roll.
    auto& synthData = pat.dataFor (synthId);
    struct N { int step, len, pitch; };
    for (auto n : std::initializer_list<N> { { 0, 4, 57 }, { 4, 2, 60 }, { 6, 2, 64 },
                                             { 8, 4, 62 }, { 12, 4, 55 } })
        synthData.notes.push_back ({ n.step * kTicksPerStep, n.len * kTicksPerStep, n.pitch, 0.7f });

    // Give the arrangement two bars of the pattern.
    p.clips.push_back ({ 0, 0, 0,             pat.lengthTicks() });
    p.clips.push_back ({ 0, 0, kTicksPerBar,  pat.lengthTicks() });

    // Sensible default mixer effect: a touch of limiter on master.
    p.mixerTracks[0].slots[0].type = EffectType::limiter;

    return p;
}

Project createEmptyProject()
{
    Project p;
    p.bpm = 140.0;

    // Instruments ready to use, but nothing programmed.
    const int kickId  = p.addChannel (GeneratorType::sampler, "Kick");
    const int clapId  = p.addChannel (GeneratorType::sampler, "Clap");
    const int hatId   = p.addChannel (GeneratorType::sampler, "Hat");
    const int snareId = p.addChannel (GeneratorType::sampler, "Snare");
    const int synthId = p.addChannel (GeneratorType::synth,   "FableSynth");

    p.channelById (kickId)->samplePath  = "builtin:kick";
    p.channelById (clapId)->samplePath  = "builtin:clap";
    p.channelById (hatId)->samplePath   = "builtin:hat";
    p.channelById (snareId)->samplePath = "builtin:snare";

    p.channelById (kickId)->mixerTrack  = 1;
    p.channelById (clapId)->mixerTrack  = 2;
    p.channelById (hatId)->mixerTrack   = 3;
    p.channelById (snareId)->mixerTrack = 4;
    p.channelById (synthId)->mixerTrack = 5;

    // One empty pattern, empty arrangement, no default effects.
    p.addPattern();

    return p;
}

} // namespace fable
