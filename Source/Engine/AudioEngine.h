#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Model/Project.h"
#include "../Plugins/PluginManager.h"
#include "ChannelNode.h"
#include "MixerBus.h"
#include "Sequencer.h"

namespace fable
{

// ---------------------------------------------------------------------------
// AudioEngine drives everything audible.
//
// Threading contract:
//   * The message thread owns the Project model and mutates the engine through
//     the public sync/transport methods.
//   * The audio thread only reads an immutable RenderSet snapshot (swapped
//     under a SpinLock held for a pointer copy) plus per-node atomics.
//   * MIDI audition (clicking notes/pads in the UI) goes through a lock-free
//     FIFO.
// ---------------------------------------------------------------------------

class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (PluginManager& pluginManagerToUse);
    ~AudioEngine() override;

    // --- device lifecycle (message thread) -------------------------------
    juce::String initialiseDevice();            // returns error message, empty on success
    void shutdownDevice();
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // --- model sync (message thread) --------------------------------------
    // Full structural sync: creates/removes nodes to match the project.
    // Returns per-channel/slot plugin load errors, if any.
    juce::StringArray syncWithProject (Project& project);

    // Cheap updates for continuous controls (no node rebuild).
    void updateChannelParams (const Project& project);
    void updateMixerParams   (const Project& project);
    void updatePlayback      (const Project& project);   // recompile note data

    std::shared_ptr<ChannelNode> getChannelNode (int channelId) const;
    std::shared_ptr<MixerBus>    getMixerBus (int index) const;

    // --- transport (any thread) -------------------------------------------
    void play();
    void stop();
    bool isPlaying() const            { return playing.load(); }
    void setSongMode (bool song)      { songMode.store (song); }
    bool isSongMode() const           { return songMode.load(); }
    void setBpm (double newBpm)       { bpm.store (juce::jlimit (kMinBpm, kMaxBpm, newBpm)); }
    double getBpm() const             { return bpm.load(); }
    void setCurrentPattern (int index){ currentPattern.store (index); }
    int  getCurrentPattern() const    { return currentPattern.load(); }
    void setPositionTicks (double t)  { seekRequest.store (t); }
    double getPlayheadTicks() const   { return playheadTicks.load(); }
    double getCpuLoad() const         { return deviceManager.getCpuUsage(); }
    float  getMasterLevel (int ch) const;

    // UI note audition
    void auditionNoteOn  (int channelId, int pitch, float velocity);
    void auditionNoteOff (int channelId, int pitch);
    void previewSampleFile (const juce::File& file, int rootNote = kDefaultRootNote);
    void stopPreviewSample();

    // --- offline export (message thread; engine must not be playing) ------
    bool renderToWav (Project& project, const juce::File& outFile, bool songModeRender,
                      double sampleRate = 44100.0, double tailSeconds = 2.0);

private:
    struct RenderSet
    {
        std::vector<std::shared_ptr<ChannelNode>> channels;
        std::vector<std::shared_ptr<MixerBus>>    buses;    // index 0 = master
        std::shared_ptr<const PlaybackData>       playback;
    };

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void processBlock (juce::AudioBuffer<float>& output);
    void publishRenderSet (std::shared_ptr<RenderSet> next);
    std::shared_ptr<RenderSet> copyRenderSet() const;
    juce::String buildChannelGenerator (ChannelNode& node, Channel& channel);
    juce::String buildEffectSlot (MixerBus& bus, int slotIndex, EffectSlot& slot);

    class SinkAdapter;

    PluginManager& pluginManager;
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    mutable juce::SpinLock renderSetLock;
    std::shared_ptr<RenderSet> renderSet;               // guarded by renderSetLock
    std::shared_ptr<RenderSet> audioThreadSet;          // audio thread's stable copy
    std::vector<std::shared_ptr<RenderSet>> graveyard;  // keeps retired sets alive briefly

    Sequencer sequencer;

    std::atomic<bool>   playing { false }, songMode { false };
    std::atomic<double> bpm { 140.0 };
    std::atomic<int>    currentPattern { 0 };
    std::atomic<double> playheadTicks { 0.0 };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool>   stopRequest { false };

    // audition FIFO
    struct AuditionEvent { int channelId, pitch; float velocity; bool isOn; };
    juce::AbstractFifo auditionFifo { 256 };
    std::array<AuditionEvent, 256> auditionEvents;

    mutable juce::SpinLock previewNodeLock;
    std::shared_ptr<ChannelNode> previewNode;

    double currentSampleRate = 44100.0;
    int    currentBlockSize  = 512;
    bool   deviceRunning     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace fable
