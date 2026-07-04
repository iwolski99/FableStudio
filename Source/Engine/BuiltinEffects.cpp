#include "BuiltinEffects.h"

namespace fable
{

std::unique_ptr<BuiltinEffect> BuiltinEffect::create (EffectType type)
{
    switch (type)
    {
        case EffectType::reverb:  return std::make_unique<ReverbEffect>();
        case EffectType::delay:   return std::make_unique<DelayEffect>();
        case EffectType::eq3:     return std::make_unique<EQ3Effect>();
        case EffectType::limiter: return std::make_unique<LimiterEffect>();
        case EffectType::none:
        case EffectType::plugin:  break;
    }
    return nullptr;
}

juce::String BuiltinEffect::typeName (EffectType type)
{
    switch (type)
    {
        case EffectType::reverb:  return "Fable Reverb";
        case EffectType::delay:   return "Fable Delay";
        case EffectType::eq3:     return "Fable EQ3";
        case EffectType::limiter: return "Fable Limiter";
        case EffectType::none:    return "(empty)";
        case EffectType::plugin:  return "Plugin";
    }
    return {};
}

// ------------------------------------------------------------------ Reverb

void ReverbEffect::prepare (double sampleRate, int maxBlockSize)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    reverb.prepare (spec);
}

void ReverbEffect::process (juce::AudioBuffer<float>& buffer)
{
    juce::dsp::Reverb::Parameters p;
    p.roomSize   = params[0];
    p.damping    = params[1];
    p.wetLevel   = params[2] * 0.8f;
    p.dryLevel   = 1.0f - params[2] * 0.4f;
    p.width      = params[3];
    reverb.setParameters (p);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);
}

// ------------------------------------------------------------------ Delay

void DelayEffect::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
    delayL.prepare (spec);
    delayR.prepare (spec);
    delayL.setMaximumDelayInSamples ((int) (sampleRate * 2.0));
    delayR.setMaximumDelayInSamples ((int) (sampleRate * 2.0));
}

void DelayEffect::process (juce::AudioBuffer<float>& buffer)
{
    const float timeParam = params[0];   // 0..1 -> 20ms .. 1.2s
    const float feedback  = params[1] * 0.9f;
    const float mix       = params[2];

    const int numSamples = buffer.getNumSamples();
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;

    for (int i = 0; i < numSamples; ++i)
    {
        // smooth time changes to avoid zipper artefacts
        smoothedTime += (timeParam - smoothedTime) * 0.0005f;
        const float delaySamples = (0.02f + smoothedTime * 1.18f) * (float) sr;
        delayL.setDelay (delaySamples);
        delayR.setDelay (delaySamples);

        const float wetL = delayL.popSample (0);
        const float wetR = delayR.popSample (0);
        delayL.pushSample (0, l[i] + wetL * feedback);
        delayR.pushSample (0, r[i] + wetR * feedback);
        l[i] += (wetL - l[i]) * mix * 0.5f + wetL * mix * 0.5f;
        if (r != l)
            r[i] += (wetR - r[i]) * mix * 0.5f + wetR * mix * 0.5f;
    }
}

// ------------------------------------------------------------------ EQ3

void EQ3Effect::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sr = sampleRate;
    lastLow = lastMid = lastHigh = -1;
    updateCoefficients();
    reset();
}

void EQ3Effect::reset()
{
    for (auto* f : { low, mid, high })
        for (int ch = 0; ch < 2; ++ch)
            f[ch].reset();
}

void EQ3Effect::updateCoefficients()
{
    auto toGainDb = [] (float v) { return (v - 0.5f) * 30.0f; };  // +-15 dB

    if (! juce::approximatelyEqual (lastLow, params[0].load()))
    {
        lastLow = params[0];
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sr, 200.0f, 0.707f, juce::Decibels::decibelsToGain (toGainDb (lastLow)));
        for (int ch = 0; ch < 2; ++ch) low[ch].coefficients = c;
    }
    if (! juce::approximatelyEqual (lastMid, params[1].load()))
    {
        lastMid = params[1];
        auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sr, 1000.0f, 0.6f, juce::Decibels::decibelsToGain (toGainDb (lastMid)));
        for (int ch = 0; ch < 2; ++ch) mid[ch].coefficients = c;
    }
    if (! juce::approximatelyEqual (lastHigh, params[2].load()))
    {
        lastHigh = params[2];
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sr, 5000.0f, 0.707f, juce::Decibels::decibelsToGain (toGainDb (lastHigh)));
        for (int ch = 0; ch < 2; ++ch) high[ch].coefficients = c;
    }
}

void EQ3Effect::process (juce::AudioBuffer<float>& buffer)
{
    updateCoefficients();
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] = high[ch].processSample (mid[ch].processSample (low[ch].processSample (d[i])));
    }
}

// ------------------------------------------------------------------ Limiter

void LimiterEffect::prepare (double sampleRate, int maxBlockSize)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
    limiter.prepare (spec);
}

void LimiterEffect::process (juce::AudioBuffer<float>& buffer)
{
    limiter.setThreshold (-12.0f + params[0] * 12.0f);   // -12..0 dB
    limiter.setRelease (10.0f + params[1] * 490.0f);     // 10..500 ms

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    limiter.process (ctx);
}

} // namespace fable
