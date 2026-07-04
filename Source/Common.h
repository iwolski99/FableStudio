#pragma once

#include <juce_core/juce_core.h>

namespace fable
{

// Musical timing constants. All positions/lengths in the model are expressed in
// ticks at a fixed pulses-per-quarter-note resolution.
constexpr int kPPQ            = 960;
constexpr int kTicksPerStep   = kPPQ / 4;   // a step is a 16th note
constexpr int kStepsPerBar    = 16;         // 4/4 only for now
constexpr int kTicksPerBar    = kTicksPerStep * kStepsPerBar;

constexpr int kNumMixerTracks = 17;         // index 0 = master, 1..16 = inserts
constexpr int kNumEffectSlots = 10;
constexpr int kDefaultRootNote = 60;        // C5 in FL convention

constexpr double kMinBpm = 20.0, kMaxBpm = 999.0;

inline juce::File getAppDataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("FableStudio");
    dir.createDirectory();
    return dir;
}

} // namespace fable
