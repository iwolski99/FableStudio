#include "SamplerSound.h"

namespace fable
{

juce::AudioBuffer<float> loadSampleFile (const juce::File& file,
                                         juce::AudioFormatManager& formats,
                                         double& sourceRateOut)
{
    sourceRateOut = 44100.0;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    // Cap at 60s to keep memory bounded; long files are for the playlist (future), not the sampler.
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60));

    juce::AudioBuffer<float> multi ((int) reader->numChannels, numSamples);
    reader->read (&multi, 0, numSamples, 0, true, true);

    juce::AudioBuffer<float> mono (1, numSamples);
    mono.clear();
    for (int ch = 0; ch < multi.getNumChannels(); ++ch)
        mono.addFrom (0, 0, multi, ch, 0, numSamples, 1.0f / (float) multi.getNumChannels());

    sourceRateOut = reader->sampleRate;
    return mono;
}

} // namespace fable
