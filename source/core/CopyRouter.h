#pragma once

#include "MpeDecoder.h"
#include "MpeEncoder.h"

#include <array>
#include <bitset>
#include <memory>
#include <vector>

namespace mperoll
{

/** One copy per note: plays MPE on several copies of a synth that has no MPE of its own.

    The input is the MPE stream MpeEncoder writes in copy mode (copyStreamSettings()). The router
    reads it note by note (MpeDecoder), gives each new note a copy of the synth to itself, and
    plays it there as Plain MIDI (an MpeEncoder per copy, in single-channel mode with
    copyPlaySettings()), so a note's own pitch bend, pressure, timbre and controllers move that
    note alone. Zone-wide expression (zone pitch, pressure and timbre, and controllers such as the
    mod wheel and sustain pedal) goes to every copy. Keys in the every-copy range (keyswitches, a
    loop instrument's modifier keys) go to every copy as plain notes, without expression.

    Choosing a copy for a new note, among the copies that may take one:
      1. a free copy (no note held or sustained) that has gone quiet, the one whose last note
         ended longest ago (never-used copies first);
      2. a free copy still sounding its last note's release, the one whose last note ended
         longest ago, so the tails left sounding are the oldest ones;
      3. a copy whose notes are only held by the sustain pedal, the one released longest ago;
      4. all copies hold notes: with Overflow::stealOldest the oldest note is stopped and its copy
         taken; with Overflow::shareChannel the note joins the copy holding the fewest notes (the
         one whose newest note is oldest, on a tie), and the copy's newest note drives its bend.
    A note is never given a copy outside numCopies or outside the available set; if no copy is
    available at all the note isn't played (its events are ignored until it ends).

    Copies that sleep: the caller may stop processing a copy that has gone quiet (it then
    discards that copy's MIDI). The router is told which copies sleep; when a note or an
    every-copy key goes to a sleeping copy, it first sends that copy what it missed, as
    introduce() does (its bend range, then every zone value again), wakes it and reports it woken.

    Audio thread only. Real-time safe: nothing allocates after construction.
*/
class CopyRouter : private MpeDecoder::Listener
{
public:
    static constexpr int maxCopies = OutputSettings::maxCopies;
    using CopySet = std::bitset<maxCopies>;

    CopyRouter();
    ~CopyRouter() override;

    /** The output settings, mode copies. Changes to what the copies are played with
        (copyPlaySettings) stop every note first and put the copies' controllers back at rest,
        written to outs (maxCopies buffers). Returns true for such a change: every copy then has
        MIDI to be given, a sleeping one included.
    */
    bool setSettings (const OutputSettings& settings, juce::MidiBuffer* outs, int samplePos);
    const OutputSettings& getSettings() const noexcept      { return settings; }

    /** How many copies notes may go to: settings.copies, clamped to minCopies..maxCopies. */
    int getNumCopies() const noexcept                       { return numCopies; }

    /** Routes a block of the MPE stream. Copy i's MIDI is added to outs[i] (outs holds maxCopies
        buffers, not cleared here), at the sample positions of the events it comes from.

        available: copies a new note may go to (loaded, in step with the first copy).
        quiet: copies whose output has gone quiet; preferred for new notes (rule 1).
        asleep: copies the caller isn't processing. A copy the router wakes is removed from it,
                and its zone state is written ahead of the note that woke it.
    */
    void route (const juce::MidiBuffer& in, juce::MidiBuffer* outs, const CopySet& available,
                const CopySet& quiet, CopySet& asleep);

    /** Note-offs for every note held in every copy (and the every-copy keys), then forgets them. */
    void stopAll (juce::MidiBuffer* outs, int samplePos);

    /** The copy is going away, or is being given new state: its notes are forgotten (it won't be
        processed again until it's back), and the stream's later events for them are ignored. Its
        encoder starts afresh. The note-offs they'd need are owed to it: endDroppedNotes(). */
    void dropCopy (int copy);

    /** Note-offs for the notes the copy held when it was dropped, if it's back: real ones, as a
        VST3 synth only hears All Notes Off if it maps that controller. */
    void endDroppedNotes (int copy, juce::MidiBuffer& out, int samplePos);

    /** Sends a copy what it needs before it plays: with settings.sendConfiguration, its pitch-bend
        range (RPN 0) on its channel; then every zone-wide value (forgetting what was sent before).
        For a copy that has just been loaded or given new state. */
    void introduce (int copy, juce::MidiBuffer& out, int samplePos);

    /** Sends every copy its pitch-bend range again, when settings.sendConfiguration is on. */
    void writeConfiguration (juce::MidiBuffer* outs, int samplePos);

    /** True if the copy holds a note, a note the sustain pedal holds, or an every-copy key: it
        mustn't sleep. */
    bool isBusy (int copy) const noexcept;

    /** True if the copy holds a note, or a note the sustain pedal holds: new state would cut it. */
    bool holdsNotes (int copy) const noexcept;

    /** Copies holding a note right now (not counting the sustain pedal or every-copy keys). */
    int getNumCopiesPlaying() const noexcept;

    /** Forgets everything without sending anything: notes, zone state, the sustain pedal. */
    void reset();

private:
    struct Note
    {
        uint32_t id = 0;
        int copy = -1;          // -1: not played (no copy was available, or its copy was dropped)
        int key = 0;
        uint64_t started = 0;   // order, for stealing the oldest
        CopySet everyCopy;      // an every-copy key: the copies its Note On went to
        bool everyCopyKey = false;
        bool inUse = false;
        bool startedPlaying = false;
    };

    struct Copy
    {
        int held = 0;           // notes held
        int sustained = 0;      // notes ended while the sustain pedal was down
        int everyCopyKeys = 0;  // every-copy keys held in this copy
        uint64_t lastEnded = 0; // order: when its last note ended (0 = never played)
        uint64_t newestStart = 0;
    };

    OutputSettings settings, playSettings;
    MpeDecoder decoder;
    std::vector<std::unique_ptr<MpeEncoder>> encoders;     // maxCopies, made in the constructor
    std::array<Copy, maxCopies> copies;
    std::array<Note, MpeDecoder::maxNotes> notes;
    std::array<std::bitset<128>, maxCopies> owedNoteOffs;  // keys a dropped copy held
    std::array<int, maxCopies> owedChannel {};              // and the channel they were on
    juce::MidiBuffer discard;                               // where dropped copies' note-offs go
    int numCopies = 8;
    uint64_t clock = 0;

    // Zone-wide values as last set, to send a copy that slept or came back.
    float zonePitch = 0.0f, zonePressure = 0.0f, zoneTimbre = 0.5f;
    bool zonePitchSet = false, zonePressureSet = false, zoneTimbreSet = false;
    std::array<float, 128> zoneControllers {};
    std::bitset<128> zoneControllersSet;
    bool pedalDown = false;

    // While routing a block.
    juce::MidiBuffer* outputs = nullptr;
    const CopySet* availableCopies = nullptr;
    const CopySet* quietCopies = nullptr;
    CopySet* sleepingCopies = nullptr;
    int position = 0;

    void handleEvent (const MpeDecoder::Event&) override;

    Note* findNote (uint32_t id) noexcept;
    Note* addNote (uint32_t id, int key) noexcept;
    int chooseCopy (int key);
    void wake (int copy);
    void sendZoneState (int copy, juce::MidiBuffer& out, int samplePos);
    void noteEnded (Note&);
    void releasePedal();
    int channel() const noexcept                            { return playSettings.singleChannel; }
};

} // namespace mperoll
