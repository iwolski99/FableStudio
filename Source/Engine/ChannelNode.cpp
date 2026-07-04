#include "ChannelNode.h"

namespace fable
{

ChannelNode::~ChannelNode()
{
    release();
}

void ChannelNode::setSynthGenerator()
{
    auto s = std::make_unique<juce::Synthesiser>();
    for (int i = 0; i < 12; ++i)
        s->addVoice (new FableSynthVoice());
    s->addSound (new FableSynthSound());
    plugin.reset();
    synth = std::move (s);
    if (prepared)
        synth->setCurrentPlaybackSampleRate (preparedRate);
}

void ChannelNode::setSamplerGenerator (juce::AudioBuffer<float>&& sample, double sourceRate, int rootNote)
{
    auto s = std::make_unique<juce::Synthesiser>();
    for (int i = 0; i < 16; ++i)
        s->addVoice (new FableSamplerVoice());
    s->addSound (new FableSamplerSound (std::move (sample), sourceRate, rootNote));
    plugin.reset();
    synth = std::move (s);
    if (prepared)
        synth->setCurrentPlaybackSampleRate (preparedRate);
}

void ChannelNode::setPluginGenerator (std::unique_ptr<juce::AudioPluginInstance> instance)
{
    synth.reset();
    plugin = std::move (instance);
    if (plugin != nullptr && prepared)
    {
        plugin->enableAllBuses();
        plugin->setPlayConfigDetails (0, 2, preparedRate, preparedBlockSize);
        plugin->prepareToPlay (preparedRate, preparedBlockSize);
    }
}

void ChannelNode::prepare (double sampleRate, int maxBlockSize)
{
    preparedRate      = sampleRate;
    preparedBlockSize = maxBlockSize;
    prepared          = true;

    scratch.setSize (2, maxBlockSize);
    midiBuffer.ensureSize (1024);

    if (synth != nullptr)
        synth->setCurrentPlaybackSampleRate (sampleRate);

    if (plugin != nullptr)
    {
        plugin->enableAllBuses();
        plugin->setPlayConfigDetails (0, 2, sampleRate, maxBlockSize);
        plugin->prepareToPlay (sampleRate, maxBlockSize);
    }
}

void ChannelNode::release()
{
    if (plugin != nullptr && prepared)
        plugin->releaseResources();
    prepared = false;
}

void ChannelNode::render (int numSamples, juce::AudioBuffer<float>& dest)
{
    if (! prepared || numSamples > scratch.getNumSamples())
    {
        midiBuffer.clear();
        return;
    }

    scratch.clear (0, 0, numSamples);
    scratch.clear (1, 0, numSamples);

    if (synth != nullptr)
    {
        synth->renderNextBlock (scratch, midiBuffer, 0, numSamples);
    }
    else if (plugin != nullptr)
    {
        // Hosted instruments may have more output channels prepared; give them
        // a view limited to this block.
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 2, 0, numSamples);
        plugin->processBlock (view, midiBuffer);
    }

    midiBuffer.clear();

    // Constant-power pan + volume, ramped across the block to avoid zipper noise.
    const float v = muted.load() ? 0.0f : volume.load();
    const float p = juce::jlimit (-1.0f, 1.0f, pan.load());
    const float angle = (p + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f;
    const float gainL = v * std::cos (angle) * 1.41f * 0.707f;
    const float gainR = v * std::sin (angle) * 1.41f * 0.707f;

    dest.addFromWithRamp (0, 0, scratch.getReadPointer (0), numSamples, lastGainL, gainL);
    dest.addFromWithRamp (1, 0, scratch.getReadPointer (1), numSamples, lastGainR, gainR);
    lastGainL = gainL;
    lastGainR = gainR;
}

} // namespace fable
