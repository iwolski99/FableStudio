#include "Sequencer.h"
#include "SamplerSound.h"

namespace fable
{

std::shared_ptr<const PlaybackData> compilePlayback (const Project& p)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return compilePlayback (p, formats);
}

std::shared_ptr<const PlaybackData> compilePlayback (const Project& p,
                                                     juce::AudioFormatManager& formats)
{
    auto data = std::make_shared<PlaybackData>();

    // Swing delays every off-beat 16th by up to half a step.
    const int swingTicks = (p.swing * kTicksPerStep) / 200;

    for (auto& pat : p.patterns)
    {
        CompiledPattern cp;
        cp.lengthTicks = pat.lengthTicks();

        for (auto& [chId, chData] : pat.channelData)
        {
            const auto* channel = p.channelById (chId);
            if (channel == nullptr)
                continue;

            for (size_t i = 0; i < chData.steps.size(); ++i)
            {
                if (! chData.steps[i].on)
                    continue;
                SeqEvent e;
                e.tick = (int) i * kTicksPerStep + ((i % 2 == 1) ? swingTicks : 0);
                e.lengthTicks = kTicksPerStep - 20;    // slight gap so retriggers articulate
                e.channelId = chId;
                e.pitch     = channel->rootNote;
                e.velocity  = chData.steps[i].velocity;
                cp.events.push_back (e);
            }

            for (auto& n : chData.notes)
            {
                SeqEvent e;
                e.tick = n.startTick + (((n.startTick / kTicksPerStep) % 2 == 1
                                          && n.startTick % kTicksPerStep == 0) ? swingTicks : 0);
                e.lengthTicks = n.lengthTicks;
                e.channelId = chId;
                e.pitch     = n.pitch;
                e.velocity  = n.velocity;
                cp.events.push_back (e);
            }
        }

        std::sort (cp.events.begin(), cp.events.end(),
                   [] (const SeqEvent& a, const SeqEvent& b) { return a.tick < b.tick; });
        data->patterns.push_back (std::move (cp));
    }

    for (auto& c : p.clips)
    {
        if (c.patternIndex < 0 || c.patternIndex >= (int) data->patterns.size())
            continue;
        CompiledClip cc;
        cc.startTick    = c.startTick;
        cc.lengthTicks  = c.lengthTicks;
        cc.patternIndex = c.patternIndex;
        cc.offsetTicks  = c.offsetTicks;
        cc.muted        = c.muted;
        data->clips.push_back (cc);
    }

    std::sort (data->clips.begin(), data->clips.end(),
               [] (const CompiledClip& a, const CompiledClip& b) { return a.startTick < b.startTick; });

    for (auto& c : p.audioClips)
    {
        if (c.muted || c.filePath.isEmpty())
            continue;

        CompiledAudioClip clip;
        clip.name       = c.name;
        clip.filePath   = c.filePath;
        clip.gain       = c.gain;
        clip.mixerTrack = c.mixerTrack;
        clip.track      = c.track;
        clip.startTick  = c.startTick;
        clip.lengthTicks = c.lengthTicks;
        clip.sourceOffsetTicks = c.sourceOffsetTicks;
        clip.muted      = c.muted;
        clip.audio      = loadAudioClipFile (juce::File (c.filePath), formats, clip.sourceRate);
        if (clip.audio.getNumSamples() > 0)
            data->audioClips.push_back (std::move (clip));
    }

    std::sort (data->audioClips.begin(), data->audioClips.end(),
               [] (const CompiledAudioClip& a, const CompiledAudioClip& b) { return a.startTick < b.startTick; });

    int songLen = 0;
    for (auto& c : data->clips)
        songLen = juce::jmax (songLen, c.endTick());
    for (auto& c : data->audioClips)
        songLen = juce::jmax (songLen, c.endTick());
    data->songLengthTicks = juce::jmax (kTicksPerBar,
                                        ((songLen + kTicksPerBar - 1) / kTicksPerBar) * kTicksPerBar);
    return data;
}

// ---------------------------------------------------------------------------

void Sequencer::reset (double startTick)
{
    positionTicks = startTick;
    for (auto& o : pendingOffs)
        o.active = false;
}

void Sequencer::allNotesOff (MidiSink& sink)
{
    for (auto& o : pendingOffs)
    {
        if (o.active)
        {
            sink.addNoteOff (o.channelId, o.pitch, 0);
            o.active = false;
        }
    }
}

void Sequencer::scheduleOff (int channelId, int pitch, double samplesFromNow)
{
    for (auto& o : pendingOffs)
    {
        if (! o.active)
        {
            o = { channelId, pitch, samplesFromNow, true };
            return;
        }
    }
    // table full — extremely unlikely; drop oldest to avoid stuck growth
    pendingOffs[0] = { channelId, pitch, samplesFromNow, true };
}

double Sequencer::process (const PlaybackData& data, const Transport& t,
                           double sampleRate, int numSamples, MidiSink& sink)
{
    // 1) fire note-offs that fall inside this block
    for (auto& o : pendingOffs)
    {
        if (! o.active)
            continue;
        if (o.samplesRemaining < numSamples)
        {
            sink.addNoteOff (o.channelId, o.pitch, juce::jmax (0, (int) o.samplesRemaining));
            o.active = false;
        }
        else
        {
            o.samplesRemaining -= numSamples;
        }
    }

    const double ticksPerSample = t.bpm * kPPQ / (60.0 * sampleRate);
    const double blockTicks     = ticksPerSample * numSamples;

    const int loopLen = t.songMode
        ? data.songLengthTicks
        : (t.patternIndex >= 0 && t.patternIndex < (int) data.patterns.size()
               ? juce::jmax (1, data.patterns[(size_t) t.patternIndex].lengthTicks)
               : kTicksPerBar);

    if (positionTicks >= loopLen)
        positionTicks = std::fmod (positionTicks, (double) loopLen);

    double remaining      = blockTicks;
    double blockStartTick = positionTicks;

    while (remaining > 1.0e-9)
    {
        const double span = juce::jmin (remaining, (double) loopLen - positionTicks);
        emitRange (data, t, positionTicks, positionTicks + span, ticksPerSample, blockStartTick, sink);

        positionTicks += span;
        remaining     -= span;

        if (positionTicks >= loopLen - 1.0e-9)
        {
            positionTicks  = 0.0;
            // subsequent spans are later in the same block: track how far in we are
            blockStartTick -= loopLen;
        }
    }

    return positionTicks;
}

void Sequencer::emitRange (const PlaybackData& data, const Transport& t,
                           double fromTick, double toTick, double ticksPerSample,
                           double blockStartTick, MidiSink& sink)
{
    auto emit = [&] (const SeqEvent& e, double absTick)
    {
        if (absTick < fromTick || absTick >= toTick)
            return;
        const int offset = juce::jmax (0, (int) ((absTick - blockStartTick) / ticksPerSample));
        sink.addNoteOn (e.channelId, e.pitch, e.velocity, offset);
        scheduleOff (e.channelId, e.pitch,
                     (absTick - blockStartTick + e.lengthTicks) / ticksPerSample);
    };

    if (! t.songMode)
    {
        if (t.patternIndex < 0 || t.patternIndex >= (int) data.patterns.size())
            return;
        for (auto& e : data.patterns[(size_t) t.patternIndex].events)
            emit (e, (double) e.tick);
        return;
    }

    for (auto& clip : data.clips)
    {
        if (clip.muted)
            continue;
        if (clip.startTick >= toTick || clip.endTick() <= fromTick)
            continue;
        auto& pat = data.patterns[(size_t) clip.patternIndex];
        const juce::int64 loopLen = (juce::int64) juce::jmax (1, pat.lengthTicks);

        // The clip's content begins offsetTicks into the pattern's own loop (set
        // when a clip is created by slicing another one, so playback continues
        // seamlessly rather than restarting the pattern from its own tick 0).
        // patternOriginTick is where the pattern's tick-0 would fall on the
        // timeline if the loop were extended backwards to align with that offset.
        const juce::int64 patternOriginTick = (juce::int64) clip.startTick - clip.offsetTicks;

        juce::int64 k = 0;
        if ((juce::int64) fromTick > patternOriginTick)
            k = ((juce::int64) fromTick - patternOriginTick) / loopLen;
        k = juce::jmax ((juce::int64) 0, k - 1);   // safety margin for integer truncation

        for (; patternOriginTick + k * loopLen < clip.endTick(); ++k)
        {
            const juce::int64 repStart = patternOriginTick + k * loopLen;
            for (auto& e : pat.events)
            {
                const juce::int64 absTick = repStart + e.tick;
                if (absTick < clip.startTick)
                    continue;
                if (absTick >= clip.endTick())
                    break;   // events sorted: nothing later in this repeat fits either
                emit (e, (double) absTick);
            }
        }
    }
}

} // namespace fable
