#include <juce_core/juce_core.h>

#include "../Source/Engine/Sequencer.h"

namespace fable
{

namespace
{
    struct CapturedEvent
    {
        int channelId, pitch, sampleOffset;
        bool isOn;
        juce::int64 absoluteSample;
    };

    struct CaptureSink : public MidiSink
    {
        juce::int64 blockStart = 0;
        std::vector<CapturedEvent> events;

        void addNoteOn (int channelId, int pitch, float, int sampleOffset) override
        {
            events.push_back ({ channelId, pitch, sampleOffset, true, blockStart + sampleOffset });
        }
        void addNoteOff (int channelId, int pitch, int sampleOffset) override
        {
            events.push_back ({ channelId, pitch, sampleOffset, false, blockStart + sampleOffset });
        }
    };

    // Runs the sequencer for `totalSamples` in blocks of `blockSize`.
    std::vector<CapturedEvent> run (const PlaybackData& data, Sequencer::Transport t,
                                    double sampleRate, int blockSize, juce::int64 totalSamples)
    {
        Sequencer seq;
        seq.reset();
        CaptureSink sink;
        for (juce::int64 pos = 0; pos < totalSamples; pos += blockSize)
        {
            sink.blockStart = pos;
            seq.process (data, t, sampleRate, (int) juce::jmin ((juce::int64) blockSize, totalSamples - pos), sink);
        }
        return sink.events;
    }
}

class SequencerTests : public juce::UnitTest
{
public:
    SequencerTests() : juce::UnitTest ("Sequencer", "Fable") {}

    void runTest() override
    {
        beginTest ("compile merges steps and notes, sorted");
        {
            Project p;
            const int drumId  = p.addChannel (GeneratorType::sampler, "D");
            const int synthId = p.addChannel (GeneratorType::synth, "S");
            auto& pat = p.addPattern();
            pat.dataFor (drumId).steps[0].on = true;
            pat.dataFor (drumId).steps[8].on = true;
            pat.dataFor (synthId).notes.push_back ({ kTicksPerStep * 4, kTicksPerStep, 67, 0.9f });

            auto data = compilePlayback (p);
            expectEquals ((int) data->patterns.size(), 1);
            auto& events = data->patterns[0].events;
            expectEquals ((int) events.size(), 3);
            expect (std::is_sorted (events.begin(), events.end(),
                                    [] (auto& a, auto& b) { return a.tick < b.tick; }));
            expectEquals (events[1].pitch, 67);
        }

        beginTest ("sample-accurate step timing at several bpm/block sizes");
        {
            for (double bpm : { 60.0, 140.0, 174.0 })
            {
                for (int blockSize : { 64, 441, 512, 1024 })
                {
                    Project p;
                    const int cId = p.addChannel (GeneratorType::sampler, "D");
                    auto& pat = p.addPattern();
                    pat.dataFor (cId).steps[0].on = true;
                    pat.dataFor (cId).steps[4].on = true;   // beat 2

                    const double sr = 44100.0;
                    Sequencer::Transport t;
                    t.bpm = bpm;

                    // exactly one pattern (one bar): expect on-events at 0 and beat 2
                    const double samplesPerBar = 4.0 * 60.0 / bpm * sr;
                    auto events = run (*compilePlayback (p), t, sr, blockSize,
                                       (juce::int64) samplesPerBar - blockSize);   // stop before wrap

                    std::vector<juce::int64> ons;
                    for (auto& e : events)
                        if (e.isOn)
                            ons.push_back (e.absoluteSample);

                    expectEquals ((int) ons.size(), 2,
                                  "bpm=" + juce::String (bpm) + " block=" + juce::String (blockSize));
                    expect (std::abs ((double) ons[0]) <= 1.0);
                    expectWithinAbsoluteError ((double) ons[1], samplesPerBar / 4.0, (double) 2.0);
                }
            }
        }

        beginTest ("every note-on gets a matching note-off");
        {
            Project p;
            const int cId = p.addChannel (GeneratorType::synth, "S");
            auto& pat = p.addPattern();
            pat.dataFor (cId).notes.push_back ({ 0, kTicksPerStep * 2, 60, 0.5f });
            pat.dataFor (cId).notes.push_back ({ kTicksPerStep * 6, kTicksPerStep, 64, 0.5f });

            Sequencer::Transport t;
            const double sr = 48000.0;
            const double samplesPerBar = 4.0 * 60.0 / t.bpm * sr;
            auto events = run (*compilePlayback (p), t, sr, 256, (juce::int64) samplesPerBar);

            int ons = 0, offs = 0;
            for (auto& e : events)
                (e.isOn ? ons : offs)++;
            expectEquals (ons, 2);
            expectEquals (offs, 2);
        }

        beginTest ("pattern loops at its length");
        {
            Project p;
            const int cId = p.addChannel (GeneratorType::sampler, "D");
            auto& pat = p.addPattern();
            pat.dataFor (cId).steps[0].on = true;

            const double sr = 44100.0;
            Sequencer::Transport t;
            t.bpm = 120.0;
            const double samplesPerBar = 4.0 * 60.0 / t.bpm * sr;   // 88200

            auto events = run (*compilePlayback (p), t, sr, 512, (juce::int64) (samplesPerBar * 2.5));
            std::vector<juce::int64> ons;
            for (auto& e : events)
                if (e.isOn)
                    ons.push_back (e.absoluteSample);

            expectEquals ((int) ons.size(), 3);   // bars 1, 2, 3
            expectWithinAbsoluteError ((double) ons[1], samplesPerBar, 2.0);
            expectWithinAbsoluteError ((double) ons[2], samplesPerBar * 2, 3.0);
        }

        beginTest ("song mode places clips at their playlist position");
        {
            Project p;
            const int cId = p.addChannel (GeneratorType::sampler, "D");
            auto& pat = p.addPattern();
            pat.dataFor (cId).steps[0].on = true;

            // clip at bar 1 and bar 3
            p.clips.push_back ({ 0, 0, 0, kTicksPerBar });
            p.clips.push_back ({ 0, 1, kTicksPerBar * 2, kTicksPerBar });

            const double sr = 44100.0;
            Sequencer::Transport t;
            t.songMode = true;
            t.bpm = 120.0;
            const double samplesPerBar = 4.0 * 60.0 / t.bpm * sr;

            auto events = run (*compilePlayback (p), t, sr, 512, (juce::int64) (samplesPerBar * 3.0) - 512);
            std::vector<juce::int64> ons;
            for (auto& e : events)
                if (e.isOn)
                    ons.push_back (e.absoluteSample);

            expectEquals ((int) ons.size(), 2);
            expect (std::abs ((double) ons[0]) <= 1.0);
            expectWithinAbsoluteError ((double) ons[1], samplesPerBar * 2, 2.0);
        }

        beginTest ("clip repeats its pattern when longer than the pattern");
        {
            Project p;
            const int cId = p.addChannel (GeneratorType::sampler, "D");
            auto& pat = p.addPattern();
            pat.dataFor (cId).steps[0].on = true;
            p.clips.push_back ({ 0, 0, 0, kTicksPerBar * 3 });

            const double sr = 44100.0;
            Sequencer::Transport t;
            t.songMode = true;
            t.bpm = 120.0;
            const double samplesPerBar = 4.0 * 60.0 / t.bpm * sr;

            auto events = run (*compilePlayback (p), t, sr, 500, (juce::int64) (samplesPerBar * 3.0) - 500);
            int ons = 0;
            for (auto& e : events)
                if (e.isOn)
                    ++ons;
            expectEquals (ons, 3);
        }

        beginTest ("swing shifts off-beat steps late");
        {
            Project p;
            p.swing = 100;
            const int cId = p.addChannel (GeneratorType::sampler, "D");
            auto& pat = p.addPattern();
            pat.dataFor (cId).steps[0].on = true;
            pat.dataFor (cId).steps[1].on = true;

            auto data = compilePlayback (p);
            auto& events = data->patterns[0].events;
            expectEquals (events[0].tick, 0);
            expectEquals (events[1].tick, kTicksPerStep + kTicksPerStep / 2);
        }

        beginTest ("muted playlist clip is silent in song mode");
        {
            Project p;
            const int cId = p.addChannel (GeneratorType::sampler, "D");
            auto& pat = p.addPattern();
            pat.dataFor (cId).steps[0].on = true;
            PlaylistClip clip;
            clip.patternIndex = 0;
            clip.startTick = 0;
            clip.lengthTicks = kTicksPerBar;
            clip.muted = true;
            p.clips.push_back (clip);

            const double sr = 44100.0;
            Sequencer::Transport t;
            t.songMode = true;
            t.bpm = 120.0;

            auto events = run (*compilePlayback (p), t, sr, 512, (juce::int64) (4.0 * 60.0 / t.bpm * sr));
            expect (events.empty());
        }

        beginTest ("sliced clip (offsetTicks) continues the pattern instead of restarting");
        {
            // A 2-note pattern: note A at step 0, note B at step 8 (half bar in).
            // Slicing a full-bar clip at the halfway point should make the second
            // clip play note B at its start (offsetTicks = half a bar), not note A.
            Project p;
            const int cId = p.addChannel (GeneratorType::synth, "S");
            auto& pat = p.addPattern();
            pat.dataFor (cId).notes.push_back ({ 0, kTicksPerStep, 60, 0.8f });                    // note A
            pat.dataFor (cId).notes.push_back ({ kTicksPerBar / 2, kTicksPerStep, 67, 0.8f });      // note B

            PlaylistClip second;
            second.patternIndex = 0;
            second.startTick    = kTicksPerBar / 2;         // second half of the bar
            second.lengthTicks  = kTicksPerBar / 2;
            second.offsetTicks  = kTicksPerBar / 2;         // content continues from the cut point
            p.clips.push_back (second);

            const double sr = 44100.0;
            Sequencer::Transport t;
            t.songMode = true;
            t.bpm = 120.0;
            const double samplesPerBar = 4.0 * 60.0 / t.bpm * sr;

            auto events = run (*compilePlayback (p), t, sr, 512, (juce::int64) samplesPerBar);

            std::vector<int> onPitches;
            for (auto& e : events)
                if (e.isOn)
                    onPitches.push_back (e.pitch);

            // Only note B (67) should sound, once, near the start of the clip -
            // not note A (60), which would indicate the pattern restarted from tick 0.
            expectEquals ((int) onPitches.size(), 1);
            if (! onPitches.empty())
                expectEquals (onPitches[0], 67);
        }

        beginTest ("sliced clip repeats correctly when longer than one pattern loop");
        {
            // Same 2-note pattern; a clip that starts at offset half-a-bar and
            // spans 1.5 bars should see: B (at 0), A (at loop wrap), B (at loop wrap + half).
            Project p;
            const int cId = p.addChannel (GeneratorType::synth, "S");
            auto& pat = p.addPattern();
            pat.dataFor (cId).notes.push_back ({ 0, kTicksPerStep, 60, 0.8f });
            pat.dataFor (cId).notes.push_back ({ kTicksPerBar / 2, kTicksPerStep, 67, 0.8f });

            PlaylistClip clip;
            clip.patternIndex = 0;
            clip.startTick    = 0;
            clip.lengthTicks  = kTicksPerBar + kTicksPerBar / 2;
            clip.offsetTicks  = kTicksPerBar / 2;
            p.clips.push_back (clip);

            const double sr = 44100.0;
            Sequencer::Transport t;
            t.songMode = true;
            t.bpm = 120.0;
            const double samplesPerBar = 4.0 * 60.0 / t.bpm * sr;

            auto events = run (*compilePlayback (p), t, sr, 512, (juce::int64) (samplesPerBar * 1.5));

            std::vector<int> onPitches;
            for (auto& e : events)
                if (e.isOn)
                    onPitches.push_back (e.pitch);

            expectEquals ((int) onPitches.size(), 3);
            if (onPitches.size() == 3)
            {
                expectEquals (onPitches[0], 67);
                expectEquals (onPitches[1], 60);
                expectEquals (onPitches[2], 67);
            }
        }
    }
};

static SequencerTests sequencerTests;

} // namespace fable
