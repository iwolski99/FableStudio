#include "MixerBus.h"

namespace fable
{

MixerBus::~MixerBus()
{
    release();
}

void MixerBus::setSlotBuiltin (int slotIndex, EffectType type)
{
    auto builtin = BuiltinEffect::create (type);
    if (builtin != nullptr && prepared)
        builtin->prepare (preparedRate, preparedBlockSize);

    const juce::SpinLock::ScopedLockType sl (slotLock);
    auto& slot = getSlot (slotIndex);
    slot.plugin.reset();
    slot.builtin = std::move (builtin);
    slot.type    = slot.builtin != nullptr ? type : EffectType::none;
}

void MixerBus::setSlotPlugin (int slotIndex, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance != nullptr && prepared)
    {
        instance->enableAllBuses();
        instance->setPlayConfigDetails (2, 2, preparedRate, preparedBlockSize);
        instance->prepareToPlay (preparedRate, preparedBlockSize);
    }

    const juce::SpinLock::ScopedLockType sl (slotLock);
    auto& slot = getSlot (slotIndex);
    slot.builtin.reset();
    slot.plugin = std::move (instance);
    slot.type   = slot.plugin != nullptr ? EffectType::plugin : EffectType::none;
}

void MixerBus::clearSlot (int slotIndex)
{
    std::unique_ptr<juce::AudioPluginInstance> retiredPlugin;
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        auto& slot = getSlot (slotIndex);
        retiredPlugin = std::move (slot.plugin);
        slot.builtin.reset();
        slot.type = EffectType::none;
    }
    if (retiredPlugin != nullptr && prepared)
        retiredPlugin->releaseResources();
}

void MixerBus::prepare (double sampleRate, int maxBlockSize)
{
    preparedRate      = sampleRate;
    preparedBlockSize = maxBlockSize;
    prepared          = true;

    buffer.setSize (2, maxBlockSize);
    buffer.clear();

    for (auto& slot : slots)
    {
        if (slot.builtin != nullptr)
        {
            slot.builtin->prepare (sampleRate, maxBlockSize);
            slot.builtin->reset();
        }
        if (slot.plugin != nullptr)
        {
            slot.plugin->enableAllBuses();
            slot.plugin->setPlayConfigDetails (2, 2, sampleRate, maxBlockSize);
            slot.plugin->prepareToPlay (sampleRate, maxBlockSize);
        }
    }
}

void MixerBus::release()
{
    if (! prepared)
        return;
    for (auto& slot : slots)
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();
    prepared = false;
}

void MixerBus::processBlock (int numSamples)
{
    if (! prepared || numSamples > buffer.getNumSamples())
        return;

    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, 0, numSamples);

    {
        // Skip the fx chain for one block if a slot is mid-edit on the message thread.
        const juce::SpinLock::ScopedTryLockType sl (slotLock);
        if (sl.isLocked())
        {
            for (auto& slot : slots)
            {
                if (! slot.enabled.load())
                    continue;
                if (slot.builtin != nullptr)
                    slot.builtin->process (view);
                else if (slot.plugin != nullptr)
                {
                    emptyMidi.clear();
                    slot.plugin->processBlock (view, emptyMidi);
                }
            }
        }
    }

    const float v = muted.load() ? 0.0f : volume.load();
    const float p = juce::jlimit (-1.0f, 1.0f, pan.load());
    const float angle = (p + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f;
    const float gainL = v * std::cos (angle) * 1.41f * 0.707f;
    const float gainR = v * std::sin (angle) * 1.41f * 0.707f;

    view.applyGainRamp (0, 0, numSamples, lastGainL, gainL);
    view.applyGainRamp (1, 0, numSamples, lastGainR, gainR);
    lastGainL = gainL;
    lastGainR = gainR;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = view.getMagnitude (ch, 0, numSamples);
        auto& m = meter[(size_t) ch];
        if (peak > m.load())
            m.store (peak);
    }
}

void MixerBus::decayMeters()
{
    for (auto& m : meter)
        m.store (m.load() * 0.82f);
}

} // namespace fable
