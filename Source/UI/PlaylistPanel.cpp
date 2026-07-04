#include "PlaylistPanel.h"

namespace fable
{

static constexpr int   kNumPlaylistTracks = 16;
static constexpr int   kTrackHeight = 34;
static constexpr int   kRulerHeight = 22;
static constexpr int   kLabelWidth  = 90;
static constexpr float kPlPixelsPerTick = 44.0f / (float) kTicksPerBar;   // bar = 44 px

static int plTickToX (double tick) { return kLabelWidth + (int) (tick * kPlPixelsPerTick); }
static double plXToTick (int x)    { return juce::jmax (0.0, (x - kLabelWidth) / (double) kPlPixelsPerTick); }

class PlaylistPanel::ClipArea : public juce::Component
{
public:
    explicit ClipArea (PlaylistPanel& ownerToUse) : owner (ownerToUse) {}

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

        // clips
        for (size_t i = 0; i < project.clips.size(); ++i)
        {
            const auto& c = project.clips[i];
            if (c.track >= kNumPlaylistTracks)
                continue;
            auto r = clipRect (c);

            juce::Colour base = juce::Colour (0xff58a05e).withRotatedHue (0.13f * (float) c.patternIndex);
            g.setColour (base.withAlpha ((int) i == draggedIndex ? 1.0f : 0.85f));
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
        draggedIndex = clipIndexAt (e.getPosition(), onEdge);

        if (e.mods.isPopupMenu())
        {
            if (draggedIndex >= 0)
            {
                project.clips.erase (project.clips.begin() + draggedIndex);
                draggedIndex = -1;
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
            resizing = false;
            dragOffsetTicks = 0;
            owner.context.contentChanged();
            repaint();
        }
        else
        {
            resizing = onEdge;
            dragOffsetTicks = (int) plXToTick (e.getPosition().x)
                              - project.clips[(size_t) draggedIndex].startTick;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& clips = owner.context.project.clips;
        if (draggedIndex < 0 || draggedIndex >= (int) clips.size())
            return;

        auto& c = clips[(size_t) draggedIndex];
        const int snap = snapTicks();

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
        changed = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggedIndex = -1;
        if (changed)
        {
            changed = false;
            owner.context.contentChanged();
            updateSize();
        }
    }

    PlaylistPanel& owner;
    int draggedIndex = -1;
    int dragOffsetTicks = 0;
    bool resizing = false;
    bool changed = false;
};

// ------------------------------------------------------------------ panel

PlaylistPanel::PlaylistPanel (AppContext& ctx) : context (ctx)
{
    snapBox.addItem ("Snap: bar",  kTicksPerBar);
    snapBox.addItem ("Snap: beat", kPPQ);
    snapBox.addItem ("Snap: 1/2 bar", kTicksPerBar / 2);
    snapBox.setSelectedId (kTicksPerBar, juce::dontSendNotification);
    addAndMakeVisible (snapBox);

    hintLabel.setText ("paint: left-click (uses selected pattern)   delete: right-click   seek: click ruler",
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, colours::textDim);
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (hintLabel);

    clipArea = std::make_unique<ClipArea> (*this);
    viewport.setViewedComponent (clipArea.get(), false);
    addAndMakeVisible (viewport);

    context.structureBroadcaster.addChangeListener (this);
    context.contentBroadcaster.addChangeListener (this);
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
    clipArea->updateSize();
    clipArea->repaint();
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
}

void PlaylistPanel::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (30).reduced (4, 3);
    snapBox.setBounds (headerArea.removeFromLeft (130));
    headerArea.removeFromLeft (10);
    hintLabel.setBounds (headerArea);
    viewport.setBounds (area);
    clipArea->updateSize();
}

} // namespace fable
