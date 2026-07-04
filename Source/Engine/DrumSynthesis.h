#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fable
{

// DSP-synthesized drum samples so the app makes sound with zero external content.
// Recognised names: "kick", "clap", "hat", "openhat", "snare".
// Returns an empty buffer for unknown names.
juce::AudioBuffer<float> synthesizeDrum (const juce::String& name, double sampleRate);

bool isBuiltinSamplePath (const juce::String& path);          // "builtin:<name>"
juce::String builtinSampleName (const juce::String& path);

} // namespace fable
