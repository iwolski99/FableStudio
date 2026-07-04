// FableTestTone — a deliberately tiny VST3 sine synth used to verify
// FableStudio's plugin scanning and hosting end-to-end (also handy in CI).

#include <juce_audio_processors/juce_audio_processors.h>

class TestToneProcessor : public juce::AudioProcessor
{
public:
    TestToneProcessor()
        : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {}

    const juce::String getName() const override { return "FableTestTone"; }

    void prepareToPlay (double sampleRate, int) override
    {
        sr = sampleRate;
        level = 0.0f;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
            {
                phaseInc = juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber())
                           * juce::MathConstants<double>::twoPi / sr;
                target = msg.getFloatVelocity() * 0.5f;
            }
            else if (msg.isNoteOff() || msg.isAllNotesOff())
            {
                target = 0.0f;
            }
        }

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            level += (target - level) * 0.002f;
            const float s = (float) std::sin (phase) * level;
            phase += phaseInc;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, s);
        }
    }

    double getTailLengthSeconds() const override { return 0.1; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return "default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        const juce::String marker ("FableTestToneState");
        dest.replaceAll (marker.toRawUTF8(), marker.getNumBytesAsUTF8());
    }
    void setStateInformation (const void*, int) override {}

private:
    double sr = 44100.0, phase = 0.0, phaseInc = 0.0;
    float level = 0.0f, target = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TestToneProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestToneProcessor();
}
