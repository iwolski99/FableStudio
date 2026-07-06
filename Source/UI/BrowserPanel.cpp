#include "BrowserPanel.h"

namespace fable
{

BrowserPanel::BrowserPanel (AppContext& ctx) : context (ctx)
{
    for (auto* header : { &pluginsHeader, &samplesHeader })
    {
        header->setColour (juce::Label::textColourId, colours::textDim);
        header->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        addAndMakeVisible (*header);
    }

    pluginList.setModel (this);
    pluginList.setRowHeight (22);
    pluginList.setColour (juce::ListBox::backgroundColourId, colours::panelDark);
    addAndMakeVisible (pluginList);

    scanThread.startThread();
    fileFilter = std::make_unique<juce::WildcardFileFilter> (
        context.engine.getFormatManager().getWildcardForAllFormats() + ";*.fable",
        "*", "audio files");
    dirContents = std::make_unique<juce::DirectoryContentsList> (fileFilter.get(), scanThread);

    loadSampleRoots();

    rootBox.onChange = [this]
    {
        const int index = rootBox.getSelectedItemIndex();
        if (index >= 0)
            setRoot (index);
    };
    addAndMakeVisible (rootBox);

    addFolderButton.setTooltip ("Add a sample folder (scans that folder + subfolders)");
    addFolderButton.onClick = [this] { addFolderClicked(); };
    addAndMakeVisible (addFolderButton);

    removeFolderButton.setTooltip ("Remove the selected folder from the list");
    removeFolderButton.onClick = [this] { removeCurrentFolderClicked(); };
    addAndMakeVisible (removeFolderButton);

    refreshRootBox();
    setRoot (0);

    fileTree = std::make_unique<juce::FileTreeComponent> (*dirContents);
    fileTree->addListener (this);
    fileTree->setColour (juce::TreeView::backgroundColourId, colours::panelDark);
    // Non-empty description makes juce::TreeView start a native drag on its own;
    // the actual file dropped is resolved by the target from the source
    // FileTreeComponent's selection (see audioFileFromDragSource in
    // ChannelRackPanel.cpp / PlaylistPanel.cpp), since the description string
    // itself is the same for every row.
    fileTree->setDragAndDropDescription ("audiofile");
    addAndMakeVisible (*fileTree);

    previewPlayer.setSource (&previewTransport);
    context.engine.getDeviceManager().addAudioCallback (&previewPlayer);

    context.structureBroadcaster.addChangeListener (this);
    refreshPlugins();
    setOpaque (true);
}

BrowserPanel::~BrowserPanel()
{
    context.engine.getDeviceManager().removeAudioCallback (&previewPlayer);
    previewPlayer.setSource (nullptr);
    context.structureBroadcaster.removeChangeListener (this);
    fileTree->removeListener (this);
}

void BrowserPanel::loadSampleRoots()
{
    sampleRoots.clear();
    sampleRoots.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    auto file = getAppDataDir().getChildFile ("sample-folders.txt");
    if (file.existsAsFile())
        for (auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
            if (line.isNotEmpty() && juce::File (line).isDirectory())
                sampleRoots.add (juce::File (line));
}

void BrowserPanel::saveSampleRoots()
{
    juce::StringArray lines;
    for (int i = 1; i < sampleRoots.size(); ++i)   // skip Home at index 0, it's implicit
        lines.add (sampleRoots.getReference (i).getFullPathName());
    getAppDataDir().getChildFile ("sample-folders.txt").replaceWithText (lines.joinIntoString ("\n"));
}

void BrowserPanel::refreshRootBox()
{
    rootBox.clear (juce::dontSendNotification);
    rootBox.addItem ("Home", 1);
    for (int i = 1; i < sampleRoots.size(); ++i)
        rootBox.addItem (sampleRoots.getReference (i).getFileName(), i + 1);
    rootBox.setSelectedItemIndex (juce::jlimit (0, sampleRoots.size() - 1, rootBox.getSelectedItemIndex()),
                                  juce::dontSendNotification);
}

void BrowserPanel::setRoot (int rootIndex)
{
    if (rootIndex < 0 || rootIndex >= sampleRoots.size())
        return;
    rootBox.setSelectedItemIndex (rootIndex, juce::dontSendNotification);
    removeFolderButton.setEnabled (rootIndex != 0);
    dirContents->setDirectory (sampleRoots.getReference (rootIndex), true, true);
}

void BrowserPanel::addFolderClicked()
{
    folderChooser = std::make_unique<juce::FileChooser> ("Add sample folder",
                        juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder == juce::File() || ! folder.isDirectory())
                return;
            if (sampleRoots.contains (folder))
            {
                setRoot (sampleRoots.indexOf (folder));
                return;
            }
            sampleRoots.add (folder);
            saveSampleRoots();
            refreshRootBox();
            setRoot (sampleRoots.size() - 1);
            if (context.showStatusMessage)
                context.showStatusMessage ("Added sample folder: " + folder.getFullPathName());
        });
}

void BrowserPanel::removeCurrentFolderClicked()
{
    const int index = rootBox.getSelectedItemIndex();
    if (index <= 0 || index >= sampleRoots.size())   // Home (0) can't be removed
        return;
    sampleRoots.remove (index);
    saveSampleRoots();
    refreshRootBox();
    setRoot (0);
}

void BrowserPanel::refreshPlugins()
{
    descriptions.clear();
    descriptions.addArray (context.plugins.getInstruments());
    descriptions.addArray (context.plugins.getEffects());
    pluginList.updateContent();
    pluginList.repaint();
}

void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshPlugins();
}

void BrowserPanel::stopPreview()
{
    previewTransport.stop();
    previewTransport.setSource (nullptr);
    previewReaderSource.reset();
    previewFilePath.clear();
}

void BrowserPanel::fileClicked (const juce::File& file, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! context.isSupportedAudioFile (file))
        return;

    // Clicking the sound that's already previewing stops it (click again to
    // cancel a long sample instead of waiting for it to finish).
    const bool sameAsPlaying = previewTransport.isPlaying()
                            && previewFilePath == file.getFullPathName();
    stopPreview();
    if (sameAsPlaying)
        return;

    if (auto* reader = context.engine.getFormatManager().createReaderFor (file))
    {
        previewReaderSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        previewTransport.setSource (previewReaderSource.get(), 0, nullptr, reader->sampleRate);
        previewFilePath = file.getFullPathName();
        previewTransport.start();
    }
}

int BrowserPanel::getNumRows()
{
    return juce::jmax (1, descriptions.size());
}

void BrowserPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (descriptions.isEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("no plugins - scan in Settings", 6, 0, width - 12, height,
                    juce::Justification::centredLeft);
        return;
    }
    if (row >= descriptions.size())
        return;

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (0, 0, width, height);
    }

    const auto& d = descriptions.getReference (row);
    g.setColour (d.isInstrument ? colours::led : colours::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ((d.isInstrument ? juce::String::fromUTF8 ("\xf0\x9f\x8e\xb9 ") : juce::String::fromUTF8 ("\xf0\x9f\x94\x8a "))
                    + d.name,
                6, 0, width - 12, height, juce::Justification::centredLeft);
}

void BrowserPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= descriptions.size())
        return;
    const auto& d = descriptions.getReference (row);

    if (d.isInstrument)
    {
        const int id = context.project.addChannel (GeneratorType::plugin, d.name);
        context.project.channelById (id)->pluginIdentifier = d.createIdentifierString();
        context.selectedChannelId = id;
        context.structureChanged();
        if (context.showStatusMessage)
            context.showStatusMessage ("Added channel: " + d.name);
        // Pop the instrument's editor open (instance built synchronously).
        if (auto node = context.engine.getChannelNode (id))
            if (auto* instance = node->getPluginInstance())
                if (context.openPluginEditor)
                    context.openPluginEditor (instance, d.name);
    }
    else
    {
        // add to the selected mixer track's first free slot
        auto& track = context.project.mixerTracks[(size_t) context.selectedMixerTrack];
        for (int slotIndex = 0; slotIndex < (int) track.slots.size(); ++slotIndex)
        {
            auto& slot = track.slots[(size_t) slotIndex];
            if (slot.type == EffectType::none)
            {
                slot.type = EffectType::plugin;
                slot.pluginIdentifier = d.createIdentifierString();
                context.structureChanged();
                if (context.showStatusMessage)
                    context.showStatusMessage ("Added " + d.name + " to mixer track "
                                               + juce::String (context.selectedMixerTrack));
                // Pop the effect's editor open (instance built synchronously).
                if (auto bus = context.engine.getMixerBus (context.selectedMixerTrack))
                    if (auto* instance = bus->getSlot (slotIndex).plugin.get())
                        if (context.openPluginEditor)
                            context.openPluginEditor (instance, d.name);
                return;
            }
        }
        if (context.showStatusMessage)
            context.showStatusMessage ("No free effect slot on the selected mixer track");
    }
}

void BrowserPanel::fileDoubleClicked (const juce::File& file)
{
    if (file.isDirectory())
        return;

    if (file.hasFileExtension ("fable"))
    {
        if (context.showStatusMessage)
            context.showStatusMessage ("Use File > Open to load projects");
        return;
    }

    context.addSamplerChannelFromFile (file);
    if (context.showStatusMessage)
        context.showStatusMessage ("Added sampler channel: " + file.getFileName());
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panelDark.darker (0.1f));
    g.setColour (colours::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    pluginsHeader.setBounds (area.removeFromTop (18));
    pluginList.setBounds (area.removeFromTop (juce::jmax (80, area.getHeight() / 3)));
    area.removeFromTop (6);
    samplesHeader.setBounds (area.removeFromTop (18));

    auto rootRow = area.removeFromTop (24);
    removeFolderButton.setBounds (rootRow.removeFromRight (22));
    rootRow.removeFromRight (3);
    addFolderButton.setBounds (rootRow.removeFromRight (22));
    rootRow.removeFromRight (3);
    rootBox.setBounds (rootRow);
    area.removeFromTop (4);

    fileTree->setBounds (area);
}

} // namespace fable
