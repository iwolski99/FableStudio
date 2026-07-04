#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Model/Project.h"
#include "BuiltinSynth.h"
#include "SamplerSound.h"

namespace fable
{

// Engine-side counterpart of a model Channel: owns the generator (built-in
// synth, sampler, or a hosted VST3 instrument) plus realtime-safe parameter
// atomics. Created/configured on the message thread, rendered on the audio
// thread; RenderSet snapshots keep it alive while in use.

class ChannelNode
{
public:
    ChannelNode (int channelIdToUse) : channelId (channelIdToUse) {}
    ~ChannelNode();

    // --- message thread ------------------------------------------------
    void setSynthGenerator();
    void setSamplerGenerator (juce::AudioBuffer<float>&& sample, double sourceRate, int rootNote);
    void setPluginGenerator (std::unique_ptr<juce::AudioPluginInstance> instance);

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    void updateFromModel (const Channel& c)
    {
        volume.store (c.volume);
        pan.store (c.pan);
        muted.store (c.muted);
        mixerTrack.store (juce::jlimit (0, kNumMixerTracks - 1, c.mixerTrack));
    }

    juce::AudioPluginInstance* getPluginInstance() const { return plugin.get(); }

    // --- audio thread ---------------------------------------------------
    juce::MidiBuffer midiBuffer;   // filled by the sequencer each block

    // Renders into its private buffer and mixes into `dest` (stereo).
    void render (int numSamples, juce::AudioBuffer<float>& dest);

    int getMixerTrack() const { return mixerTrack.load(); }
    int channelId;

    std::atomic<float> volume { 0.78f }, pan { 0.0f };
    std::atomic<bool>  muted { false };

private:
    std::atomic<int> mixerTrack { 1 };

    std::unique_ptr<juce::Synthesiser> synth;             // builtin synth or sampler
    std::unique_ptr<juce::AudioPluginInstance> plugin;    // hosted instrument

    juce::AudioBuffer<float> scratch;
    float lastGainL = 0.0f, lastGainR = 0.0f;
    double preparedRate = 0.0;
    int preparedBlockSize = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelNode)
};

} // namespace fable
