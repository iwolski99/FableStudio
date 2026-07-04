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
    dirContents->setDirectory (juce::File::getSpecialLocation (juce::File::userHomeDirectory), true, true);

    fileTree = std::make_unique<juce::FileTreeComponent> (*dirContents);
    fileTree->addListener (this);
    fileTree->setColour (juce::TreeView::backgroundColourId, colours::panelDark);
    addAndMakeVisible (*fileTree);

    context.structureBroadcaster.addChangeListener (this);
    refreshPlugins();
    setOpaque (true);
}

BrowserPanel::~BrowserPanel()
{
    context.structureBroadcaster.removeChangeListener (this);
    fileTree->removeListener (this);
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
        context.structureChanged();
        if (context.showStatusMessage)
            context.showStatusMessage ("Added channel: " + d.name);
    }
    else
    {
        // add to the selected mixer track's first free slot
        auto& track = context.project.mixerTracks[(size_t) context.selectedMixerTrack];
        for (auto& slot : track.slots)
        {
            if (slot.type == EffectType::none)
            {
                slot.type = EffectType::plugin;
                slot.pluginIdentifier = d.createIdentifierString();
                context.structureChanged();
                if (context.showStatusMessage)
                    context.showStatusMessage ("Added " + d.name + " to mixer track "
                                               + juce::String (context.selectedMixerTrack));
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

    const int id = context.project.addChannel (GeneratorType::sampler,
                                               file.getFileNameWithoutExtension());
    context.project.channelById (id)->samplePath = file.getFullPathName();
    context.structureChanged();
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
    fileTree->setBounds (area);
}

} // namespace fable
