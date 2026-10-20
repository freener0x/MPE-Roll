#pragma once

#include "ExpressionCurve.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace mperoll
{

/** Identifies an expression lane, either on a note (per-note, sent on its member channel)
    or on the zone (sent on the master channel and affecting every note).
*/
struct LaneId
{
    enum class Kind : uint8_t { pitch, pressure, timbre, controller, vibrato };

    Kind kind = Kind::pitch;
    int controller = 0;     // only meaningful when kind == controller

    static LaneId pitch() noexcept                  { return { Kind::pitch, 0 }; }
    static LaneId pressure() noexcept               { return { Kind::pressure, 0 }; }
    static LaneId timbre() noexcept                 { return { Kind::timbre, 0 }; }
    static LaneId vibrato() noexcept                { return { Kind::vibrato, 0 }; }
    static LaneId cc (int number) noexcept          { return { Kind::controller, number }; }

    bool operator== (const LaneId&) const = default;
};

struct ControllerLane
{
    int controller = 1;
    ExpressionCurve curve;

    bool operator== (const ControllerLane&) const = default;
};

/** A set of expression lanes: pitch (in semitones), pressure, timbre and any number of controllers (0..1),
    plus the depth of the vibrato MPE Roll performs itself by wobbling the pitch (0..1, see maxVibratoDepth).
*/
struct LaneSet
{
    ExpressionCurve pitch;
    ExpressionCurve pressure;
    ExpressionCurve timbre;
    std::vector<ControllerLane> controllers;
    ExpressionCurve vibrato;

    const ExpressionCurve* find (LaneId id) const noexcept;
    ExpressionCurve* find (LaneId id) noexcept;

    /** Returns the lane, creating a controller lane if needed. */
    ExpressionCurve& get (LaneId id);

    void removeEmptyControllers();
    bool hasAnyData() const noexcept;
    void shiftTime (double delta);
    void scaleTime (double factor);

    bool operator== (const LaneSet&) const = default;
};

struct MpeNote
{
    uint32_t id = 0;            // unique within its clip and stable across edits
    int key = 60;               // MIDI key number
    double start = 0.0;         // beats from the clip start
    double length = 1.0;        // beats
    float strike = 0.8f;        // note-on velocity, 0..1 (14-bit capable)
    float lift = 0.5f;          // release velocity, 0..1
    LaneSet lanes;              // curve times are relative to the note start

    double end() const noexcept { return start + length; }

    // Exact on purpose: these comparisons detect edits, not closeness.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const MpeNote&) const = default;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
};

/** Semitones either way that a vibrato lane at full depth swings the pitch. */
inline constexpr float maxVibratoDepth = 1.0f;

inline constexpr double defaultVibratoRate = 5.5, minVibratoRate = 0.5, maxVibratoRate = 12.0;

/** The pitch offset, in semitones, of a vibrato at the given depth (0..1) and phase (in cycles). */
float vibratoOffset (float depth, double phase) noexcept;

/** A clip of MPE notes plus zone-wide (master channel) automation. Times are in beats (quarter notes). */
struct MpeClip
{
    juce::String name;
    double length = 16.0;       // loop / display length in beats
    bool looping = true;
    std::vector<MpeNote> notes; // kept sorted by start time
    LaneSet zone;               // curve times are relative to the clip start
    uint32_t nextNoteId = 1;
    double vibratoRate = defaultVibratoRate;    // Hz, for every vibrato lane in the clip

    /** Adds a note, giving it a fresh id, and keeps the notes sorted. Returns the new id. */
    uint32_t addNote (MpeNote note);

    void sortNotes();
    MpeNote* findNote (uint32_t id) noexcept;
    const MpeNote* findNote (uint32_t id) const noexcept;
    bool removeNote (uint32_t id);

    /** End time of the last note or automation point. */
    double contentEnd() const noexcept;

    bool isEmpty() const noexcept { return notes.empty() && ! zone.hasAnyData(); }

    void writeTo (juce::OutputStream& out) const;
    static bool readFrom (juce::InputStream& in, MpeClip& result);

    juce::MemoryBlock toMemoryBlock() const;
    static bool fromMemoryBlock (const juce::MemoryBlock& data, MpeClip& result);

    // Exact on purpose: these comparisons detect edits, not closeness.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const MpeClip&) const = default;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
};

/** The value a lane holds when there is no data. */
float defaultLaneValue (LaneId lane, float defaultTimbre = 0.5f) noexcept;

juce::String laneName (LaneId lane);

} // namespace mperoll
