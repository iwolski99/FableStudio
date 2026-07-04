#include "TransportBar.h"

namespace fable
{

TransportBar::TransportBar (AppContext& ctx) : context (ctx)
{
    playButton.setColour (juce::TextButton::buttonColourId, colours::panelLight);
    playButton.setColour (juce::TextButton::textColourOffId, colours::led);
    playButton.onClick = [this]
    {
        if (context.engine.isPlaying())
            context.engine.stop();
        else
            context.engine.play();
    };
    addAndMakeVisible (playButton);

    stopButton.onClick = [this] { context.engine.stop(); };
    addAndMakeVisible (stopButton);

    auto styleModeButton = [this] (juce::TextButton& b, bool song)
    {
        b.setClickingTogglesState (false);
        b.onClick = [this, song]
        {
            context.engine.setSongMode (song);
            patButton.repaint();
            songButton.repaint();
        };
        addAndMakeVisible (b);
    };
    styleModeButton (patButton, false);
    styleModeButton (songButton, true);

    bpmValue.suffix = " bpm";
    bpmValue.getValue = [this] { return context.engine.getBpm(); };
    bpmValue.setValue = [this] (double v)
    {
        context.project.bpm = juce::jlimit (kMinBpm, kMaxBpm, v);
        context.engine.setBpm (context.project.bpm);
        context.dirty = true;
    };
    bpmValue.onEditRequest = [this]
    {
        auto* editor = new juce::AlertWindow ("Tempo", "Enter BPM:", juce::MessageBoxIconType::NoIcon);
        editor->addTextEditor ("bpm", juce::String (context.engine.getBpm(), 1));
        editor->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        editor->enterModalState (true, juce::ModalCallbackFunction::create ([this, editor] (int result)
        {
            if (result == 1)
                bpmValue.setValue (editor->getTextEditorContents ("bpm").getDoubleValue());
            bpmValue.repaint();
            delete editor;
        }), false);
    };
    addAndMakeVisible (bpmValue);

    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, colours::text);
    positionLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (positionLabel);

    startTimerHz (30);
    setOpaque (true);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (colours::titlebar);
    g.setColour (colours::outline);
    g.drawLine (0.0f, (float) getHeight() - 0.5f, (float) getWidth(), (float) getHeight() - 0.5f);

    // mode highlight
    auto highlight = [&g] (juce::TextButton& b, bool active)
    {
        if (active)
        {
            g.setColour (colours::accent);
            g.fillRoundedRectangle (b.getBounds().toFloat().expanded (1.0f), 4.0f);
        }
    };
    highlight (patButton,  ! context.engine.isSongMode());
    highlight (songButton, context.engine.isSongMode());

    // level meter (right side)
    auto meterArea = getLocalBounds().removeFromRight (110).reduced (6, 8);
    auto cpuArea = meterArea.removeFromBottom (0);
    juce::ignoreUnused (cpuArea);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto row = juce::Rectangle<int> (meterArea.getX(), meterArea.getY() + ch * (meterArea.getHeight() / 2 + 1),
                                         meterArea.getWidth() - 46, meterArea.getHeight() / 2 - 2);
        g.setColour (colours::panelDark.darker (0.4f));
        g.fillRect (row);
        const float level = juce::jlimit (0.0f, 1.0f, displayedLevel[ch]);
        g.setColour (level > 0.95f ? juce::Colours::red : colours::led);
        g.fillRect (row.withWidth ((int) ((float) row.getWidth() * level)));
    }
    g.setColour (colours::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (juce::String ((int) (context.engine.getCpuLoad() * 100.0)) + "% cpu",
                meterArea.removeFromRight (44), juce::Justification::centredRight);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (6, 6);
    playButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (4);
    stopButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (14);
    patButton.setBounds (area.removeFromLeft (52).reduced (0, 2));
    area.removeFromLeft (4);
    songButton.setBounds (area.removeFromLeft (52).reduced (0, 2));
    area.removeFromLeft (14);
    bpmValue.setBounds (area.removeFromLeft (110));
    area.removeFromLeft (14);
    positionLabel.setBounds (area.removeFromLeft (130));
}

void TransportBar::timerCallback()
{
    const double ticks = context.engine.getPlayheadTicks();
    const int bar  = (int) (ticks / kTicksPerBar) + 1;
    const int beat = ((int) (ticks / kPPQ)) % 4 + 1;
    const int step = ((int) (ticks / kTicksPerStep)) % 4 + 1;
    positionLabel.setText (juce::String (bar).paddedLeft ('0', 3) + " : "
                           + juce::String (beat) + " : " + juce::String (step),
                           juce::dontSendNotification);

    playButton.setColour (juce::TextButton::buttonColourId,
                          context.engine.isPlaying() ? colours::accent.darker (0.1f) : colours::panelLight);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float level = context.engine.getMasterLevel (ch);
        displayedLevel[ch] = juce::jmax (level, displayedLevel[ch] * 0.85f);
    }
    if (auto bus = context.engine.getMixerBus (0))
        bus->decayMeters();

    repaint();
}

} // namespace fable
