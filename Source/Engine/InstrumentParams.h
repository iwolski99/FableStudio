#pragma once

#include <atomic>
#include <map>
#include <vector>
#include <juce_core/juce_core.h>

namespace fable
{

// Shared plumbing for the native instruments (FableSynth, Kick designer).
// Each instrument declares a table of named parameters with a range and a
// default. The model stores parameter values as a name->float map on the
// Channel (so they serialize generically and presets round-trip), and the
// engine keeps a realtime-safe atomic snapshot the voices read from.

struct ParamSpec
{
    juce::String key;
    juce::String label;
    float min = 0.0f;
    float max = 1.0f;
    float defaultValue = 0.0f;
    juce::String suffix;   // display suffix, e.g. " Hz", " ms", " st"
    bool  isChoice = false;
    juce::StringArray choices;   // for isChoice params, index = value

    float clamp (float v) const { return juce::jlimit (min, max, v); }
};

// A realtime-safe bank of atomic floats keyed by the same names as the specs,
// so the audio thread never touches the std::map or std::string machinery.
class AtomicParams
{
public:
    void configure (const std::vector<ParamSpec>& specs)
    {
        table.clear();
        for (auto& s : specs)
            table.emplace (s.key, std::make_unique<std::atomic<float>> (s.defaultValue));
    }

    // message thread: push the model's values in (missing keys keep defaults).
    void setFrom (const std::vector<ParamSpec>& specs, const std::map<juce::String, float>& values)
    {
        for (auto& s : specs)
        {
            auto it = table.find (s.key);
            if (it == table.end())
                continue;
            auto v = values.find (s.key);
            it->second->store (s.clamp (v != values.end() ? v->second : s.defaultValue));
        }
    }

    // audio thread: read a value (returns fallback if the key is unknown).
    float get (const juce::String& key, float fallback = 0.0f) const
    {
        auto it = table.find (key);
        return it != table.end() ? it->second->load() : fallback;
    }

private:
    std::map<juce::String, std::unique_ptr<std::atomic<float>>> table;
};

// Fills a model param map with the spec defaults for any keys it's missing.
inline void applyDefaults (const std::vector<ParamSpec>& specs, std::map<juce::String, float>& values)
{
    for (auto& s : specs)
        if (values.find (s.key) == values.end())
            values[s.key] = s.defaultValue;
}

} // namespace fable
