#include "InstrumentEditor.h"
#include "../Common.h"

namespace fable
{

static constexpr int kCellW    = 92;
static constexpr int kCellH    = 96;
static constexpr int kTopBar    = 40;
static constexpr int kPadding   = 12;
static constexpr int kMaxCols   = 6;

InstrumentEditor::InstrumentEditor (AppContext& ctx, int channelIdToUse, juce::String titleToUse,
                                    std::vector<ParamSpec> specsToUse, juce::String presetSubdirToUse)
    : context (ctx), channelId (channelIdToUse), title (std::move (titleToUse)),
      specs (std::move (specsToUse)), presetSubdir (std::move (presetSubdirToUse))
{
    // Make sure the model has an entry for every parameter so the controls and
    // the engine start from the same, complete set of values.
    if (auto* c = channel())
        applyDefaults (specs, c->synthParams);

    buildControls();

    initButton.onClick = [this] { initPatch(); };
    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };
    for (auto* b : { &initButton, &saveButton, &loadButton })
        addAndMakeVisible (*b);

    const int cols = juce::jmin (kMaxCols, (int) controls.size());
    const int rows = ((int) controls.size() + kMaxCols - 1) / kMaxCols;
    setSize (juce::jmax (3, cols) * kCellW + 2 * kPadding,
             kTopBar + rows * kCellH + kPadding);

    loadFromModel();
}

void InstrumentEditor::buildControls()
{
    for (auto& spec : specs)
    {
        auto control = std::make_unique<Control>();
        control->spec = spec;

        control->name.setText (spec.label, juce::dontSendNotification);
        control->name.setJustificationType (juce::Justification::centred);
        control->name.setFont (juce::Font (juce::FontOptions (10.5f)));
        control->name.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (control->name);

        if (spec.isChoice)
        {
            auto box = std::make_unique<juce::ComboBox>();
            for (int i = 0; i < spec.choices.size(); ++i)
                box->addItem (spec.choices[i], i + 1);
            const juce::String key = spec.key;
            box->onChange = [this, key, raw = box.get()]
            {
                writeParam (key, (float) (raw->getSelectedId() - 1));
            };
            addAndMakeVisible (*box);
            control->combo = std::move (box);
        }
        else
        {
            auto knob = std::make_unique<juce::Slider>();
            knob->setSliderStyle (juce::Slider::RotaryVerticalDrag);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, kCellW - 12, 16);
            // A little skew so time/frequency knobs feel musical rather than linear.
            const bool wide = (spec.max / juce::jmax (0.0001f, spec.min)) > 50.0f && spec.min > 0.0f;
            knob->setRange (spec.min, spec.max, spec.max > 40.0f ? 1.0 : 0.001);
            if (wide)
                knob->setSkewFactor (0.35);
            knob->setTextValueSuffix (spec.suffix);
            knob->setDoubleClickReturnValue (true, spec.defaultValue);
            const juce::String key = spec.key;
            knob->onValueChange = [this, key, raw = knob.get()]
            {
                writeParam (key, (float) raw->getValue());
            };
            addAndMakeVisible (*knob);
            control->knob = std::move (knob);
        }

        controls.push_back (std::move (control));
    }
}

void InstrumentEditor::loadFromModel()
{
    auto* c = channel();
    if (c == nullptr)
        return;

    for (auto& control : controls)
    {
        auto it = c->synthParams.find (control->spec.key);
        const float value = it != c->synthParams.end() ? it->second : control->spec.defaultValue;
        if (control->combo != nullptr)
            control->combo->setSelectedId ((int) (value + 0.5f) + 1, juce::dontSendNotification);
        else if (control->knob != nullptr)
            control->knob->setValue (value, juce::dontSendNotification);
    }
}

void InstrumentEditor::writeParam (const juce::String& key, float value)
{
    if (auto* c = channel())
    {
        c->synthParams[key] = value;
        context.instrumentParamsChanged();
    }
}

void InstrumentEditor::initPatch()
{
    if (auto* c = channel())
    {
        for (auto& spec : specs)
            c->synthParams[spec.key] = spec.defaultValue;
        context.instrumentParamsChanged();
        loadFromModel();
    }
}

void InstrumentEditor::savePreset()
{
    auto* c = channel();
    if (c == nullptr)
        return;

    auto dir = getAppDataDir().getChildFile ("Presets").getChildFile (presetSubdir);
    dir.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Save " + title + " preset", dir, "*.fablepreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            auto* ch = channel();
            if (file == juce::File() || ch == nullptr)
                return;
            file = file.withFileExtension ("fablepreset");

            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            root->setProperty ("fablePreset", presetSubdir);
            for (auto& spec : specs)
            {
                auto v = ch->synthParams.find (spec.key);
                root->setProperty (spec.key, v != ch->synthParams.end() ? v->second : spec.defaultValue);
            }
            file.replaceWithText (juce::JSON::toString (juce::var (root.get())));
            if (context.showStatusMessage)
                context.showStatusMessage ("Saved preset: " + file.getFileNameWithoutExtension());
        });
}

void InstrumentEditor::loadPreset()
{
    auto dir = getAppDataDir().getChildFile ("Presets").getChildFile (presetSubdir);
    dir.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Load " + title + " preset", dir, "*.fablepreset");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            auto* ch = channel();
            if (file == juce::File() || ! file.existsAsFile() || ch == nullptr)
                return;

            auto parsed = juce::JSON::parse (file.loadFileAsString());
            if (auto* obj = parsed.getDynamicObject())
            {
                for (auto& spec : specs)
                    if (obj->hasProperty (spec.key))
                        ch->synthParams[spec.key] = spec.clamp ((float) (double) obj->getProperty (spec.key));
                context.instrumentParamsChanged();
                loadFromModel();
                if (context.showStatusMessage)
                    context.showStatusMessage ("Loaded preset: " + file.getFileNameWithoutExtension());
            }
        });
}

void InstrumentEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Brushed-metal-ish vertical gradient for an analog hardware feel.
    juce::ColourGradient grad (juce::Colour (0xff2f333a), 0.0f, 0.0f,
                               juce::Colour (0xff1a1d21), 0.0f, (float) getHeight(), false);
    g.setGradientFill (grad);
    g.fillRect (bounds);

    auto top = bounds.removeFromTop (kTopBar);
    g.setColour (colours::titlebar);
    g.fillRect (top);
    g.setColour (colours::accent);
    g.fillRect (top.removeFromLeft (4));
    g.setColour (colours::text);
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText (title, 14, 0, 240, kTopBar, juce::Justification::centredLeft);

    g.setColour (colours::outline);
    g.drawRect (getLocalBounds(), 1);
}

void InstrumentEditor::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (kTopBar).reduced (6, 7);
    loadButton.setBounds (top.removeFromRight (96));
    top.removeFromRight (6);
    saveButton.setBounds (top.removeFromRight (96));
    top.removeFromRight (6);
    initButton.setBounds (top.removeFromRight (52));

    area = area.reduced (kPadding, 0);
    for (size_t i = 0; i < controls.size(); ++i)
    {
        const int col = (int) i % kMaxCols;
        const int row = (int) i / kMaxCols;
        juce::Rectangle<int> cell (area.getX() + col * kCellW,
                                   area.getY() + row * kCellH,
                                   kCellW, kCellH);
        auto inner = cell.reduced (6);
        controls[i]->name.setBounds (inner.removeFromTop (16));
        if (controls[i]->combo != nullptr)
            controls[i]->combo->setBounds (inner.removeFromTop (26).reduced (0, 2));
        else if (controls[i]->knob != nullptr)
            controls[i]->knob->setBounds (inner);
    }
}

} // namespace fable
