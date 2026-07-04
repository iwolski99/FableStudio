#include "DrumSynthesis.h"

namespace fable
{

namespace
{
    juce::AudioBuffer<float> makeBuffer (double sampleRate, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (sampleRate * seconds));
        b.clear();
        return b;
    }

    // Simple one-pole filters, good enough for percussion shaping.
    struct OnePole
    {
        float z = 0.0f, a = 0.5f;
        void setLowpass  (double sr, double hz) { a = (float) std::exp (-juce::MathConstants<double>::twoPi * hz / sr); }
        float lp (float x) { z = (1.0f - a) * x + a * z; return z; }
        float hp (float x) { return x - lp (x); }
    };

    juce::AudioBuffer<float> makeKick (double sr)
    {
        auto buf = makeBuffer (sr, 0.45);
        auto* d = buf.getWritePointer (0);
        double phase = 0.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            double t = i / sr;
            double freq = 48.0 + 110.0 * std::exp (-t * 28.0);       // pitch sweep
            phase += juce::MathConstants<double>::twoPi * freq / sr;
            double body  = std::sin (phase) * std::exp (-t * 9.0);
            double click = std::exp (-t * 700.0) * 0.6;
            d[i] = (float) juce::jlimit (-1.0, 1.0, (body * 1.1 + click) * 0.9);
        }
        return buf;
    }

    juce::AudioBuffer<float> makeSnare (double sr)
    {
        auto buf = makeBuffer (sr, 0.30);
        auto* d = buf.getWritePointer (0);
        juce::Random rng (5);
        OnePole noiseHp; noiseHp.setLowpass (sr, 900.0);
        double phase = 0.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            double t = i / sr;
            phase += juce::MathConstants<double>::twoPi * (185.0 + 60.0 * std::exp (-t * 60.0)) / sr;
            double tone  = std::sin (phase) * std::exp (-t * 22.0) * 0.55;
            double noise = noiseHp.hp (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-t * 16.0) * 0.8;
            d[i] = (float) juce::jlimit (-1.0, 1.0, tone + noise);
        }
        return buf;
    }

    juce::AudioBuffer<float> makeClap (double sr)
    {
        auto buf = makeBuffer (sr, 0.35);
        auto* d = buf.getWritePointer (0);
        juce::Random rng (7);
        OnePole bp1, bp2;
        bp1.setLowpass (sr, 700.0);   // highpass at 700
        bp2.setLowpass (sr, 3500.0);  // lowpass at 3.5k -> band 0.7-3.5k
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            double t = i / sr;
            // three quick bursts then a decaying tail — classic clap envelope
            double env = 0.0;
            for (int b = 0; b < 3; ++b)
            {
                double bt = t - b * 0.011;
                if (bt >= 0.0) env = juce::jmax (env, std::exp (-bt * 220.0));
            }
            double tail = t > 0.028 ? std::exp (-(t - 0.028) * 14.0) * 0.8 : 0.0;
            float n = rng.nextFloat() * 2.0f - 1.0f;
            float shaped = bp2.lp (bp1.hp (n));
            d[i] = (float) juce::jlimit (-1.0, 1.0, shaped * (env + tail) * 1.6);
        }
        return buf;
    }

    juce::AudioBuffer<float> makeHat (double sr, bool open)
    {
        auto buf = makeBuffer (sr, open ? 0.45 : 0.09);
        auto* d = buf.getWritePointer (0);
        juce::Random rng (11);
        OnePole hp; hp.setLowpass (sr, 6500.0);
        double decay = open ? 9.0 : 55.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            double t = i / sr;
            // metallic-ish: sum of detuned square partials + filtered noise
            double metal = 0.0;
            for (double f : { 5078.0, 6143.0, 7412.0, 8934.0 })
                metal += (std::fmod (t * f, 1.0) < 0.5 ? 1.0 : -1.0) * 0.12;
            float n = rng.nextFloat() * 2.0f - 1.0f;
            double x = hp.hp ((float) (metal * 0.5 + n * 0.6));
            d[i] = (float) juce::jlimit (-1.0, 1.0, x * std::exp (-t * decay) * 0.8);
        }
        return buf;
    }
}

juce::AudioBuffer<float> synthesizeDrum (const juce::String& name, double sampleRate)
{
    if (name == "kick")    return makeKick (sampleRate);
    if (name == "snare")   return makeSnare (sampleRate);
    if (name == "clap")    return makeClap (sampleRate);
    if (name == "hat")     return makeHat (sampleRate, false);
    if (name == "openhat") return makeHat (sampleRate, true);
    return {};
}

bool isBuiltinSamplePath (const juce::String& path)
{
    return path.startsWith ("builtin:");
}

juce::String builtinSampleName (const juce::String& path)
{
    return path.fromFirstOccurrenceOf ("builtin:", false, false);
}

} // namespace fable
