#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace fable
{

// Minimal pitched sample playback: one sample per channel, repitched around a
// root note, velocity-scaled, with a short release fade to avoid clicks.
// Sampler channels are intentionally mono-voice so retriggers cut the previous
// sample like FL's common "Cut itself" workflow for drums/808s.
// (juce::SamplerSound exists but this keeps full control and stays trivial.)

class FableSamplerSound : public juce::SynthesiserSound
{
public:
    FableSamplerSound (juce::AudioBuffer<float>&& sampleData, double sampleSourceRate, int rootMidiNote,
                       float fadeInMsToUse, float fadeOutMsToUse)
        : data (std::move (sampleData)), sourceRate (sampleSourceRate), rootNote (rootMidiNote),
          fadeInMs (fadeInMsToUse), fadeOutMs (fadeOutMsToUse) {}

    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }

    juce::AudioBuffer<float> data;
    double sourceRate = 44100.0;
    int    rootNote   = 60;
    float  fadeInMs   = 0.0f;
    float  fadeOutMs  = 0.0f;
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
        if (! allowTailOff)
        {
            clearCurrentNote();
            sound = nullptr;
        }
        // Ignore normal note-offs so step-sequenced samples play their full
        // audio file. Retriggers cut the previous voice because the sampler is
        // intentionally single-voice.
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (sound == nullptr)
            return;

        const auto& data = sound->data;
        const int   dataCh = data.getNumChannels();
        const float* srcL = data.getReadPointer (0);
        const float* srcR = data.getReadPointer (dataCh > 1 ? 1 : 0);   // mono: reuse L
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
            float envelope = 1.0f;
            if (sound->fadeInMs > 0.0f)
            {
                const double fadeInSamples = sound->sourceRate * sound->fadeInMs / 1000.0;
                if (fadeInSamples > 1.0)
                    envelope = juce::jmin (envelope, (float) juce::jlimit (0.0, 1.0, position / fadeInSamples));
            }
            if (sound->fadeOutMs > 0.0f)
            {
                const double fadeOutSamples = sound->sourceRate * sound->fadeOutMs / 1000.0;
                const double remaining = len - position;
                if (fadeOutSamples > 1.0)
                    envelope = juce::jmin (envelope, (float) juce::jlimit (0.0, 1.0, remaining / fadeOutSamples));
            }

            const float envGain = gain * releaseGain * envelope;
            const float l = (srcL[idx] + frac * (srcL[idx + 1] - srcL[idx])) * envGain;
            const float r = (srcR[idx] + frac * (srcR[idx + 1] - srcR[idx])) * envGain;

            // Preserve the sample's stereo image: L->ch0, R->ch1. Extra output
            // channels (rare) get the left channel.
            const int outCh = output.getNumChannels();
            if (outCh > 0) output.addSample (0, startSample + i, l);
            if (outCh > 1) output.addSample (1, startSample + i, r);
            for (int ch = 2; ch < outCh; ++ch)
                output.addSample (ch, startSample + i, l);

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

// Loads an audio file into a sound usable by FableSamplerVoice (keeps stereo).
juce::AudioBuffer<float> loadSampleFile (const juce::File& file,
                                         juce::AudioFormatManager& formats,
                                         double& sourceRateOut);

// Loads an audio file for playlist clip playback, preserving mono/stereo.
juce::AudioBuffer<float> loadAudioClipFile (const juce::File& file,
                                            juce::AudioFormatManager& formats,
                                            double& sourceRateOut);

} // namespace fable
