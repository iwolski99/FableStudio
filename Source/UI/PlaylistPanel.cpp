#include "PlaylistPanel.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
#include <utility>

namespace fable
{

static constexpr int   kNumPlaylistTracks = 16;
static constexpr int   kTrackHeight = 34;
static constexpr int   kRulerHeight = 22;
static constexpr int   kLabelWidth  = 90;
static constexpr float kPlPixelsPerTick = 44.0f / (float) kTicksPerBar;   // bar = 44 px

static int plTickToX (double tick) { return kLabelWidth + (int) (tick * kPlPixelsPerTick); }
static double plXToTick (int x)    { return juce::jmax (0.0, (x - kLabelWidth) / (double) kPlPixelsPerTick); }

static juce::File audioFileFromDragDescription (const juce::var& description)
{
    const auto text = description.toString();
    if (! text.startsWith ("audiofile:"))
        return {};
    return juce::File (text.fromFirstOccurrenceOf ("audiofile:", false, false));
}

class PlaylistPanel::WaveformCache
{
public:
    explicit WaveformCache (AppContext& ctx)
        : context (ctx), thumbnailCache (64) {}

    juce::AudioThumbnail* getThumbnail (const juce::String& filePath)
    {
        if (filePath.isEmpty())
            return nullptr;

        auto it = thumbnails.find (filePath);
        if (it != thumbnails.end())
            return it->second.get();

        auto thumbnail = std::make_unique<juce::AudioThumbnail> (512, context.engine.getFormatManager(),
                                                                 thumbnailCache);
        const juce::File file (filePath);
        if (! file.existsAsFile())
            return nullptr;

        thumbnail->setSource (new juce::FileInputSource (file));
        auto* raw = thumbnail.get();
        thumbnails[filePath] = std::move (thumbnail);
        return raw;
    }

private:
    AppContext& context;
    juce::AudioThumbnailCache thumbnailCache;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
};

class PlaylistPanel::ClipArea : public juce::Component
{
public:
    explicit ClipArea (PlaylistPanel& ownerToUse) : owner (ownerToUse)
    {
        setWantsKeyboardFocus (true);
    }

    enum class DragKind { none, pattern, audio };

    int snapTicks() const
    {
        const int id = owner.snapBox.getSelectedId();
        return id > 0 ? id : kTicksPerBar;
    }

    int contentLengthTicks()
    {
        return juce::jmax (owner.context.project.songLengthTicks() + 8 * kTicksPerBar,
                           32 * kTicksPerBar);
    }

    void updateSize()
    {
        setSize (kLabelWidth + (int) (contentLengthTicks() * kPlPixelsPerTick),
                 kRulerHeight + kNumPlaylistTracks * kTrackHeight);
    }

    juce::Rectangle<float> clipRect (const PlaylistClip& c) const
    {
        return { (float) plTickToX (c.startTick),
                 (float) (kRulerHeight + c.track * kTrackHeight) + 2.0f,
                 juce::jmax (6.0f, c.lengthTicks * kPlPixelsPerTick) - 1.0f,
                 (float) kTrackHeight - 4.0f };
    }

    juce::Rectangle<float> audioRect (const AudioClip& c) const
    {
        return { (float) plTickToX (c.startTick),
                 (float) (kRulerHeight + c.track * kTrackHeight) + 2.0f,
                 juce::jmax (6.0f, c.lengthTicks * kPlPixelsPerTick) - 1.0f,
                 (float) kTrackHeight - 4.0f };
    }

    void drawAudioWaveform (juce::Graphics& g, const AudioClip& clip, juce::Rectangle<float> r, juce::Colour colour)
    {
        if (owner.waveformCache == nullptr)
            return;

        auto* thumbnail = owner.waveformCache->getThumbnail (clip.filePath);
        if (thumbnail == nullptr || thumbnail->getTotalLength() <= 0.0)
            return;

        const double fileTicks = thumbnail->getTotalLength() * owner.context.project.bpm * kPPQ / 60.0;
        const float waveformWidth = r.getWidth() * (float) juce::jlimit (0.0, 1.0,
                                                  fileTicks / (double) juce::jmax (1, clip.lengthTicks));
        auto waveformBounds = r.reduced (4.0f, 7.0f).withWidth (juce::jmax (2.0f, waveformWidth - 8.0f));

        if (waveformBounds.getWidth() <= 1.0f)
            return;

        g.setColour (colour.withAlpha (0.75f));
        thumbnail->drawChannels (g, waveformBounds.toNearestInt(), 0.0, thumbnail->getTotalLength(), 1.0f);
    }

    void paint (juce::Graphics& g) override
    {
        auto& project = owner.context.project;
        g.fillAll (colours::panelDark);

        const int lengthTicks = contentLengthTicks();

        // track rows
        for (int t = 0; t < kNumPlaylistTracks; ++t)
        {
            const int y = kRulerHeight + t * kTrackHeight;
            g.setColour (t % 2 == 0 ? colours::panelDark : colours::panelDark.brighter (0.03f));
            g.fillRect (kLabelWidth, y, getWidth() - kLabelWidth, kTrackHeight);
            g.setColour (colours::outline.withAlpha (0.6f));
            g.drawHorizontalLine (y + kTrackHeight, (float) kLabelWidth, (float) getWidth());
        }

        // bar lines + ruler
        g.setColour (colours::titlebar);
        g.fillRect (kLabelWidth, 0, getWidth() - kLabelWidth, kRulerHeight);
        for (int tick = 0, bar = 1; tick <= lengthTicks; tick += kTicksPerBar, ++bar)
        {
            const int x = plTickToX (tick);
            const bool major = (bar - 1) % 4 == 0;
            g.setColour (major ? colours::outline.brighter (0.4f) : colours::outline);
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
            if (major)
            {
                g.setColour (colours::textDim);
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText (juce::String (bar), x + 3, 2, 40, kRulerHeight - 4,
                            juce::Justification::centredLeft);
            }
        }

        // pattern clips
        for (size_t i = 0; i < project.clips.size(); ++i)
        {
            const auto& c = project.clips[i];
            if (c.track >= kNumPlaylistTracks)
                continue;
            auto r = clipRect (c);

            juce::Colour base = juce::Colour (0xff58a05e).withRotatedHue (0.13f * (float) c.patternIndex);
            if (c.muted)
                base = base.withSaturation (base.getSaturation() * 0.35f).darker (0.25f);
            const float alpha = c.muted ? 0.4f
                                        : (draggedKind == DragKind::pattern && (int) i == draggedIndex ? 1.0f : 0.85f);
            g.setColour (base.withAlpha (alpha));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (base.brighter (0.4f).withAlpha (c.muted ? 0.4f : 1.0f));
            g.fillRect (r.withHeight (4.0f));
            g.setColour (colours::outline);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);

            if (c.patternIndex < (int) project.patterns.size())
            {
                g.setColour (juce::Colours::black.withAlpha (c.muted ? 0.5f : 0.75f));
                g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
                auto label = project.patterns[(size_t) c.patternIndex].name;
                if (c.muted)
                    label += "  (muted)";
                g.drawText (label, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
            }
        }

        // audio clips
        for (size_t i = 0; i < project.audioClips.size(); ++i)
        {
            const auto& c = project.audioClips[i];
            if (c.track >= kNumPlaylistTracks)
                continue;
            auto r = audioRect (c);

            auto base = juce::Colour (0xffc9803c);
            if (c.muted)
                base = base.withSaturation (base.getSaturation() * 0.35f).darker (0.25f);
            const float alpha = c.muted ? 0.45f
                                        : (draggedKind == DragKind::audio && (int) i == draggedIndex ? 1.0f : 0.88f);
            g.setColour (base.withAlpha (alpha));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (base.brighter (0.35f).withAlpha (c.muted ? 0.45f : 1.0f));
            g.fillRect (r.withHeight (4.0f));
            if (! c.muted)
                drawAudioWaveform (g, c, r, juce::Colours::white);
            g.setColour (colours::outline);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);

            g.setColour (juce::Colours::black.withAlpha (c.muted ? 0.5f : 0.75f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            auto label = (c.name.isNotEmpty() ? c.name : juce::File (c.filePath).getFileNameWithoutExtension())
                       + "  >  " + (c.mixerTrack == 0 ? juce::String ("Master")
                                                      : "Insert " + juce::String (c.mixerTrack));
            if (c.muted)
                label += "  (muted)";
            g.drawText (label, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
        }

        // song end marker
        g.setColour (colours::accent.withAlpha (0.4f));
        g.drawVerticalLine (plTickToX (project.songLengthTicks()), 0.0f, (float) getHeight());

        // playhead
        if (owner.context.engine.isSongMode())
        {
            g.setColour (colours::playhead);
            const int x = plTickToX (owner.context.engine.getPlayheadTicks());
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
            g.drawVerticalLine (x + 1, 0.0f, (float) kRulerHeight);
        }

        // pinned track labels
        const int x0 = owner.viewport.getViewPositionX();
        g.setColour (colours::panel);
        g.fillRect (x0, 0, kLabelWidth, getHeight());
        g.setColour (colours::outline);
        g.drawVerticalLine (x0 + kLabelWidth - 1, 0.0f, (float) getHeight());
        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        for (int t = 0; t < kNumPlaylistTracks; ++t)
            g.drawText ("Track " + juce::String (t + 1),
                        x0 + 8, kRulerHeight + t * kTrackHeight, kLabelWidth - 12, kTrackHeight,
                        juce::Justification::centredLeft);
    }

    int clipIndexAt (juce::Point<int> pos, bool& onRightEdge)
    {
        onRightEdge = false;
        auto& clips = owner.context.project.clips;
        for (int i = (int) clips.size() - 1; i >= 0; --i)
        {
            auto r = clipRect (clips[(size_t) i]);
            if (r.contains (pos.toFloat()))
            {
                onRightEdge = pos.x > r.getRight() - 8.0f;
                return i;
            }
        }
        return -1;
    }

    int audioClipIndexAt (juce::Point<int> pos, bool& onRightEdge)
    {
        onRightEdge = false;
        auto& clips = owner.context.project.audioClips;
        for (int i = (int) clips.size() - 1; i >= 0; --i)
        {
            auto r = audioRect (clips[(size_t) i]);
            if (r.contains (pos.toFloat()))
            {
                onRightEdge = pos.x > r.getRight() - 8.0f;
                return i;
            }
        }
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        lastMousePos = e.getPosition();
    }

    void mouseEnter (const juce::MouseEvent& e) override
    {
        // Grab focus on hover (not just click) so keyboard shortcuts (Delete,
        // tool-switch numbers, Home/End) work without needing a click first -
        // a click would otherwise paint/slice/mute depending on the active tool.
        lastMousePos = e.getPosition();
        grabKeyboardFocus();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        lastMousePos = e.getPosition();
        auto& project = owner.context.project;

        // click in ruler: seek (song mode) - tool independent
        if (e.getPosition().y < kRulerHeight)
        {
            owner.context.engine.setPositionTicks (
                ((int) plXToTick (e.getPosition().x) / snapTicks()) * snapTicks());
            return;
        }

        bool onEdge = false;
        draggedIndex = audioClipIndexAt (e.getPosition(), onEdge);
        draggedKind = draggedIndex >= 0 ? DragKind::audio : DragKind::none;

        if (draggedIndex < 0)
        {
            draggedIndex = clipIndexAt (e.getPosition(), onEdge);
            draggedKind = draggedIndex >= 0 ? DragKind::pattern : DragKind::none;
        }

        // right-click context menu (delete / route / open editor) - tool independent
        if (e.mods.isPopupMenu())
        {
            if (draggedIndex >= 0)
            {
                if (draggedKind == DragKind::pattern)
                {
                    showPatternClipMenu (draggedIndex);
                    return;
                }
                if (draggedKind == DragKind::audio)
                {
                    showAudioClipMenu (draggedIndex);
                    return;
                }
            }
            return;
        }

        switch (owner.currentTool)
        {
            case PlaylistPanel::Tool::draw:  drawMouseDown (e, onEdge); break;
            case PlaylistPanel::Tool::paint: paintMouseDown (e); break;
            case PlaylistPanel::Tool::slice: sliceAt (e.getPosition()); break;
            case PlaylistPanel::Tool::mute:  muteAt (e.getPosition()); break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        lastMousePos = e.getPosition();

        if (owner.currentTool == PlaylistPanel::Tool::paint)
        {
            paintMouseDrag (e);
            return;
        }

        // draw tool: move/resize the clip grabbed on mouseDown; other tools don't drag
        if (owner.currentTool != PlaylistPanel::Tool::draw || draggedIndex < 0)
            return;

        const int snap = snapTicks();

        auto applyDrag = [&] (auto& c)
        {
            if (resizing)
            {
                const int endTick = juce::jmax (c.startTick + snap,
                                                (((int) plXToTick (e.getPosition().x) + snap / 2) / snap) * snap);
                c.lengthTicks = endTick - c.startTick;
            }
            else
            {
                c.startTick = juce::jmax (0, ((((int) plXToTick (e.getPosition().x) - dragOffsetTicks)
                                               + snap / 2) / snap) * snap);
                c.track = juce::jlimit (0, kNumPlaylistTracks - 1,
                                        (e.getPosition().y - kRulerHeight) / kTrackHeight);
            }
        };

        if (draggedKind == DragKind::pattern)
        {
            auto& clips = owner.context.project.clips;
            if (draggedIndex >= (int) clips.size())
                return;
            applyDrag (clips[(size_t) draggedIndex]);
        }
        else if (draggedKind == DragKind::audio)
        {
            auto& clips = owner.context.project.audioClips;
            if (draggedIndex >= (int) clips.size())
                return;
            applyDrag (clips[(size_t) draggedIndex]);
        }
        else
        {
            return;
        }
        changed = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggedIndex = -1;
        draggedKind = DragKind::none;
        paintedCells.clear();
        if (changed)
        {
            changed = false;
            owner.context.contentChanged();
            updateSize();
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            bool onEdge = false;
            if (const int i = audioClipIndexAt (lastMousePos, onEdge); i >= 0)
            {
                owner.context.project.audioClips.erase (owner.context.project.audioClips.begin() + i);
                owner.context.contentChanged();
                updateSize();
                repaint();
                return true;
            }
            if (const int i = clipIndexAt (lastMousePos, onEdge); i >= 0)
            {
                owner.context.project.clips.erase (owner.context.project.clips.begin() + i);
                owner.context.contentChanged();
                updateSize();
                repaint();
                return true;
            }
            return false;
        }

        if (key == juce::KeyPress ('1', juce::ModifierKeys(), 0)) { owner.setTool (PlaylistPanel::Tool::draw);  return true; }
        if (key == juce::KeyPress ('2', juce::ModifierKeys(), 0)) { owner.setTool (PlaylistPanel::Tool::paint); return true; }
        if (key == juce::KeyPress ('3', juce::ModifierKeys(), 0)) { owner.setTool (PlaylistPanel::Tool::slice); return true; }
        if (key == juce::KeyPress ('4', juce::ModifierKeys(), 0)) { owner.setTool (PlaylistPanel::Tool::mute);  return true; }

        if (key == juce::KeyPress::homeKey)
        {
            owner.context.engine.setPositionTicks (0.0);
            return true;
        }
        if (key == juce::KeyPress::endKey)
        {
            const int songEnd = owner.context.project.songLengthTicks();
            owner.context.engine.setPositionTicks ((double) juce::jmax (0, songEnd - kTicksPerBar));
            return true;
        }

        return false;
    }

    PlaylistPanel& owner;
    int draggedIndex = -1;
    DragKind draggedKind = DragKind::none;
    int dragOffsetTicks = 0;
    bool resizing = false;
    bool changed = false;
    juce::Point<int> lastMousePos;
    std::set<std::pair<int, int>> paintedCells;   // (track, cellStartTick) touched this drag gesture

private:
    void drawMouseDown (const juce::MouseEvent& e, bool onEdge)
    {
        auto& project = owner.context.project;

        if (draggedIndex < 0)
        {
            // paint the currently selected pattern here
            auto* pattern = owner.context.selectedPattern();
            if (pattern == nullptr || e.getPosition().x < kLabelWidth)
                return;

            PlaylistClip c;
            c.patternIndex = owner.context.selectedPatternIndex;
            c.track        = juce::jlimit (0, kNumPlaylistTracks - 1,
                                           (e.getPosition().y - kRulerHeight) / kTrackHeight);
            c.startTick    = ((int) plXToTick (e.getPosition().x) / snapTicks()) * snapTicks();
            c.lengthTicks  = pattern->lengthTicks();
            project.clips.push_back (c);
            draggedIndex = (int) project.clips.size() - 1;
            draggedKind = DragKind::pattern;
            resizing = false;
            dragOffsetTicks = 0;
            owner.context.contentChanged();
            repaint();
        }
        else
        {
            resizing = onEdge;
            if (draggedKind == DragKind::pattern)
                dragOffsetTicks = (int) plXToTick (e.getPosition().x)
                                  - project.clips[(size_t) draggedIndex].startTick;
            else if (draggedKind == DragKind::audio)
                dragOffsetTicks = (int) plXToTick (e.getPosition().x)
                                  - project.audioClips[(size_t) draggedIndex].startTick;
        }
    }

    // Paint tool: drag across a track to fill consecutive pattern-length cells
    // with the currently selected pattern (skips cells that already hold a clip).
    void paintCellAt (juce::Point<int> pos)
    {
        auto* pattern = owner.context.selectedPattern();
        if (pattern == nullptr || pos.x < kLabelWidth || pos.y < kRulerHeight)
            return;

        const int track = juce::jlimit (0, kNumPlaylistTracks - 1, (pos.y - kRulerHeight) / kTrackHeight);
        const int cellLen = juce::jmax (1, pattern->lengthTicks());
        const int cellStart = ((int) plXToTick (pos.x) / cellLen) * cellLen;

        const auto key = std::make_pair (track, cellStart);
        if (paintedCells.count (key) > 0)
            return;
        paintedCells.insert (key);

        auto& clips = owner.context.project.clips;
        for (auto& c : clips)
            if (c.track == track && c.startTick == cellStart)
                return;   // already occupied

        PlaylistClip c;
        c.patternIndex = owner.context.selectedPatternIndex;
        c.track        = track;
        c.startTick    = cellStart;
        c.lengthTicks  = cellLen;
        clips.push_back (c);
        owner.context.contentChanged();
        updateSize();
        repaint();
    }

    void paintMouseDown (const juce::MouseEvent& e)
    {
        paintedCells.clear();
        paintCellAt (e.getPosition());
    }

    void paintMouseDrag (const juce::MouseEvent& e)
    {
        paintCellAt (e.getPosition());
    }

    // Slice tool: split whichever clip is under the click into two, preserving
    // continuity (the second half continues the pattern/audio rather than
    // restarting) via offsetTicks / sourceOffsetTicks.
    void sliceAt (juce::Point<int> pos)
    {
        bool onEdge = false;
        if (const int i = audioClipIndexAt (pos, onEdge); i >= 0)
        {
            sliceAudioClip (i, pos.x);
            return;
        }
        if (const int i = clipIndexAt (pos, onEdge); i >= 0)
        {
            slicePatternClip (i, pos.x);
            return;
        }
    }

    void slicePatternClip (int index, int mouseX)
    {
        auto& clips = owner.context.project.clips;
        if (index < 0 || index >= (int) clips.size())
            return;

        auto original = clips[(size_t) index];
        const int snap = snapTicks();
        const int cutTick = ((int) plXToTick (mouseX) / snap) * snap;

        // require at least one snap unit on each side, else there's nothing to cut
        if (cutTick <= original.startTick + snap - 1 || cutTick >= original.endTick() - snap + 1)
            return;

        PlaylistClip second = original;
        second.startTick   = cutTick;
        second.lengthTicks = original.endTick() - cutTick;
        second.offsetTicks = original.offsetTicks + (cutTick - original.startTick);

        clips[(size_t) index].lengthTicks = cutTick - original.startTick;
        clips.push_back (second);

        owner.context.contentChanged();
        updateSize();
        repaint();
    }

    void sliceAudioClip (int index, int mouseX)
    {
        auto& clips = owner.context.project.audioClips;
        if (index < 0 || index >= (int) clips.size())
            return;

        auto original = clips[(size_t) index];
        const int snap = snapTicks();
        const int cutTick = ((int) plXToTick (mouseX) / snap) * snap;

        if (cutTick <= original.startTick + snap - 1 || cutTick >= original.endTick() - snap + 1)
            return;

        AudioClip second = original;
        second.startTick         = cutTick;
        second.lengthTicks       = original.endTick() - cutTick;
        second.sourceOffsetTicks = original.sourceOffsetTicks + (cutTick - original.startTick);

        clips[(size_t) index].lengthTicks = cutTick - original.startTick;
        clips.push_back (second);

        owner.context.contentChanged();
        updateSize();
        repaint();
    }

    // Mute tool: toggle a clip's muted flag without moving or deleting it.
    void muteAt (juce::Point<int> pos)
    {
        bool onEdge = false;
        if (const int i = audioClipIndexAt (pos, onEdge); i >= 0)
        {
            owner.context.project.audioClips[(size_t) i].muted =
                ! owner.context.project.audioClips[(size_t) i].muted;
            owner.context.contentChanged();
            repaint();
            return;
        }
        if (const int i = clipIndexAt (pos, onEdge); i >= 0)
        {
            owner.context.project.clips[(size_t) i].muted = ! owner.context.project.clips[(size_t) i].muted;
            owner.context.contentChanged();
            repaint();
        }
    }

    void showPatternClipMenu (int clipIndex)
    {
        auto& clips = owner.context.project.clips;
        if (clipIndex < 0 || clipIndex >= (int) clips.size())
            return;

        juce::PopupMenu m;
        m.addItem (clips[(size_t) clipIndex].muted ? "Unmute" : "Mute", [this, clipIndex]
        {
            if (clipIndex >= 0 && clipIndex < (int) owner.context.project.clips.size())
            {
                owner.context.project.clips[(size_t) clipIndex].muted =
                    ! owner.context.project.clips[(size_t) clipIndex].muted;
                owner.context.contentChanged();
                repaint();
            }
        });
        m.addItem ("Delete pattern clip", [this, clipIndex]
        {
            if (clipIndex >= 0 && clipIndex < (int) owner.context.project.clips.size())
            {
                owner.context.project.clips.erase (owner.context.project.clips.begin() + clipIndex);
                owner.context.contentChanged();
                updateSize();
                repaint();
            }
        });

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        draggedIndex = -1;
        draggedKind = DragKind::none;
    }

    void showAudioClipMenu (int clipIndex)
    {
        if (clipIndex < 0 || clipIndex >= (int) owner.context.project.audioClips.size())
            return;

        juce::PopupMenu m;
        juce::PopupMenu route;
        const auto currentTrack = owner.context.project.audioClips[(size_t) clipIndex].mixerTrack;
        for (int i = 0; i < kNumMixerTracks; ++i)
        {
            const auto name = i == 0 ? juce::String ("Master") : "Insert " + juce::String (i);
            route.addItem (name, true, currentTrack == i, [this, clipIndex, i]
            {
                if (clipIndex >= 0 && clipIndex < (int) owner.context.project.audioClips.size())
                {
                    owner.context.project.audioClips[(size_t) clipIndex].mixerTrack = i;
                    owner.context.contentChanged();
                    repaint();
                }
            });
        }
        m.addSubMenu ("Route to mixer track", route);
        m.addSeparator();
        m.addItem (owner.context.project.audioClips[(size_t) clipIndex].muted ? "Unmute" : "Mute",
                  [this, clipIndex]
        {
            if (clipIndex >= 0 && clipIndex < (int) owner.context.project.audioClips.size())
            {
                owner.context.project.audioClips[(size_t) clipIndex].muted =
                    ! owner.context.project.audioClips[(size_t) clipIndex].muted;
                owner.context.contentChanged();
                repaint();
            }
        });
        m.addItem ("Delete audio clip", [this, clipIndex]
        {
            if (clipIndex >= 0 && clipIndex < (int) owner.context.project.audioClips.size())
            {
                owner.context.project.audioClips.erase (owner.context.project.audioClips.begin() + clipIndex);
                owner.context.contentChanged();
                updateSize();
                repaint();
            }
        });

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        draggedIndex = -1;
        draggedKind = DragKind::none;
    }
};

// ------------------------------------------------------------------ panel

PlaylistPanel::PlaylistPanel (AppContext& ctx) : context (ctx)
{
    waveformCache = std::make_unique<WaveformCache> (context);

    patternBox.onChange = [this]
    {
        const int index = patternBox.getSelectedItemIndex();
        if (index >= 0 && index != context.selectedPatternIndex)
            context.selectPattern (index);
    };
    addAndMakeVisible (patternBox);

    snapBox.addItem ("Snap: bar",  kTicksPerBar);
    snapBox.addItem ("Snap: beat", kPPQ);
    snapBox.addItem ("Snap: 1/2 bar", kTicksPerBar / 2);
    snapBox.setSelectedId (kTicksPerBar, juce::dontSendNotification);
    addAndMakeVisible (snapBox);

    drawToolButton.setTooltip ("Draw (1): paint/move/resize clips");
    paintToolButton.setTooltip ("Paint (2): drag to fill consecutive cells with the picked pattern");
    sliceToolButton.setTooltip ("Slice (3): click a clip to split it in two");
    muteToolButton.setTooltip ("Mute (4): click a clip to toggle it on/off");
    drawToolButton.onClick  = [this] { setTool (Tool::draw); };
    paintToolButton.onClick = [this] { setTool (Tool::paint); };
    sliceToolButton.onClick = [this] { setTool (Tool::slice); };
    muteToolButton.onClick  = [this] { setTool (Tool::mute); };
    for (auto* b : { &drawToolButton, &paintToolButton, &sliceToolButton, &muteToolButton })
        addAndMakeVisible (*b);

    hintLabel.setText ("right-click: delete/route   seek: click ruler   1-4: switch tool",
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, colours::textDim);
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (hintLabel);

    clipArea = std::make_unique<ClipArea> (*this);
    viewport.setViewedComponent (clipArea.get(), false);
    addAndMakeVisible (viewport);

    context.structureBroadcaster.addChangeListener (this);
    context.contentBroadcaster.addChangeListener (this);
    refreshHeader();
    refreshToolButtons();
    clipArea->updateSize();

    startTimerHz (30);
    setOpaque (true);
}

PlaylistPanel::~PlaylistPanel()
{
    context.structureBroadcaster.removeChangeListener (this);
    context.contentBroadcaster.removeChangeListener (this);
}

void PlaylistPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshHeader();
    clipArea->updateSize();
    clipArea->repaint();
}

void PlaylistPanel::refreshHeader()
{
    patternBox.clear (juce::dontSendNotification);

    int id = 1;
    for (auto& pattern : context.project.patterns)
        patternBox.addItem (pattern.name, id++);

    patternBox.setTextWhenNothingSelected ("Pattern");
    patternBox.setSelectedItemIndex (context.selectedPatternIndex, juce::dontSendNotification);
}

void PlaylistPanel::setTool (Tool t)
{
    currentTool = t;
    refreshToolButtons();
}

void PlaylistPanel::refreshToolButtons()
{
    auto style = [this] (juce::TextButton& b, Tool t)
    {
        b.setColour (juce::TextButton::buttonColourId,
                    currentTool == t ? colours::accent.darker (0.2f) : colours::panelLight);
        b.setColour (juce::TextButton::textColourOffId,
                    currentTool == t ? juce::Colours::black : colours::text);
    };
    style (drawToolButton,  Tool::draw);
    style (paintToolButton, Tool::paint);
    style (sliceToolButton, Tool::slice);
    style (muteToolButton,  Tool::mute);
}

void PlaylistPanel::timerCallback()
{
    if (context.engine.isPlaying() && context.engine.isSongMode())
        clipArea->repaint();

    static int lastX = -1;
    if (viewport.getViewPositionX() != lastX)
    {
        lastX = viewport.getViewPositionX();
        clipArea->repaint();
    }
}

void PlaylistPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    if (dragActive)
    {
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillRoundedRectangle (viewport.getBounds().toFloat().reduced (2.0f), 6.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (viewport.getBounds().toFloat().reduced (2.5f), 6.0f, 2.0f);
    }
}

void PlaylistPanel::resized()
{
    auto area = getLocalBounds();

    auto toolRow = area.removeFromTop (26).reduced (4, 2);
    for (auto* b : { &drawToolButton, &paintToolButton, &sliceToolButton, &muteToolButton })
    {
        b->setBounds (toolRow.removeFromLeft (56));
        toolRow.removeFromLeft (3);
    }

    auto headerArea = area.removeFromTop (30).reduced (4, 3);
    patternBox.setBounds (headerArea.removeFromLeft (170));
    headerArea.removeFromLeft (8);
    snapBox.setBounds (headerArea.removeFromLeft (130));
    headerArea.removeFromLeft (10);
    hintLabel.setBounds (headerArea);

    viewport.setBounds (area);
    clipArea->updateSize();
}

bool PlaylistPanel::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    return context.isSupportedAudioFile (audioFileFromDragDescription (dragSourceDetails.description));
}

void PlaylistPanel::itemDragEnter (const SourceDetails& dragSourceDetails)
{
    dragActive = isInterestedInDragSource (dragSourceDetails);
    dragPosition = { (int) dragSourceDetails.localPosition.x, (int) dragSourceDetails.localPosition.y };
    repaint();
}

void PlaylistPanel::itemDragMove (const SourceDetails& dragSourceDetails)
{
    dragPosition = { (int) dragSourceDetails.localPosition.x, (int) dragSourceDetails.localPosition.y };
    repaint();
}

void PlaylistPanel::itemDragExit (const SourceDetails&)
{
    dragActive = false;
    repaint();
}

void PlaylistPanel::itemDropped (const SourceDetails& dragSourceDetails)
{
    dragActive = false;
    repaint();

    const auto file = audioFileFromDragDescription (dragSourceDetails.description);
    if (! context.isSupportedAudioFile (file))
        return;

    const juce::Point<int> local ((int) dragSourceDetails.localPosition.x,
                                  (int) dragSourceDetails.localPosition.y);
    if (! viewport.getBounds().contains (local))
        return;

    const auto contentPos = local - viewport.getPosition() + viewport.getViewPosition();
    if (contentPos.x < kLabelWidth || contentPos.y < kRulerHeight)
        return;

    const int track = juce::jlimit (0, kNumPlaylistTracks - 1,
                                    (contentPos.y - kRulerHeight) / kTrackHeight);
    const int snap = snapBox.getSelectedId() > 0 ? snapBox.getSelectedId() : kTicksPerBar;
    const int startTick = ((int) plXToTick (contentPos.x) / snap) * snap;

    context.addAudioClipFromFile (file, track, startTick);
    if (context.showStatusMessage)
        context.showStatusMessage ("Added audio clip: " + file.getFileName());
}

} // namespace fable
