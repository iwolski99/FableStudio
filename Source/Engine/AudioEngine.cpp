#include "AudioEngine.h"
#include "DrumSynthesis.h"

namespace fable
{

// Routes sequencer events into the matching channel node's MIDI buffer.
class AudioEngine::SinkAdapter : public MidiSink
{
public:
    explicit SinkAdapter (RenderSet& setToUse) : set (setToUse) {}

    void addNoteOn (int channelId, int pitch, float velocity, int sampleOffset) override
    {
        if (auto* node = find (channelId))
            node->midiBuffer.addEvent (juce::MidiMessage::noteOn (1, pitch, velocity), sampleOffset);
    }

    void addNoteOff (int channelId, int pitch, int sampleOffset) override
    {
        if (auto* node = find (channelId))
            node->midiBuffer.addEvent (juce::MidiMessage::noteOff (1, pitch), sampleOffset);
    }

private:
    ChannelNode* find (int channelId) const
    {
        for (auto& n : set.channels)
            if (n->channelId == channelId)
                return n.get();
        return nullptr;
    }

    RenderSet& set;
};

AudioEngine::AudioEngine (PluginManager& pluginManagerToUse)
    : pluginManager (pluginManagerToUse)
{
    formatManager.registerBasicFormats();

    auto set = std::make_shared<RenderSet>();
    for (int i = 0; i < kNumMixerTracks; ++i)
        set->buses.push_back (std::make_shared<MixerBus> (i));
    set->playback = std::make_shared<const PlaybackData>();
    renderSet = std::move (set);
}

AudioEngine::~AudioEngine()
{
    shutdownDevice();
}

juce::String AudioEngine::initialiseDevice()
{
    auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
    return error;
}

void AudioEngine::shutdownDevice()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

float AudioEngine::getMasterLevel (int ch) const
{
    if (auto bus = getMixerBus (0))
        return bus->getMeterLevel (ch);
    return 0.0f;
}

// --------------------------------------------------------------- model sync

std::shared_ptr<ChannelNode> AudioEngine::getChannelNode (int channelId) const
{
    auto set = copyRenderSet();
    for (auto& n : set->channels)
        if (n->channelId == channelId)
            return n;
    return nullptr;
}

std::shared_ptr<MixerBus> AudioEngine::getMixerBus (int index) const
{
    auto set = copyRenderSet();
    if (index >= 0 && index < (int) set->buses.size())
        return set->buses[(size_t) index];
    return nullptr;
}

static juce::String generatorSourceFor (const Channel& channel)
{
    switch (channel.type)
    {
        case GeneratorType::synth:   return "synth";
        case GeneratorType::sampler: return "sampler:" + channel.samplePath;
        case GeneratorType::plugin:  return "plugin:" + channel.pluginIdentifier;
    }
    return {};
}

juce::String AudioEngine::buildChannelGenerator (ChannelNode& node, Channel& channel)
{
    node.generatorSource = generatorSourceFor (channel);

    switch (channel.type)
    {
        case GeneratorType::synth:
            node.setSynthGenerator();
            return {};

        case GeneratorType::sampler:
        {
            double sourceRate = currentSampleRate;
            juce::AudioBuffer<float> sample;

            if (isBuiltinSamplePath (channel.samplePath))
                sample = synthesizeDrum (builtinSampleName (channel.samplePath), currentSampleRate);
            else if (channel.samplePath.isNotEmpty())
                sample = loadSampleFile (juce::File (channel.samplePath), formatManager, sourceRate);

            node.setSamplerGenerator (std::move (sample), sourceRate, channel.rootNote);
            return {};
        }

        case GeneratorType::plugin:
        {
            if (auto desc = pluginManager.findByIdentifier (channel.pluginIdentifier))
            {
                juce::String error;
                auto instance = pluginManager.createInstance (*desc, currentSampleRate,
                                                              currentBlockSize, error);
                if (instance == nullptr)
                    return channel.name + ": " + error;

                if (channel.pluginState.getSize() > 0)
                    instance->setStateInformation (channel.pluginState.getData(),
                                                   (int) channel.pluginState.getSize());
                node.setPluginGenerator (std::move (instance));
                return {};
            }
            return channel.name + ": plugin not found (" + channel.pluginIdentifier + ")";
        }
    }
    return {};
}

juce::String AudioEngine::buildEffectSlot (MixerBus& bus, int slotIndex, EffectSlot& slot)
{
    if (slot.type == EffectType::none)
    {
        bus.clearSlot (slotIndex);
        return {};
    }

    if (slot.type != EffectType::plugin)
    {
        bus.setSlotBuiltin (slotIndex, slot.type);
        return {};
    }

    if (auto desc = pluginManager.findByIdentifier (slot.pluginIdentifier))
    {
        juce::String error;
        auto instance = pluginManager.createInstance (*desc, currentSampleRate,
                                                      currentBlockSize, error);
        if (instance == nullptr)
            return "Mixer FX: " + error;

        if (slot.pluginState.getSize() > 0)
            instance->setStateInformation (slot.pluginState.getData(),
                                           (int) slot.pluginState.getSize());
        bus.setSlotPlugin (slotIndex, std::move (instance));
        return {};
    }
    return "Mixer FX: plugin not found (" + slot.pluginIdentifier + ")";
}

juce::StringArray AudioEngine::syncWithProject (Project& project)
{
    juce::StringArray errors;
    auto oldSet = copyRenderSet();
    auto newSet = std::make_shared<RenderSet>();

    // Reuse a node only when its generator source is unchanged (published nodes
    // are immutable apart from their parameter atomics); otherwise build fresh.
    for (auto& channel : project.channels)
    {
        std::shared_ptr<ChannelNode> node;
        const auto wantedSource = generatorSourceFor (channel);

        for (auto& existing : oldSet->channels)
            if (existing->channelId == channel.id && existing->generatorSource == wantedSource)
                node = existing;

        if (node == nullptr)
        {
            node = std::make_shared<ChannelNode> (channel.id);
            auto err = buildChannelGenerator (*node, channel);
            if (err.isNotEmpty())
                errors.add (err);
        }

        node->updateFromModel (channel);
        if (deviceRunning && ! node->isPrepared())
            node->prepare (currentSampleRate, currentBlockSize);
        newSet->channels.push_back (std::move (node));
    }

    // Mixer buses persist forever; sync their slots to the model.
    newSet->buses = oldSet->buses;
    for (int i = 0; i < kNumMixerTracks; ++i)
    {
        auto& bus   = *newSet->buses[(size_t) i];
        auto& model = project.mixerTracks[(size_t) i];

        for (int s = 0; s < kNumEffectSlots; ++s)
        {
            auto& slotModel = model.slots[(size_t) s];
            auto& slot      = bus.getSlot (s);

            const bool matches =
                   (slot.type == slotModel.type)
                && (slot.type != EffectType::plugin
                    || (slot.plugin != nullptr
                        && slot.plugin->getPluginDescription().createIdentifierString()
                               == slotModel.pluginIdentifier));
            if (! matches)
            {
                auto err = buildEffectSlot (bus, s, slotModel);
                if (err.isNotEmpty())
                    errors.add (err);
            }
        }
        bus.updateFromModel (model);
    }

    newSet->playback = compilePlayback (project);
    publishRenderSet (std::move (newSet));
    return errors;
}

void AudioEngine::updateChannelParams (const Project& project)
{
    auto set = copyRenderSet();
    for (auto& node : set->channels)
        if (auto* c = project.channelById (node->channelId))
            node->updateFromModel (*c);
}

void AudioEngine::updateMixerParams (const Project& project)
{
    auto set = copyRenderSet();
    for (int i = 0; i < juce::jmin ((int) set->buses.size(), kNumMixerTracks); ++i)
        set->buses[(size_t) i]->updateFromModel (project.mixerTracks[(size_t) i]);
}

void AudioEngine::updatePlayback (const Project& project)
{
    auto next = std::make_shared<RenderSet> (*copyRenderSet());
    next->playback = compilePlayback (project);
    publishRenderSet (std::move (next));
}

void AudioEngine::publishRenderSet (std::shared_ptr<RenderSet> next)
{
    std::shared_ptr<RenderSet> old;
    {
        const juce::SpinLock::ScopedLockType sl (renderSetLock);
        old = std::move (renderSet);
        renderSet = std::move (next);
    }
    // Retire the old set without freeing plugin instances the audio thread may
    // still be inside of this block; the graveyard drains on later calls.
    graveyard.push_back (std::move (old));
    if (graveyard.size() > 4)
        graveyard.erase (graveyard.begin());
}

std::shared_ptr<AudioEngine::RenderSet> AudioEngine::copyRenderSet() const
{
    const juce::SpinLock::ScopedLockType sl (renderSetLock);
    return renderSet;
}

// ----------------------------------------------------------------- transport

void AudioEngine::play()
{
    if (! playing.load())
    {
        seekRequest.store (0.0);
        playing.store (true);
    }
}

void AudioEngine::stop()
{
    playing.store (false);
    stopRequest.store (true);
    playheadTicks.store (0.0);
}

void AudioEngine::auditionNoteOn (int channelId, int pitch, float velocity)
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditionEvents[(size_t) scope.startIndex1] = { channelId, pitch, velocity, true };
}

void AudioEngine::auditionNoteOff (int channelId, int pitch)
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditionEvents[(size_t) scope.startIndex1] = { channelId, pitch, 0.0f, false };
}

// ------------------------------------------------------------- audio thread

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize  = device->getCurrentBufferSizeSamples();
    deviceRunning     = true;

    auto set = copyRenderSet();
    for (auto& n : set->channels) n->prepare (currentSampleRate, currentBlockSize);
    for (auto& b : set->buses)    b->prepare (currentSampleRate, currentBlockSize);
}

void AudioEngine::audioDeviceStopped()
{
    deviceRunning = false;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioBuffer<float> output (outputChannelData, numOutputChannels, numSamples);
    output.clear();
    processBlock (output);
}

void AudioEngine::processBlock (juce::AudioBuffer<float>& output)
{
    const int numSamples = output.getNumSamples();

    // Pick up the latest snapshot; on lock contention keep using the previous one.
    {
        const juce::SpinLock::ScopedTryLockType sl (renderSetLock);
        if (sl.isLocked())
            audioThreadSet = renderSet;
    }
    auto* set = audioThreadSet.get();
    if (set == nullptr)
        return;

    SinkAdapter sink (*set);

    if (stopRequest.exchange (false))
    {
        sequencer.allNotesOff (sink);
        sequencer.reset();
        // also hard-stop any voices left ringing
        for (auto& n : set->channels)
            n->midiBuffer.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    const double seek = seekRequest.exchange (-1.0);
    if (seek >= 0.0)
    {
        sequencer.allNotesOff (sink);
        sequencer.setPositionTicks (seek);
    }

    // audition events from the UI
    {
        const auto scope = auditionFifo.read (auditionFifo.getNumReady());
        auto handle = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                auto& e = auditionEvents[(size_t) (start + i)];
                if (e.isOn) sink.addNoteOn (e.channelId, e.pitch, e.velocity, 0);
                else        sink.addNoteOff (e.channelId, e.pitch, 0);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    if (playing.load() && set->playback != nullptr)
    {
        Sequencer::Transport t;
        t.songMode     = songMode.load();
        t.patternIndex = currentPattern.load();
        t.bpm          = bpm.load();
        playheadTicks.store (sequencer.process (*set->playback, t, currentSampleRate, numSamples, sink));
    }

    // channels render into their assigned mixer bus
    for (auto& bus : set->buses)
    {
        bus->buffer.clear (0, 0, juce::jmin (numSamples, bus->buffer.getNumSamples()));
        bus->buffer.clear (1, 0, juce::jmin (numSamples, bus->buffer.getNumSamples()));
    }

    for (auto& node : set->channels)
    {
        const int busIndex = juce::jlimit (0, (int) set->buses.size() - 1, node->getMixerTrack());
        node->render (numSamples, set->buses[(size_t) busIndex]->buffer);
    }

    // solo logic: if any insert is soloed, mute the others
    bool anySolo = false;
    for (size_t i = 1; i < set->buses.size(); ++i)
        anySolo = anySolo || set->buses[i]->solo.load();

    auto& master = *set->buses[0];

    for (size_t i = 1; i < set->buses.size(); ++i)
    {
        auto& bus = *set->buses[i];
        bus.processBlock (numSamples);
        const bool audible = ! anySolo || bus.solo.load();
        if (audible)
            for (int ch = 0; ch < 2; ++ch)
                master.buffer.addFrom (ch, 0, bus.buffer, ch, 0, numSamples);
    }

    master.processBlock (numSamples);

    for (int ch = 0; ch < output.getNumChannels(); ++ch)
        output.copyFrom (ch, 0, master.buffer, juce::jmin (ch, 1), 0, numSamples);
}

// ------------------------------------------------------------ offline render

bool AudioEngine::renderToWav (Project& project, const juce::File& outFile, bool songModeRender,
                               double sampleRate, double tailSeconds)
{
    jassert (! deviceRunning);   // offline render assumes the device is idle

    currentSampleRate = sampleRate;
    currentBlockSize  = 512;

    syncWithProject (project);
    auto set = copyRenderSet();
    for (auto& n : set->channels) n->prepare (sampleRate, currentBlockSize);
    for (auto& b : set->buses)    b->prepare (sampleRate, currentBlockSize);

    const auto playback = set->playback;
    const int lengthTicks = songModeRender
        ? playback->songLengthTicks
        : (playback->patterns.empty() ? kTicksPerBar : playback->patterns[0].lengthTicks);

    const double ticksPerSecond = project.bpm * kPPQ / 60.0;
    const juce::int64 musicSamples = (juce::int64) (lengthTicks / ticksPerSecond * sampleRate);
    const juce::int64 totalSamples = musicSamples + (juce::int64) (tailSeconds * sampleRate);

    outFile.deleteFile();
    auto stream = outFile.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();   // writer owns it now

    songMode.store (songModeRender);
    setCurrentPattern (0);
    setBpm (project.bpm);
    sequencer.reset();
    playing.store (true);

    juce::AudioBuffer<float> block (2, currentBlockSize);
    juce::int64 done = 0;
    while (done < totalSamples)
    {
        const int n = (int) juce::jmin ((juce::int64) currentBlockSize, totalSamples - done);
        block.setSize (2, currentBlockSize, false, false, true);
        block.clear();

        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, 0, n);

        if (done >= musicSamples)
            playing.store (false);   // stop sequencing, let the tail ring out

        processBlock (view);
        writer->writeFromAudioSampleBuffer (view, 0, n);
        done += n;
    }

    playing.store (false);
    sequencer.reset();
    return true;
}

} // namespace fable
