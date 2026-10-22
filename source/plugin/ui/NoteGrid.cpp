#include "NoteGrid.h"
#include "Icons.h"
#include "MpeLookAndFeel.h"
#include "core/NoteTools.h"

namespace mperoll
{

std::vector<MpeNote> NoteGrid::clipboard;

namespace
{
    constexpr double minNoteLength = 1.0 / 64.0;

    /** Adds a curve to a path, skipping points that land on the same pixel column. */
    template <typename MapY>
    void addCurve (juce::Path& path, const ExpressionCurve& curve, float fallback, double noteStart, double length,
                   const ViewState& view, MapY&& mapY)
    {
        auto pointAt = [&] (double t)
        {
            return juce::Point<float> ((float) view.beatToX (noteStart + t), mapY (curve.valueAt (t, fallback)));
        };

        auto last = pointAt (0.0);
        path.lineTo (last);

        for (const auto& p : curve.getPoints())
        {
            if (p.beat <= 0.0 || p.beat >= length)
                continue;

            const auto next = pointAt (p.beat);

            if (next.x - last.x >= 1.0f)
            {
                path.lineTo (next);
                last = next;
            }
        }

        path.lineTo (pointAt (length));
    }

    juce::String keyName (int key)
    {
        return juce::MidiMessage::getMidiNoteName (key, true, true, 5);
    }

}

NoteGrid::NoteGrid (MpeRollProcessor& p, ViewState& v)
    : processor (p), document (p.getDocument()), view (v)
{
    makeCursors();
    setWantsKeyboardFocus (true);
    view.addListener (this);
    document.addChangeListener (this);
}

NoteGrid::~NoteGrid()
{
    stopAudition();
    document.removeChangeListener (this);
    view.removeListener (this);
}

//==============================================================================
juce::Rectangle<float> NoteGrid::noteBounds (const MpeNote& n) const
{
    const auto x = (float) view.beatToX (n.start);
    const auto w = juce::jmax (4.0f, (float) (n.length * view.pixelsPerBeat));
    return { x, view.keyToY (n.key), w, view.rowHeight };
}

const MpeNote* NoteGrid::noteAt (juce::Point<float> position) const
{
    const auto& notes = document.getEditClip().notes;

    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
        if (noteBounds (*it).contains (position))
            return &*it;

    return nullptr;
}

bool NoteGrid::isNearEnd (const MpeNote& n, juce::Point<float> position) const
{
    const auto r = noteBounds (n);
    return position.x >= r.getRight() - juce::jmin (7.0f, r.getWidth() * 0.3f);
}

void NoteGrid::setPlayhead (double beat, bool visible)
{
    if (visible == playheadVisible && std::abs (beat - playheadBeat) * view.pixelsPerBeat < 0.5)
        return;

    auto strip = [this] (double b) { return juce::Rectangle<int> ((int) view.beatToX (b) - 2, 0, 5, getHeight()); };

    if (playheadVisible)
        repaint (strip (playheadBeat));

    playheadBeat = beat;
    playheadVisible = visible;

    if (playheadVisible)
        repaint (strip (playheadBeat));
}

//==============================================================================
void NoteGrid::paintBackground (juce::Graphics& g, const MpeClip& clip) const
{
    const auto w = (float) getWidth();
    const auto h = (float) getHeight();
    const auto topKey = juce::jlimit (0, 127, view.yToKey (0.0f));
    const auto bottomKey = juce::jlimit (0, 127, view.yToKey (h));

    for (int key = bottomKey; key <= topKey; ++key)
    {
        const auto y = view.keyToY (key);
        g.setColour (juce::MidiMessage::isMidiNoteBlack (key) ? Theme::rowBlack : Theme::rowWhite);
        g.fillRect (0.0f, y, w, view.rowHeight);

        if (key % 12 == 0)
        {
            g.setColour (Theme::lineBeat);
            g.fillRect (0.0f, y + view.rowHeight - 1.0f, w, 1.0f);
        }
    }

    // The background picture, over the rows and under the lines and notes.
    if (const auto picture = skins->getBackgroundFor (getLocalBounds()); picture.isValid())
    {
        g.setOpacity (skins->getBackgroundStrength());
        g.drawImageAt (picture, 0, 0);
        g.setOpacity (1.0f);
    }

    const auto startBeat = view.scrollBeat;
    const auto endBeat = view.xToBeat (w);

    auto drawLines = [&] (double step, juce::Colour colour)
    {
        if (step <= 0.0 || step * view.pixelsPerBeat < 6.0)
            return;

        g.setColour (colour);

        for (auto b = std::ceil (startBeat / step) * step; b <= endBeat; b += step)
            g.fillRect ((float) view.beatToX (b), 0.0f, 1.0f, h);
    };

    drawLines (view.snap, Theme::lineSub);
    drawLines (1.0, Theme::lineBeat);
    drawLines (4.0, Theme::lineBar);

    if (clip.looping)
    {
        const auto loopEnd = (float) view.beatToX (clip.length);

        if (loopEnd < w)
        {
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRect (loopEnd, 0.0f, w - loopEnd, h);
        }

        g.setColour (Theme::loopRegion.brighter (0.5f));
        g.fillRect (loopEnd - 1.0f, 0.0f, 2.0f, h);
    }
}

void NoteGrid::paintNote (juce::Graphics& g, const MpeNote& n, bool selected, bool outsideLoop, juce::Colour base) const
{
    const auto r = noteBounds (n).reduced (0.0f, 0.5f);
    auto fill = selected ? Theme::noteSelected : base.withMultipliedBrightness (0.55f + 0.45f * n.strike);

    if (outsideLoop)
        fill = fill.withAlpha (0.35f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 2.5f);
    g.setColour (selected ? Theme::text : fill.darker (0.7f));
    g.drawRoundedRectangle (r, 2.5f, selected ? 1.5f : 1.0f);

    const auto& lanes = n.lanes;
    const auto innerHeight = r.getHeight() - 2.0f;

    if (! lanes.pressure.isEmpty() && innerHeight >= 4.0f)
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getBottom());
        addCurve (p, lanes.pressure, 0.0f, n.start, n.length, view,
                  [&] (float v) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, v) * innerHeight; });
        p.lineTo (r.getRight(), r.getBottom());
        p.closeSubPath();
        g.setColour (Theme::pressure.withAlpha (0.5f));
        g.fillPath (p);
    }

    if (! lanes.timbre.isEmpty() && innerHeight >= 4.0f)
    {
        juce::Path p;
        p.startNewSubPath ((float) view.beatToX (n.start), r.getBottom() - lanes.timbre.valueAt (0.0, 0.5f) * innerHeight);
        addCurve (p, lanes.timbre, 0.5f, n.start, n.length, view,
                  [&] (float v) { return r.getBottom() - 1.0f - juce::jlimit (0.0f, 1.0f, v) * innerHeight; });
        g.setColour (Theme::timbre);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    if (r.getWidth() > 30.0f && view.rowHeight >= 11.0f)
    {
        g.setColour (Theme::noteText);
        g.setFont (juce::FontOptions (juce::jmin (12.0f, view.rowHeight - 2.0f)));
        g.drawText (keyName (n.key), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
    }

    if (! lanes.pitch.isEmpty())
    {
        const auto startPoint = juce::Point<float> ((float) view.beatToX (n.start),
                                                    view.pitchToY ((float) n.key + lanes.pitch.valueAt (0.0, 0.0f)));
        juce::Path p;
        p.startNewSubPath (startPoint);
        addCurve (p, lanes.pitch, 0.0f, n.start, n.length, view,
                  [&] (float v) { return view.pitchToY ((float) n.key + v); });

        g.setColour (Theme::glide.withAlpha (outsideLoop ? 0.4f : 1.0f));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (startPoint));
    }

    // Vibrato is in Hz: drawn at the project's tempo, as it will play.
    if (lanes.vibrato.maxValue (0.0f) > 0.0f)
    {
        const auto cyclesPerBeat = document.getEditClip().vibratoRate * 60.0 / juce::jmax (1.0, processor.getTransport().bpm);
        const auto noteX = (float) view.beatToX (n.start);
        const auto fromX = juce::jmax (noteX, -4.0f);
        const auto toX = juce::jmin ((float) view.beatToX (n.end()), (float) getWidth() + 4.0f);
        auto pointAt = [&] (float x)
        {
            const auto t = (double) (x - noteX) / view.pixelsPerBeat;
            const auto pitch = (float) n.key + lanes.pitch.valueAt (t, 0.0f)
                             + vibratoOffset (lanes.vibrato.valueAt (t, 0.0f), t * cyclesPerBeat);
            return juce::Point<float> (x, view.pitchToY (pitch));
        };

        juce::Path p;
        p.startNewSubPath (pointAt (fromX));

        for (auto x = fromX + 1.5f; x <= toX; x += 1.5f)
            p.lineTo (pointAt (x));

        g.setColour (Theme::glide.withAlpha (outsideLoop ? 0.3f : 0.8f));
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }
}

void NoteGrid::paint (juce::Graphics& g)
{
    const auto& clip = document.getEditClip();
    paintBackground (g, clip);

    const auto w = (float) getWidth();
    const auto& selection = document.selection;

    for (const auto& n : clip.notes)
    {
        if (view.beatToX (n.start) > w)
            break;

        if (view.beatToX (n.end()) < -4.0)
            continue;

        paintNote (g, n, selection.count (n.id) > 0, clip.looping && n.start >= clip.length, Theme::note);
    }

    if (processor.getRecordingSlot() == document.getEditSlot())
        for (const auto& n : processor.getRecordingPreview().getNotes())
            paintNote (g, n, false, false, Theme::record);

    if (dragMode == DragMode::marquee)
    {
        g.setColour (Theme::text.withAlpha (0.1f));
        g.fillRect (marquee);
        g.setColour (Theme::text.withAlpha (0.6f));
        g.drawRect (marquee, 1.0f);
    }

    if (dragMode == DragMode::slice)
    {
        // The cut line, across the keys the pointer has been dragged over, at the snapped beat.
        const auto x = (float) view.beatToX (sliceBeat);
        const auto top = view.keyToY (sliceHighKey);
        const auto bottom = view.keyToY (sliceLowKey) + view.rowHeight;
        g.setColour (Theme::text.withAlpha (0.9f));
        g.fillRect (x - 0.75f, top, 1.5f, bottom - top);
        g.setColour (Theme::glide);
        g.fillEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f);
        g.fillEllipse (x - 3.0f, bottom - 3.0f, 6.0f, 6.0f);
    }

    if (dragMode == DragMode::glide && glideLabel.isNotEmpty())
    {
        const juce::Font font (juce::FontOptions (12.0f, juce::Font::bold));
        g.setFont (font);
        const auto box = juce::Rectangle<float> (0.0f, 0.0f, juce::GlyphArrangement::getStringWidth (font, glideLabel) + 10.0f, 18.0f)
                             .withPosition (glideLabelPosition.translated (8.0f, -22.0f));
        g.setColour (Theme::background.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (Theme::glide);
        g.drawText (glideLabel, box, juce::Justification::centred);
    }

    if (playheadVisible)
    {
        g.setColour (Theme::playhead.withAlpha (0.85f));
        g.fillRect ((float) view.beatToX (playheadBeat), 0.0f, 1.5f, (float) getHeight());
    }
}

//==============================================================================
void NoteGrid::makeCursors()
{
    // Each tool's pointer in the skin's colours, its hot spot where the tool acts: the brush's tip,
    // the eraser's rubber end, the scissors' blades.
    brushCursor = makeIconCursor (Icon::paint, { 3.0f, 21.0f }, Theme::text, Theme::note);
    eraserCursor = makeIconCursor (Icon::erase, { 3.5f, 15.5f }, Theme::text, Theme::note);
    scissorsCursor = makeIconCursor (Icon::slice, { 21.0f, 12.0f }, Theme::text, Theme::note);
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    switch (view.tool)
    {
        case ViewState::Tool::glide:    setMouseCursor (juce::MouseCursor::CrosshairCursor); return;
        case ViewState::Tool::paint:    setMouseCursor (brushCursor); return;
        case ViewState::Tool::erase:    setMouseCursor (eraserCursor); return;
        case ViewState::Tool::slice:    setMouseCursor (scissorsCursor); return;
        case ViewState::Tool::select:   setMouseCursor (juce::MouseCursor::NormalCursor); return;
        case ViewState::Tool::edit:     break;
    }

    if (auto* n = noteAt (e.position))
        setMouseCursor (isNearEnd (*n, e.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                   : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void NoteGrid::lookAndFeelChanged()
{
    // The skin has changed: the tools' pointers take up its colours.
    makeCursors();

    if (view.tool == ViewState::Tool::paint)
        setMouseCursor (brushCursor);
    else if (view.tool == ViewState::Tool::erase)
        setMouseCursor (eraserCursor);
    else if (view.tool == ViewState::Tool::slice)
        setMouseCursor (scissorsCursor);
}

void NoteGrid::captureOriginals()
{
    originals.clear();

    for (auto id : document.selection)
        if (auto* n = document.getEditClip().findNote (id))
            originals.push_back ({ id, n->start, n->length, n->key });
}

void NoteGrid::beginMove (bool duplicate)
{
    document.beginEdit (duplicate ? "Duplicate notes" : "Move notes");

    if (duplicate)
    {
        auto& clip = document.editClip();
        std::vector<MpeNote> copies;

        for (auto id : document.selection)
            if (auto* n = clip.findNote (id))
                copies.push_back (*n);

        std::set<uint32_t> newSelection;

        for (auto& c : copies)
        {
            const auto oldId = c.id;
            const auto newId = clip.addNote (c);
            newSelection.insert (newId);

            if (oldId == anchorId)
                anchorId = newId;
        }

        document.selection = newSelection;
        document.changed();
    }

    captureOriginals();
    dragMode = DragMode::move;
}

void NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    if (isShowing())
        grabKeyboardFocus();
    downPos = e.position;
    auto& selection = document.selection;

    if (view.tool == ViewState::Tool::glide)
    {
        if (e.mods.isPopupMenu())
        {
            dragMode = DragMode::glideErase;
            document.beginEdit ("Erase glide");
            eraseGlide (e.position);
            return;
        }

        if (auto* n = noteAt (e.position))
        {
            if (selection.count (n->id) == 0)
            {
                selection = { n->id };
                document.selectionChanged();
            }

            document.beginEdit ("Draw glide");
            dragMode = DragMode::glide;
            startGlide (*n, e.position, view.snapGlide && ! e.mods.isAltDown());
            drawGlide (e.position);
        }

        return;
    }

    if (view.tool == ViewState::Tool::slice && ! e.mods.isPopupMenu())
    {
        beginSlice (e);
        return;
    }

    if (view.tool == ViewState::Tool::select && ! e.mods.isPopupMenu())
    {
        beginSelect (e);
        return;
    }

    // The right button deletes with any tool but Glide; the Delete tool does it with the left.
    if (e.mods.isPopupMenu() || view.tool == ViewState::Tool::erase)
    {
        dragMode = DragMode::erase;
        document.beginEdit ("Delete notes");
        eraseAt (e.position);
        return;
    }

    if (view.tool == ViewState::Tool::paint && ! e.mods.isCommandDown())
    {
        document.beginEdit ("Paint notes");
        dragMode = DragMode::paint;
        startPaint (e.position, e.mods.isAltDown());
        return;
    }

    if (auto* n = noteAt (e.position))
    {
        const auto id = n->id;
        const auto key = n->key;
        const auto nearEnd = isNearEnd (*n, e.position);
        lastVelocity = n->strike;

        if (e.mods.isCommandDown())
        {
            if (selection.count (id) > 0)
                selection.erase (id);
            else
                selection.insert (id);

            document.selectionChanged();
            return;
        }

        if (selection.count (id) == 0)
        {
            selection = { id };
            document.selectionChanged();
        }

        anchorId = id;

        if (nearEnd)
        {
            document.beginEdit ("Resize notes");
            captureOriginals();
            dragMode = DragMode::resize;
        }
        else
        {
            view.lastNoteLength = n->length;
            beginMove (e.mods.isShiftDown());
            startAudition (key, lastVelocity);
        }

        return;
    }

    if (e.mods.isCommandDown())
    {
        dragMode = DragMode::marquee;
        marqueeBase = selection;
        marquee = {};
        return;
    }

    // Empty space: add a note and keep dragging it.
    MpeNote note;
    note.key = juce::jlimit (0, 127, view.yToKey (e.position.y));
    note.start = juce::jmax (0.0, view.snapFloor (view.xToBeat (e.position.x), e.mods.isAltDown()));
    note.length = view.lastNoteLength;
    note.strike = lastVelocity;

    document.beginEdit ("Add note");
    anchorId = document.editClip().addNote (note);
    selection = { anchorId };
    document.changed();

    captureOriginals();
    dragMode = DragMode::move;
    startAudition (note.key, note.strike);
}

void NoteGrid::dragMove (const juce::MouseEvent& e)
{
    const auto anchor = std::find_if (originals.begin(), originals.end(), [this] (const Original& o) { return o.id == anchorId; });

    if (anchor == originals.end())
        return;

    const auto rawDelta = view.xToBeat (e.position.x) - view.xToBeat (downPos.x);
    auto deltaBeats = view.snapRound (anchor->start + rawDelta, e.mods.isAltDown()) - anchor->start;
    auto deltaKeys = view.yToKey (e.position.y) - view.yToKey (downPos.y);

    double minStart = 1.0e12;
    int minKey = 127, maxKey = 0;

    for (const auto& o : originals)
    {
        minStart = std::min (minStart, o.start);
        minKey = std::min (minKey, o.key);
        maxKey = std::max (maxKey, o.key);
    }

    deltaBeats = std::max (deltaBeats, -minStart);
    deltaKeys = juce::jlimit (-minKey, 127 - maxKey, deltaKeys);

    auto& clip = document.editClip();

    for (const auto& o : originals)
    {
        if (auto* n = clip.findNote (o.id))
        {
            n->start = o.start + deltaBeats;
            n->key = o.key + deltaKeys;
        }
    }

    clip.sortNotes();
    document.changed();

    if (anchor->key + deltaKeys != auditionKey && auditionKey >= 0)
        startAudition (anchor->key + deltaKeys, lastVelocity);
}

void NoteGrid::dragResize (const juce::MouseEvent& e)
{
    const auto anchor = std::find_if (originals.begin(), originals.end(), [this] (const Original& o) { return o.id == anchorId; });

    if (anchor == originals.end())
        return;

    const auto bypass = e.mods.isAltDown();
    const auto rawDelta = view.xToBeat (e.position.x) - view.xToBeat (downPos.x);
    const auto originalEnd = anchor->start + anchor->length;
    const auto newEnd = view.snapRound (originalEnd + rawDelta, bypass);
    const auto delta = newEnd - originalEnd;
    const auto minimum = (view.snap > 0.0 && ! bypass) ? view.snap : minNoteLength;

    auto& clip = document.editClip();

    for (const auto& o : originals)
        if (auto* n = clip.findNote (o.id))
            n->length = std::max (std::min (minimum, o.length), o.length + delta);

    if (auto* n = clip.findNote (anchorId))
        view.lastNoteLength = n->length;

    document.changed();
}

void NoteGrid::eraseAt (juce::Point<float> position)
{
    if (auto* n = noteAt (position))
    {
        const auto id = n->id;
        document.editClip().removeNote (id);
        document.selection.erase (id);
        document.changed();
    }
}

void NoteGrid::startPaint (juce::Point<float> position, bool bypassSnap)
{
    paintLength = juce::jmax (minNoteLength, view.lastNoteLength);
    paintOrigin = juce::jmax (0.0, view.snapFloor (view.xToBeat (position.x), bypassSnap));

    // Starting on a note carries on in its rhythm.
    if (auto* n = noteAt (position))
    {
        paintOrigin = n->start;
        paintLength = juce::jmax (minNoteLength, n->length);
        view.lastNoteLength = paintLength;
        lastVelocity = n->strike;
    }

    document.selection.clear();
    paintSlot = 0;
    paintTo (position, true);
}

void NoteGrid::paintTo (juce::Point<float> position, bool first)
{
    const auto key = juce::jlimit (0, 127, view.yToKey (position.y));
    const auto slot = (long) std::floor ((view.xToBeat (position.x) - paintOrigin) / paintLength + 1.0e-9);

    if (key != auditionKey)
        startAudition (key, lastVelocity);

    if (slot == paintSlot && ! first)
        return;

    // Every slot the pen passed since the last one gets a note, so a fast drag leaves no gaps. Slots
    // where this key already has a note are left alone, so going back over them adds nothing.
    auto& clip = document.editClip();
    const auto step = slot >= paintSlot ? 1L : -1L;
    bool added = false;

    for (auto s = first ? slot : paintSlot + step;; s += step)
    {
        const auto start = paintOrigin + (double) s * paintLength;
        const auto taken = std::any_of (clip.notes.begin(), clip.notes.end(), [&] (const MpeNote& n)
        {
            return n.key == key && n.start < start + paintLength - 1.0e-9 && n.end() > start + 1.0e-9;
        });

        if (start > -1.0e-9 && ! taken)
        {
            MpeNote note;
            note.key = key;
            note.start = juce::jmax (0.0, start);
            note.length = paintLength;
            note.strike = lastVelocity;
            document.selection.insert (clip.addNote (note));
            added = true;
        }

        if (s == slot)
            break;
    }

    paintSlot = slot;

    if (added || first)
        document.changed();
}

void NoteGrid::startGlide (const MpeNote& n, juce::Point<float> position, bool snapped)
{
    glideNote = n.id;
    glideStarted = false;
    glideSnapped = snapped;
    glideOriginal = n.lanes.pitch;
    glidePressTime = juce::jlimit (0.0, n.length, view.xToBeat (position.x) - n.start);

    // A snapped glide starts on the grid, from wherever the note's pitch is at that point.
    glideAnchorTime = juce::jlimit (0.0, n.length, view.snapRound (view.xToBeat (position.x), false) - n.start);
    glideAnchorValue = glideOriginal.valueAt (glideAnchorTime, 0.0f);
}

void NoteGrid::drawGlide (juce::Point<float> position)
{
    auto* n = document.editClip().findNote (glideNote);

    if (n == nullptr)
        return;

    const auto range = document.getSettings().output.memberBendRange;
    auto& curve = n->lanes.pitch;

    if (glideSnapped)
    {
        // A straight line from the anchor to a whole semitone on the grid. Redrawn from the original
        // each time, so the result only depends on where the pen is now.
        const auto t = juce::jlimit (0.0, n->length, view.snapRound (view.xToBeat (position.x), false) - n->start);
        const auto value = std::round (juce::jlimit (-range, range, view.yToPitch (position.y) - (float) n->key));

        curve = glideOriginal;

        // Drawn forwards from inside the note, the glide leaves the note's own pitch, held from the
        // start. Drawn back towards where it was pressed, it scoops into that pitch instead, so
        // nothing is added before the pen: its pitch is what's held from the start.
        if (curve.isEmpty() && glideAnchorTime > 1.0e-6 && t >= glideAnchorTime)
            curve.addPoint (0.0, 0.0f);

        if (std::abs (t - glideAnchorTime) < 1.0e-9)
            curve.addPoint (t, value);
        else if (t > glideAnchorTime)
            curve.replaceRange (glideAnchorTime, t, { { glideAnchorTime, glideAnchorValue }, { t, value } });
        else
            curve.replaceRange (t, glideAnchorTime, { { t, value }, { glideAnchorTime, glideAnchorValue } });

        const auto semitones = juce::roundToInt (value);
        glideLabel = (semitones > 0 ? "+" : "") + juce::String (semitones) + "  " + keyName (n->key + semitones);
        glideLabelPosition = { (float) view.beatToX (n->start + t), view.pitchToY ((float) n->key + value) };
        document.changed();
        return;
    }

    const auto t = juce::jlimit (0.0, n->length, view.xToBeat (position.x) - n->start);
    const auto value = juce::jlimit (-range, range, view.yToPitch (position.y) - (float) n->key);

    if (! glideStarted)
    {
        curve.addPoint (t, value);
        glideStarted = true;
    }
    else
    {
        // As with snap: drawn forwards from inside the note, the glide leaves the note's own pitch,
        // held from the start; drawn back towards where it was pressed, it scoops into that pitch
        // instead. So the start is only pinned once the pen has gone right of the press, and never
        // left of it (the first point is still at the press or later).
        if (glideOriginal.isEmpty() && glidePressTime > 1.0e-6 && t > glidePressTime + 1.0e-6
             && ! curve.isEmpty() && curve.getPoints().front().beat >= glidePressTime - 1.0e-9)
            curve.addPoint (0.0, 0.0f);

        curve.replaceRange (std::min (glideLastTime, t), std::max (glideLastTime, t),
                            { { glideLastTime, glideLastValue }, { t, value } });
    }

    glideLastTime = t;
    glideLastValue = value;
    document.changed();
}

void NoteGrid::eraseGlide (juce::Point<float> position)
{
    auto& clip = document.editClip();
    MpeNote* target = nullptr;

    for (auto& n : clip.notes)
    {
        const auto t = view.xToBeat (position.x) - n.start;

        if (t < 0.0 || t > n.length)
            continue;

        const auto lineY = view.pitchToY ((float) n.key + n.lanes.pitch.valueAt (t, 0.0f));

        if (noteBounds (n).contains (position) || std::abs (lineY - position.y) < 6.0f)
        {
            target = &n;
            break;
        }
    }

    if (target == nullptr)
        return;

    const auto t = view.xToBeat (position.x) - target->start;
    const auto reach = 5.0 / view.pixelsPerBeat;
    target->lanes.pitch.removeRange (t - reach, t + reach);

    if (target->lanes.pitch.size() == 1 && std::abs (target->lanes.pitch.getPoints()[0].value) < 1.0e-4f)
        target->lanes.pitch.clear();

    document.changed();
}

void NoteGrid::mouseDrag (const juce::MouseEvent& e)
{
    switch (dragMode)
    {
        case DragMode::move:        dragMove (e); break;
        case DragMode::resize:      dragResize (e); break;
        case DragMode::erase:       eraseAt (e.position); break;
        case DragMode::paint:       paintTo (e.position, false); break;
        case DragMode::glide:       drawGlide (e.position); break;
        case DragMode::glideErase:  eraseGlide (e.position); break;
        case DragMode::slice:       dragSlice (e); break;

        case DragMode::marquee:
        {
            marquee = juce::Rectangle<float> (downPos, e.position);
            auto selection = marqueeBase;

            for (const auto& n : document.getEditClip().notes)
                if (noteBounds (n).intersects (marquee))
                    selection.insert (n.id);

            document.selection = selection;
            document.selectionChanged();
            repaint();
            break;
        }

        case DragMode::none:
            break;
    }
}

void NoteGrid::mouseUp (const juce::MouseEvent& e)
{
    // A free glide pressed on a note without one and let go where it was pressed leaves the point
    // under the pen, which would be the whole note's pitch: detuned by up to half a semitone. A
    // click draws nothing, as a snapped one lands on the note's own pitch. The pointer's travel
    // tells a click from a drag, since a click usually slips a pixel or two.
    if (dragMode == DragMode::glide && ! glideSnapped && glideOriginal.isEmpty()
         && e.position.getDistanceFrom (downPos) < 4.0f)
        if (auto* n = document.editClip().findNote (glideNote))
        {
            n->lanes.pitch = glideOriginal;
            document.changed();
        }

    if (dragMode == DragMode::slice)
        finishSlice (e);
    else if (dragMode != DragMode::none && dragMode != DragMode::marquee)
        document.endEdit();

    stopAudition();
    dragMode = DragMode::none;
    glideLabel.clear();
    repaint();
}

void NoteGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (view.tool != ViewState::Tool::glide)
        return;

    if (auto* n = noteAt (e.position))
    {
        const auto id = n->id;
        document.perform ("Clear glide", [id] (MpeClip& clip)
        {
            if (auto* note = clip.findNote (id))
                note->lanes.pitch.clear();
        });
    }
}

void NoteGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;

    if (e.mods.isCommandDown())
        view.zoomHorizontally (delta > 0 ? 1.15 : 1.0 / 1.15, e.position.x);
    else if (e.mods.isAltDown())
        view.zoomVertically (delta > 0 ? 1.1f : 1.0f / 1.1f, e.position.y, (float) getHeight());
    else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        view.scrollHorizontally (-delta * 600.0 / view.pixelsPerBeat);
    else
        view.scrollVertically (wheel.deltaY * 30.0f, (float) getHeight());
}

//==============================================================================
bool NoteGrid::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto letter = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (mods.isCommandDown())
    {
        switch (letter)
        {
            case 'a': selectAll(); return true;
            case 'c': copySelection(); return true;
            case 'x': cutSelection(); return true;
            case 'v': paste(); return true;
            case 'b': duplicateSelection(); return true;
            case 'd': deselectAll(); return true;
            case 'z':
                if (mods.isShiftDown())
                    document.getUndoManager().redo();
                else
                    document.getUndoManager().undo();
                return true;
            case 'y': document.getUndoManager().redo(); return true;
            default: break;
        }
    }

    // FL's Shift keys: Shift+Q lands starts on the grid, lengths left alone (Ctrl+Q, the editor's,
    // lands ends too); Shift+I inverts the selection; Shift+D discards lengths.
    if (mods.isShiftDown() && ! mods.isAltDown() && ! mods.isCommandDown())
    {
        switch (letter)
        {
            case 'q': quantizeSelection(); return true;
            case 'i': invertSelection(); return true;
            case 'd': discardLengths(); return true;
            default: break;
        }
    }

    // Arrows as in FL: Shift+Up/Down a semitone, Ctrl+Up/Down an octave (plain arrows go a semitone too).
    if (code == juce::KeyPress::upKey)      { transposeSelection (mods.isCommandDown() ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey)    { transposeSelection (mods.isCommandDown() ? -12 : -1); return true; }
    if (code == juce::KeyPress::leftKey)    { nudgeSelection (-view.nudgeStep()); return true; }
    if (code == juce::KeyPress::rightKey)   { nudgeSelection (view.nudgeStep()); return true; }

    if (! mods.isAnyModifierKeyDown())
    {
        switch (letter)
        {
            // FL Studio's tool keys, and G for the glide tool.
            case 'p': view.tool = ViewState::Tool::edit; view.changed(); return true;
            case 'b': view.tool = ViewState::Tool::paint; view.changed(); return true;
            case 'd': view.tool = ViewState::Tool::erase; view.changed(); return true;
            case 'c': view.tool = ViewState::Tool::slice; view.changed(); return true;
            case 'e': view.tool = ViewState::Tool::select; view.changed(); return true;
            case 'g': view.tool = ViewState::Tool::glide; view.changed(); return true;
            default: break;
        }
    }

    return false;
}

void NoteGrid::selectAll()
{
    document.selection.clear();

    for (const auto& n : document.getEditClip().notes)
        document.selection.insert (n.id);

    document.selectionChanged();
}

void NoteGrid::deselectAll()
{
    if (document.selection.empty())
        return;

    document.selection.clear();
    document.selectionChanged();
}

void NoteGrid::invertSelection()
{
    std::set<uint32_t> inverted;

    for (const auto& n : document.getEditClip().notes)
        if (document.selection.count (n.id) == 0)
            inverted.insert (n.id);

    document.selection = inverted;
    document.selectionChanged();
}

void NoteGrid::discardLengths()
{
    // FL's Shift+D: the notes shrink to the snap length (every note when nothing is selected).
    const auto ids = document.selection;
    const auto length = view.nudgeStep();

    document.perform ("Discard lengths", [&] (MpeClip& clip)
    {
        for (auto& n : clip.notes)
        {
            if (ids.empty() || ids.count (n.id) > 0)
            {
                n.length = length;

                for (auto* lane : { &n.lanes.pitch, &n.lanes.pressure, &n.lanes.timbre, &n.lanes.vibrato })
                    lane->trimTo (0.0, length);

                for (auto& c : n.lanes.controllers)
                    c.curve.trimTo (0.0, length);
            }
        }
    });
}

//==============================================================================
void NoteGrid::beginSlice (const juce::MouseEvent& e)
{
    sliceBeat = juce::jmax (0.0, view.snapRound (view.xToBeat (e.position.x), e.mods.isAltDown()));
    sliceLowKey = sliceHighKey = juce::jlimit (0, 127, view.yToKey (e.position.y));
    const auto* n = noteAt (e.position);
    sliceClickNote = n != nullptr ? n->id : 0;
    dragMode = DragMode::slice;
    repaint();
}

void NoteGrid::dragSlice (const juce::MouseEvent& e)
{
    const auto key = juce::jlimit (0, 127, view.yToKey (e.position.y));
    const auto downKey = juce::jlimit (0, 127, view.yToKey (downPos.y));
    sliceLowKey = juce::jmin (key, downKey);
    sliceHighKey = juce::jmax (key, downKey);
    repaint();
}

void NoteGrid::finishSlice (const juce::MouseEvent& e)
{
    // A drag cuts every note its line crossed; a click cuts the note under it. A cut lands strictly
    // inside a note, so a note the (snapped) beat only touches is left whole.
    const bool dragged = sliceLowKey != sliceHighKey || std::abs (e.position.y - downPos.y) >= view.rowHeight;
    const auto& clip = document.getEditClip();
    std::set<uint32_t> ids;

    auto crosses = [this] (const MpeNote& n) { return n.start < sliceBeat && sliceBeat < n.end(); };

    if (dragged)
    {
        for (const auto& n : clip.notes)
            if (n.key >= sliceLowKey && n.key <= sliceHighKey && crosses (n))
                ids.insert (n.id);
    }
    else if (auto* n = clip.findNote (sliceClickNote); n != nullptr && crosses (*n))
    {
        ids.insert (n->id);
    }

    if (ids.empty())
        return;

    const auto beat = sliceBeat;
    document.perform ("Slice notes", [&ids, beat] (MpeClip& c) { notetools::sliceAt (c, ids, beat); });
}

void NoteGrid::beginSelect (const juce::MouseEvent& e)
{
    // A click picks the note under it (Ctrl or Shift adds it to, or takes it from, the selection);
    // on empty space it clears the selection; a drag from either grows a box.
    auto& selection = document.selection;
    const bool adding = e.mods.isCommandDown() || e.mods.isShiftDown();

    if (auto* n = noteAt (e.position))
    {
        if (adding && selection.count (n->id) > 0)
            selection.erase (n->id);
        else if (adding)
            selection.insert (n->id);
        else
            selection = { n->id };
    }
    else if (! adding)
    {
        selection.clear();
    }

    document.selectionChanged();
    dragMode = DragMode::marquee;
    marqueeBase = selection;
    marquee = {};
}

void NoteGrid::deleteSelection()
{
    if (document.selection.empty())
        return;

    const auto ids = document.selection;
    document.perform ("Delete notes", [&ids] (MpeClip& clip)
    {
        for (auto id : ids)
            clip.removeNote (id);
    });
    document.selection.clear();
    document.selectionChanged();
}

void NoteGrid::copySelection()
{
    clipboard.clear();

    for (const auto& n : document.getEditClip().notes)
        if (document.selection.count (n.id) > 0)
            clipboard.push_back (n);
}

void NoteGrid::cutSelection()
{
    copySelection();
    deleteSelection();
}

void NoteGrid::paste()
{
    if (clipboard.empty())
        return;

    double earliest = clipboard.front().start;

    for (const auto& n : clipboard)
        earliest = std::min (earliest, n.start);

    auto target = earliest;

    if (isMouseOver())
        target = juce::jmax (0.0, view.snapFloor (view.xToBeat ((double) getMouseXYRelative().x), false));

    std::set<uint32_t> pasted;
    document.perform ("Paste", [&] (MpeClip& clip)
    {
        for (auto n : clipboard)
        {
            n.start = n.start - earliest + target;
            pasted.insert (clip.addNote (std::move (n)));
        }
    });

    document.selection = pasted;
    document.selectionChanged();
}

void NoteGrid::duplicateSelection()
{
    if (document.selection.empty())
        return;

    const auto& clip = document.getEditClip();
    double first = 1.0e12, last = 0.0;

    for (const auto& n : clip.notes)
    {
        if (document.selection.count (n.id) > 0)
        {
            first = std::min (first, n.start);
            last = std::max (last, n.end());
        }
    }

    const auto step = view.nudgeStep();
    const auto shift = std::ceil ((last - first) / step - 1.0e-9) * step;
    const auto ids = document.selection;
    std::set<uint32_t> copies;

    document.perform ("Duplicate notes", [&] (MpeClip& c)
    {
        std::vector<MpeNote> sources;

        for (auto id : ids)
            if (auto* n = c.findNote (id))
                sources.push_back (*n);

        for (auto& n : sources)
        {
            n.start += shift;
            copies.insert (c.addNote (std::move (n)));
        }
    });

    document.selection = copies;
    document.selectionChanged();
}

void NoteGrid::quantizeSelection()
{
    const auto ids = document.selection;
    document.perform ("Quantize", [&] (MpeClip& clip)
    {
        for (auto& n : clip.notes)
            if (ids.empty() || ids.count (n.id) > 0)
                n.start = juce::jmax (0.0, view.snapRound (n.start, false));
    });
}

void NoteGrid::transposeSelection (int semitones)
{
    const auto ids = document.selection;

    if (ids.empty())
        return;

    document.perform ("Transpose", [&] (MpeClip& clip)
    {
        for (auto& n : clip.notes)
            if (ids.count (n.id) > 0)
                n.key = juce::jlimit (0, 127, n.key + semitones);
    });
}

void NoteGrid::nudgeSelection (double beats)
{
    const auto ids = document.selection;

    if (ids.empty())
        return;

    document.perform ("Nudge", [&] (MpeClip& clip)
    {
        for (auto& n : clip.notes)
            if (ids.count (n.id) > 0)
                n.start = juce::jmax (0.0, n.start + beats);
    });
}

//==============================================================================
void NoteGrid::startAudition (int key, float velocity)
{
    stopAudition();
    processor.auditionNote (key, velocity, true);
    auditionKey = key;
}

void NoteGrid::stopAudition()
{
    if (auditionKey >= 0)
        processor.auditionNote (auditionKey, 0.0f, false);

    auditionKey = -1;
}

} // namespace mperoll
