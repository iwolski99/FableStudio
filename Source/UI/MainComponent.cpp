#include "MainComponent.h"
#include "SettingsDialog.h"

namespace fable
{

namespace menuIds
{
    enum
    {
        fileNew = 1, fileOpen, fileSave, fileSaveAs, fileExport, fileExit,
        optSettings, optScanPlugins,
        viewChannelRack, viewPianoRoll, viewPlaylist, viewMixer,
        helpAbout,
    };
}

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    context.openPluginEditor = [this] (juce::AudioPluginInstance* instance, const juce::String& title)
    { openPluginEditor (instance, title); };
    context.showPianoRoll = [this]
    {
        pianoRollPanel.toFrontAndShow();
    };
    context.showStatusMessage = [this] (const juce::String& message) { showStatus (message); };
    context.plugins.onListChanged = [this]
    {
        context.structureBroadcaster.sendChangeMessage();
    };
    context.plugins.onScanFinished = [this]
    {
        showStatus ("VST3 scan complete: " + juce::String (context.plugins.knownPlugins.getNumTypes())
                   + " plugin(s) found");
    };

    menuBar.setModel (this);
    addAndMakeVisible (menuBar);
    addAndMakeVisible (transport);
    addAndMakeVisible (browser);
    addAndMakeVisible (workspace);

    for (auto* panel : { &channelRackPanel, &pianoRollPanel, &playlistPanel, &mixerPanel })
        workspace.addAndMakeVisible (*panel);

    for (auto entry : { std::pair<juce::TextButton*, FloatingPanel*> { &playlistTabButton,    &playlistPanel },
                       std::pair<juce::TextButton*, FloatingPanel*> { &channelRackTabButton, &channelRackPanel },
                       std::pair<juce::TextButton*, FloatingPanel*> { &pianoRollTabButton,   &pianoRollPanel },
                       std::pair<juce::TextButton*, FloatingPanel*> { &mixerTabButton,       &mixerPanel } })
    {
        entry.first->setClickingTogglesState (false);
        entry.first->onClick = [this, panel = entry.second] { togglePanel (*panel); };
        addAndMakeVisible (*entry.first);
    }

    // start with an empty project (instruments ready, nothing programmed)
    context.project = createEmptyProject();
    context.selectedChannelId = context.project.channels.empty() ? -1
                                    : context.project.channels.front().id;

    const auto deviceError = context.engine.initialiseDevice();
    context.engine.setBpm (context.project.bpm);
    context.structureChanged();
    context.dirty = false;
    if (deviceError.isNotEmpty())
        showStatus ("Audio device: " + deviceError);

    setWantsKeyboardFocus (true);
    startTimerHz (10);
    setSize (1440, 860);
}

MainComponent::~MainComponent()
{
    pluginWindows.clear();
    context.engine.shutdownDevice();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::workspace);

    if (statusMessage.isNotEmpty())
    {
        auto area = getLocalBounds().removeFromBottom (24);
        g.setColour (colours::titlebar);
        g.fillRect (area);
        g.setColour (colours::accent);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (statusMessage, area.reduced (10, 0), juce::Justification::centredLeft);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds (area.removeFromTop (26));
    transport.setBounds (area.removeFromTop (44));

    auto tabRow = area.removeFromTop (30);
    int tabX = tabRow.getX() + 6;
    for (auto* b : { &playlistTabButton, &channelRackTabButton, &pianoRollTabButton, &mixerTabButton })
    {
        b->setBounds (tabX, tabRow.getY() + 3, 108, tabRow.getHeight() - 6);
        tabX += 114;
    }

    browser.setBounds (area.removeFromLeft (230));
    workspace.setBounds (area);

    // initial panel layout, staggered like a classic pattern DAW (first layout only)
    if (! initialLayoutDone && workspace.getWidth() > 400)
    {
        initialLayoutDone = true;
        const int w = workspace.getWidth(), h = workspace.getHeight();
        playlistPanel.setBounds    (juce::jmin (430, w / 3), 12, juce::jmax (500, w - 450), h / 2 - 20);
        channelRackPanel.setBounds (12, h / 2 - 40, juce::jmin (700, w - 40), h / 2 + 20);
        pianoRollPanel.setBounds   (w / 4, 60, juce::jmax (520, w / 2), h - 140);
        mixerPanel.setBounds       (w / 5, h / 3, juce::jmax (760, w / 2 + 100), h / 2);
        pianoRollPanel.setVisible (false);
        mixerPanel.setVisible (false);
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        context.engine.isPlaying() ? context.engine.stop() : context.engine.play();
        return true;
    }
    if (key == juce::KeyPress::F5Key)  { togglePanel (playlistPanel);    return true; }
    if (key == juce::KeyPress::F6Key)  { togglePanel (channelRackPanel); return true; }
    if (key == juce::KeyPress::F7Key)  { togglePanel (pianoRollPanel);   return true; }
    if (key == juce::KeyPress::F9Key)  { togglePanel (mixerPanel);       return true; }
    if (key == juce::KeyPress ('l', juce::ModifierKeys(), 0))
    {
        context.engine.setSongMode (! context.engine.isSongMode());
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::ctrlModifier, 0)) { saveProject (false); return true; }
    if (key == juce::KeyPress ('o', juce::ModifierKeys::ctrlModifier, 0)) { openProject();       return true; }
    if (key == juce::KeyPress ('n', juce::ModifierKeys::ctrlModifier, 0)) { newProject();        return true; }
    return false;
}

// ------------------------------------------------------------------- menus

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Options", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int, const juce::String& menuName)
{
    juce::PopupMenu m;
    if (menuName == "File")
    {
        m.addItem (menuIds::fileNew,    "New project");
        m.addItem (menuIds::fileOpen,   "Open... (Ctrl+O)");
        m.addSeparator();
        m.addItem (menuIds::fileSave,   "Save (Ctrl+S)");
        m.addItem (menuIds::fileSaveAs, "Save as...");
        m.addSeparator();
        m.addItem (menuIds::fileExport, "Export song to WAV...");
        m.addSeparator();
        m.addItem (menuIds::fileExit,   "Exit");
    }
    else if (menuName == "Options")
    {
        m.addItem (menuIds::optSettings,    "Audio & plugin settings...");
        m.addItem (menuIds::optScanPlugins, "Scan for VST3 plugins");
    }
    else if (menuName == "View")
    {
        m.addItem (menuIds::viewPlaylist,    "Playlist (F5)",     true, playlistPanel.isVisible());
        m.addItem (menuIds::viewChannelRack, "Channel Rack (F6)", true, channelRackPanel.isVisible());
        m.addItem (menuIds::viewPianoRoll,   "Piano Roll (F7)",   true, pianoRollPanel.isVisible());
        m.addItem (menuIds::viewMixer,       "Mixer (F9)",        true, mixerPanel.isVisible());
    }
    else if (menuName == "Help")
    {
        m.addItem (menuIds::helpAbout, "About FableStudio");
    }
    return m;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case menuIds::fileNew:    newProject(); break;
        case menuIds::fileOpen:   openProject(); break;
        case menuIds::fileSave:   saveProject (false); break;
        case menuIds::fileSaveAs: saveProject (true); break;
        case menuIds::fileExport: exportWav(); break;
        case menuIds::fileExit:   juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;

        case menuIds::optSettings:    SettingsDialog::show (context); break;
        case menuIds::optScanPlugins: context.plugins.startScan();
                                      showStatus ("Scanning VST3 folders in the background...");
                                      break;

        case menuIds::viewPlaylist:    togglePanel (playlistPanel); break;
        case menuIds::viewChannelRack: togglePanel (channelRackPanel); break;
        case menuIds::viewPianoRoll:   togglePanel (pianoRollPanel); break;
        case menuIds::viewMixer:       togglePanel (mixerPanel); break;

        case menuIds::helpAbout:
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "FableStudio",
                "An open-source pattern-based DAW with VST3 support.\n"
                "Workflow inspired by classic pattern DAWs; all original code.\n\n"
                "Space: play/stop   F5-F9: panels   PAT/SONG: loop mode");
            break;

        default: break;
    }
    menuItemsChanged();
}

// ------------------------------------------------------------------ actions

void MainComponent::newProject()
{
    context.engine.stop();
    context.project = createEmptyProject();
    context.currentFile = juce::File();
    context.selectedPatternIndex = 0;
    context.selectedChannelId = context.project.channels.empty() ? -1
                                    : context.project.channels.front().id;
    context.engine.setBpm (context.project.bpm);
    context.structureChanged();
    context.dirty = false;
    updateWindowTitle();
}

void MainComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open project",
                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fable");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File())
                loadProjectFromFileAndSync (fc.getResult());
        });
}

void MainComponent::loadProjectFromFileAndSync (const juce::File& file)
{
    Project loaded;
    if (! loadProjectFromFile (file, loaded))
    {
        showStatus ("Could not load " + file.getFileName());
        return;
    }

    context.engine.stop();
    context.project = std::move (loaded);
    context.currentFile = file;
    context.selectedPatternIndex = 0;
    context.selectedChannelId = context.project.channels.empty() ? -1
                                    : context.project.channels.front().id;
    context.engine.setBpm (context.project.bpm);
    context.structureChanged();
    context.dirty = false;
    updateWindowTitle();
    showStatus ("Loaded " + file.getFileName());
}

void MainComponent::saveProject (bool saveAs, std::function<void (bool)> onComplete)
{
    // capture live plugin state into the model before writing
    for (auto& channel : context.project.channels)
        if (channel.type == GeneratorType::plugin)
            if (auto node = context.engine.getChannelNode (channel.id))
                if (auto* instance = node->getPluginInstance())
                {
                    channel.pluginState.reset();
                    instance->getStateInformation (channel.pluginState);
                }

    for (int trackIndex = 0; trackIndex < kNumMixerTracks; ++trackIndex)
        if (auto bus = context.engine.getMixerBus (trackIndex))
            for (int s = 0; s < kNumEffectSlots; ++s)
                if (auto* instance = bus->getSlot (s).plugin.get())
                {
                    auto& slot = context.project.mixerTracks[(size_t) trackIndex].slots[(size_t) s];
                    slot.pluginState.reset();
                    instance->getStateInformation (slot.pluginState);
                }

    if (! saveAs && context.currentFile.existsAsFile())
    {
        if (saveProjectToFile (context.project, context.currentFile))
        {
            context.dirty = false;
            showStatus ("Saved " + context.currentFile.getFileName());
            if (onComplete) onComplete (true);
        }
        else
        {
            showStatus ("Save failed");
            if (onComplete) onComplete (false);
        }
        return;
    }

    chooser = std::make_unique<juce::FileChooser> ("Save project",
                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile (context.project.name + ".fable"), "*.fable");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, onComplete] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
            {
                if (onComplete) onComplete (false);
                return;
            }
            if (! file.hasFileExtension ("fable"))
                file = file.withFileExtension ("fable");

            context.project.name = file.getFileNameWithoutExtension();
            if (saveProjectToFile (context.project, file))
            {
                context.currentFile = file;
                context.dirty = false;
                updateWindowTitle();
                showStatus ("Saved " + file.getFileName());
                if (onComplete) onComplete (true);
            }
            else
            {
                showStatus ("Save failed");
                if (onComplete) onComplete (false);
            }
        });
}

void MainComponent::requestClose (std::function<void()> onConfirmedClose)
{
    if (! context.dirty)
    {
        if (onConfirmedClose) onConfirmedClose();
        return;
    }

    auto* editor = new juce::AlertWindow ("Unsaved changes",
                                          "Save your project before closing?",
                                          juce::MessageBoxIconType::WarningIcon);
    editor->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    editor->addButton ("Don't Save", 2);
    editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    editor->enterModalState (true, juce::ModalCallbackFunction::create ([this, editor, onConfirmedClose] (int result)
    {
        if (result == 1)
            saveProject (false, [onConfirmedClose] (bool saved)
            {
                if (saved && onConfirmedClose)
                    onConfirmedClose();
            });
        else if (result == 2)
        {
            if (onConfirmedClose) onConfirmedClose();
        }
        delete editor;
    }), false);
}

void MainComponent::exportWav()
{
    chooser = std::make_unique<juce::FileChooser> ("Export WAV",
                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile (context.project.name + ".wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            // the offline renderer needs exclusive use of the processing graph
            context.engine.stop();
            context.engine.shutdownDevice();
            const bool ok = context.engine.renderToWav (context.project, file,
                                                        ! context.project.clips.empty());
            context.engine.initialiseDevice();
            context.structureChanged();   // re-prepare after device restart

            showStatus (ok ? "Exported " + file.getFullPathName() : "Export failed");
        });
}

void MainComponent::openPluginEditor (juce::AudioPluginInstance* instance, const juce::String& title)
{
    if (instance == nullptr)
        return;

    for (auto& window : pluginWindows)
    {
        if (&window->getPlugin() == instance)
        {
            window->toFront (true);
            return;
        }
    }

    pluginWindows.push_back (std::make_unique<PluginWindow> (*instance, title,
        [this] (PluginWindow* w)
        {
            pluginWindows.erase (std::remove_if (pluginWindows.begin(), pluginWindows.end(),
                                                 [w] (auto& p) { return p.get() == w; }),
                                 pluginWindows.end());
        }));
}

void MainComponent::showStatus (const juce::String& message)
{
    statusMessage = message;
    statusMessageTime = juce::Time::getMillisecondCounter();
    repaint();
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ("FableStudio - " + (context.currentFile == juce::File()
                                                 ? context.project.name
                                                 : context.currentFile.getFileNameWithoutExtension()));
}

void MainComponent::timerCallback()
{
    if (statusMessage.isNotEmpty()
        && juce::Time::getMillisecondCounter() - statusMessageTime > 5000)
    {
        statusMessage.clear();
        repaint();
    }
    updatePanelTabStates();
}

bool MainComponent::isPanelFrontmost (const FloatingPanel& panel) const
{
    return panel.isVisible()
        && workspace.getIndexOfChildComponent (&panel) == workspace.getNumChildComponents() - 1;
}

void MainComponent::togglePanel (FloatingPanel& panel)
{
    if (isPanelFrontmost (panel))
        panel.setVisible (false);
    else
        panel.toFrontAndShow();
    updatePanelTabStates();
}

void MainComponent::updatePanelTabStates()
{
    playlistTabButton.setToggleState    (isPanelFrontmost (playlistPanel),    juce::dontSendNotification);
    channelRackTabButton.setToggleState (isPanelFrontmost (channelRackPanel), juce::dontSendNotification);
    pianoRollTabButton.setToggleState   (isPanelFrontmost (pianoRollPanel),   juce::dontSendNotification);
    mixerTabButton.setToggleState       (isPanelFrontmost (mixerPanel),       juce::dontSendNotification);
}

} // namespace fable
