#pragma once

#include "MpeClip.h"

#include <cstdint>
#include <set>
#include <vector>

namespace mperoll::notetools
{

/** The note operations behind the piano roll's Tools menu, modelled on FL Studio's: each works on
    the given notes (by id), or on every note of the clip when the set is empty, as FL does with no
    selection. Each returns the ids of the notes it produced or touched, so the editor can select
    them. Expression lanes travel with their notes: a split keeps each piece's part of every curve
    (shape preserved at the cut), a shortened note keeps the part it still plays, and a lengthened
    note holds its last value, as curves do anyway.

    Times are in beats. Pitches are MIDI keys. Velocities and lane values are 0..1.
*/

//==============================================================================
/** Which notes an operation works on: the ids given, or every note when none are. */
std::vector<uint32_t> targets (const MpeClip&, const std::set<uint32_t>& ids);

/** Splits a note at a time relative to its start, in place: the note keeps [0, at) and the new
    note (returned, already added to the clip) gets the rest, with each lane cut between them. A
    cut at or beyond either end does nothing and returns 0. */
uint32_t splitNote (MpeClip&, uint32_t id, double at);

/** Cuts every note crossing the given clip time (strictly inside the note), as the Slice tool does
    along a line. Returns the ids of the new right-hand pieces. */
std::set<uint32_t> sliceAt (MpeClip&, const std::set<uint32_t>& ids, double beat);

//==============================================================================
struct ChopOptions
{
    double length = 0.25;       // beats per piece
    float gate = 1.0f;          // 0..1: how much of each piece sounds (1 = pieces touch)
};

/** FL's Chop: each note becomes a run of pieces of the given length (the last piece takes the remainder). */
std::set<uint32_t> chop (MpeClip&, const std::set<uint32_t>& ids, const ChopOptions&);

//==============================================================================
struct ArpeggiateOptions
{
    enum class Pattern { up, down, upDown, downUp, random };

    Pattern pattern = Pattern::up;
    double step = 0.25;         // beats per arpeggio note
    float gate = 1.0f;          // 0..1 of the step
    int range = 1;              // octaves: 1 plays the chord's own notes, 2 adds them an octave up...
    uint32_t seed = 1;          // for Pattern::random
};

/** FL's Arpeggiate: each chord (notes overlapping in time) is replaced by a run of single notes
    cycling through its pitches, step by step, for as long as the chord lasted. A lone note is a
    chord of one and is repeated. Pitches above 127 (from the range) are left out. */
std::set<uint32_t> arpeggiate (MpeClip&, const std::set<uint32_t>& ids, const ArpeggiateOptions&);

//==============================================================================
struct StrumOptions
{
    double time = 0.0625;       // beats between one chord note's start and the next
    bool upwards = true;        // true: the lowest note first, as a guitar downstroke sounds
    float velocityFall = 0.0f;  // 0..1: how much softer the last note is than the first
    bool keepEnds = true;       // ends stay put (notes get shorter), else notes keep their length
};

/** FL's Strum: notes starting together are spread in time, one after another in pitch order. */
std::set<uint32_t> strum (MpeClip&, const std::set<uint32_t>& ids, const StrumOptions&);

//==============================================================================
struct FlamOptions
{
    double time = 0.0625;       // beats before the note each grace note lands
    float velocity = 0.6f;      // the grace note's velocity as a share of the note's
    int count = 1;              // grace notes per note, 1..4
};

/** FL's Flam: a short grace note (or several) is placed just before each note, at the same pitch.
    Returns the grace notes' ids. */
std::set<uint32_t> flam (MpeClip&, const std::set<uint32_t>& ids, const FlamOptions&);

//==============================================================================
struct GlueOptions
{
    double gap = 0.0;           // beats: notes of one pitch this close (or overlapping) become one
};

/** FL's Glue: notes of the same pitch that touch or overlap (or sit within the gap) are joined into
    one note spanning them, the first note's lanes continued by the next's. */
std::set<uint32_t> glue (MpeClip&, const std::set<uint32_t>& ids, const GlueOptions&);

//==============================================================================
struct LegatoOptions
{
    double overlap = 0.0;       // beats a note reaches past the next note's start (negative leaves a gap)
};

/** FL's Quick legato: each note is lengthened (or shortened) to end where the next note of the
    set starts, whatever its pitch; the last note keeps its length. Notes starting together end
    together. */
std::set<uint32_t> legato (MpeClip&, const std::set<uint32_t>& ids, const LegatoOptions&);

//==============================================================================
struct ArticulateOptions
{
    float lengthFactor = 0.5f;  // each length is multiplied by this (0.5 = staccato, 1 = unchanged)
    double minimumLength = 0.0625;
};

/** FL's Articulate: note lengths are scaled, never below the minimum. */
std::set<uint32_t> articulate (MpeClip&, const std::set<uint32_t>& ids, const ArticulateOptions&);

//==============================================================================
struct LimitOptions
{
    int lowKey = 48;            // C3
    int highKey = 72;           // C5
};

/** FL's Limit: notes outside the key range are moved by whole octaves into it (a range narrower
    than an octave is widened to one around its centre). */
std::set<uint32_t> limit (MpeClip&, const std::set<uint32_t>& ids, const LimitOptions&);

//==============================================================================
struct FlipOptions
{
    bool vertical = true;       // mirror pitches around the notes' middle pitch
    bool horizontal = false;    // reverse time within the notes' span (lanes reversed with their notes)
};

/** FL's Flip. */
std::set<uint32_t> flip (MpeClip&, const std::set<uint32_t>& ids, const FlipOptions&);

//==============================================================================
struct RandomizeOptions
{
    uint32_t seed = 1;
    float strike = 0.0f;        // 0..1: velocities move by up to this much either way
    float lift = 0.0f;
    float pressure = 0.0f;      // 0..1: a starting pressure is set within this much of the current one
    float timbre = 0.0f;
    int pitch = 0;              // semitones either way
    float pitchChance = 1.0f;   // 0..1: the share of notes whose pitch moves
    float length = 0.0f;        // 0..1: lengths scale by up to this share either way (never below minimumLength)
    double start = 0.0;         // beats: starts move by up to this much either way (never before 0)
    double minimumLength = 0.0625;
};

/** FL's Randomize (its levels half): velocities, pitches, lengths, starts and the expression a
    note starts with are shaken by the given amounts. The same seed gives the same result. */
std::set<uint32_t> randomize (MpeClip&, const std::set<uint32_t>& ids, const RandomizeOptions&);

//==============================================================================
struct ScaleLevelsOptions
{
    float strikeMultiplier = 1.0f, strikeOffset = 0.0f;
    float liftMultiplier = 1.0f, liftOffset = 0.0f;
    float pressureMultiplier = 1.0f;    // scales each pressure lane (a note without one is left alone)
    float timbreMultiplier = 1.0f;      // scales each timbre lane around the middle (0.5)
};

/** FL's Scale levels: velocities (and lanes) are scaled and offset, then clipped to 0..1. */
std::set<uint32_t> scaleLevels (MpeClip&, const std::set<uint32_t>& ids, const ScaleLevelsOptions&);

//==============================================================================
struct QuantizeOptions
{
    double grid = 0.25;         // beats
    float strength = 1.0f;      // 0..1: how far each note moves towards the grid
    bool starts = true;
    bool ends = false;          // ends land on the grid too (lengths change; a note never shrinks below the grid)
};

/** FL's Quantize. */
std::set<uint32_t> quantize (MpeClip&, const std::set<uint32_t>& ids, const QuantizeOptions&);

} // namespace mperoll::notetools
