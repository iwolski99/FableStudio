#include "ChannelRackPanel.h"

namespace fable
{

static constexpr int kRowHeight  = 30;
static constexpr int kLeftWidth  = 224;   // led + knobs + name
static constexpr int kStepSize   = 24;

static juce::File audioFileFromDragDescription (const juce::var& description)
{
    const auto text = description.toString();
    if (! text.startsWith ("audiofile:"))
        return {};
    return juce::File (text.fromFirstOccurrenceOf ("audiofile:", false, false));
}

// Child controls (button/sliders) sit on top of the row and would otherwise
// swallow a right-click before it ever reaches ChannelRow::mouseDown, so
// right-clicking the channel appeared to do nothing. These forward it back.
class RightClickButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
};

class RightClickSlider : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
};

// ------------------------------------------------------------------ row

class ChannelRackPanel::ChannelRow : public juce::Component
{
public:
    ChannelRow (AppContext& ctx, int channelIdToUse, ChannelRackPanel& ownerToUse)
        : context (ctx), channelId (channelIdToUse), owner (ownerToUse)
    {
        auto* channel = context.project.channelById (channelId);
        jassert (channel != nullptr);

        muteLed.setClickingTogglesState (false);
        muteLed.onClick = [this]
        {
            if (auto* c = context.project.channelById (channelId))
            {
                c->muted = ! c->muted;
                context.channelParamsChanged();
                updateLedColour();
            }
        };
        muteLed.onRightClick = [this] { channelMenu(); };
        addAndMakeVisible (muteLed);
        updateLedColour();

        auto setupKnob = [this] (juce::Slider& s, float value, juce::Range<double> range,
                                 std::function<void (float)> apply)
        {
            s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.setRange (range.getStart(), range.getEnd(), 0.001);
            s.setValue (value, juce::dontSendNotification);
            s.setDoubleClickReturnValue (true, range.getStart() + range.getLength() * 0.5);
            s.onValueChange = [this, &s, apply] { apply ((float) s.getValue()); };
            addAndMakeVisible (s);
        };
        setupKnob (panKnob, channel->pan, { -1.0, 1.0 }, [this] (float v)
        {
            if (auto* c = context.project.channelById (channelId))
            {
                c->pan = v;
                context.channelParamsChanged();
            }
        });
        panKnob.setDoubleClickReturnValue (true, 0.0);
        panKnob.onRightClick = [this] { channelMenu(); };
        volKnob.onRightClick = [this] { channelMenu(); };
        setupKnob (volKnob, channel->volume, { 0.0, 1.0 }, [this] (float v)
        {
            if (auto* c = context.project.channelById (channelId))
            {
                c->volume = v;
                context.channelParamsChanged();
            }
        });

        nameButton.setButtonText (channel->name);
        nameButton.setColour (juce::TextButton::buttonColourId, channel->colour.darker (0.55f));
        nameButton.setColour (juce::TextButton::textColourOffId, colours::text);
        nameButton.onClick = [this]
        {
            context.selectChannel (channelId);
            // audition on select, like clicking a channel button
            context.engine.auditionNoteOn (channelId, rootNoteOf(), 0.8f);
            juce::Timer::callAfterDelay (220, [ctx = &context, id = channelId, pitch = rootNoteOf()]
                                         { ctx->engine.auditionNoteOff (id, pitch); });
        };
        nameButton.onRightClick = [this] { channelMenu(); };
        addAndMakeVisible (nameButton);
    }

    void updateLedColour()
    {
        auto* c = context.project.channelById (channelId);
        const bool audible = c != nullptr && ! c->muted;
        muteLed.setColour (juce::TextButton::buttonColourId,
                           audible ? colours::led.darker (0.15f)
                                   : colours::panelDark.darker (0.3f));
        muteLed.repaint();
    }

    int rootNoteOf() const
    {
        if (auto* c = context.project.channelById (channelId))
            return c->rootNote;
        return kDefaultRootNote;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && e.getPosition().x < kLeftWidth)
        {
            channelMenu();
            return;
        }
        handleStepClick (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override { handleStepClick (e); }

    void handleStepClick (const juce::MouseEvent& e)
    {
        auto* pattern = context.selectedPattern();
        if (pattern == nullptr)
            return;

        const int stepIndex = stepAt (e.getPosition().x);
        if (stepIndex < 0 || stepIndex >= pattern->lengthSteps)
            return;

        auto& data = pattern->dataFor (channelId);
        const bool value = ! e.mods.isRightButtonDown();   // right button erases
        if (data.steps[(size_t) stepIndex].on != value)
        {
            data.steps[(size_t) stepIndex].on = value;
            context.contentChanged();
            repaint();
        }
    }

    int stepAt (int x) const
    {
        auto* pattern = context.selectedPattern();
        if (pattern == nullptr)
            return -1;
        int sx = kLeftWidth;
        for (int i = 0; i < pattern->lengthSteps; ++i)
        {
            const int w = kStepSize + ((i % 4 == 3) ? 4 : 0);   // beat gap
            if (x >= sx && x < sx + kStepSize)
                return i;
            sx += w;
        }
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        auto* channel = context.project.channelById (channelId);
        auto* pattern = context.selectedPattern();
        if (channel == nullptr)
            return;

        const bool selected = context.selectedChannelId == channelId;
        g.fillAll (selected ? colours::panelLight.withAlpha (0.4f) : colours::panel);

        // steps
        if (pattern != nullptr)
        {
            const auto& data = const_cast<Pattern*> (pattern)->dataFor (channelId);
            int x = kLeftWidth;
            const double playTicks = context.engine.getPlayheadTicks();
            const int playStep = context.engine.isPlaying() && ! context.engine.isSongMode()
                                     ? (int) (playTicks / kTicksPerStep) % juce::jmax (1, pattern->lengthSteps)
                                     : -1;

            for (int i = 0; i < pattern->lengthSteps; ++i)
            {
                juce::Rectangle<float> cell ((float) x, 4.0f, (float) kStepSize - 2.0f, (float) kRowHeight - 8.0f);
                const bool beatGroup = (i / 4) % 2 == 0;
                auto base = beatGroup ? colours::stepBeat : colours::stepOffBeat;

                if (data.steps[(size_t) i].on)
                    g.setColour (base);
                else
                    g.setColour (base.withAlpha (0.16f));
                g.fillRoundedRectangle (cell, 3.0f);

                if (i == playStep)
                {
                    g.setColour (colours::playhead.withAlpha (0.55f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 2.0f);
                }

                x += kStepSize + ((i % 4 == 3) ? 4 : 0);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        muteLed.setBounds (area.removeFromLeft (kRowHeight));
        panKnob.setBounds (area.removeFromLeft (kRowHeight));
        volKnob.setBounds (area.removeFromLeft (kRowHeight));
        nameButton.setBounds (area.removeFromLeft (kLeftWidth - 3 * kRowHeight - 6).reduced (2, 4));
    }

    void channelMenu()
    {
        auto* channel = context.project.channelById (channelId);
        if (channel == nullptr)
            return;

        juce::PopupMenu m;
        m.addItem ("Rename...", [this] { renameChannel(); });
        m.addItem ("Delete channel", [this]
        {
            context.project.removeChannel (channelId);
            if (context.selectedChannelId == channelId)
                context.selectedChannelId = -1;
            context.structureChanged();
        });

        juce::PopupMenu route;
        for (int i = 0; i < kNumMixerTracks; ++i)
        {
            const auto name = i == 0 ? juce::String ("Master") : "Insert " + juce::String (i);
            route.addItem (name, true, channel->mixerTrack == i, [this, i]
            {
                if (auto* c = context.project.channelById (channelId))
                {
                    c->mixerTrack = i;
                    context.channelParamsChanged();
                }
            });
        }
        m.addSubMenu ("Route to mixer track", route);

        m.addItem ("Go to Piano Roll", [this]
        {
            context.openPianoRollForChannel (channelId);
        });

        if (channel->type == GeneratorType::sampler)
        {
            m.addSeparator();
            m.addItem ("Load sample...", [this] { loadSample(); });
        }
        if (channel->type == GeneratorType::plugin)
        {
            m.addSeparator();
            m.addItem ("Open plugin editor", [this]
            {
                if (auto node = context.engine.getChannelNode (channelId))
                    if (auto* instance = node->getPluginInstance())
                        if (context.openPluginEditor)
                            context.openPluginEditor (instance, nameButton.getButtonText());
            });
        }

        juce::PopupMenu steps;
        steps.addItem ("Fill every 4th", [this] { fillSteps (4); });
        steps.addItem ("Fill every 2nd", [this] { fillSteps (2); });
        steps.addItem ("Clear steps", [this] { fillSteps (0); });
        m.addSubMenu ("Steps", steps);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton));
    }

    void fillSteps (int every)
    {
        if (auto* pattern = context.selectedPattern())
        {
            auto& data = pattern->dataFor (channelId);
            for (size_t i = 0; i < data.steps.size(); ++i)
                data.steps[i].on = every > 0 && (int) i % every == 0;
            context.contentChanged();
            repaint();
        }
    }

    void renameChannel()
    {
        auto* editor = new juce::AlertWindow ("Rename channel", {}, juce::MessageBoxIconType::NoIcon);
        editor->addTextEditor ("name", nameButton.getButtonText());
        editor->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        editor->enterModalState (true, juce::ModalCallbackFunction::create ([this, editor] (int result)
        {
            if (result == 1)
                if (auto* c = context.project.channelById (channelId))
                {
                    c->name = editor->getTextEditorContents ("name");
                    nameButton.setButtonText (c->name);
                }
            delete editor;
        }), false);
    }

    void loadSample()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load sample",
                      juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                      context.engine.getFormatManager().getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File())
                    return;
                if (auto* c = context.project.channelById (channelId))
                {
                    c->samplePath = fc.getResult().getFullPathName();
                    c->name = fc.getResult().getFileNameWithoutExtension();
                    nameButton.setButtonText (c->name);
                    context.structureChanged();
                }
            });
    }

    AppContext& context;
    const int channelId;
    ChannelRackPanel& owner;

    RightClickButton muteLed { "" };
    RightClickSlider panKnob, volKnob;
    RightClickButton nameButton;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ------------------------------------------------------------------ header

class ChannelRackPanel::Header : public juce::Component
{
public:
    Header (AppContext& ctx, ChannelRackPanel& ownerToUse) : context (ctx), owner (ownerToUse)
    {
        prevPattern.onClick = [this] { context.selectPattern (context.selectedPatternIndex - 1); };
        nextPattern.onClick = [this] { context.selectPattern (context.selectedPatternIndex + 1); };
        addAndMakeVisible (prevPattern);
        addAndMakeVisible (nextPattern);

        patternBox.onChange = [this]
        {
            const int index = patternBox.getSelectedItemIndex();
            if (index >= 0 && index != context.selectedPatternIndex)
                context.selectPattern (index);
        };
        addAndMakeVisible (patternBox);

        addPatternButton.onClick = [this]
        {
            context.project.addPattern();
            context.selectedPatternIndex = (int) context.project.patterns.size() - 1;
            context.structureChanged();
        };
        addAndMakeVisible (addPatternButton);

        lengthBox.onChange = [this]
        {
            if (auto* pattern = context.selectedPattern())
            {
                const int steps = lengthBox.getText().getIntValue();
                if (steps > 0 && steps != pattern->lengthSteps)
                {
                    pattern->lengthSteps = steps;
                    for (auto& [id, data] : pattern->channelData)
                        data.steps.resize ((size_t) steps);
                    context.contentChanged();
                    owner.resized();
                    owner.repaint();
                }
            }
        };
        addAndMakeVisible (lengthBox);

        addChannelButton.setColour (juce::TextButton::buttonColourId, colours::accent.darker (0.4f));
        addChannelButton.onClick = [this] { owner.addChannelMenu(); };
        addAndMakeVisible (addChannelButton);

        refresh();
    }

    void refresh()
    {
        patternBox.clear (juce::dontSendNotification);
        int id = 1;
        for (auto& p : context.project.patterns)
            patternBox.addItem (p.name, id++);
        patternBox.setSelectedItemIndex (context.selectedPatternIndex, juce::dontSendNotification);

        lengthBox.clear (juce::dontSendNotification);
        for (int steps : { 16, 32, 48, 64, 128 })
            lengthBox.addItem (juce::String (steps) + " steps", steps);
        if (auto* pattern = context.selectedPattern())
        {
            if (lengthBox.indexOfItemId (pattern->lengthSteps) < 0)
                lengthBox.addItem (juce::String (pattern->lengthSteps) + " steps", pattern->lengthSteps);
            lengthBox.setSelectedId (pattern->lengthSteps, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panelDark);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4);
        prevPattern.setBounds (area.removeFromLeft (26));
        area.removeFromLeft (2);
        patternBox.setBounds (area.removeFromLeft (150));
        area.removeFromLeft (2);
        nextPattern.setBounds (area.removeFromLeft (26));
        area.removeFromLeft (2);
        addPatternButton.setBounds (area.removeFromLeft (30));
        area.removeFromLeft (10);
        lengthBox.setBounds (area.removeFromLeft (110));
        addChannelButton.setBounds (area.removeFromRight (110));
    }

private:
    AppContext& context;
    ChannelRackPanel& owner;
    juce::TextButton prevPattern { "<" }, nextPattern { ">" }, addPatternButton { "+" };
    juce::ComboBox patternBox, lengthBox;
    juce::TextButton addChannelButton { "+ Channel" };

    friend class ChannelRackPanel;
};

// ------------------------------------------------------------------ panel

ChannelRackPanel::ChannelRackPanel (AppContext& ctx) : context (ctx)
{
    header = std::make_unique<Header> (context, *this);
    addAndMakeVisible (*header);

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    context.structureBroadcaster.addChangeListener (this);
    context.contentBroadcaster.addChangeListener (this);

    rebuildRows();
    startTimerHz (24);   // playhead on steps
    setOpaque (true);
}

ChannelRackPanel::~ChannelRackPanel()
{
    context.structureBroadcaster.removeChangeListener (this);
    context.contentBroadcaster.removeChangeListener (this);
}

void ChannelRackPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    header->refresh();
    rebuildRows();
    repaint();
}

void ChannelRackPanel::timerCallback()
{
    if (context.engine.isPlaying())
        rowHolder.repaint();
}

void ChannelRackPanel::rebuildRows()
{
    rows.clear();
    for (auto& channel : context.project.channels)
    {
        auto row = std::make_unique<ChannelRow> (context, channel.id, *this);
        rowHolder.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    resized();
}

void ChannelRackPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    if (dragActive)
    {
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.5f), 6.0f, 2.0f);
    }
}

void ChannelRackPanel::resized()
{
    auto area = getLocalBounds();
    header->setBounds (area.removeFromTop (34));
    viewport.setBounds (area);

    int rowWidth = kLeftWidth + 8;
    if (auto* pattern = context.selectedPattern())
        rowWidth = kLeftWidth + pattern->lengthSteps * kStepSize + (pattern->lengthSteps / 4) * 4 + 8;

    rowHolder.setSize (juce::jmax (rowWidth, viewport.getMaximumVisibleWidth()),
                       juce::jmax ((int) rows.size() * kRowHeight, viewport.getMaximumVisibleHeight()));
    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds (0, y, rowHolder.getWidth(), kRowHeight);
        y += kRowHeight;
    }
}

void ChannelRackPanel::addChannelMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Built-in");
    m.addItem ("FableSynth", [this]
    {
        context.project.addChannel (GeneratorType::synth, "FableSynth");
        context.structureChanged();
    });

    juce::PopupMenu kits;
    for (auto* name : { "kick", "clap", "hat", "openhat", "snare" })
    {
        kits.addItem (juce::String (name), [this, name = juce::String (name)]
        {
            const int id = context.project.addChannel (GeneratorType::sampler, name);
            context.project.channelById (id)->samplePath = "builtin:" + name;
            context.structureChanged();
        });
    }
    m.addSubMenu ("Drum kit", kits);
    m.addItem ("Sampler (empty)", [this]
    {
        context.project.addChannel (GeneratorType::sampler, "Sampler");
        context.structureChanged();
    });

    const auto instruments = context.plugins.getInstruments();
    if (! instruments.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("VST3 instruments");
        for (const auto& desc : instruments)
        {
            m.addItem (desc.name, [this, desc]
            {
                const int id = context.project.addChannel (GeneratorType::plugin, desc.name);
                context.project.channelById (id)->pluginIdentifier = desc.createIdentifierString();
                context.structureChanged();
            });
        }
    }
    else
    {
        m.addSeparator();
        m.addItem ("(no VST3 instruments found - scan in Settings)", false, false, nullptr);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header->addChannelButton));
}

bool ChannelRackPanel::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    return context.isSupportedAudioFile (audioFileFromDragDescription (dragSourceDetails.description));
}

void ChannelRackPanel::itemDragEnter (const SourceDetails&)
{
    dragActive = true;
    repaint();
}

void ChannelRackPanel::itemDragExit (const SourceDetails&)
{
    dragActive = false;
    repaint();
}

void ChannelRackPanel::itemDropped (const SourceDetails& dragSourceDetails)
{
    dragActive = false;
    repaint();

    const auto file = audioFileFromDragDescription (dragSourceDetails.description);
    if (! context.isSupportedAudioFile (file))
        return;

    context.addSamplerChannelFromFile (file);
    if (context.showStatusMessage)
        context.showStatusMessage ("Added sampler channel: " + file.getFileName());
}

} // namespace fable
