#include <juce_core/juce_core.h>

#include "../Source/Model/Serialization.h"

namespace fable
{

class ModelTests : public juce::UnitTest
{
public:
    ModelTests() : juce::UnitTest ("Model", "Fable") {}

    void runTest() override
    {
        beginTest ("default project shape");
        {
            auto p = createDefaultProject();
            expect (p.channels.size() == 5);
            expect (p.patterns.size() == 1);
            expect (! p.clips.empty());
            expect (p.mixerTracks[0].name == "Master");
            expectEquals (p.patterns[0].lengthTicks(), kTicksPerBar);
        }

        beginTest ("channel add/remove keeps ids stable");
        {
            Project p;
            const int idA = p.addChannel (GeneratorType::sampler, "A");
            const int idB = p.addChannel (GeneratorType::synth, "B");
            expect (idA != idB);

            auto& pat = p.addPattern();
            pat.dataFor (idA).steps[0].on = true;
            pat.dataFor (idB).notes.push_back ({ 0, 240, 64, 0.5f });

            p.removeChannel (idA);
            expect (p.channelById (idA) == nullptr);
            expect (p.channelById (idB) != nullptr);
            expect (pat.channelData.count (idA) == 0);
            expect (pat.channelData.count (idB) == 1);
        }

        beginTest ("pattern length grows with piano-roll notes");
        {
            Project p;
            const int id = p.addChannel (GeneratorType::synth, "S");
            auto& pat = p.addPattern();
            expectEquals (pat.lengthTicks(), kTicksPerBar);

            pat.dataFor (id).notes.push_back ({ kTicksPerBar + 10, kTicksPerStep, 60, 0.5f });
            expectEquals (pat.lengthTicks(), 2 * kTicksPerBar);
        }

        beginTest ("JSON round trip preserves everything");
        {
            auto p = createDefaultProject();
            p.name  = "RoundTrip";
            p.bpm   = 173.5;
            p.swing = 33;
            p.channels[1].pan = -0.25f;
            p.channels[2].muted = true;
            p.mixerTracks[3].slots[2].type = EffectType::delay;
            p.mixerTracks[3].slots[2].params[0] = 0.7f;
            p.mixerTracks[5].solo = true;
            p.channels[0].pluginState.append ("\x01\x02\xff", 3);

            Project q;
            expect (projectFromJson (projectToJson (p), q));

            expectEquals (q.name, juce::String ("RoundTrip"));
            expectWithinAbsoluteError (q.bpm, 173.5, 1e-9);
            expectEquals (q.swing, 33);
            expectEquals ((int) q.channels.size(), (int) p.channels.size());
            expectWithinAbsoluteError (q.channels[1].pan, -0.25f, 1e-6f);
            expect (q.channels[2].muted);
            expect (q.mixerTracks[3].slots[2].type == EffectType::delay);
            expectWithinAbsoluteError (q.mixerTracks[3].slots[2].params[0], 0.7f, 1e-6f);
            expect (q.mixerTracks[5].solo);
            expect (q.channels[0].pluginState == p.channels[0].pluginState);
            expectEquals ((int) q.clips.size(), (int) p.clips.size());

            // step/note data
            auto& pd = p.patterns[0].channelData;
            auto& qd = q.patterns[0].channelData;
            expectEquals ((int) qd.size(), (int) pd.size());
            for (auto& [id, data] : pd)
            {
                expect (qd.count (id) == 1);
                auto& qData = qd.at (id);
                expectEquals ((int) qData.steps.size(), (int) data.steps.size());
                for (size_t i = 0; i < data.steps.size(); ++i)
                    expect (qData.steps[i].on == data.steps[i].on);
                expect (qData.notes == data.notes);
            }
        }

        beginTest ("corrupt input is rejected, not crashed on");
        {
            Project q;
            expect (! projectFromJson ("", q));
            expect (! projectFromJson ("{}", q));
            expect (! projectFromJson ("not json at all {{{", q));
            expect (! projectFromJson ("{\"app\":\"SomethingElse\"}", q));
        }

        beginTest ("file save/load");
        {
            auto tmp = juce::File::createTempFile (".fable");
            auto p = createDefaultProject();
            expect (saveProjectToFile (p, tmp));

            Project q;
            expect (loadProjectFromFile (tmp, q));
            expectEquals (q.name, p.name);
            tmp.deleteFile();
        }
    }
};

static ModelTests modelTests;

} // namespace fable
