#include "Serialization.h"

namespace fable
{

namespace
{
    juce::DynamicObject::Ptr obj() { return new juce::DynamicObject(); }

    juce::String blockToBase64 (const juce::MemoryBlock& mb)
    {
        return mb.isEmpty() ? juce::String() : mb.toBase64Encoding();
    }

    juce::MemoryBlock base64ToBlock (const juce::String& s)
    {
        juce::MemoryBlock mb;
        if (s.isNotEmpty())
            mb.fromBase64Encoding (s);
        return mb;
    }

    juce::var noteToVar (const Note& n)
    {
        auto o = obj();
        o->setProperty ("t", n.startTick);
        o->setProperty ("l", n.lengthTicks);
        o->setProperty ("p", n.pitch);
        o->setProperty ("v", n.velocity);
        return o.get();
    }

    Note noteFromVar (const juce::var& v)
    {
        Note n;
        n.startTick   = (int) v["t"];
        n.lengthTicks = juce::jmax (1, (int) v["l"]);
        n.pitch       = juce::jlimit (0, 127, (int) v["p"]);
        n.velocity    = juce::jlimit (0.0f, 1.0f, (float) (double) v["v"]);
        return n;
    }
}

juce::var projectToVar (const Project& p)
{
    auto root = obj();
    root->setProperty ("app", "FableStudio");
    root->setProperty ("version", 3);
    root->setProperty ("name", p.name);
    root->setProperty ("bpm", p.bpm);
    root->setProperty ("swing", p.swing);
    root->setProperty ("nextChannelId", p.nextChannelId);

    juce::Array<juce::var> channels;
    for (auto& c : p.channels)
    {
        auto o = obj();
        o->setProperty ("id", c.id);
        o->setProperty ("name", c.name);
        o->setProperty ("type", (int) c.type);
        o->setProperty ("sample", c.samplePath);
        o->setProperty ("plugin", c.pluginIdentifier);
        o->setProperty ("pluginState", blockToBase64 (c.pluginState));
        o->setProperty ("volume", c.volume);
        o->setProperty ("pan", c.pan);
        o->setProperty ("muted", c.muted);
        o->setProperty ("mixerTrack", c.mixerTrack);
        o->setProperty ("rootNote", c.rootNote);
        o->setProperty ("sampleFadeInMs", c.sampleFadeInMs);
        o->setProperty ("sampleFadeOutMs", c.sampleFadeOutMs);
        o->setProperty ("colour", c.colour.toString());
        if (! c.synthParams.empty())
        {
            auto sp = obj();
            for (auto& [key, value] : c.synthParams)
                sp->setProperty (key, value);
            o->setProperty ("synthParams", sp.get());
        }
        channels.add (o.get());
    }
    root->setProperty ("channels", channels);

    juce::Array<juce::var> patterns;
    for (auto& pat : p.patterns)
    {
        auto o = obj();
        o->setProperty ("name", pat.name);
        o->setProperty ("lengthSteps", pat.lengthSteps);

        juce::Array<juce::var> chData;
        for (auto& [chId, data] : pat.channelData)
        {
            if (data.isEmpty())
                continue;

            auto d = obj();
            d->setProperty ("channel", chId);

            juce::String stepString;   // compact "1001..." + velocities only when non-default
            juce::Array<juce::var> vels;
            for (auto& s : data.steps)
            {
                stepString << (s.on ? '1' : '0');
                vels.add (s.velocity);
            }
            d->setProperty ("steps", stepString);
            d->setProperty ("stepVel", vels);

            juce::Array<juce::var> notes;
            for (auto& n : data.notes)
                notes.add (noteToVar (n));
            d->setProperty ("notes", notes);

            chData.add (d.get());
        }
        o->setProperty ("channelData", chData);
        patterns.add (o.get());
    }
    root->setProperty ("patterns", patterns);

    juce::Array<juce::var> clips;
    for (auto& c : p.clips)
    {
        auto o = obj();
        o->setProperty ("pattern", c.patternIndex);
        o->setProperty ("track", c.track);
        o->setProperty ("start", c.startTick);
        o->setProperty ("length", c.lengthTicks);
        o->setProperty ("offset", c.offsetTicks);
        o->setProperty ("muted", c.muted);
        clips.add (o.get());
    }
    root->setProperty ("clips", clips);

    juce::Array<juce::var> audioClips;
    for (auto& c : p.audioClips)
    {
        auto o = obj();
        o->setProperty ("file", c.filePath);
        o->setProperty ("name", c.name);
        o->setProperty ("gain", c.gain);
        o->setProperty ("mixerTrack", c.mixerTrack);
        o->setProperty ("track", c.track);
        o->setProperty ("start", c.startTick);
        o->setProperty ("length", c.lengthTicks);
        o->setProperty ("srcOffset", c.sourceOffsetTicks);
        o->setProperty ("muted", c.muted);
        o->setProperty ("unique", c.uniqueSettings);
        audioClips.add (o.get());
    }
    root->setProperty ("audioClips", audioClips);

    juce::Array<juce::var> tracks;
    for (auto& t : p.mixerTracks)
    {
        auto o = obj();
        o->setProperty ("name", t.name);
        o->setProperty ("volume", t.volume);
        o->setProperty ("pan", t.pan);
        o->setProperty ("muted", t.muted);
        o->setProperty ("solo", t.solo);

        juce::Array<juce::var> slots;
        for (auto& s : t.slots)
        {
            auto so = obj();
            so->setProperty ("type", (int) s.type);
            so->setProperty ("plugin", s.pluginIdentifier);
            so->setProperty ("pluginState", blockToBase64 (s.pluginState));
            so->setProperty ("enabled", s.enabled);
            juce::Array<juce::var> params;
            for (auto f : s.params) params.add (f);
            so->setProperty ("params", params);
            slots.add (so.get());
        }
        o->setProperty ("slots", slots);
        tracks.add (o.get());
    }
    root->setProperty ("mixerTracks", tracks);

    return root.get();
}

juce::String projectToJson (const Project& p)
{
    return juce::JSON::toString (projectToVar (p), false);
}

bool projectFromVar (const juce::var& v, Project& out)
{
    if (! v.isObject() || v["app"].toString() != "FableStudio")
        return false;

    Project p;
    p.name  = v["name"].toString();
    p.bpm   = juce::jlimit (kMinBpm, kMaxBpm, (double) v["bpm"]);
    p.swing = juce::jlimit (0, 100, (int) v["swing"]);
    p.nextChannelId = juce::jmax (1, (int) v["nextChannelId"]);

    if (auto* channels = v["channels"].getArray())
    {
        for (auto& cv : *channels)
        {
            Channel c;
            c.id   = (int) cv["id"];
            c.name = cv["name"].toString();
            c.type = (GeneratorType) juce::jlimit (0, 3, (int) cv["type"]);
            c.samplePath       = cv["sample"].toString();
            c.pluginIdentifier = cv["plugin"].toString();
            c.pluginState      = base64ToBlock (cv["pluginState"].toString());
            c.volume     = juce::jlimit (0.0f, 1.25f, (float) (double) cv["volume"]);
            c.pan        = juce::jlimit (-1.0f, 1.0f, (float) (double) cv["pan"]);
            c.muted      = (bool) cv["muted"];
            c.mixerTrack = juce::jlimit (0, kNumMixerTracks - 1, (int) cv["mixerTrack"]);
            c.rootNote   = juce::jlimit (0, 127, (int) cv["rootNote"]);
            c.sampleFadeInMs  = juce::jlimit (0.0f, 5000.0f, (float) (double) cv["sampleFadeInMs"]);
            c.sampleFadeOutMs = juce::jlimit (0.0f, 5000.0f, (float) (double) cv["sampleFadeOutMs"]);
            c.colour     = juce::Colour::fromString (cv["colour"].toString());
            if (auto* sp = cv["synthParams"].getDynamicObject())
                for (auto& prop : sp->getProperties())
                    c.synthParams[prop.name.toString()] = (float) (double) prop.value;
            p.nextChannelId = juce::jmax (p.nextChannelId, c.id + 1);
            p.channels.push_back (std::move (c));
        }
    }

    if (auto* patterns = v["patterns"].getArray())
    {
        for (auto& pv : *patterns)
        {
            Pattern pat;
            pat.name        = pv["name"].toString();
            pat.lengthSteps = juce::jlimit (1, 1024, (int) pv["lengthSteps"]);

            if (auto* chData = pv["channelData"].getArray())
            {
                for (auto& dv : *chData)
                {
                    int chId = (int) dv["channel"];
                    if (p.channelById (chId) == nullptr)
                        continue;

                    ChannelPatternData data;
                    auto stepString = dv["steps"].toString();
                    auto* vels = dv["stepVel"].getArray();
                    data.steps.resize ((size_t) pat.lengthSteps);
                    for (int i = 0; i < juce::jmin (pat.lengthSteps, stepString.length()); ++i)
                    {
                        data.steps[(size_t) i].on = stepString[i] == '1';
                        if (vels != nullptr && i < vels->size())
                            data.steps[(size_t) i].velocity =
                                juce::jlimit (0.0f, 1.0f, (float) (double) (*vels)[i]);
                    }

                    if (auto* notes = dv["notes"].getArray())
                        for (auto& nv : *notes)
                            data.notes.push_back (noteFromVar (nv));

                    pat.channelData[chId] = std::move (data);
                }
            }
            p.patterns.push_back (std::move (pat));
        }
    }

    if (auto* clips = v["clips"].getArray())
    {
        for (auto& cv : *clips)
        {
            PlaylistClip c;
            c.patternIndex = (int) cv["pattern"];
            c.track        = juce::jmax (0, (int) cv["track"]);
            c.startTick    = juce::jmax (0, (int) cv["start"]);
            c.lengthTicks  = juce::jmax (1, (int) cv["length"]);
            c.offsetTicks  = juce::jmax (0, (int) cv["offset"]);
            c.muted        = (bool) cv["muted"];
            if (c.patternIndex >= 0 && c.patternIndex < (int) p.patterns.size())
                p.clips.push_back (c);
        }
    }

    if (auto* audioClips = v["audioClips"].getArray())
    {
        for (auto& cv : *audioClips)
        {
            AudioClip c;
            c.filePath    = cv["file"].toString();
            c.name        = cv["name"].toString();
            c.gain        = cv.hasProperty ("gain")
                                ? juce::jlimit (0.0f, 2.0f, (float) (double) cv["gain"])
                                : 1.0f;
            c.mixerTrack  = juce::jlimit (0, kNumMixerTracks - 1, (int) cv["mixerTrack"]);
            c.track       = juce::jmax (0, (int) cv["track"]);
            c.startTick   = juce::jmax (0, (int) cv["start"]);
            c.lengthTicks = juce::jmax (1, (int) cv["length"]);
            c.sourceOffsetTicks = juce::jmax (0, (int) cv["srcOffset"]);
            c.muted       = (bool) cv["muted"];
            c.uniqueSettings = (bool) cv["unique"];
            if (c.filePath.isNotEmpty())
                p.audioClips.push_back (std::move (c));
        }
    }

    if (auto* tracks = v["mixerTracks"].getArray())
    {
        for (int i = 0; i < juce::jmin ((int) kNumMixerTracks, tracks->size()); ++i)
        {
            const auto& tv = tracks->getReference (i);
            auto& t  = p.mixerTracks[(size_t) i];
            t.name   = tv["name"].toString();
            t.volume = juce::jlimit (0.0f, 1.25f, (float) (double) tv["volume"]);
            t.pan    = juce::jlimit (-1.0f, 1.0f, (float) (double) tv["pan"]);
            t.muted  = (bool) tv["muted"];
            t.solo   = (bool) tv["solo"];

            if (auto* slots = tv["slots"].getArray())
            {
                for (int s = 0; s < juce::jmin ((int) kNumEffectSlots, slots->size()); ++s)
                {
                    const auto& sv = slots->getReference (s);
                    auto& slot = t.slots[(size_t) s];
                    slot.type             = (EffectType) juce::jlimit (0, 5, (int) sv["type"]);
                    slot.pluginIdentifier = sv["plugin"].toString();
                    slot.pluginState      = base64ToBlock (sv["pluginState"].toString());
                    slot.enabled          = ! sv.hasProperty ("enabled") || (bool) sv["enabled"];
                    if (auto* params = sv["params"].getArray())
                        for (int k = 0; k < juce::jmin ((int) slot.params.size(), params->size()); ++k)
                            slot.params[(size_t) k] = juce::jlimit (0.0f, 1.0f, (float) (double) (*params)[k]);
                }
            }
        }
    }

    out = std::move (p);
    return true;
}

bool projectFromJson (const juce::String& json, Project& out)
{
    return projectFromVar (juce::JSON::parse (json), out);
}

bool saveProjectToFile (const Project& p, const juce::File& file)
{
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (projectToJson (p)))
        return false;
    return temp.overwriteTargetFileWithTemporary();
}

bool loadProjectFromFile (const juce::File& file, Project& out)
{
    if (! file.existsAsFile())
        return false;
    return projectFromJson (file.loadFileAsString(), out);
}

} // namespace fable
