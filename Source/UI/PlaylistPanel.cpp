#include "PlaylistPanel.h"
#include <juce_audio_utils/juce_audio_utils.h>

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
    explicit ClipArea (PlaylistPanel& ownerToUse) : owner (ownerToUse) {}

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
            g.setColour (base.withAlpha (draggedKind == DragKind::pattern && (int) i == draggedIndex ? 1.0f : 0.85f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (base.brighter (0.4f));
            g.fillRect (r.withHeight (4.0f));
            g.setColour (colours::outline);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);

            if (c.patternIndex < (int) project.patterns.size())
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
                g.drawText (project.patterns[(size_t) c.patternIndex].name,
                            r.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
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
            g.setColour (base.withAlpha (draggedKind == DragKind::audio && (int) i == draggedIndex ? 1.0f : 0.88f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (base.brighter (0.35f));
            g.fillRect (r.withHeight (4.0f));
            drawAudioWaveform (g, c, r, juce::Colours::white);
            g.setColour (colours::outline);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);

            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            const auto label = (c.name.isNotEmpty() ? c.name : juce::File (c.filePath).getFileNameWithoutExtension())
                             + "  >  " + (c.mixerTrack == 0 ? juce::String ("Master")
                                                            : "Insert " + juce::String (c.mixerTrack));
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

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& project = owner.context.project;

        // click in ruler: seek (song mode)
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

        if (e.mods.isPopupMenu())
        {
            if (draggedIndex >= 0)
            {
                if (draggedKind == DragKind::pattern)
                    project.clips.erase (project.clips.begin() + draggedIndex);
                else if (draggedKind == DragKind::audio)
                {
                    showAudioClipMenu (draggedIndex);
                    return;
                }
                draggedIndex = -1;
                draggedKind = DragKind::none;
                owner.context.contentChanged();
                updateSize();
                repaint();
            }
            return;
        }

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

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggedIndex < 0)
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
        if (changed)
        {
            changed = false;
            owner.context.contentChanged();
            updateSize();
        }
    }

    PlaylistPanel& owner;
    int draggedIndex = -1;
    DragKind draggedKind = DragKind::none;
    int dragOffsetTicks = 0;
    bool resizing = false;
    bool changed = false;

private:
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

    hintLabel.setText ("paint: left-click (uses playlist pattern picker)   delete: right-click   seek: click ruler",
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
