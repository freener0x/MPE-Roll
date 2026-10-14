#include "ClipBuilder.h"

#include <algorithm>

namespace mperoll
{

namespace
{
    // MIDI controllers hold their value until the next message. Dense streams (a controller being
    // moved) read best as straight lines, but a sparse change - a pedal, a one-off CC - must stay a step.
    constexpr double stepGap = 1.0 / 32.0;
    constexpr double stepEdge = 1.0e-4;

    void appendHeld (ExpressionCurve& curve, double beat, float value)
    {
        if (! curve.isEmpty())
        {
            const auto& last = curve.getPoints().back();

            if (beat - last.beat > stepGap && ! juce::exactlyEqual (last.value, value))
                curve.append (beat - stepEdge, last.value);
        }

        curve.append (beat, value);
    }

    // Before its first message a zone controller is at rest.
    void appendZone (ExpressionCurve& curve, double beat, float value, float restValue)
    {
        if (curve.isEmpty() && beat > stepEdge && ! juce::exactlyEqual (value, restValue))
            curve.append (beat - stepEdge, restValue);

        appendHeld (curve, beat, value);
    }
}

ClipBuilder::ClipBuilder() : ClipBuilder (Options {}) {}

ClipBuilder::ClipBuilder (Options o) : options (o) {}

void ClipBuilder::setTime (double newClipBeat, double newElapsedBeat) noexcept
{
    clipBeat = newClipBeat;
    elapsedBeat = std::max (elapsedBeat, newElapsedBeat);
}

void ClipBuilder::handleEvent (const MpeDecoder::Event& e)
{
    using Type = MpeDecoder::Event::Type;

    // Runs fn (note, timeInNote, heldIterator) if the event belongs to a note being recorded.
    auto withNote = [&] (auto&& fn)
    {
        auto it = held.find (e.noteId);

        if (it != held.end())
            fn (notes[it->second.index], elapsedBeat - it->second.elapsedStart, it);
    };

    switch (e.type)
    {
        case Type::notePrepare:
        {
            MpeNote note;
            note.key = e.key;
            note.start = clipBeat;
            note.length = 0.0;
            notes.push_back (std::move (note));
            held[e.noteId] = { notes.size() - 1, elapsedBeat };
            break;
        }

        case Type::noteStart:
            withNote ([&] (MpeNote& n, double, auto) { n.strike = e.value; });
            break;

        case Type::noteStop:
            withNote ([&] (MpeNote& n, double t, auto it)
                      {
                          n.length = std::max (options.minNoteLength, t);
                          n.lift = e.value;
                          held.erase (it);
                      });
            break;

        case Type::notePitch:
            withNote ([&] (MpeNote& n, double t, auto) { appendHeld (n.lanes.pitch, t, e.value); });
            break;

        case Type::notePressure:
            withNote ([&] (MpeNote& n, double t, auto) { appendHeld (n.lanes.pressure, t, e.value); });
            break;

        case Type::noteTimbre:
            withNote ([&] (MpeNote& n, double t, auto) { appendHeld (n.lanes.timbre, t, e.value); });
            break;

        case Type::noteController:
            withNote ([&] (MpeNote& n, double t, auto) { appendHeld (n.lanes.get (LaneId::cc (e.controller)), t, e.value); });
            break;

        case Type::zonePitch:       appendZone (zone.pitch, clipBeat, e.value, 0.0f); break;
        case Type::zonePressure:    appendZone (zone.pressure, clipBeat, e.value, 0.0f); break;
        case Type::zoneTimbre:      appendZone (zone.timbre, clipBeat, e.value, options.defaultTimbre); break;
        case Type::zoneController:
            appendZone (zone.get (LaneId::cc (e.controller)), clipBeat, e.value, defaultControllerValue (e.controller));
            break;
    }
}

void ClipBuilder::updateHeldNotes()
{
    for (auto& [id, h] : held)
        notes[h.index].length = std::max (options.minNoteLength, elapsedBeat - h.elapsedStart);
}

void ClipBuilder::finish()
{
    updateHeldNotes();
    held.clear();

    for (auto& n : notes)
        tidy (n.lanes, false);

    tidy (zone, true);

    std::stable_sort (notes.begin(), notes.end(),
                      [] (const MpeNote& a, const MpeNote& b) { return a.start < b.start; });
}

void ClipBuilder::tidy (LaneSet& lanes, bool isZone) const
{
    lanes.pitch.simplify (options.pitchTolerance);
    lanes.pressure.simplify (options.valueTolerance);
    lanes.timbre.simplify (options.valueTolerance);

    for (auto& c : lanes.controllers)
        c.curve.simplify (options.valueTolerance);

    if (! options.dropDefaultCurves)
        return;

    if (lanes.pitch.isConstant (0.0f, options.pitchTolerance))
        lanes.pitch.clear();

    if (lanes.pressure.isConstant (0.0f, options.valueTolerance))
        lanes.pressure.clear();

    if (lanes.timbre.isConstant (options.defaultTimbre, options.valueTolerance) && ! isZone)
        lanes.timbre.clear();

    for (auto& c : lanes.controllers)
        if (c.curve.isConstant (defaultControllerValue (c.controller), options.valueTolerance) && ! isZone)
            c.curve.clear();

    lanes.removeEmptyControllers();
}

bool ClipBuilder::hasData() const noexcept
{
    return ! notes.empty() || zone.hasAnyData();
}

std::vector<MpeNote> ClipBuilder::takeNotes()
{
    held.clear();
    return std::exchange (notes, {});
}

LaneSet ClipBuilder::takeZone()
{
    return std::exchange (zone, {});
}

void ClipBuilder::clear()
{
    notes.clear();
    zone = {};
    held.clear();
    clipBeat = elapsedBeat = 0.0;
}

} // namespace mperoll
