#include <juce_gui_extra/juce_gui_extra.h>

#include "Plugins/PluginManager.h"
#include "UI/MainComponent.h"

namespace fable
{

class FableStudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "FableStudio"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        // Sandboxed VST3 probe mode: this same executable is relaunched as a
        // disposable child process (see PluginManager::probeOneFile) to query
        // one plugin's descriptions. If the plugin crashes, only this child
        // dies - the real running DAW is never touched. No window/audio
        // device is created; it exits as soon as the probe is done.
        {
            const auto params = getCommandLineParameterArray();
            const int scanIndex = params.indexOf ("--scan-plugin");
            const int outIndex  = params.indexOf ("--out");
            if (scanIndex >= 0 && outIndex >= 0
                && scanIndex + 1 < params.size() && outIndex + 1 < params.size())
            {
                PluginManager::runScanChildProcess (juce::File (params[scanIndex + 1]),
                                                    juce::File (params[outIndex + 1]));
                quit();
                return;
            }
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // Headless UI verification: --screenshot <dir> captures each panel
        // to PNG files and exits (used by CI / development under Xvfb).
        const auto params = getCommandLineParameterArray();
        const int flagIndex = params.indexOf ("--screenshot");
        if (flagIndex >= 0)
            screenshotDriver = std::make_unique<ScreenshotDriver> (
                *mainWindow,
                juce::File::getCurrentWorkingDirectory()
                    .getChildFile (flagIndex + 1 < params.size() ? params[flagIndex + 1]
                                                                 : juce::String (".")));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name, colours::titlebar,
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (960, 600, 10000, 10000);

            // Fit within the actual screen instead of always opening at a fixed
            // 1440x860 - on a smaller/scaled display that could put the title bar
            // off-screen with no way to drag it back or resize.
            const auto userArea = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            const int w = juce::jmin (getWidth(),  userArea.getWidth()  - 40);
            const int h = juce::jmin (getHeight(), userArea.getHeight() - 40);
            setBounds (userArea.withSizeKeepingCentre (juce::jmax (960, w), juce::jmax (600, h)));

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::F11Key)
            {
                setFullScreen (! isFullScreen());
                return true;
            }
            return juce::DocumentWindow::keyPressed (key);
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    // Steps through panel layouts, saving a PNG of the window for each.
    class ScreenshotDriver : private juce::Timer
    {
    public:
        ScreenshotDriver (MainWindow& windowToUse, const juce::File& dirToUse)
            : window (windowToUse), dir (dirToUse)
        {
            dir.createDirectory();
            startTimer (1500);
        }

    private:
        void timerCallback() override
        {
            auto* content = window.getContentComponent();
            if (content == nullptr)
                return;

            static const struct { const char* name; int key; } stages[] = {
                { "01-main.png",      0 },
                { "02-pianoroll.png", juce::KeyPress::F7Key },
                { "03-mixer.png",     juce::KeyPress::F9Key },
            };

            if (stage > 0 && stage <= (int) std::size (stages))
            {
                auto image = content->createComponentSnapshot (content->getLocalBounds());
                juce::File out = dir.getChildFile (stages[stage - 1].name);
                out.deleteFile();
                juce::FileOutputStream stream (out);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            if (stage >= (int) std::size (stages))
            {
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
                return;
            }

            const int key = stages[(size_t) stage].key;
            if (key != 0)
                content->keyPressed (juce::KeyPress (key));
            ++stage;
        }

        MainWindow& window;
        juce::File dir;
        int stage = 1;
    };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<ScreenshotDriver> screenshotDriver;
};

} // namespace fable

START_JUCE_APPLICATION (fable::FableStudioApplication)
