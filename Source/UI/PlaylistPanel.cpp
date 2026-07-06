#include "PlaylistPanel.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <limits>
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

// See the matching comment in ChannelRackPanel.cpp: the Browser's file tree
// uses one fixed drag description, so the actual file is read from the
// source component's selection instead.
static juce::File audioFileFromDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
        return tree->getSelectedFile (0);
    return {};
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

class PlaylistPanel::PatternRenameMouseListener : public juce::MouseListener
{
public:
    explicit PatternRenameMouseListener (PlaylistPanel& ownerToUse) : owner (ownerToUse) {}

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner.showPatternMenu();
    }

private:
    PlaylistPanel& owner;
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

    // Snap resolution for the current gesture: holding Alt bypasses the grid
    // (1 tick = effectively free) so clips can be nudged into precise alignment.
    int snapTicks (const juce::MouseEvent& e) const
    {
        return e.mods.isAltDown() ? 1 : snapTicks();
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

            if (selectedClips.count ((int) i) > 0)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 2.0f);
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
                                                      : "Insert " + juce::String (c.mixerTrack))
                       + "  >  Vol " + juce::String (juce::roundToInt (c.gain * 100.0f)) + "%";
            if (c.muted)
                label += "  (muted)";
            g.drawText (label, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft);

            if (selectedAudioClips.count ((int) i) > 0)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 2.0f);
            }
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

        if (selecting)
        {
            g.setColour (colours::accent.withAlpha (0.2f));
            g.fillRect (selectionRect);
            g.setColour (colours::accent);
            g.drawRect (selectionRect, 1);
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
        grabKeyboardFocusIfWindowActive (*this);
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
                if (draggedKind == DragKind::audio && e.mods.isShiftDown())
                {
                    showAudioClipMenu (draggedIndex);
                    return;
                }

                deleteClipAt (e.getPosition());
            }
            return;
        }

        if (e.mods.isCtrlDown() && e.mods.isLeftButtonDown())
        {
            if (draggedIndex >= 0)
            {
                toggleMuteAt (e.getPosition());
            }
            else
            {
                // Ctrl+drag on empty space box-selects, like FL, so a
                // selection can be duplicated (Ctrl+B/Ctrl+D) as a group.
                selecting = true;
                selectionStart = e.getPosition();
                updateSelectionRect (e.getPosition());
            }
            draggedIndex = -1;
            draggedKind = DragKind::none;
            repaint();
            return;
        }

        // Plain left-click on a pattern clip selects that pattern in the picker,
        // so the next clip you draw is the same pattern (FL workflow).
        if (draggedKind == DragKind::pattern && draggedIndex >= 0 && e.mods.isLeftButtonDown())
        {
            const int pi = project.clips[(size_t) draggedIndex].patternIndex;
            if (pi >= 0 && pi < (int) project.patterns.size() && pi != owner.context.selectedPatternIndex)
                owner.context.selectPattern (pi);
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

        if (selecting)
        {
            updateSelectionRect (e.getPosition());
            repaint();
            return;
        }

        if (e.mods.isRightButtonDown())
        {
            deleteClipAt (e.getPosition());
            return;
        }

        if (owner.currentTool == PlaylistPanel::Tool::paint)
        {
            paintMouseDrag (e);
            return;
        }

        // draw tool: move/resize the clip grabbed on mouseDown; other tools don't drag
        if (owner.currentTool != PlaylistPanel::Tool::draw || draggedIndex < 0)
            return;

        const int snap = snapTicks (e);
        auto& project = owner.context.project;

        // Group move: shift every selected clip by the same delta as the anchor.
        if (draggingGroup && ! resizing)
        {
            const int newStart = juce::jmax (0, ((((int) plXToTick (e.getPosition().x) - dragOffsetTicks)
                                                  + snap / 2) / snap) * snap);
            const int newTrack = juce::jlimit (0, kNumPlaylistTracks - 1,
                                               (e.getPosition().y - kRulerHeight) / kTrackHeight);
            const int dTick  = newStart - dragAnchorStartTick;
            const int dTrack = newTrack - dragAnchorTrack;

            for (auto& s : groupPatternSnap)
                if (s.index >= 0 && s.index < (int) project.clips.size())
                {
                    project.clips[(size_t) s.index].startTick = juce::jmax (0, s.startTick + dTick);
                    project.clips[(size_t) s.index].track = juce::jlimit (0, kNumPlaylistTracks - 1, s.track + dTrack);
                }
            for (auto& s : groupAudioSnap)
                if (s.index >= 0 && s.index < (int) project.audioClips.size())
                {
                    project.audioClips[(size_t) s.index].startTick = juce::jmax (0, s.startTick + dTick);
                    project.audioClips[(size_t) s.index].track = juce::jlimit (0, kNumPlaylistTracks - 1, s.track + dTrack);
                }
            changed = true;
            repaint();
            return;
        }

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
        if (selecting)
        {
            selecting = false;
            repaint();
            return;
        }
        draggedIndex = -1;
        draggedKind = DragKind::none;
        draggingGroup = false;
        groupPatternSnap.clear();
        groupAudioSnap.clear();
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
        const int code = key.getKeyCode();

        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            if (! selectedClips.empty() || ! selectedAudioClips.empty())
            {
                deleteSelected();
                return true;
            }

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

        if (key == juce::KeyPress ('d', juce::ModifierKeys::ctrlModifier, 0)
            || key == juce::KeyPress ('b', juce::ModifierKeys::ctrlModifier, 0))
        {
            cloneSelectionOrHovered();
            return true;
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

    // Group move: when the grabbed clip is part of the multi-selection, every
    // selected clip is shifted by the same tick/track delta as the dragged one.
    struct ClipSnap { int index; int startTick; int track; };
    bool draggingGroup = false;
    int dragAnchorStartTick = 0;
    int dragAnchorTrack = 0;
    std::vector<ClipSnap> groupPatternSnap, groupAudioSnap;

    // Ctrl+drag rubber-band multi-select (tool-independent, like right-click).
    // Ctrl+click toggles a single clip; Ctrl+drag on empty space box-selects.
    bool selecting = false;
    juce::Point<int> selectionStart;
    juce::Rectangle<int> selectionRect;
    std::set<int> selectedClips, selectedAudioClips;

    void updateSelectionRect (juce::Point<int> current)
    {
        selectionRect = juce::Rectangle<int> (selectionStart, current);
        selectedClips.clear();
        selectedAudioClips.clear();

        auto& project = owner.context.project;
        for (size_t i = 0; i < project.clips.size(); ++i)
            if (clipRect (project.clips[i]).getSmallestIntegerContainer().intersects (selectionRect))
                selectedClips.insert ((int) i);
        for (size_t i = 0; i < project.audioClips.size(); ++i)
            if (audioRect (project.audioClips[i]).getSmallestIntegerContainer().intersects (selectionRect))
                selectedAudioClips.insert ((int) i);
    }

    // Erases in descending order so earlier indices in the same call stay valid.
    void deleteSelected()
    {
        auto& project = owner.context.project;
        for (auto it = selectedAudioClips.rbegin(); it != selectedAudioClips.rend(); ++it)
            if (*it >= 0 && *it < (int) project.audioClips.size())
                project.audioClips.erase (project.audioClips.begin() + *it);
        for (auto it = selectedClips.rbegin(); it != selectedClips.rend(); ++it)
            if (*it >= 0 && *it < (int) project.clips.size())
                project.clips.erase (project.clips.begin() + *it);
        selectedClips.clear();
        selectedAudioClips.clear();
        owner.context.contentChanged();
        updateSize();
        repaint();
    }

    // Clones the selection (or, if nothing is selected, the clip under the
    // cursor) shifted to start right after the group's current end.
    void cloneSelectionOrHovered()
    {
        auto& project = owner.context.project;

        if (selectedClips.empty() && selectedAudioClips.empty())
        {
            bool onEdge = false;
            if (const int i = audioClipIndexAt (lastMousePos, onEdge); i >= 0)
                selectedAudioClips.insert (i);
            else if (const int i = clipIndexAt (lastMousePos, onEdge); i >= 0)
                selectedClips.insert (i);
            else
                return;
        }

        int minStart = std::numeric_limits<int>::max();
        int maxEnd = 0;
        for (int i : selectedClips)
        {
            minStart = juce::jmin (minStart, project.clips[(size_t) i].startTick);
            maxEnd   = juce::jmax (maxEnd, project.clips[(size_t) i].endTick());
        }
        for (int i : selectedAudioClips)
        {
            minStart = juce::jmin (minStart, project.audioClips[(size_t) i].startTick);
            maxEnd   = juce::jmax (maxEnd, project.audioClips[(size_t) i].endTick());
        }
        const int shift = maxEnd - minStart;

        std::set<int> newClips, newAudioClips;
        for (int i : selectedClips)
        {
            auto clone = project.clips[(size_t) i];
            clone.startTick += shift;
            project.clips.push_back (clone);
            newClips.insert ((int) project.clips.size() - 1);
        }
        for (int i : selectedAudioClips)
        {
            auto clone = project.audioClips[(size_t) i];
            clone.startTick += shift;
            project.audioClips.push_back (clone);
            newAudioClips.insert ((int) project.audioClips.size() - 1);
        }

        selectedClips = std::move (newClips);
        selectedAudioClips = std::move (newAudioClips);
        owner.context.contentChanged();
        updateSize();
        repaint();
    }

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
            c.startTick    = ((int) plXToTick (e.getPosition().x) / snapTicks (e)) * snapTicks (e);
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
            draggingGroup = false;
            groupPatternSnap.clear();
            groupAudioSnap.clear();

            const bool inSelection = (draggedKind == DragKind::pattern && selectedClips.count (draggedIndex) > 0)
                                  || (draggedKind == DragKind::audio && selectedAudioClips.count (draggedIndex) > 0);

            if (draggedKind == DragKind::pattern)
            {
                auto& c = project.clips[(size_t) draggedIndex];
                dragOffsetTicks = (int) plXToTick (e.getPosition().x) - c.startTick;
                dragAnchorStartTick = c.startTick;
                dragAnchorTrack = c.track;
            }
            else if (draggedKind == DragKind::audio)
            {
                auto& c = project.audioClips[(size_t) draggedIndex];
                dragOffsetTicks = (int) plXToTick (e.getPosition().x) - c.startTick;
                dragAnchorStartTick = c.startTick;
                dragAnchorTrack = c.track;
            }

            // If the grabbed clip belongs to the current selection, move the
            // whole group; otherwise this click starts a fresh single-clip drag.
            if (! resizing && inSelection
                && (selectedClips.size() + selectedAudioClips.size()) > 1)
            {
                draggingGroup = true;
                for (int i : selectedClips)
                    if (i >= 0 && i < (int) project.clips.size())
                        groupPatternSnap.push_back ({ i, project.clips[(size_t) i].startTick,
                                                         project.clips[(size_t) i].track });
                for (int i : selectedAudioClips)
                    if (i >= 0 && i < (int) project.audioClips.size())
                        groupAudioSnap.push_back ({ i, project.audioClips[(size_t) i].startTick,
                                                       project.audioClips[(size_t) i].track });
            }
            else if (! resizing)
            {
                // grabbing a clip outside the selection clears it
                selectedClips.clear();
                selectedAudioClips.clear();
            }
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
            owner.context.project.clips[(size_t) i].muted =
                ! owner.context.project.clips[(size_t) i].muted;
            owner.context.contentChanged();
            repaint();
        }
    }

    void deleteClipAt (juce::Point<int> pos)
    {
        bool onEdge = false;
        if (const int index = audioClipIndexAt (pos, onEdge); index >= 0)
        {
            owner.context.project.audioClips.erase (owner.context.project.audioClips.begin() + index);
            selectedClips.clear();
            selectedAudioClips.clear();
            owner.context.contentChanged();
            updateSize();
            repaint();
            draggedIndex = -1;
            draggedKind = DragKind::none;
            changed = false;
            return;
        }

        if (const int index = clipIndexAt (pos, onEdge); index >= 0)
        {
            owner.context.project.clips.erase (owner.context.project.clips.begin() + index);
            selectedClips.clear();
            selectedAudioClips.clear();
            owner.context.contentChanged();
            updateSize();
            repaint();
            draggedIndex = -1;
            draggedKind = DragKind::none;
            changed = false;
        }
    }

    void toggleMuteAt (juce::Point<int> pos)
    {
        muteAt (pos);
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
                    propagateAudioClipSettings (clipIndex);
                    owner.context.contentChanged();
                    repaint();
                }
            });
        }
        m.addSubMenu ("Route to mixer track", route);
        m.addItem ("Clip volume...", [this, clipIndex] { editAudioClipGain (clipIndex); });
        m.addSeparator();

        const bool unique = owner.context.project.audioClips[(size_t) clipIndex].uniqueSettings;
        m.addItem ("Make unique", ! unique, unique, [this, clipIndex]
        {
            if (clipIndex >= 0 && clipIndex < (int) owner.context.project.audioClips.size())
            {
                owner.context.project.audioClips[(size_t) clipIndex].uniqueSettings = true;
                owner.context.contentChanged();
                repaint();
            }
        });
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
                selectedClips.clear();
                selectedAudioClips.clear();
                owner.context.contentChanged();
                updateSize();
                repaint();
            }
        });

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        draggedIndex = -1;
        draggedKind = DragKind::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        bool onEdge = false;
        const int audioIndex = audioClipIndexAt (e.getPosition(), onEdge);
        if (audioIndex >= 0)
        {
            showAudioClipMenu (audioIndex);
            return;
        }

        // Double-clicking a pattern clip opens that pattern for editing: select
        // it and bring the Channel Rack to the front (FL: double-click a clip
        // jumps you into editing its pattern).
        const int clipIndex = clipIndexAt (e.getPosition(), onEdge);
        if (clipIndex >= 0 && clipIndex < (int) owner.context.project.clips.size())
        {
            const int pi = owner.context.project.clips[(size_t) clipIndex].patternIndex;
            if (pi >= 0 && pi < (int) owner.context.project.patterns.size())
                owner.context.selectPattern (pi);
            if (owner.context.showChannelRack)
                owner.context.showChannelRack();
        }
    }

    // FL-style: non-unique clips of the same source file share gain/routing.
    // Push the just-edited clip's settings out to its non-unique siblings.
    void propagateAudioClipSettings (int clipIndex)
    {
        auto& clips = owner.context.project.audioClips;
        if (clipIndex < 0 || clipIndex >= (int) clips.size())
            return;
        const auto& src = clips[(size_t) clipIndex];
        if (src.uniqueSettings)
            return;
        for (auto& c : clips)
            if (! c.uniqueSettings && c.filePath == src.filePath)
            {
                c.gain       = src.gain;
                c.mixerTrack = src.mixerTrack;
            }
    }

    void editAudioClipGain (int clipIndex)
    {
        if (clipIndex < 0 || clipIndex >= (int) owner.context.project.audioClips.size())
            return;

        auto* clip = &owner.context.project.audioClips[(size_t) clipIndex];
        auto* editor = new juce::AlertWindow ("Audio clip volume", clip->name, juce::MessageBoxIconType::NoIcon);
        auto* gainSlider = new juce::Slider();
        gainSlider->setSliderStyle (juce::Slider::LinearHorizontal);
        gainSlider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        gainSlider->setRange (0.0, 2.0, 0.01);
        gainSlider->setTextValueSuffix (" x");
        gainSlider->setValue (clip->gain, juce::dontSendNotification);
        // AlertWindow lays custom components out at their current size; a fresh
        // slider is 0x0 and would be invisible without an explicit size.
        gainSlider->setSize (320, 26);
        editor->addCustomComponent (gainSlider);
        editor->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        editor->enterModalState (true, juce::ModalCallbackFunction::create ([this, editor, gainSlider, clipIndex] (int result)
        {
            if (result == 1 && clipIndex >= 0 && clipIndex < (int) owner.context.project.audioClips.size())
            {
                owner.context.project.audioClips[(size_t) clipIndex].gain = (float) gainSlider->getValue();
                propagateAudioClipSettings (clipIndex);
                owner.context.contentChanged();
                repaint();
            }
            delete gainSlider;
            delete editor;
        }), false);
    }
};

// ------------------------------------------------------------------ panel

PlaylistPanel::PlaylistPanel (AppContext& ctx) : context (ctx)
{
    waveformCache = std::make_unique<WaveformCache> (context);
    patternRenameListener = std::make_unique<PatternRenameMouseListener> (*this);

    patternBox.addMouseListener (patternRenameListener.get(), false);
    patternBox.onChange = [this]
    {
        const int index = patternBox.getSelectedItemIndex();
        if (index >= 0 && index != context.selectedPatternIndex)
            context.selectPattern (index);
    };
    addAndMakeVisible (patternBox);

    addPatternButton.onClick = [this]
    {
        context.project.addPattern();
        context.selectPattern ((int) context.project.patterns.size() - 1);
    };
    addAndMakeVisible (addPatternButton);

    // Full snap range from a whole bar down to a single step, plus a free
    // "off" mode. Hold Alt while dragging to bypass snapping momentarily for
    // pixel-perfect alignment regardless of the selected mode.
    snapBox.addItem ("Snap: bar",      kTicksPerBar);
    snapBox.addItem ("Snap: 1/2 bar",  kTicksPerBar / 2);
    snapBox.addItem ("Snap: beat",     kPPQ);
    snapBox.addItem ("Snap: 1/2 beat", kPPQ / 2);
    snapBox.addItem ("Snap: step",     kTicksPerStep);
    snapBox.addItem ("Snap: off (Alt)", 1);
    snapBox.setSelectedId (kTicksPerBar, juce::dontSendNotification);
    snapBox.setTooltip ("Grid snap for moving/resizing clips - hold Alt while dragging to snap freely");
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

    hintLabel.setText ("tools: 1-4   Ctrl+left: mute   right-drag/right-click: delete   Shift+right audio: route",
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

void PlaylistPanel::showPatternMenu()
{
    juce::PopupMenu m;
    m.addItem ("Rename pattern...", [this] { renameSelectedPattern(); });
    m.addItem ("Duplicate pattern", [this]
    {
        const int newIndex = context.project.duplicatePattern (context.selectedPatternIndex);
        if (newIndex >= 0)
            context.selectPattern (newIndex);   // rebuilds header + selects the copy
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (patternBox));
}

void PlaylistPanel::renameSelectedPattern()
{
    auto* pattern = context.selectedPattern();
    if (pattern == nullptr)
        return;

    auto* editor = new juce::AlertWindow ("Rename pattern", {}, juce::MessageBoxIconType::NoIcon);
    editor->addTextEditor ("name", pattern->name);
    editor->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    editor->enterModalState (true, juce::ModalCallbackFunction::create ([this, editor] (int result)
    {
        if (result == 1)
            if (auto* current = context.selectedPattern())
            {
                current->name = editor->getTextEditorContents ("name").trim();
                if (current->name.isEmpty())
                    current->name = "Pattern " + juce::String (context.selectedPatternIndex + 1);
                context.structureChanged();
            }
        delete editor;
    }), false);
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
    addPatternButton.setBounds (headerArea.removeFromLeft (28));
    headerArea.removeFromLeft (8);
    snapBox.setBounds (headerArea.removeFromLeft (130));
    headerArea.removeFromLeft (10);
    hintLabel.setBounds (headerArea);

    viewport.setBounds (area);
    clipArea->updateSize();
}

bool PlaylistPanel::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    return context.isSupportedAudioFile (audioFileFromDragSource (dragSourceDetails));
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

    const auto file = audioFileFromDragSource (dragSourceDetails);
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
