#include "PianoRollPanel.h"

namespace fable
{

static constexpr int   kKeyWidth   = 64;
static constexpr int   kNoteHeight = 12;
static constexpr int   kLowNote    = 24;   // C2
static constexpr int   kHighNote   = 108;  // C8
static constexpr float kPixelsPerTick = 48.0f / (float) kTicksPerStep;  // step = 48 px
static constexpr int   kVelocityLaneHeight = 70;

static int   tickToX (double tick)   { return kKeyWidth + (int) (tick * kPixelsPerTick); }
static double xToTick (int x)        { return juce::jmax (0.0, (x - kKeyWidth) / (double) kPixelsPerTick); }
static int   pitchToY (int pitch)    { return (kHighNote - pitch) * kNoteHeight; }
static int   yToPitch (int y)        { return juce::jlimit (0, 127, kHighNote - y / kNoteHeight); }

// ------------------------------------------------------------------ grid

class PianoRollPanel::NoteGrid : public juce::Component
{
public:
    explicit NoteGrid (PianoRollPanel& ownerToUse) : owner (ownerToUse)
    {
        setWantsKeyboardFocus (true);
    }

    int snapTicks() const
    {
        const int id = owner.snapBox.getSelectedId();
        return id > 0 ? id : kTicksPerStep;
    }

    int gridLengthTicks()
    {
        auto* pattern = owner.currentPattern();
        // draw at least 4 bars, plus a bar of headroom past the musical end
        return juce::jmax (pattern != nullptr ? pattern->lengthTicks() + kTicksPerBar : 0,
                           4 * kTicksPerBar);
    }

    void updateSize()
    {
        setSize (kKeyWidth + (int) (gridLengthTicks() * kPixelsPerTick),
                 (kHighNote - kLowNote + 1) * kNoteHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panelDark);
        const int lengthTicks = gridLengthTicks();

        // row shading + horizontal lines
        for (int pitch = kLowNote; pitch <= kHighNote; ++pitch)
        {
            const int y = pitchToY (pitch);
            const int semitone = pitch % 12;
            const bool black = semitone == 1 || semitone == 3 || semitone == 6
                            || semitone == 8 || semitone == 10;
            g.setColour (black ? colours::panelDark.darker (0.25f) : colours::panelDark);
            g.fillRect (kKeyWidth, y, getWidth() - kKeyWidth, kNoteHeight);
            if (semitone == 0)
            {
                g.setColour (colours::outline.brighter (0.15f));
                g.drawHorizontalLine (y + kNoteHeight, (float) kKeyWidth, (float) getWidth());
            }
        }

        // vertical grid lines
        for (int tick = 0; tick <= lengthTicks; tick += kTicksPerStep)
        {
            const int x = tickToX (tick);
            if (tick % kTicksPerBar == 0)        g.setColour (colours::outline.brighter (0.35f));
            else if (tick % kPPQ == 0)           g.setColour (colours::outline.brighter (0.15f));
            else                                 g.setColour (colours::outline.withAlpha (0.5f));
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        }

        // pattern end marker
        if (auto* pattern = owner.currentPattern())
        {
            g.setColour (colours::accent.withAlpha (0.5f));
            g.drawVerticalLine (tickToX (pattern->lengthTicks()), 0.0f, (float) getHeight());
        }

        // notes
        if (auto* notes = owner.currentNotes())
        {
            for (size_t i = 0; i < notes->size(); ++i)
            {
                const auto& n = (*notes)[i];
                auto r = noteRect (n);
                const bool isDragged = (int) i == draggedIndex;
                g.setColour (colours::accent.withAlpha (0.35f + 0.6f * n.velocity)
                                            .brighter (isDragged ? 0.3f : 0.0f));
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (colours::outline);
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
            }
        }

        // playhead
        if (owner.context.engine.isPlaying() && ! owner.context.engine.isSongMode())
        {
            g.setColour (colours::playhead);
            g.drawVerticalLine (tickToX (owner.context.engine.getPlayheadTicks()), 0.0f, (float) getHeight());
        }

        // keyboard column (drawn last, fixed at the left of the scrolled area is
        // handled by the viewport position in PianoRollPanel::paint; here we draw
        // it at x=0 of the grid which scrolls out of view horizontally)
        drawKeyboard (g);
    }

    void drawKeyboard (juce::Graphics& g)
    {
        const int x0 = owner.viewport.getViewPositionX();
        g.setColour (colours::panel);
        g.fillRect (x0, 0, kKeyWidth, getHeight());

        for (int pitch = kLowNote; pitch <= kHighNote; ++pitch)
        {
            const int y = pitchToY (pitch);
            const int semitone = pitch % 12;
            const bool black = semitone == 1 || semitone == 3 || semitone == 6
                            || semitone == 8 || semitone == 10;
            g.setColour (black ? juce::Colour (0xff202225) : juce::Colour (0xffb9bec3));
            g.fillRect (x0, y, kKeyWidth - 4, kNoteHeight - 1);

            if (semitone == 0)
            {
                g.setColour (black ? colours::text : juce::Colour (0xff33373b));
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText ("C" + juce::String (pitch / 12),
                            x0, y, kKeyWidth - 10, kNoteHeight, juce::Justification::centredRight);
            }
        }
        g.setColour (colours::outline);
        g.drawVerticalLine (x0 + kKeyWidth - 4, 0.0f, (float) getHeight());
    }

    juce::Rectangle<float> noteRect (const Note& n) const
    {
        return { (float) tickToX (n.startTick), (float) pitchToY (n.pitch) + 1.0f,
                 juce::jmax (4.0f, n.lengthTicks * kPixelsPerTick), (float) kNoteHeight - 2.0f };
    }

    int noteIndexAt (juce::Point<int> pos, bool& onRightEdge)
    {
        onRightEdge = false;
        if (auto* notes = owner.currentNotes())
        {
            for (int i = (int) notes->size() - 1; i >= 0; --i)
            {
                auto r = noteRect ((*notes)[(size_t) i]);
                if (r.expanded (2.0f, 0.0f).contains (pos.toFloat()))
                {
                    onRightEdge = pos.x > r.getRight() - 6.0f;
                    return i;
                }
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
        // Grab focus on hover (not just click) so keyboard shortcuts that act on
        // "the note under the cursor" work without first having to click - a
        // click on empty space would otherwise create a note as a side effect.
        lastMousePos = e.getPosition();
        grabKeyboardFocus();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        lastMousePos = e.getPosition();

        auto* notes = owner.currentNotes();
        if (notes == nullptr)
            return;

        // keyboard preview
        if (e.getPosition().x - owner.viewport.getViewPositionX() < kKeyWidth)
        {
            previewPitch = yToPitch (e.getPosition().y);
            owner.context.engine.auditionNoteOn (owner.context.selectedChannelId, previewPitch, 0.8f);
            return;
        }

        bool onEdge = false;
        draggedIndex = noteIndexAt (e.getPosition(), onEdge);

        if (e.mods.isPopupMenu())
        {
            if (draggedIndex >= 0)
            {
                notes->erase (notes->begin() + draggedIndex);
                draggedIndex = -1;
                owner.context.contentChanged();
                repaint();
            }
            return;
        }

        if (draggedIndex < 0)
        {
            // create a note at the snapped position
            Note n;
            n.startTick   = ((int) xToTick (e.getPosition().x) / snapTicks()) * snapTicks();
            n.lengthTicks = lastLength;
            n.pitch       = yToPitch (e.getPosition().y);
            n.velocity    = 0.8f;
            notes->push_back (n);
            draggedIndex = (int) notes->size() - 1;
            resizing = false;
            dragOffsetTicks = 0;
            owner.context.engine.auditionNoteOn (owner.context.selectedChannelId, n.pitch, n.velocity);
            previewPitch = n.pitch;
            owner.context.contentChanged();
            repaint();
        }
        else
        {
            resizing = onEdge;
            dragOffsetTicks = (int) xToTick (e.getPosition().x) - (*notes)[(size_t) draggedIndex].startTick;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        lastMousePos = e.getPosition();

        auto* notes = owner.currentNotes();
        if (notes == nullptr || draggedIndex < 0 || draggedIndex >= (int) notes->size())
            return;

        auto& n = (*notes)[(size_t) draggedIndex];
        const int snap = snapTicks();

        if (resizing)
        {
            const int endTick = juce::jmax (n.startTick + snap / 2,
                                            (((int) xToTick (e.getPosition().x) + snap / 2) / snap) * snap);
            n.lengthTicks = endTick - n.startTick;
            lastLength = n.lengthTicks;
        }
        else
        {
            const int newStart = juce::jmax (0, ((((int) xToTick (e.getPosition().x) - dragOffsetTicks)
                                                  + snap / 2) / snap) * snap);
            const int newPitch = yToPitch (e.getPosition().y);
            if (newPitch != n.pitch && previewPitch >= 0)
            {
                owner.context.engine.auditionNoteOff (owner.context.selectedChannelId, previewPitch);
                owner.context.engine.auditionNoteOn (owner.context.selectedChannelId, newPitch, n.velocity);
                previewPitch = newPitch;
            }
            n.startTick = newStart;
            n.pitch     = newPitch;
        }
        changedWhileDragging = true;
        repaint();
        repaintVelocityLane();   // defined after VelocityLane below
    }

    void repaintVelocityLane();

    void mouseUp (const juce::MouseEvent&) override
    {
        if (previewPitch >= 0)
        {
            owner.context.engine.auditionNoteOff (owner.context.selectedChannelId, previewPitch);
            previewPitch = -1;
        }
        if (changedWhileDragging)
        {
            changedWhileDragging = false;
            owner.context.contentChanged();
            owner.grid->updateSize();
        }
        draggedIndex = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // double-click a note deletes it (matches right-click)
        bool onEdge = false;
        const int index = noteIndexAt (e.getPosition(), onEdge);
        if (auto* notes = owner.currentNotes(); notes != nullptr && index >= 0)
        {
            notes->erase (notes->begin() + index);
            owner.context.contentChanged();
            repaint();
        }
    }

    // Keyboard shortcuts act on the note currently under the mouse cursor
    // (there is no multi-select model in the piano roll, so these are
    // single-note operations rather than FL's full selection-based editing).
    bool keyPressed (const juce::KeyPress& key) override
    {
        auto* notes = owner.currentNotes();
        const int code = key.getKeyCode();   // compare the raw key, independent of modifiers

        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            bool onEdge = false;
            const int index = notes != nullptr ? noteIndexAt (lastMousePos, onEdge) : -1;
            if (index < 0)
                return false;
            notes->erase (notes->begin() + index);
            owner.context.contentChanged();
            repaint();
            return true;
        }

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            bool onEdge = false;
            const int index = notes != nullptr ? noteIndexAt (lastMousePos, onEdge) : -1;
            if (index < 0)
                return false;

            const int semitones = key.getModifiers().isShiftDown() ? 12 : 1;
            const int delta = (code == juce::KeyPress::upKey ? 1 : -1) * semitones;
            auto& n = (*notes)[(size_t) index];
            n.pitch = juce::jlimit (0, 127, n.pitch + delta);
            owner.context.contentChanged();
            repaint();
            return true;
        }

        if (code == juce::KeyPress::homeKey)
        {
            owner.context.engine.setPositionTicks (0.0);
            return true;
        }
        if (code == juce::KeyPress::endKey)
        {
            if (auto* pattern = owner.currentPattern())
                owner.context.engine.setPositionTicks (
                    (double) juce::jmax (0, pattern->lengthTicks() - kTicksPerStep));
            return true;
        }

        return false;
    }

    PianoRollPanel& owner;
    int draggedIndex = -1;
    int dragOffsetTicks = 0;
    int lastLength = kTicksPerStep;
    int previewPitch = -1;
    bool resizing = false;
    bool changedWhileDragging = false;
    juce::Point<int> lastMousePos;
};

// ------------------------------------------------------------- velocity lane

class PianoRollPanel::VelocityLane : public juce::Component
{
public:
    explicit VelocityLane (PianoRollPanel& ownerToUse) : owner (ownerToUse) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::panelDark.darker (0.2f));
        g.setColour (colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("velocity", 4, 2, 60, 12, juce::Justification::left);

        if (auto* notes = owner.currentNotes())
        {
            const int xOffset = owner.viewport.getViewPositionX();
            for (auto& n : *notes)
            {
                const int x = tickToX (n.startTick) - xOffset;
                const int h = (int) ((getHeight() - 6) * n.velocity);
                g.setColour (colours::accent.withAlpha (0.35f + 0.6f * n.velocity));
                g.fillRect (x, getHeight() - h, 5, h);
            }
        }
    }

    void applyDrag (const juce::MouseEvent& e)
    {
        auto* notes = owner.currentNotes();
        if (notes == nullptr)
            return;

        const int xOffset = owner.viewport.getViewPositionX();
        const float velocity = juce::jlimit (0.05f, 1.0f,
                                             1.0f - (float) e.getPosition().y / (float) getHeight());
        bool changed = false;
        for (auto& n : *notes)
        {
            const int x = tickToX (n.startTick) - xOffset;
            if (std::abs (e.getPosition().x - x - 2) < 6)
            {
                n.velocity = velocity;
                changed = true;
            }
        }
        if (changed)
        {
            dirty = true;
            repaint();
            owner.grid->repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { applyDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { applyDrag (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dirty)
        {
            dirty = false;
            owner.context.contentChanged();
        }
    }

    PianoRollPanel& owner;
    bool dirty = false;
};

void PianoRollPanel::NoteGrid::repaintVelocityLane()
{
    if (owner.velocityLane != nullptr)
        owner.velocityLane->repaint();
}

// ------------------------------------------------------------------ panel

PianoRollPanel::PianoRollPanel (AppContext& ctx) : context (ctx)
{
    channelBox.onChange = [this]
    {
        const int index = channelBox.getSelectedItemIndex();
        if (index >= 0 && index < (int) context.project.channels.size())
        {
            context.selectChannel (context.project.channels[(size_t) index].id);
        }
    };
    addAndMakeVisible (channelBox);

    snapBox.addItem ("1/16 step", kTicksPerStep);
    snapBox.addItem ("1/8",       kTicksPerStep * 2);
    snapBox.addItem ("1/4 beat",  kPPQ);
    snapBox.addItem ("1/2",       kPPQ * 2);
    snapBox.addItem ("Bar",       kTicksPerBar);
    snapBox.addItem ("1/32",      kTicksPerStep / 2);
    snapBox.setSelectedId (kTicksPerStep, juce::dontSendNotification);
    addAndMakeVisible (snapBox);

    hintLabel.setText ("draw: left-click   delete: right-click/Del   resize: drag right edge   "
                       "Up/Down: transpose (Shift=octave)   Home/End: seek",
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, colours::textDim);
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (hintLabel);

    grid = std::make_unique<NoteGrid> (*this);
    viewport.setViewedComponent (grid.get(), false);
    addAndMakeVisible (viewport);

    velocityLane = std::make_unique<VelocityLane> (*this);
    addAndMakeVisible (*velocityLane);

    context.structureBroadcaster.addChangeListener (this);
    context.contentBroadcaster.addChangeListener (this);
    refreshHeader();
    grid->updateSize();

    startTimerHz (30);
    setOpaque (true);
}

PianoRollPanel::~PianoRollPanel()
{
    context.structureBroadcaster.removeChangeListener (this);
    context.contentBroadcaster.removeChangeListener (this);
}

std::vector<Note>* PianoRollPanel::currentNotes()
{
    auto* pattern = currentPattern();
    if (pattern == nullptr || context.project.channelById (context.selectedChannelId) == nullptr)
        return nullptr;
    return &pattern->dataFor (context.selectedChannelId).notes;
}

void PianoRollPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshHeader();
    grid->updateSize();
    grid->repaint();
    velocityLane->repaint();
}

void PianoRollPanel::timerCallback()
{
    if (context.engine.isPlaying())
        grid->repaint();
    // keep the keyboard column pinned while scrolling
    static int lastX = -1;
    if (viewport.getViewPositionX() != lastX)
    {
        lastX = viewport.getViewPositionX();
        grid->repaint();
        velocityLane->repaint();
    }
}

void PianoRollPanel::refreshHeader()
{
    channelBox.clear (juce::dontSendNotification);
    int id = 1, selectIndex = -1;
    for (auto& c : context.project.channels)
    {
        channelBox.addItem (c.name, id);
        if (c.id == context.selectedChannelId)
            selectIndex = id - 1;
        ++id;
    }
    if (selectIndex < 0 && ! context.project.channels.empty())
    {
        context.selectedChannelId = context.project.channels[0].id;
        selectIndex = 0;
    }
    channelBox.setSelectedItemIndex (selectIndex, juce::dontSendNotification);
}

void PianoRollPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void PianoRollPanel::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (30).reduced (4, 3);
    channelBox.setBounds (headerArea.removeFromLeft (170));
    headerArea.removeFromLeft (6);
    snapBox.setBounds (headerArea.removeFromLeft (110));
    headerArea.removeFromLeft (10);
    hintLabel.setBounds (headerArea);

    velocityLane->setBounds (area.removeFromBottom (kVelocityLaneHeight));
    viewport.setBounds (area);
    grid->updateSize();

    // start scrolled to C5
    if (viewport.getViewPositionY() == 0)
        viewport.setViewPosition (0, pitchToY (kDefaultRootNote + 12));
}

} // namespace fable
