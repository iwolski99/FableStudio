#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Model/Project.h"
#include "BuiltinEffects.h"

namespace fable
{

// One mixer strip: 10 effect slots (built-in or hosted VST3), fader, pan,
// mute/solo and an output meter. Same threading contract as ChannelNode.

class MixerBus
{
public:
    explicit MixerBus (int indexToUse) : index (indexToUse) {}
    ~MixerBus();

    // --- message thread ------------------------------------------------
    struct Slot
    {
        EffectType type = EffectType::none;
        std::unique_ptr<BuiltinEffect> builtin;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::atomic<bool> enabled { true };
    };

    void setSlotBuiltin (int slotIndex, EffectType type);
    void setSlotPlugin  (int slotIndex, std::unique_ptr<juce::AudioPluginInstance> instance);
    void clearSlot      (int slotIndex);

    Slot& getSlot (int slotIndex) { return slots[(size_t) juce::jlimit (0, kNumEffectSlots - 1, slotIndex)]; }

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    void updateFromModel (const MixerTrackModel& m)
    {
        volume.store (m.volume);
        pan.store (m.pan);
        muted.store (m.muted);
        solo.store (m.solo);
        for (int s = 0; s < kNumEffectSlots; ++s)
        {
            slots[(size_t) s].enabled.store (m.slots[(size_t) s].enabled);
            if (auto* fx = slots[(size_t) s].builtin.get())
                for (int k = 0; k < fx->getNumParams(); ++k)
                    fx->setParam (k, m.slots[(size_t) s].params[(size_t) k]);
        }
    }

    // --- audio thread ---------------------------------------------------
    juce::AudioBuffer<float> buffer;    // channels sum in here before fx/fader

    void processBlock (int numSamples);  // fx chain + fader + meter, in place

    float getMeterLevel (int channel) const { return meter[(size_t) juce::jlimit (0, 1, channel)].load(); }
    void  decayMeters();

    const int index;
    std::atomic<float> volume { 0.8f }, pan { 0.0f };
    std::atomic<bool>  muted { false }, solo { false };

private:
    std::array<Slot, kNumEffectSlots> slots;
    std::array<std::atomic<float>, 2> meter { 0.0f, 0.0f };
    juce::MidiBuffer emptyMidi;
    float lastGainL = 0.0f, lastGainR = 0.0f;
    double preparedRate = 0.0;
    int preparedBlockSize = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerBus)
};

} // namespace fable
