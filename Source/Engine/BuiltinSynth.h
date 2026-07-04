#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fable
{

// FableSynth: a small polyphonic subtractive synth (2 detuned saws + sub sine,
// one-pole lowpass with envelope, ADSR amp). Fixed musical preset — enough to
// write melodies with zero plugins installed.

struct FableSynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class FableSynthVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<FableSynthSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        const double freq = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        const double sr   = getSampleRate();
        inc1 = freq * 0.9985 / sr;
        inc2 = freq * 1.0015 / sr;
        incSub = freq * 0.5 / sr;
        phase1 = 0.13; phase2 = 0.57; phaseSub = 0.0;   // fixed spread avoids phasing flams
        level  = 0.25f + 0.55f * velocity;

        env = 0.0f;
        envState = State::attack;
        filterEnv = 1.0f;
        filterZ[0] = filterZ[1] = 0.0f;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            envState = State::release;
        else
        {
            clearCurrentNote();
            envState = State::idle;
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (envState == State::idle)
            return;

        const float sr = (float) getSampleRate();
        const float attackStep  = 1.0f / (0.004f * sr);
        const float decayMul    = std::exp (-1.0f / (0.35f * sr));
        const float sustain     = 0.65f;
        const float releaseMul  = std::exp (-1.0f / (0.12f * sr));
        const float filterDecay = std::exp (-1.0f / (0.30f * sr));

        for (int i = 0; i < numSamples; ++i)
        {
            switch (envState)
            {
                case State::attack:
                    env += attackStep;
                    if (env >= 1.0f) { env = 1.0f; envState = State::decay; }
                    break;
                case State::decay:
                    env = sustain + (env - sustain) * decayMul;
                    break;
                case State::release:
                    env *= releaseMul;
                    if (env < 0.0005f)
                    {
                        clearCurrentNote();
                        envState = State::idle;
                        return;
                    }
                    break;
                case State::idle: return;
            }

            filterEnv *= filterDecay;

            auto polySaw = [] (double& phase, double inc)
            {
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
                return (float) (2.0 * phase - 1.0);
            };

            float osc = polySaw (phase1, inc1) * 0.4f
                      + polySaw (phase2, inc2) * 0.4f;
            phaseSub += incSub;
            if (phaseSub >= 1.0) phaseSub -= 1.0;
            osc += (float) std::sin (phaseSub * juce::MathConstants<double>::twoPi) * 0.35f;

            // 2-pole lowpass, cutoff swept by the filter envelope
            const float cutoffHz = 500.0f + 4200.0f * filterEnv;
            const float g = juce::jlimit (0.01f, 0.99f,
                                          1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / sr));
            filterZ[0] += g * (osc - filterZ[0]);
            filterZ[1] += g * (filterZ[0] - filterZ[1]);

            const float s = filterZ[1] * env * level;
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, s);
        }
    }

private:
    enum class State { idle, attack, decay, release };

    double phase1 = 0, phase2 = 0, phaseSub = 0;
    double inc1 = 0, inc2 = 0, incSub = 0;
    float  level = 0.5f, env = 0.0f, filterEnv = 1.0f;
    float  filterZ[2] { 0, 0 };
    State  envState = State::idle;
};

} // namespace fable
