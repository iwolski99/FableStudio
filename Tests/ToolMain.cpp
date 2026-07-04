// FableTool — headless companion binary: unit tests and offline rendering.
//
//   FableTool test                        run all unit tests
//   FableTool render <in.fable> <out.wav> [--pattern]   offline-render a project
//   FableTool render-demo <out.wav>       render the built-in demo project
//   FableTool write-demo <out.fable>      write the demo project file

#include <juce_events/juce_events.h>

#include "../Source/Engine/AudioEngine.h"
#include "../Source/Model/Serialization.h"

namespace
{

int runTests()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}

int renderProject (fable::Project& project, const juce::File& out, bool songMode)
{
    fable::PluginManager plugins;
    fable::AudioEngine engine (plugins);

    if (! engine.renderToWav (project, out, songMode))
    {
        std::cerr << "Render failed: " << out.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "Rendered " << out.getFullPathName()
              << " (" << out.getSize() << " bytes)" << std::endl;
    return 0;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));

    if (args.contains ("test"))
        return runTests();

    if (args.size() >= 2 && args[0] == "render-demo")
    {
        auto project = fable::createDefaultProject();
        return renderProject (project, juce::File::getCurrentWorkingDirectory().getChildFile (args[1]),
                              ! args.contains ("--pattern"));
    }

    if (args.size() >= 2 && args[0] == "write-demo")
    {
        auto project = fable::createDefaultProject();
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
        if (! fable::saveProjectToFile (project, file))
        {
            std::cerr << "Failed to write " << file.getFullPathName() << std::endl;
            return 1;
        }
        std::cout << "Wrote " << file.getFullPathName() << std::endl;
        return 0;
    }

    if (args.contains ("scan"))
    {
        fable::PluginManager plugins;
        std::cout << "Scanning: " << plugins.getSearchPath().toString() << std::endl;
        plugins.scanSynchronously();
        for (const auto& d : plugins.knownPlugins.getTypes())
            std::cout << (d.isInstrument ? "[inst] " : "[fx]   ") << d.name
                      << "  (" << d.fileOrIdentifier << ")" << std::endl;
        std::cout << plugins.knownPlugins.getNumTypes() << " plugins found" << std::endl;
        return 0;
    }

    if (args.size() >= 2 && args[0] == "render-vst-test")
    {
        // End-to-end VST3 hosting check: scan, load the first instrument found
        // as a channel, sequence some notes through it, render, verify audio.
        fable::PluginManager plugins;
        plugins.scanSynchronously();

        const auto instruments = plugins.getInstruments();
        if (instruments.isEmpty())
        {
            std::cerr << "No VST3 instruments found - nothing to test" << std::endl;
            return 1;
        }
        const auto desc = instruments.getReference (0);
        std::cout << "Testing with instrument: " << desc.name << std::endl;

        fable::Project project;
        project.bpm = 120.0;
        const int channelId = project.addChannel (fable::GeneratorType::plugin, desc.name);
        project.channelById (channelId)->pluginIdentifier = desc.createIdentifierString();
        auto& pattern = project.addPattern();
        auto& data = pattern.dataFor (channelId);
        data.notes.push_back ({ 0,                       fable::kPPQ * 2, 69, 0.9f });
        data.notes.push_back ({ fable::kTicksPerBar / 2, fable::kPPQ,     72, 0.9f });
        project.clips.push_back ({ 0, 0, 0, fable::kTicksPerBar });

        fable::AudioEngine engine (plugins);
        auto out = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
        if (! engine.renderToWav (project, out, true))
        {
            std::cerr << "Render failed" << std::endl;
            return 1;
        }

        // verify the file contains signal
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (out));
        if (reader == nullptr)
            return 1;
        juce::AudioBuffer<float> check (2, (int) reader->lengthInSamples);
        reader->read (&check, 0, check.getNumSamples(), 0, true, true);
        const float peak = check.getMagnitude (0, check.getNumSamples());
        std::cout << "Rendered " << out.getFullPathName() << "  peak=" << peak << std::endl;
        if (peak < 0.01f)
        {
            std::cerr << "FAIL: output is silent - plugin hosting is broken" << std::endl;
            return 1;
        }
        std::cout << "VST3 HOSTING OK" << std::endl;
        return 0;
    }

    if (args.size() >= 3 && args[0] == "render")
    {
        fable::Project project;
        auto in = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
        if (! fable::loadProjectFromFile (in, project))
        {
            std::cerr << "Failed to load " << in.getFullPathName() << std::endl;
            return 1;
        }
        return renderProject (project, juce::File::getCurrentWorkingDirectory().getChildFile (args[2]),
                              ! args.contains ("--pattern"));
    }

    std::cout << "Usage:\n"
                 "  FableTool test\n"
                 "  FableTool render <in.fable> <out.wav> [--pattern]\n"
                 "  FableTool render-demo <out.wav> [--pattern]\n"
                 "  FableTool write-demo <out.fable>\n";
    return 1;
}
