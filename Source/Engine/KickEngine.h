#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include "InstrumentParams.h"
#include "../Common.h"

namespace fable
{

// Kick designer: a native, fully-synthesised kick drum voice generated live per
// note (no pre-baked sample), so every knob is playable and preset-able. A
// sine body with an exponential pitch envelope for punch, a decay that sets the
// tail length, a transient click, and tanh drive for weight/distortion. The
// played MIDI note transposes it, so it doubles as an 808.

inline std::vector<ParamSpec> kickSpecs()
{
    return {
        { "tune",      "Tune",        30.0f,  150.0f,  50.0f,  " Hz" },
        { "pitchAmt",  "Punch Amt",   0.0f,   1.0f,    0.7f },
        { "pitchTime", "Punch Time",  2.0f,   200.0f,  35.0f,  " ms" },
        { "decay",     "Length",      40.0f,  2000.0f, 420.0f, " ms" },
        { "click",     "Click",       0.0f,   1.0f,    0.35f },
        { "drive",     "Drive",       0.0f,   1.0f,    0.15f },
        { "level",     "Level",       0.0f,   1.0f,    0.9f },
    };
}

struct KickSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class KickVoice : public juce::SynthesiserVoice
{
public:
    explicit KickVoice (std::shared_ptr<AtomicParams> paramsToUse)
        : params (std::move (paramsToUse)) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<KickSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        t = 0.0;
        phase = 0.0;
        vel = 0.25f + 0.75f * velocity;
        pitchMul = (float) std::pow (2.0, (midiNote - kDefaultRootNote) / 12.0);
        playing = true;
    }

    void stopNote (float, bool) override
    {
        // Percussive one-shot: note-off doesn't cut it, it rings out for its
        // full designed length (retrigger/steal handles overlaps).
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! playing)
            return;

        const double sr = getSampleRate();
        const float tune      = params->get ("tune", 50.0f) * pitchMul;
        const float pitchAmt  = params->get ("pitchAmt", 0.7f);
        const float pitchTime = juce::jmax (0.001f, params->get ("pitchTime", 35.0f) * 0.001f);
        const float decayS    = juce::jmax (0.01f, params->get ("decay", 420.0f) * 0.001f);
        const float clickAmt  = params->get ("click", 0.35f);
        const float drive     = params->get ("drive", 0.15f);
        const float outLvl    = params->get ("level", 0.9f);

        // Pitch env sweeps from tune + up to ~4 octaves down to tune.
        const float pitchTop = tune * (1.0f + pitchAmt * 12.0f);
        const float driveGain = 1.0f + drive * 8.0f;
        const float normDrive = std::tanh (driveGain);

        for (int i = 0; i < numSamples; ++i)
        {
            const float pitchEnv = std::exp (-(float) t / pitchTime);
            const float freq = tune + (pitchTop - tune) * pitchEnv;
            phase += juce::MathConstants<double>::twoPi * freq / sr;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            const float ampEnv = std::exp (-(float) t / decayS);
            float body = (float) std::sin (phase) * ampEnv;

            // Short noisy transient for beater "click".
            const float clickEnv = std::exp (-(float) t * 500.0f);
            float clickSig = (rng.nextFloat() * 2.0f - 1.0f) * clickEnv * clickAmt * 0.7f;

            float s = body + clickSig;
            if (drive > 0.0f)
                s = std::tanh (s * driveGain) / normDrive;

            s *= vel * outLvl;
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, s);

            t += 1.0 / sr;
            if (ampEnv < 0.0005f && t > 0.02)
            {
                playing = false;
                clearCurrentNote();
                break;
            }
        }
    }

private:
    std::shared_ptr<AtomicParams> params;
    juce::Random rng;
    double t = 0.0, phase = 0.0;
    float vel = 1.0f, pitchMul = 1.0f;
    bool playing = false;
};

} // namespace fable
