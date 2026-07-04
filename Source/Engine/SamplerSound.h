#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace fable
{

// Minimal pitched sample playback: one sample per channel, repitched around a
// root note, velocity-scaled, with a short release fade to avoid clicks.
// (juce::SamplerSound exists but this keeps full control and stays trivial.)

class FableSamplerSound : public juce::SynthesiserSound
{
public:
    FableSamplerSound (juce::AudioBuffer<float>&& sampleData, double sampleSourceRate, int rootMidiNote)
        : data (std::move (sampleData)), sourceRate (sampleSourceRate), rootNote (rootMidiNote) {}

    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }

    juce::AudioBuffer<float> data;
    double sourceRate = 44100.0;
    int    rootNote   = 60;
};

class FableSamplerVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<FableSamplerSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound* s, int) override
    {
        sound = dynamic_cast<FableSamplerSound*> (s);
        if (sound == nullptr || sound->data.getNumSamples() == 0)
        {
            clearCurrentNote();
            return;
        }
        position = 0.0;
        gain     = velocity;
        releaseGain = 1.0f;
        releasing   = false;
        ratio = std::pow (2.0, (midiNote - sound->rootNote) / 12.0)
                * sound->sourceRate / getSampleRate();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff && sound != nullptr)
            releasing = true;    // quick fade instead of hard cut
        else
        {
            clearCurrentNote();
            sound = nullptr;
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (sound == nullptr)
            return;

        const auto& data = sound->data;
        const float* src = data.getReadPointer (0);
        const int    len = data.getNumSamples();
        const float  releaseStep = 1.0f / (0.005f * (float) getSampleRate());  // 5 ms fade

        for (int i = 0; i < numSamples; ++i)
        {
            const int idx = (int) position;
            if (idx + 1 >= len || releaseGain <= 0.0f)
            {
                clearCurrentNote();
                sound = nullptr;
                break;
            }

            const float frac = (float) (position - idx);
            float s = (src[idx] + frac * (src[idx + 1] - src[idx])) * gain * releaseGain;

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, s);

            position += ratio;
            if (releasing)
                releaseGain -= releaseStep;
        }
    }

private:
    FableSamplerSound* sound = nullptr;
    double position = 0.0, ratio = 1.0;
    float  gain = 1.0f, releaseGain = 1.0f;
    bool   releasing = false;
};

// Loads an audio file into a sound usable by FableSamplerVoice (mono-mixed).
juce::AudioBuffer<float> loadSampleFile (const juce::File& file,
                                         juce::AudioFormatManager& formats,
                                         double& sourceRateOut);

} // namespace fable
