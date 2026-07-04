#include "SettingsDialog.h"

namespace fable
{

// ------------------------------------------------------------- plugins tab

class SettingsDialog::PluginsTab : public juce::Component
{
public:
    explicit PluginsTab (AppContext& ctx) : context (ctx)
    {
        infoLabel.setColour (juce::Label::textColourId, colours::textDim);
        infoLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (infoLabel);

        folderBox.setMultiLine (true);
        folderBox.setReturnKeyStartsNewLine (true);
        folderBox.setTextToShowWhenEmpty ("extra VST3 folders, one per line",
                                          colours::textDim);
        folderBox.setText (context.plugins.getUserFolders().joinIntoString ("\n"),
                           juce::dontSendNotification);
        addAndMakeVisible (folderBox);

        scanButton.setColour (juce::TextButton::buttonColourId, colours::accent.darker (0.4f));
        scanButton.onClick = [this]
        {
            context.plugins.setUserFolders (
                juce::StringArray::fromLines (folderBox.getText().trim()));
            context.plugins.startScan();
        };
        addAndMakeVisible (scanButton);

        progress.setPercentageDisplay (true);
        addAndMakeVisible (progress);

        statusLabel.setColour (juce::Label::textColourId, colours::text);
        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (statusLabel);

        updateInfo();
    }

    void updateInfo()
    {
        juce::StringArray defaults;
        for (int i = 0; i < context.plugins.getSearchPath().getNumPaths(); ++i)
            defaults.add (context.plugins.getSearchPath()[i].getFullPathName());

        infoLabel.setText ("Default VST3 folders (always scanned):\n" + defaults.joinIntoString ("\n"),
                           juce::dontSendNotification);

        progressValue = context.plugins.getScanProgress();
        const bool scanning = context.plugins.isScanning();
        scanButton.setEnabled (! scanning);
        scanButton.setButtonText (scanning ? "Scanning..." : "Scan for VST3 plugins");

        juce::String status = juce::String (context.plugins.knownPlugins.getNumTypes()) + " plugins known";
        if (scanning)
        {
            const auto current = context.plugins.getCurrentlyScannedPlugin();
            if (current.isNotEmpty())
                status << "  -  scanning: " << juce::File (current).getFileName();
        }
        statusLabel.setText (status, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        infoLabel.setBounds (area.removeFromTop (110));
        area.removeFromTop (6);
        folderBox.setBounds (area.removeFromTop (80));
        area.removeFromTop (8);
        scanButton.setBounds (area.removeFromTop (30).removeFromLeft (220));
        area.removeFromTop (8);
        progress.setBounds (area.removeFromTop (22));
        area.removeFromTop (6);
        statusLabel.setBounds (area.removeFromTop (22));
    }

    AppContext& context;
    juce::Label infoLabel, statusLabel;
    juce::TextEditor folderBox;
    juce::TextButton scanButton { "Scan for VST3 plugins" };
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
};

// ------------------------------------------------------------------ dialog

SettingsDialog::SettingsDialog (AppContext& ctx) : context (ctx)
{
    auto* audioSelector = new juce::AudioDeviceSelectorComponent (
        context.engine.getDeviceManager(), 0, 0, 0, 2, false, false, true, false);

    tabs.addTab ("Audio",   colours::panel, audioSelector, true);
    pluginsTab = new PluginsTab (context);
    tabs.addTab ("Plugins", colours::panel, pluginsTab, true);
    addAndMakeVisible (tabs);

    setSize (560, 480);
    startTimerHz (10);
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::timerCallback()
{
    if (pluginsTab != nullptr)
        pluginsTab->updateInfo();
}

void SettingsDialog::refreshFolderList() {}

void SettingsDialog::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void SettingsDialog::resized()
{
    tabs.setBounds (getLocalBounds());
}

void SettingsDialog::show (AppContext& ctx)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new SettingsDialog (ctx));
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = colours::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace fable
