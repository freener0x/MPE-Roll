#include "NoteTools.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <utility>

namespace mperoll::notetools
{

namespace
{
    constexpr double timeEpsilon = 1.0e-9;

    /** The shortest note any tool leaves behind: a note that cannot shrink to nothing stays
        playable, and stays visible in the roll. */
    constexpr double minimumNoteLength = 1.0e-3;

    float clamp01 (float value) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, value);
    }

    bool isTarget (const std::set<uint32_t>& ids, uint32_t id)
    {
        return ids.empty() || ids.count (id) > 0;
    }

    std::set<uint32_t> asSet (const std::vector<uint32_t>& ids)
    {
        return { ids.begin(), ids.end() };
    }

    template <typename Fn>
    void forEachLane (LaneSet& lanes, Fn&& fn)
    {
        fn (lanes.pitch);
        fn (lanes.pressure);
        fn (lanes.timbre);
        fn (lanes.vibrato);

        for (auto& c : lanes.controllers)
            fn (c.curve);
    }

    void trimLanes (LaneSet& lanes, double start, double end)
    {
        forEachLane (lanes, [start, end] (ExpressionCurve& curve) { curve.trimTo (start, end); });
    }

    /** Gives a note a new length. A shortened note keeps only the part of each lane it still
        plays; a lengthened one needs nothing done, since a curve holds its last value. */
    void setLength (MpeNote& note, double newLength)
    {
        if (newLength < note.length - timeEpsilon)
            trimLanes (note.lanes, 0.0, newLength);

        note.length = newLength;
    }

    /** Plays the curve backwards within a note of the given length: a point at t lands at length - t. */
    void reverseInTime (ExpressionCurve& curve, double length)
    {
        if (curve.isEmpty())
            return;

        const auto points = curve.getPoints();
        curve.clear();

        // Walking the old points from last to first adds the new ones in increasing time order.
        for (auto it = points.rbegin(); it != points.rend(); ++it)
            curve.addPoint (length - it->beat, it->value);
    }

    /** Adds every point of one curve into another, moved later by the given delta. */
    void continueCurve (ExpressionCurve& into, const ExpressionCurve& from, double delta)
    {
        for (const auto& p : from.getPoints())
            into.addPoint (p.beat + delta, p.value);
    }

    /** Continues the first note's lanes with the next note's, whose times are relative to its own
        start, delta beats after the first note's. Controller lanes the first note lacks are created. */
    void continueLanes (LaneSet& into, const LaneSet& from, double delta)
    {
        continueCurve (into.pitch, from.pitch, delta);
        continueCurve (into.pressure, from.pressure, delta);
        continueCurve (into.timbre, from.timbre, delta);
        continueCurve (into.vibrato, from.vibrato, delta);

        for (const auto& c : from.controllers)
            if (! c.curve.isEmpty())
                continueCurve (into.get (LaneId::cc (c.controller)), c.curve, delta);
    }
}

//==============================================================================
std::vector<uint32_t> targets (const MpeClip& clip, const std::set<uint32_t>& ids)
{
    std::vector<uint32_t> result;

    for (const auto& n : clip.notes)
        if (isTarget (ids, n.id))
            result.push_back (n.id);

    return result;
}

//==============================================================================
uint32_t splitNote (MpeClip& clip, uint32_t id, double at)
{
    auto* note = clip.findNote (id);

    if (note == nullptr || at <= timeEpsilon || at >= note->length - timeEpsilon)
        return 0;

    // The right piece is a copy: it keeps the velocities, as FL does, and takes the part of each
    // lane after the cut, re-timed to its own start.
    MpeNote right = *note;
    right.start += at;
    right.length -= at;
    trimLanes (right.lanes, at, note->length);
    right.lanes.shiftTime (-at);

    trimLanes (note->lanes, 0.0, at);
    note->length = at;

    // Adding to the clip may move the notes in memory, so the left piece is finished first.
    return clip.addNote (std::move (right));
}

std::set<uint32_t> sliceAt (MpeClip& clip, const std::set<uint32_t>& ids, double beat)
{
    std::set<uint32_t> result;

    for (auto id : targets (clip, ids))
    {
        const auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        const auto at = beat - note->start;

        if (at > timeEpsilon && at < note->length - timeEpsilon)
            if (const auto newId = splitNote (clip, id, at))
                result.insert (newId);
    }

    return result;
}

//==============================================================================
std::set<uint32_t> chop (MpeClip& clip, const std::set<uint32_t>& ids, const ChopOptions& options)
{
    std::set<uint32_t> result;
    const auto pieceLength = std::max (minimumNoteLength, options.length);
    const auto gate = (double) clamp01 (options.gate);

    for (auto id : targets (clip, ids))
    {
        std::vector<uint32_t> pieces { id };

        // Cut piece after piece off the front; what is left after the last full piece stays as the
        // remainder, unless it is too small to be a note at all.
        for (;;)
        {
            const auto* current = clip.findNote (pieces.back());

            if (current == nullptr || current->length - pieceLength < 1.0e-6)
                break;

            const auto newId = splitNote (clip, pieces.back(), pieceLength);

            if (newId == 0)
                break;

            pieces.push_back (newId);
        }

        for (auto pieceId : pieces)
        {
            if (auto* piece = clip.findNote (pieceId))
                setLength (*piece, std::max (minimumNoteLength, piece->length * gate));

            result.insert (pieceId);
        }
    }

    return result;
}

//==============================================================================
std::set<uint32_t> arpeggiate (MpeClip& clip, const std::set<uint32_t>& ids, const ArpeggiateOptions& options)
{
    using Pattern = ArpeggiateOptions::Pattern;

    std::set<uint32_t> result;
    const auto targetIds = targets (clip, ids);

    std::vector<MpeNote> notes;

    for (auto id : targetIds)
        if (const auto* n = clip.findNote (id))
            notes.push_back (*n);

    // Notes are in start order already; a note overlapping the chord so far joins it.
    std::vector<std::vector<MpeNote>> chords;
    double chordEnd = 0.0;

    for (const auto& n : notes)
    {
        if (chords.empty() || n.start >= chordEnd - timeEpsilon)
        {
            chords.emplace_back();
            chordEnd = n.end();
        }
        else
        {
            chordEnd = std::max (chordEnd, n.end());
        }

        chords.back().push_back (n);
    }

    for (auto id : targetIds)
        clip.removeNote (id);

    const auto step = std::max (minimumNoteLength, options.step);
    const auto gate = (double) clamp01 (options.gate);
    const auto range = std::max (1, options.range);
    juce::Random rng ((juce::int64) options.seed);

    for (const auto& chord : chords)
    {
        double spanStart = chord.front().start;
        double spanEnd = chord.front().end();
        std::vector<int> keys;

        for (const auto& n : chord)
        {
            spanStart = std::min (spanStart, n.start);
            spanEnd = std::max (spanEnd, n.end());
            keys.push_back (n.key);
        }

        std::sort (keys.begin(), keys.end());
        keys.erase (std::unique (keys.begin(), keys.end()), keys.end());

        std::vector<int> pitches;

        for (int octave = 0; octave < range; ++octave)
            for (auto k : keys)
                if (k + 12 * octave <= 127)
                    pitches.push_back (k + 12 * octave);

        if (pitches.empty())
            continue;

        // The up-and-down patterns turn round without sounding the top or bottom twice.
        std::vector<int> sequence = pitches;

        if (options.pattern == Pattern::down || options.pattern == Pattern::downUp)
            std::reverse (sequence.begin(), sequence.end());

        if ((options.pattern == Pattern::upDown || options.pattern == Pattern::downUp) && sequence.size() > 2)
            for (auto i = sequence.size() - 2; i > 0; --i)
                sequence.push_back (sequence[i]);

        // The chord note a step takes its velocities from: the one at the same pitch class and
        // nearest, as an octave up still plays "that" note; failing that, the chord's first note.
        auto sourceFor = [&chord] (int pitch) -> const MpeNote&
        {
            const MpeNote* best = nullptr;

            for (const auto& n : chord)
                if ((n.key - pitch) % 12 == 0 && (best == nullptr || std::abs (n.key - pitch) < std::abs (best->key - pitch)))
                    best = &n;

            return best != nullptr ? *best : chord.front();
        };

        for (int i = 0;; ++i)
        {
            const auto stepStart = spanStart + i * step;

            if (stepStart >= spanEnd - timeEpsilon)
                break;

            const auto pitch = options.pattern == Pattern::random
                                   ? pitches[(size_t) rng.nextInt ((int) pitches.size())]
                                   : sequence[(size_t) i % sequence.size()];

            const auto& source = sourceFor (pitch);

            MpeNote n;
            n.key = pitch;
            n.start = stepStart;
            n.length = std::max (minimumNoteLength, std::min (step * gate, spanEnd - stepStart));
            n.strike = source.strike;
            n.lift = source.lift;
            result.insert (clip.addNote (std::move (n)));
        }
    }

    return result;
}

//==============================================================================
std::set<uint32_t> strum (MpeClip& clip, const std::set<uint32_t>& ids, const StrumOptions& options)
{
    std::set<uint32_t> result;

    // Runs of notes starting together, in start order, are the chords to spread.
    std::vector<std::vector<uint32_t>> groups;
    double groupStart = 0.0;

    for (auto id : targets (clip, ids))
    {
        const auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        if (groups.empty() || std::abs (note->start - groupStart) > 1.0e-6)
        {
            groups.emplace_back();
            groupStart = note->start;
        }

        groups.back().push_back (id);
    }

    for (auto& group : groups)
    {
        if (group.size() < 2)
            continue;

        std::sort (group.begin(), group.end(), [&clip, &options] (uint32_t a, uint32_t b)
        {
            const auto keyA = clip.findNote (a)->key;
            const auto keyB = clip.findNote (b)->key;
            return options.upwards ? keyA < keyB : keyA > keyB;
        });

        const auto count = (int) group.size();

        for (int i = 0; i < count; ++i)
        {
            auto& note = *clip.findNote (group[(size_t) i]);
            const auto originalEnd = note.end();
            note.start += i * options.time;

            // With the ends kept, each later note gets shorter, but never shorter than a note can be.
            if (options.keepEnds)
                setLength (note, std::max (minimumNoteLength, originalEnd - note.start));

            note.strike = clamp01 (note.strike * (1.0f - options.velocityFall * (float) i / (float) (count - 1)));
            result.insert (note.id);
        }
    }

    clip.sortNotes();
    return result;
}

//==============================================================================
std::set<uint32_t> flam (MpeClip& clip, const std::set<uint32_t>& ids, const FlamOptions& options)
{
    std::set<uint32_t> result;

    if (options.time <= timeEpsilon)
        return result;

    const auto count = std::max (0, options.count);

    for (auto id : targets (clip, ids))
    {
        const auto* found = clip.findNote (id);

        if (found == nullptr)
            continue;

        const auto note = *found;   // adding grace notes moves the clip's notes about

        for (int k = 1; k <= count; ++k)
        {
            const auto start = note.start - k * options.time;

            if (start < -timeEpsilon)
                break;

            // A touch shorter than the gap, so one grace note has ended before the next begins.
            MpeNote grace;
            grace.key = note.key;
            grace.start = std::max (0.0, start);
            grace.length = std::max (minimumNoteLength, options.time * 0.9);
            grace.strike = clamp01 (note.strike * options.velocity);
            grace.lift = note.lift;
            result.insert (clip.addNote (std::move (grace)));
        }
    }

    return result;
}

//==============================================================================
std::set<uint32_t> glue (MpeClip& clip, const std::set<uint32_t>& ids, const GlueOptions& options)
{
    const auto targetIds = targets (clip, ids);
    auto result = asSet (targetIds);

    // Only notes of one pitch join, so each key's notes are walked on their own, in start order.
    std::map<int, std::vector<uint32_t>> byKey;

    for (auto id : targetIds)
        if (const auto* note = clip.findNote (id))
            byKey[note->key].push_back (id);

    for (auto& entry : byKey)
    {
        auto& run = entry.second;
        size_t current = 0;

        for (size_t i = 1; i < run.size(); ++i)
        {
            auto* first = clip.findNote (run[current]);
            const auto* next = clip.findNote (run[i]);

            if (next->start > first->end() + options.gap + timeEpsilon)
            {
                current = i;
                continue;
            }

            // The first note grows to cover the next and takes over its lanes, re-timed from its own start.
            const auto delta = next->start - first->start;
            first->length = std::max (first->end(), next->end()) - first->start;
            continueLanes (first->lanes, next->lanes, delta);

            clip.removeNote (run[i]);
            result.erase (run[i]);
        }
    }

    return result;
}

//==============================================================================
std::set<uint32_t> legato (MpeClip& clip, const std::set<uint32_t>& ids, const LegatoOptions& options)
{
    const auto targetIds = targets (clip, ids);
    std::vector<double> starts;

    for (auto id : targetIds)
        if (const auto* note = clip.findNote (id))
            starts.push_back (note->start);

    std::sort (starts.begin(), starts.end());

    for (auto id : targetIds)
    {
        auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        // The next start is the first one clearly after this note's, so a chord's notes all reach
        // the same place; the notes of the last start have nowhere to reach and keep their length.
        const auto next = std::upper_bound (starts.begin(), starts.end(), note->start + timeEpsilon);

        if (next == starts.end())
            continue;

        setLength (*note, std::max (minimumNoteLength, *next + options.overlap - note->start));
    }

    return asSet (targetIds);
}

//==============================================================================
std::set<uint32_t> articulate (MpeClip& clip, const std::set<uint32_t>& ids, const ArticulateOptions& options)
{
    const auto targetIds = targets (clip, ids);
    const auto minimum = std::max (minimumNoteLength, options.minimumLength);

    for (auto id : targetIds)
        if (auto* note = clip.findNote (id))
            setLength (*note, std::max (minimum, note->length * (double) options.lengthFactor));

    return asSet (targetIds);
}

//==============================================================================
std::set<uint32_t> limit (MpeClip& clip, const std::set<uint32_t>& ids, const LimitOptions& options)
{
    const auto targetIds = targets (clip, ids);
    auto low = juce::jlimit (0, 127, std::min (options.lowKey, options.highKey));
    auto high = juce::jlimit (0, 127, std::max (options.lowKey, options.highKey));

    // A range narrower than an octave could not hold every pitch class, so it is widened to one
    // around its centre, and slid back into the key range if it reaches past an end.
    if (high - low < 12)
    {
        const auto centre = (low + high) / 2;
        low = juce::jlimit (0, 127 - 12, centre - 6);
        high = low + 12;
    }

    for (auto id : targetIds)
    {
        if (auto* note = clip.findNote (id))
        {
            while (note->key < low)
                note->key += 12;

            while (note->key > high)
                note->key -= 12;
        }
    }

    return asSet (targetIds);
}

//==============================================================================
std::set<uint32_t> flip (MpeClip& clip, const std::set<uint32_t>& ids, const FlipOptions& options)
{
    const auto targetIds = targets (clip, ids);

    if (targetIds.empty())
        return {};

    int minKey = 127, maxKey = 0;
    double spanStart = 0.0, spanEnd = 0.0;
    bool first = true;

    for (auto id : targetIds)
    {
        if (const auto* note = clip.findNote (id))
        {
            minKey = std::min (minKey, note->key);
            maxKey = std::max (maxKey, note->key);
            spanStart = first ? note->start : std::min (spanStart, note->start);
            spanEnd = first ? note->end() : std::max (spanEnd, note->end());
            first = false;
        }
    }

    for (auto id : targetIds)
    {
        auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        if (options.vertical)
            note->key = minKey + maxKey - note->key;

        if (options.horizontal)
        {
            // The note's end becomes its start, measured from the other end of the span, and its
            // lanes play backwards so the expression still lines up with the note.
            note->start = spanStart + (spanEnd - note->end());
            const auto length = note->length;
            forEachLane (note->lanes, [length] (ExpressionCurve& curve) { reverseInTime (curve, length); });
        }
    }

    clip.sortNotes();
    return asSet (targetIds);
}

//==============================================================================
std::set<uint32_t> randomize (MpeClip& clip, const std::set<uint32_t>& ids, const RandomizeOptions& options)
{
    const auto targetIds = targets (clip, ids);
    juce::Random rng ((juce::int64) options.seed);
    const auto minimum = std::max (minimumNoteLength, options.minimumLength);

    auto spread = [&rng] (float amount) { return (rng.nextFloat() * 2.0f - 1.0f) * amount; };

    // Moves the value a lane starts with: an empty lane gets its one point, a lane with data is
    // moved whole so the shape it had is kept.
    auto shakeLane = [&spread] (ExpressionCurve& lane, LaneId laneId, float amount)
    {
        const auto delta = spread (amount);

        if (lane.isEmpty())
            lane.addPoint (0.0, clamp01 (defaultLaneValue (laneId) + delta));
        else
            lane.transformValues ([delta] (float v) { return clamp01 (v + delta); });
    };

    // The random numbers are drawn in a fixed order per note, so a seed always gives one result.
    for (auto id : targetIds)
    {
        auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        if (options.strike > 0.0f)
            note->strike = clamp01 (note->strike + spread (options.strike));

        if (options.lift > 0.0f)
            note->lift = clamp01 (note->lift + spread (options.lift));

        if (options.pressure > 0.0f)
            shakeLane (note->lanes.pressure, LaneId::pressure(), options.pressure);

        if (options.timbre > 0.0f)
            shakeLane (note->lanes.timbre, LaneId::timbre(), options.timbre);

        if (options.pitch > 0 && rng.nextFloat() < options.pitchChance)
            note->key = juce::jlimit (0, 127, note->key + rng.nextInt (juce::Range<int> (-options.pitch, options.pitch + 1)));

        if (options.length > 0.0f)
            setLength (*note, std::max (minimum, note->length * (1.0 + (double) spread (options.length))));

        if (options.start > 0.0)
            note->start = std::max (0.0, note->start + (rng.nextFloat() * 2.0f - 1.0f) * options.start);
    }

    clip.sortNotes();
    return asSet (targetIds);
}

//==============================================================================
std::set<uint32_t> scaleLevels (MpeClip& clip, const std::set<uint32_t>& ids, const ScaleLevelsOptions& options)
{
    const auto targetIds = targets (clip, ids);

    for (auto id : targetIds)
    {
        auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        note->strike = clamp01 (note->strike * options.strikeMultiplier + options.strikeOffset);
        note->lift = clamp01 (note->lift * options.liftMultiplier + options.liftOffset);

        note->lanes.pressure.transformValues ([&options] (float v) { return clamp01 (v * options.pressureMultiplier); });

        // Timbre rests in the middle, so it is scaled about 0.5 rather than about 0.
        note->lanes.timbre.transformValues ([&options] (float v) { return clamp01 (0.5f + (v - 0.5f) * options.timbreMultiplier); });
    }

    return asSet (targetIds);
}

//==============================================================================
std::set<uint32_t> quantize (MpeClip& clip, const std::set<uint32_t>& ids, const QuantizeOptions& options)
{
    const auto targetIds = targets (clip, ids);

    if (options.grid <= timeEpsilon)
        return asSet (targetIds);

    const auto grid = options.grid;
    const auto strength = (double) clamp01 (options.strength);
    auto nearestGridLine = [grid] (double beat) { return std::round (beat / grid) * grid; };

    for (auto id : targetIds)
    {
        auto* note = clip.findNote (id);

        if (note == nullptr)
            continue;

        const auto originalEnd = note->end();
        auto newStart = note->start;

        if (options.starts)
            newStart = std::max (0.0, note->start + (nearestGridLine (note->start) - note->start) * strength);

        if (options.ends)
        {
            // The end goes to its own grid line, but a note never shrinks below one grid step.
            const auto newLength = std::max (grid, nearestGridLine (originalEnd) - newStart);
            setLength (*note, note->length + (newLength - note->length) * strength);
        }

        note->start = newStart;
    }

    clip.sortNotes();
    return asSet (targetIds);
}

} // namespace mperoll::notetools
