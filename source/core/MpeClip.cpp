#include "MpeClip.h"
#include "MpeSettings.h"

#include <algorithm>
#include <cmath>

namespace mperoll
{

//==============================================================================
const ExpressionCurve* LaneSet::find (LaneId id) const noexcept
{
    switch (id.kind)
    {
        case LaneId::Kind::pitch:       return &pitch;
        case LaneId::Kind::pressure:    return &pressure;
        case LaneId::Kind::timbre:      return &timbre;
        case LaneId::Kind::controller:  break;
    }

    for (const auto& c : controllers)
        if (c.controller == id.controller)
            return &c.curve;

    return nullptr;
}

ExpressionCurve* LaneSet::find (LaneId id) noexcept
{
    return const_cast<ExpressionCurve*> (std::as_const (*this).find (id));
}

ExpressionCurve& LaneSet::get (LaneId id)
{
    if (auto* existing = find (id))
        return *existing;

    auto insertAt = std::lower_bound (controllers.begin(), controllers.end(), id.controller,
                                      [] (const ControllerLane& c, int cc) { return c.controller < cc; });
    return controllers.insert (insertAt, ControllerLane { id.controller, {} })->curve;
}

void LaneSet::removeEmptyControllers()
{
    controllers.erase (std::remove_if (controllers.begin(), controllers.end(),
                                       [] (const ControllerLane& c) { return c.curve.isEmpty(); }),
                       controllers.end());
}

bool LaneSet::hasAnyData() const noexcept
{
    return ! pitch.isEmpty() || ! pressure.isEmpty() || ! timbre.isEmpty()
        || std::any_of (controllers.begin(), controllers.end(),
                        [] (const ControllerLane& c) { return ! c.curve.isEmpty(); });
}

void LaneSet::shiftTime (double delta)
{
    pitch.shiftTime (delta);
    pressure.shiftTime (delta);
    timbre.shiftTime (delta);

    for (auto& c : controllers)
        c.curve.shiftTime (delta);
}

void LaneSet::scaleTime (double factor)
{
    pitch.scaleTime (factor);
    pressure.scaleTime (factor);
    timbre.scaleTime (factor);

    for (auto& c : controllers)
        c.curve.scaleTime (factor);
}

//==============================================================================
uint32_t MpeClip::addNote (MpeNote note)
{
    note.id = nextNoteId++;
    const auto id = note.id;

    auto insertAt = std::upper_bound (notes.begin(), notes.end(), note.start,
                                      [] (double s, const MpeNote& n) { return s < n.start; });
    notes.insert (insertAt, std::move (note));
    return id;
}

void MpeClip::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(),
                      [] (const MpeNote& a, const MpeNote& b) { return a.start < b.start; });
}

MpeNote* MpeClip::findNote (uint32_t id) noexcept
{
    for (auto& n : notes)
        if (n.id == id)
            return &n;

    return nullptr;
}

const MpeNote* MpeClip::findNote (uint32_t id) const noexcept
{
    return const_cast<MpeClip*> (this)->findNote (id);
}

bool MpeClip::removeNote (uint32_t id)
{
    auto it = std::find_if (notes.begin(), notes.end(), [id] (const MpeNote& n) { return n.id == id; });

    if (it == notes.end())
        return false;

    notes.erase (it);
    return true;
}

double MpeClip::contentEnd() const noexcept
{
    double end = 0.0;

    for (const auto& n : notes)
        end = std::max (end, n.end());

    auto curveEnd = [&end] (const ExpressionCurve& c)
    {
        if (! c.isEmpty())
            end = std::max (end, c.getPoints().back().beat);
    };

    curveEnd (zone.pitch);
    curveEnd (zone.pressure);
    curveEnd (zone.timbre);

    for (const auto& c : zone.controllers)
        curveEnd (c.curve);

    return end;
}

//==============================================================================
namespace
{
    constexpr int clipMagic = 0x4d505243;   // "MPRC"
    constexpr int clipVersion = 1;
    constexpr int maxReasonableCount = 10'000'000;

    void writeCurve (juce::OutputStream& out, const ExpressionCurve& curve)
    {
        out.writeInt ((int) curve.size());

        for (const auto& p : curve.getPoints())
        {
            out.writeDouble (p.beat);
            out.writeFloat (p.value);
        }
    }

    bool readCurve (juce::InputStream& in, ExpressionCurve& curve)
    {
        const auto count = in.readInt();

        if (count < 0 || count > maxReasonableCount)
            return false;

        curve.clear();

        for (int i = 0; i < count; ++i)
        {
            const auto beat = in.readDouble();
            const auto value = in.readFloat();
            if (! std::isfinite (beat) || ! std::isfinite (value))
                return false;

            curve.append (beat, value);
        }

        return true;
    }

    void writeLanes (juce::OutputStream& out, const LaneSet& lanes)
    {
        writeCurve (out, lanes.pitch);
        writeCurve (out, lanes.pressure);
        writeCurve (out, lanes.timbre);
        out.writeInt ((int) lanes.controllers.size());

        for (const auto& c : lanes.controllers)
        {
            out.writeInt (c.controller);
            writeCurve (out, c.curve);
        }
    }

    bool readLanes (juce::InputStream& in, LaneSet& lanes)
    {
        if (! readCurve (in, lanes.pitch) || ! readCurve (in, lanes.pressure) || ! readCurve (in, lanes.timbre))
            return false;

        const auto numControllers = in.readInt();

        if (numControllers < 0 || numControllers > 128)
            return false;

        lanes.controllers.clear();

        for (int i = 0; i < numControllers; ++i)
        {
            const auto cc = in.readInt();

            if (cc < 0 || cc > 127)
                return false;

            if (! readCurve (in, lanes.get (LaneId::cc (cc))))
                return false;
        }

        return true;
    }
}

void MpeClip::writeTo (juce::OutputStream& out) const
{
    out.writeInt (clipMagic);
    out.writeInt (clipVersion);
    out.writeString (name);
    out.writeDouble (length);
    out.writeBool (looping);
    out.writeInt ((int) nextNoteId);
    out.writeInt ((int) notes.size());

    for (const auto& n : notes)
    {
        out.writeInt ((int) n.id);
        out.writeInt (n.key);
        out.writeDouble (n.start);
        out.writeDouble (n.length);
        out.writeFloat (n.strike);
        out.writeFloat (n.lift);
        writeLanes (out, n.lanes);
    }

    writeLanes (out, zone);
}

bool MpeClip::readFrom (juce::InputStream& in, MpeClip& result)
{
    if (in.readInt() != clipMagic)
        return false;

    const auto version = in.readInt();

    if (version < 1 || version > clipVersion)
        return false;

    MpeClip clip;
    clip.name = in.readString();
    clip.length = in.readDouble();
    clip.looping = in.readBool();
    clip.nextNoteId = (uint32_t) in.readInt();

    const auto numNotes = in.readInt();

    if (numNotes < 0 || numNotes > maxReasonableCount)
        return false;

    clip.notes.reserve ((size_t) numNotes);

    for (int i = 0; i < numNotes; ++i)
    {
        MpeNote n;
        n.id = (uint32_t) in.readInt();
        n.key = juce::jlimit (0, 127, in.readInt());
        n.start = in.readDouble();
        n.length = in.readDouble();
        n.strike = in.readFloat();
        n.lift = in.readFloat();

        if (! readLanes (in, n.lanes))
            return false;

        clip.nextNoteId = std::max (clip.nextNoteId, n.id + 1);
        clip.notes.push_back (std::move (n));
    }

    if (! readLanes (in, clip.zone))
        return false;

    if (! std::isfinite (clip.length) || clip.length <= 0.0)
        clip.length = 16.0;

    clip.sortNotes();
    result = std::move (clip);
    return true;
}

juce::MemoryBlock MpeClip::toMemoryBlock() const
{
    juce::MemoryOutputStream out;
    writeTo (out);
    return out.getMemoryBlock();
}

bool MpeClip::fromMemoryBlock (const juce::MemoryBlock& data, MpeClip& result)
{
    juce::MemoryInputStream in (data, false);
    return readFrom (in, result);
}

//==============================================================================
float defaultLaneValue (LaneId lane, float defaultTimbre) noexcept
{
    switch (lane.kind)
    {
        case LaneId::Kind::pitch:       return 0.0f;
        case LaneId::Kind::pressure:    return 0.0f;
        case LaneId::Kind::timbre:      return defaultTimbre;
        case LaneId::Kind::controller:  return defaultControllerValue (lane.controller);
    }

    return 0.0f;
}

juce::String laneName (LaneId lane)
{
    switch (lane.kind)
    {
        case LaneId::Kind::pitch:       return "Pitch";
        case LaneId::Kind::pressure:    return "Pressure";
        case LaneId::Kind::timbre:      return "Timbre";
        case LaneId::Kind::controller:  return controllerName (lane.controller);
    }

    return {};
}

} // namespace mperoll
