#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include "InstrumentParams.h"

namespace fable
{

// FableSynth: a native, fully-parameterised polyphonic subtractive synth -
// two oscillators (sine/saw/square/triangle) with semitone + fine detune and a
// mix, a sub oscillator and a noise source, a resonant state-variable lowpass
// with its own envelope, and separate amp/filter ADSRs plus glide and drive.
// Parameters live in a shared AtomicParams the editor drives live.

// ---- parameter table -------------------------------------------------------

inline const juce::StringArray& synthWaveNames()
{
    static const juce::StringArray names { "Sine", "Saw", "Square", "Triangle" };
    return names;
}

inline std::vector<ParamSpec> fableSynthSpecs()
{
    ParamSpec wave1 { "osc1Wave", "Osc 1 Wave", 0, 3, 1, {}, true, synthWaveNames() };
    ParamSpec wave2 { "osc2Wave", "Osc 2 Wave", 0, 3, 1, {}, true, synthWaveNames() };
    return {
        wave1,
        { "osc1Level", "Osc 1",       0.0f,   1.0f,   0.5f },
        wave2,
        { "osc2Level", "Osc 2",       0.0f,   1.0f,   0.5f },
        { "osc2Semi",  "Osc 2 Semi", -24.0f, 24.0f,   0.0f, " st" },
        { "osc2Fine",  "Osc 2 Fine", -50.0f, 50.0f,   8.0f, " ct" },
        { "subLevel",  "Sub",         0.0f,   1.0f,   0.35f },
        { "noiseLevel","Noise",       0.0f,   1.0f,   0.0f },
        { "cutoff",    "Cutoff",      40.0f, 16000.0f, 3200.0f, " Hz" },
        { "resonance", "Resonance",   0.0f,   1.0f,   0.20f },
        { "filterEnv", "Filter Env",  0.0f,   1.0f,   0.55f },
        { "ampAttack", "Amp Attack",  1.0f,   4000.0f, 4.0f,  " ms" },
        { "ampDecay",  "Amp Decay",   1.0f,   4000.0f, 350.0f," ms" },
        { "ampSustain","Amp Sustain", 0.0f,   1.0f,   0.65f },
        { "ampRelease","Amp Release", 1.0f,   6000.0f, 140.0f," ms" },
        { "filtAttack","Flt Attack",  1.0f,   4000.0f, 4.0f,  " ms" },
        { "filtDecay", "Flt Decay",   1.0f,   4000.0f, 300.0f," ms" },
        { "filtSustain","Flt Sustain",0.0f,   1.0f,   0.0f },
        { "glide",     "Glide",       0.0f,   1.0f,   0.0f },
        { "drive",     "Drive",       0.0f,   1.0f,   0.0f },
        { "level",     "Level",       0.0f,   1.0f,   0.8f },
    };
}

// ---- small DSP helpers -----------------------------------------------------

struct AdsrEnv
{
    enum class Stage { idle, attack, decay, release };
    Stage stage = Stage::idle;
    float value = 0.0f;

    void noteOn()  { stage = Stage::attack; }
    void noteOff() { if (stage != Stage::idle) stage = Stage::release; }
    bool active() const { return stage != Stage::idle; }

    // times in seconds, sustain 0..1. Returns the current level.
    float tick (float sr, float attackS, float decayS, float sustain, float releaseS)
    {
        switch (stage)
        {
            case Stage::attack:
            {
                value += 1.0f / juce::jmax (1.0f, attackS * sr);
                if (value >= 1.0f) { value = 1.0f; stage = Stage::decay; }
                break;
            }
            case Stage::decay:
            {
                const float mul = std::exp (-1.0f / juce::jmax (1.0f, decayS * sr));
                value = sustain + (value - sustain) * mul;
                break;
            }
            case Stage::release:
            {
                const float mul = std::exp (-1.0f / juce::jmax (1.0f, releaseS * sr));
                value *= mul;
                if (value < 0.0004f) { value = 0.0f; stage = Stage::idle; }
                break;
            }
            case Stage::idle: break;
        }
        return value;
    }
};

struct SynthOsc
{
    double phase = 0.0, inc = 0.0;

    // polyBLEP-corrected saw/square keep the synth from aliasing harshly up high.
    static double polyBlep (double t, double dt)
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt)  { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    float next (int wave)
    {
        const double dt = inc;
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;

        switch (wave)
        {
            case 0: return (float) std::sin (phase * juce::MathConstants<double>::twoPi);   // sine
            case 1: // saw
            {
                double v = 2.0 * phase - 1.0;
                v -= polyBlep (phase, dt);
                return (float) v;
            }
            case 2: // square
            {
                double v = phase < 0.5 ? 1.0 : -1.0;
                v += polyBlep (phase, dt);
                double t2 = phase + 0.5; if (t2 >= 1.0) t2 -= 1.0;
                v -= polyBlep (t2, dt);
                return (float) (v * 0.85);
            }
            case 3: // triangle (integrated square, cheap approximation)
            {
                return (float) (2.0 * std::abs (2.0 * phase - 1.0) - 1.0);
            }
        }
        return 0.0f;
    }
};

// Zavalishin TPT state-variable filter (resonant lowpass).
struct SvfLowpass
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() { ic1 = ic2 = 0.0f; }

    float process (float x, float cutoffHz, float resonance, float sr)
    {
        const float g = std::tan (juce::MathConstants<float>::pi
                                  * juce::jlimit (20.0f, sr * 0.45f, cutoffHz) / sr);
        const float k = 2.0f - 1.94f * juce::jlimit (0.0f, 1.0f, resonance);   // 2..0.06
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float v1 = a1 * ic1 + a2 * (x - ic2);
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;   // lowpass output
    }
};

// ---- synth voice/sound -----------------------------------------------------

struct FableSynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class FableSynthVoice : public juce::SynthesiserVoice
{
public:
    explicit FableSynthVoice (std::shared_ptr<AtomicParams> paramsToUse)
        : params (std::move (paramsToUse)) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<FableSynthSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        noteNumber = midiNote;
        level = 0.15f + 0.85f * velocity;

        const double sr = getSampleRate();
        targetFreq = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        if (currentFreq <= 0.0 || params->get ("glide", 0.0f) <= 0.0001f)
            currentFreq = targetFreq;

        osc1.phase = 0.10; osc2.phase = 0.55; sub.phase = 0.0;
        filter.reset();
        ampEnv.noteOn();
        filtEnv.noteOn();
        juce::ignoreUnused (sr);
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            filtEnv.noteOff();
        }
        else
        {
            ampEnv.stage = AdsrEnv::Stage::idle;
            filtEnv.stage = AdsrEnv::Stage::idle;
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! ampEnv.active())
            return;

        const float sr = (float) getSampleRate();

        // Snapshot params once per block (realtime-safe, cheap enough per voice).
        const int   w1     = (int) (params->get ("osc1Wave", 1.0f) + 0.5f);
        const int   w2     = (int) (params->get ("osc2Wave", 1.0f) + 0.5f);
        const float o1lvl  = params->get ("osc1Level", 0.5f);
        const float o2lvl  = params->get ("osc2Level", 0.5f);
        const float o2semi = params->get ("osc2Semi", 0.0f);
        const float o2fine = params->get ("osc2Fine", 0.0f);
        const float subLvl = params->get ("subLevel", 0.35f);
        const float nseLvl = params->get ("noiseLevel", 0.0f);
        const float cutoff = params->get ("cutoff", 3200.0f);
        const float reso   = params->get ("resonance", 0.2f);
        const float fltEnvAmt = params->get ("filterEnv", 0.55f);
        const float ampA = params->get ("ampAttack", 4.0f) * 0.001f;
        const float ampD = params->get ("ampDecay", 350.0f) * 0.001f;
        const float ampS = params->get ("ampSustain", 0.65f);
        const float ampR = params->get ("ampRelease", 140.0f) * 0.001f;
        const float fltA = params->get ("filtAttack", 4.0f) * 0.001f;
        const float fltD = params->get ("filtDecay", 300.0f) * 0.001f;
        const float fltS = params->get ("filtSustain", 0.0f);
        const float glide = params->get ("glide", 0.0f);
        const float drive = params->get ("drive", 0.0f);
        const float outLvl = params->get ("level", 0.8f);

        const double o2ratio = std::pow (2.0, (o2semi + o2fine * 0.01) / 12.0);
        // glide 0..1 -> per-sample smoothing coefficient toward target freq
        const double glideCoeff = glide <= 0.0001f ? 1.0
            : 1.0 - std::exp (-1.0 / juce::jmax (1.0, (double) glide * 0.25 * sr));
        const float driveGain = 1.0f + drive * 6.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            currentFreq += (targetFreq - currentFreq) * glideCoeff;
            const double f = currentFreq;
            osc1.inc = f / sr;
            osc2.inc = f * o2ratio / sr;
            sub.inc  = f * 0.5 / sr;

            float s = osc1.next (w1) * o1lvl
                    + osc2.next (w2) * o2lvl
                    + (float) std::sin (sub.phase * juce::MathConstants<double>::twoPi) * subLvl;
            sub.phase += sub.inc; if (sub.phase >= 1.0) sub.phase -= 1.0;
            if (nseLvl > 0.0f)
                s += (rng.nextFloat() * 2.0f - 1.0f) * nseLvl * 0.5f;

            const float fe = filtEnv.tick (sr, fltA, fltD, fltS, ampR);
            const float sweptCutoff = juce::jlimit (30.0f, sr * 0.45f,
                                                    cutoff * (1.0f + fltEnvAmt * 3.5f * fe));
            s = filter.process (s, sweptCutoff, reso, sr);

            if (drive > 0.0f)
                s = std::tanh (s * driveGain) / std::tanh (driveGain > 1.0f ? 1.0f : driveGain);

            const float ae = ampEnv.tick (sr, ampA, ampD, ampS, ampR);
            const float outSample = s * ae * level * outLvl * 0.5f;

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, outSample);

            if (! ampEnv.active())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    std::shared_ptr<AtomicParams> params;
    SynthOsc osc1, osc2, sub;
    SvfLowpass filter;
    AdsrEnv ampEnv, filtEnv;
    juce::Random rng;
    int noteNumber = 60;
    float level = 0.7f;
    double currentFreq = 0.0, targetFreq = 0.0;
};

} // namespace fable
