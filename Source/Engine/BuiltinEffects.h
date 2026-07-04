#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Model/Project.h"

namespace fable
{

// Built-in mixer effects. Each maps a fixed set of 0..1 knobs (EffectSlot::params)
// onto its DSP parameters. Parameter changes are message-thread writes into
// atomics, read once per block on the audio thread.

class BuiltinEffect
{
public:
    virtual ~BuiltinEffect() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;

    // param knobs: names for the UI, values pushed from the model
    virtual int getNumParams() const = 0;
    virtual juce::String getParamName (int index) const = 0;
    void setParam (int index, float v) { params[(size_t) juce::jlimit (0, 7, index)] = v; }
    float getParam (int index) const   { return params[(size_t) juce::jlimit (0, 7, index)]; }

    static std::unique_ptr<BuiltinEffect> create (EffectType type);
    static juce::String typeName (EffectType type);

protected:
    std::array<std::atomic<float>, 8> params { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
};

class ReverbEffect : public BuiltinEffect
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override { reverb.reset(); }
    void process (juce::AudioBuffer<float>& buffer) override;
    int getNumParams() const override { return 4; }
    juce::String getParamName (int i) const override
    {
        static const char* names[] = { "Size", "Damp", "Wet", "Width" };
        return names[juce::jlimit (0, 3, i)];
    }
private:
    juce::dsp::Reverb reverb;
};

class DelayEffect : public BuiltinEffect
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override { delayL.reset(); delayR.reset(); }
    void process (juce::AudioBuffer<float>& buffer) override;
    int getNumParams() const override { return 3; }
    juce::String getParamName (int i) const override
    {
        static const char* names[] = { "Time", "Feedback", "Mix" };
        return names[juce::jlimit (0, 2, i)];
    }
private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL { 96000 * 2 },
                                                                                delayR { 96000 * 2 };
    double sr = 44100.0;
    float smoothedTime = 0.25f;
};

class EQ3Effect : public BuiltinEffect
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer) override;
    int getNumParams() const override { return 3; }
    juce::String getParamName (int i) const override
    {
        static const char* names[] = { "Low", "Mid", "High" };
        return names[juce::jlimit (0, 2, i)];
    }
private:
    void updateCoefficients();
    juce::dsp::IIR::Filter<float> low[2], mid[2], high[2];
    double sr = 44100.0;
    float lastLow = -1, lastMid = -1, lastHigh = -1;
};

class LimiterEffect : public BuiltinEffect
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override { limiter.reset(); }
    void process (juce::AudioBuffer<float>& buffer) override;
    int getNumParams() const override { return 2; }
    juce::String getParamName (int i) const override
    {
        static const char* names[] = { "Threshold", "Release" };
        return names[juce::jlimit (0, 1, i)];
    }
private:
    juce::dsp::Limiter<float> limiter;
};

} // namespace fable
