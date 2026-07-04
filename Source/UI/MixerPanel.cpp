#include "MixerPanel.h"

namespace fable
{

static constexpr int kStripWidth = 68;
static constexpr int kSlotRackWidth = 230;

// ------------------------------------------------------------------ strip

class MixerPanel::Strip : public juce::Component
{
public:
    Strip (AppContext& ctx, int trackIndexToUse, MixerPanel& ownerToUse)
        : context (ctx), trackIndex (trackIndexToUse), owner (ownerToUse)
    {
        auto& model = context.project.mixerTracks[(size_t) trackIndex];

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        fader.setRange (0.0, 1.25, 0.001);
        fader.setValue (model.volume, juce::dontSendNotification);
        fader.setDoubleClickReturnValue (true, 0.8);
        fader.onValueChange = [this]
        {
            track().volume = (float) fader.getValue();
            context.mixerParamsChanged();
        };
        addAndMakeVisible (fader);

        panKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        panKnob.setRange (-1.0, 1.0, 0.001);
        panKnob.setValue (model.pan, juce::dontSendNotification);
        panKnob.setDoubleClickReturnValue (true, 0.0);
        panKnob.onValueChange = [this]
        {
            track().pan = (float) panKnob.getValue();
            context.mixerParamsChanged();
        };
        addAndMakeVisible (panKnob);

        muteButton.onClick = [this]
        {
            track().muted = ! track().muted;
            styleButtons();
            context.mixerParamsChanged();
        };
        addAndMakeVisible (muteButton);

        soloButton.onClick = [this]
        {
            track().solo = ! track().solo;
            styleButtons();
            context.mixerParamsChanged();
        };
        if (trackIndex > 0)
            addAndMakeVisible (soloButton);

        styleButtons();
    }

    MixerTrackModel& track() { return context.project.mixerTracks[(size_t) trackIndex]; }

    void styleButtons()
    {
        muteButton.setColour (juce::TextButton::buttonColourId,
                              track().muted ? juce::Colour (0xffc0504a) : colours::panelLight);
        soloButton.setColour (juce::TextButton::buttonColourId,
                              track().solo ? colours::accent : colours::panelLight);
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override;   // defined after SlotRack

    void paint (juce::Graphics& g) override
    {
        const bool selected = context.selectedMixerTrack == trackIndex;
        g.fillAll (selected ? colours::panelLight.withAlpha (0.5f)
                            : (trackIndex == 0 ? colours::panelDark.brighter (0.06f) : colours::panel));

        g.setColour (colours::outline);
        g.drawRect (getLocalBounds());

        g.setColour (selected ? colours::accent : colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, trackIndex == 0 ? juce::Font::bold : juce::Font::plain)));
        g.drawText (trackIndex == 0 ? "Master" : juce::String (trackIndex),
                    getLocalBounds().removeFromTop (18), juce::Justification::centred);

        // meters flanking the fader
        if (auto bus = context.engine.getMixerBus (trackIndex))
        {
            auto meterArea = getLocalBounds().reduced (6, 0)
                                 .withTop (46).withBottom (getHeight() - 56);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto col = ch == 0 ? meterArea.removeFromLeft (5)
                                   : meterArea.removeFromRight (5);
                g.setColour (colours::panelDark.darker (0.4f));
                g.fillRect (col);
                const float level = juce::jlimit (0.0f, 1.0f, displayed[ch]);
                const int h = (int) (level * (float) col.getHeight());
                g.setColour (level > 0.95f ? juce::Colours::red : colours::led);
                g.fillRect (col.removeFromBottom (h));
            }
        }
    }

    void updateMeters()
    {
        if (auto bus = context.engine.getMixerBus (trackIndex))
        {
            for (int ch = 0; ch < 2; ++ch)
                displayed[ch] = juce::jmax (bus->getMeterLevel (ch), displayed[ch] * 0.85f);
            bus->decayMeters();
        }
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (18);
        panKnob.setBounds (area.removeFromTop (28).reduced (18, 0));
        auto buttons = area.removeFromBottom (52);
        muteButton.setBounds (buttons.removeFromTop (24).reduced (8, 2));
        soloButton.setBounds (buttons.removeFromTop (24).reduced (8, 2));
        fader.setBounds (area.reduced (14, 2));
    }

    AppContext& context;
    const int trackIndex;
    MixerPanel& owner;
    juce::Slider fader, panKnob;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    float displayed[2] { 0, 0 };
};

// ------------------------------------------------------------------ slot rack

class MixerPanel::SlotRack : public juce::Component
{
public:
    SlotRack (AppContext& ctx, MixerPanel& ownerToUse) : context (ctx), owner (ownerToUse)
    {
        for (int i = 0; i < kNumEffectSlots; ++i)
        {
            auto& row = rows[(size_t) i];

            row.enableButton.setClickingTogglesState (false);
            row.enableButton.onClick = [this, i]
            {
                auto& slot = trackModel().slots[(size_t) i];
                slot.enabled = ! slot.enabled;
                context.mixerParamsChanged();
                refresh();
            };
            addAndMakeVisible (row.enableButton);

            row.nameButton.onClick = [this, i] { slotMenu (i); };
            addAndMakeVisible (row.nameButton);
        }

        for (auto& knob : paramKnobs)
        {
            knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            knob.setRange (0.0, 1.0, 0.001);
            addChildComponent (knob);
        }

        refresh();
    }

    MixerTrackModel& trackModel()
    {
        return context.project.mixerTracks[(size_t) context.selectedMixerTrack];
    }

    void refresh()
    {
        auto& model = trackModel();
        for (int i = 0; i < kNumEffectSlots; ++i)
        {
            auto& row  = rows[(size_t) i];
            auto& slot = model.slots[(size_t) i];

            juce::String name = BuiltinEffect::typeName (slot.type);
            if (slot.type == EffectType::plugin)
            {
                name = "(plugin missing)";
                if (auto desc = context.plugins.findByIdentifier (slot.pluginIdentifier))
                    name = desc->name;
            }
            row.nameButton.setButtonText (juce::String (i + 1) + "  " + name);
            row.nameButton.setColour (juce::TextButton::buttonColourId,
                                      slot.type == EffectType::none ? colours::panelDark
                                                                    : colours::panelLight);
            row.enableButton.setColour (juce::TextButton::buttonColourId,
                                        slot.enabled && slot.type != EffectType::none
                                            ? colours::led.darker (0.35f) : colours::panelDark);
        }
        updateParamKnobs();
        repaint();
    }

    void updateParamKnobs()
    {
        auto& slot = trackModel().slots[(size_t) selectedSlot];
        auto bus = context.engine.getMixerBus (context.selectedMixerTrack);
        BuiltinEffect* fx = bus != nullptr ? bus->getSlot (selectedSlot).builtin.get() : nullptr;

        for (int k = 0; k < (int) paramKnobs.size(); ++k)
        {
            auto& knob = paramKnobs[(size_t) k];
            const bool show = fx != nullptr && k < fx->getNumParams();
            knob.setVisible (show);
            if (show)
            {
                knob.setValue (slot.params[(size_t) k], juce::dontSendNotification);
                knob.onValueChange = [this, k, &knob]
                {
                    trackModel().slots[(size_t) selectedSlot].params[(size_t) k] = (float) knob.getValue();
                    context.mixerParamsChanged();
                };
            }
        }
        paramNames.clear();
        if (fx != nullptr)
            for (int k = 0; k < fx->getNumParams(); ++k)
                paramNames.add (fx->getParamName (k));
    }

    void slotMenu (int slotIndex)
    {
        selectedSlot = slotIndex;

        juce::PopupMenu m;
        m.addSectionHeader ("Built-in effects");
        for (auto type : { EffectType::reverb, EffectType::delay, EffectType::eq3, EffectType::limiter })
            m.addItem (BuiltinEffect::typeName (type), [this, slotIndex, type]
            {
                auto& slot = trackModel().slots[(size_t) slotIndex];
                slot = EffectSlot();
                slot.type = type;
                context.structureChanged();
            });

        const auto effects = context.plugins.getEffects();
        if (! effects.isEmpty())
        {
            m.addSeparator();
            m.addSectionHeader ("VST3 effects");
            for (const auto& desc : effects)
                m.addItem (desc.name, [this, slotIndex, desc]
                {
                    auto& slot = trackModel().slots[(size_t) slotIndex];
                    slot = EffectSlot();
                    slot.type = EffectType::plugin;
                    slot.pluginIdentifier = desc.createIdentifierString();
                    context.structureChanged();
                });
        }

        auto& slot = trackModel().slots[(size_t) slotIndex];
        if (slot.type != EffectType::none)
        {
            m.addSeparator();
            if (slot.type == EffectType::plugin)
                m.addItem ("Open plugin editor", [this, slotIndex]
                {
                    if (auto bus = context.engine.getMixerBus (context.selectedMixerTrack))
                        if (auto* instance = bus->getSlot (slotIndex).plugin.get())
                            if (context.openPluginEditor)
                                context.openPluginEditor (instance, instance->getName());
                });
            m.addItem ("Remove effect", [this, slotIndex]
            {
                trackModel().slots[(size_t) slotIndex] = EffectSlot();
                context.structureChanged();
            });
        }

        m.showMenuAsync (juce::PopupMenu::Options()
                             .withTargetComponent (rows[(size_t) slotIndex].nameButton),
                         [this] (int) { updateParamKnobs(); repaint(); });
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panelDark.brighter (0.03f));
        g.setColour (colours::outline);
        g.drawRect (getLocalBounds());

        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        const auto trackName = context.selectedMixerTrack == 0
                                   ? juce::String ("Master")
                                   : "Insert " + juce::String (context.selectedMixerTrack);
        g.drawText (trackName + "  -  effect slots", getLocalBounds().removeFromTop (22).reduced (8, 0),
                    juce::Justification::centredLeft);

        // param labels under knobs
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (int k = 0; k < paramNames.size(); ++k)
        {
            auto& knob = paramKnobs[(size_t) k];
            if (knob.isVisible())
                g.drawText (paramNames[k],
                            knob.getX() - 6, knob.getBottom(), knob.getWidth() + 12, 12,
                            juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4);
        area.removeFromTop (22);
        for (auto& row : rows)
        {
            auto r = area.removeFromTop (24);
            row.enableButton.setBounds (r.removeFromLeft (22).reduced (2));
            row.nameButton.setBounds (r.reduced (2));
        }
        area.removeFromTop (6);
        auto knobArea = area.removeFromTop (44);
        const int kw = knobArea.getWidth() / (int) paramKnobs.size();
        for (auto& knob : paramKnobs)
            knob.setBounds (knobArea.removeFromLeft (kw).reduced (6, 2).withHeight (36));
    }

    struct SlotRow
    {
        juce::TextButton enableButton { "" };
        juce::TextButton nameButton;
    };

    AppContext& context;
    MixerPanel& owner;
    std::array<SlotRow, kNumEffectSlots> rows;
    std::array<juce::Slider, 4> paramKnobs;
    juce::StringArray paramNames;
    int selectedSlot = 0;
};

void MixerPanel::Strip::mouseDown (const juce::MouseEvent&)
{
    context.selectedMixerTrack = trackIndex;
    owner.slotRack->refresh();
    owner.stripHolder.repaint();
}

// ------------------------------------------------------------------ panel

MixerPanel::MixerPanel (AppContext& ctx) : context (ctx)
{
    stripViewport.setViewedComponent (&stripHolder, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    slotRack = std::make_unique<SlotRack> (context, *this);
    addAndMakeVisible (*slotRack);

    context.structureBroadcaster.addChangeListener (this);
    rebuildStrips();
    startTimerHz (24);
    setOpaque (true);
}

MixerPanel::~MixerPanel()
{
    context.structureBroadcaster.removeChangeListener (this);
}

void MixerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    slotRack->refresh();
}

void MixerPanel::timerCallback()
{
    for (auto& strip : strips)
        if (strip->isShowing())
            strip->updateMeters();
}

void MixerPanel::rebuildStrips()
{
    strips.clear();
    for (int i = 0; i < kNumMixerTracks; ++i)
    {
        auto strip = std::make_unique<Strip> (context, i, *this);
        stripHolder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    resized();
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void MixerPanel::resized()
{
    auto area = getLocalBounds();
    slotRack->setBounds (area.removeFromRight (kSlotRackWidth));
    stripViewport.setBounds (area);

    stripHolder.setSize (juce::jmax (kNumMixerTracks * kStripWidth, stripViewport.getWidth()),
                         stripViewport.getHeight() - (stripViewport.isHorizontalScrollBarShown() ? 8 : 0));
    int x = 0;
    for (auto& strip : strips)
    {
        strip->setBounds (x, 0, kStripWidth, stripHolder.getHeight());
        x += kStripWidth;
    }
}

} // namespace fable
