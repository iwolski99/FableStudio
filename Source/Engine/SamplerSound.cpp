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

    // Cap at 60s to keep memory bounded.
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60));
    const int numSourceChannels = juce::jmax (1, (int) reader->numChannels);

    juce::AudioBuffer<float> multi (numSourceChannels, numSamples);
    reader->read (&multi, 0, numSamples, 0, true, true);

    // Preserve stereo (up to 2 channels) so samples keep their stereo image.
    const int outChannels = numSourceChannels >= 2 ? 2 : 1;
    juce::AudioBuffer<float> out (outChannels, numSamples);
    out.clear();
    for (int ch = 0; ch < outChannels; ++ch)
        out.copyFrom (ch, 0, multi, ch, 0, numSamples);

    sourceRateOut = reader->sampleRate;
    return out;
}

juce::AudioBuffer<float> loadAudioClipFile (const juce::File& file,
                                            juce::AudioFormatManager& formats,
                                            double& sourceRateOut)
{
    sourceRateOut = 44100.0;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    // Keep clips reasonably bounded in memory for now.
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    const int numSourceChannels = juce::jmax (1, (int) reader->numChannels);
    juce::AudioBuffer<float> source (numSourceChannels, numSamples);
    reader->read (&source, 0, numSamples, 0, true, true);

    const int outChannels = numSourceChannels >= 2 ? 2 : 1;
    juce::AudioBuffer<float> out (outChannels, numSamples);
    out.clear();

    if (numSourceChannels == 1)
    {
        out.copyFrom (0, 0, source, 0, 0, numSamples);
    }
    else
    {
        out.copyFrom (0, 0, source, 0, 0, numSamples);
        out.copyFrom (1, 0, source, 1, 0, numSamples);
    }

    sourceRateOut = reader->sampleRate;
    return out;
}

} // namespace fable
