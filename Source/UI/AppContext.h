#pragma once

#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/AudioEngine.h"
#include "../Model/Serialization.h"

namespace fable
{

// Shared state + mediation between panels. Owned by MainComponent.
// Panels mutate the Project through their own logic, then call the matching
// *Changed() helper so the engine and the other panels stay in sync.

struct AppContext
{
    AppContext() : engine (plugins) {}

    Project       project;
    PluginManager plugins;
    AudioEngine   engine;

    juce::File currentFile;
    bool       dirty = false;
    std::vector<Note> noteClipboard;
    int noteClipboardMinStart = 0;
    int noteClipboardBasePitch = kDefaultRootNote;

    int selectedPatternIndex = 0;   // channel rack + playlist paint source
    int selectedChannelId    = -1;  // piano roll target
    int selectedMixerTrack   = 0;

    // UI listeners (panels register to rebuild/repaint on model changes)
    juce::ChangeBroadcaster structureBroadcaster;   // channels/patterns/slots added/removed
    juce::ChangeBroadcaster contentBroadcaster;     // notes/steps/clips edited

    // Set by MainComponent
    std::function<void (juce::AudioPluginInstance*, const juce::String& title)> openPluginEditor;
    std::function<void()> showPianoRoll;
    std::function<void()> showChannelRack;
    std::function<void (int channelId)> openInstrumentEditor;   // native synth/kick editor windows
    std::function<void (const juce::String&)> showStatusMessage;

    Pattern* selectedPattern()
    {
        if (selectedPatternIndex >= 0 && selectedPatternIndex < (int) project.patterns.size())
            return &project.patterns[(size_t) selectedPatternIndex];
        return nullptr;
    }

    // structural change: channels/patterns/effect slots changed shape
    void structureChanged()
    {
        dirty = true;
        auto errors = engine.syncWithProject (project);
        if (! errors.isEmpty() && showStatusMessage)
            showStatusMessage (errors.joinIntoString ("  |  "));
        engine.setCurrentPattern (selectedPatternIndex);
        structureBroadcaster.sendChangeMessage();
        contentBroadcaster.sendChangeMessage();
    }

    // notes/steps/clips changed (no engine node rebuild needed)
    void contentChanged()
    {
        dirty = true;
        engine.updatePlayback (project);
        contentBroadcaster.sendChangeMessage();
    }

    // continuous controls: knobs/faders (cheap, no broadcast)
    void channelParamsChanged() { dirty = true; engine.updateChannelParams (project); }
    void mixerParamsChanged()   { dirty = true; engine.updateMixerParams (project); }
    // native synth/kick knob edits (cheap, pushes atomics; no node rebuild)
    void instrumentParamsChanged() { dirty = true; engine.updateInstrumentParams (project); }

    void selectChannel (int channelId)
    {
        selectedChannelId = channelId;
        structureBroadcaster.sendChangeMessage();
        contentBroadcaster.sendChangeMessage();
    }

    void selectPattern (int index)
    {
        selectedPatternIndex = juce::jlimit (0, juce::jmax (0, (int) project.patterns.size() - 1), index);
        engine.setCurrentPattern (selectedPatternIndex);
        structureBroadcaster.sendChangeMessage();
        contentBroadcaster.sendChangeMessage();
    }

    void openPianoRollForChannel (int channelId)
    {
        if (project.channelById (channelId) == nullptr)
            return;

        selectChannel (channelId);
        if (showPianoRoll)
            showPianoRoll();
    }

    bool isSupportedAudioFile (const juce::File& file)
    {
        if (! file.existsAsFile() || file.hasFileExtension ("fable"))
            return false;
        return engine.getFormatManager().createReaderFor (file) != nullptr;
    }

    int estimateAudioFileLengthTicks (const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (engine.getFormatManager().createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            return kTicksPerBar;

        const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
        return juce::jmax (1, (int) std::round (seconds * project.bpm * kPPQ / 60.0));
    }

    int addSamplerChannelFromFile (const juce::File& file)
    {
        const int id = project.addChannel (GeneratorType::sampler, file.getFileNameWithoutExtension());
        if (auto* c = project.channelById (id))
            c->samplePath = file.getFullPathName();
        selectedChannelId = id;
        structureChanged();
        return id;
    }

    void addAudioClipFromFile (const juce::File& file, int track, int startTick)
    {
        AudioClip clip;
        clip.filePath    = file.getFullPathName();
        clip.name        = file.getFileNameWithoutExtension();
        clip.gain        = 1.0f;
        clip.mixerTrack  = 0;
        clip.track       = track;
        clip.startTick   = startTick;
        clip.lengthTicks = estimateAudioFileLengthTicks (file);

        // FL-style: a new clip of an existing (non-unique) source inherits that
        // source's shared gain/routing so edits stay consistent across siblings.
        for (const auto& existing : project.audioClips)
            if (! existing.uniqueSettings && existing.filePath == clip.filePath)
            {
                clip.gain       = existing.gain;
                clip.mixerTrack = existing.mixerTrack;
                break;
            }

        project.audioClips.push_back (std::move (clip));
        contentChanged();
    }
};

} // namespace fable
