#pragma once

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

    int selectedPatternIndex = 0;   // channel rack + playlist paint source
    int selectedChannelId    = -1;  // piano roll target
    int selectedMixerTrack   = 0;

    // UI listeners (panels register to rebuild/repaint on model changes)
    juce::ChangeBroadcaster structureBroadcaster;   // channels/patterns/slots added/removed
    juce::ChangeBroadcaster contentBroadcaster;     // notes/steps/clips edited

    // Set by MainComponent
    std::function<void (juce::AudioPluginInstance*, const juce::String& title)> openPluginEditor;
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

    void selectPattern (int index)
    {
        selectedPatternIndex = juce::jlimit (0, juce::jmax (0, (int) project.patterns.size() - 1), index);
        engine.setCurrentPattern (selectedPatternIndex);
        structureBroadcaster.sendChangeMessage();
        contentBroadcaster.sendChangeMessage();
    }
};

} // namespace fable
