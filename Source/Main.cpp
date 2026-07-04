#include <juce_gui_extra/juce_gui_extra.h>

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
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
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
