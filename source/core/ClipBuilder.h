#pragma once

#include "MpeClip.h"
#include "MpeDecoder.h"

#include <unordered_map>

namespace mperoll
{

/** Collects decoder events into notes with expression curves. Used for recording and MIDI file import.

    Not real-time safe (it allocates): feed it on the message thread.
*/
class ClipBuilder : public MpeDecoder::Listener
{
public:
    struct Options
    {
        float pitchTolerance = 0.005f;      // semitones
        float valueTolerance = 0.002f;      // for 0..1 lanes
        double minNoteLength = 1.0 / 256.0; // beats
        float defaultTimbre = 0.5f;
        bool dropDefaultCurves = true;      // remove lanes that never leave their rest value
    };

    ClipBuilder();
    explicit ClipBuilder (Options);

    /** Sets the time of the events that follow.

        clipBeat is where events land in the clip (it may wrap around when recording into a loop),
        elapsedBeat must never go backwards and is used for durations and curve times.
    */
    void setTime (double clipBeat, double elapsedBeat) noexcept;

    void handleEvent (const MpeDecoder::Event&) override;

    /** Extends held notes up to the current time (for drawing a recording in progress). */
    void updateHeldNotes();

    /** Ends held notes at the current time and tidies up every curve. */
    void finish();

    bool hasData() const noexcept;
    bool hasHeldNotes() const noexcept                      { return ! held.empty(); }
    const std::vector<MpeNote>& getNotes() const noexcept   { return notes; }
    const LaneSet& getZone() const noexcept                 { return zone; }

    std::vector<MpeNote> takeNotes();
    LaneSet takeZone();
    void clear();

private:
    struct Held
    {
        size_t index = 0;
        double elapsedStart = 0.0;
    };

    Options options;
    std::vector<MpeNote> notes;
    LaneSet zone;
    std::unordered_map<uint32_t, Held> held;
    double clipBeat = 0.0, elapsedBeat = 0.0;

    void tidy (LaneSet& lanes, bool isZone) const;
};

} // namespace mperoll
