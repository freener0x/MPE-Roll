#pragma once

#include "ClapEventList.h"
#include "core/MpeDecoder.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace mperoll
{

/** Turns the MPE MIDI stream MPE Roll sends its synth into CLAP events.

    With note events, each MPE note becomes a CLAP note with its own ID, and its per-note
    expression becomes CLAP note expressions: pitch bend becomes tuning, pressure becomes
    pressure, the timbre controller becomes brightness, and per-note CC7, CC10, CC1 and CC11
    become volume, pan, vibrato and expression.

    The zone's own bend, pressure and timbre are combined into every note of the zone (as an MPE
    synth would combine them), and so are its CC7/10/1/11 for a synth that doesn't take MIDI.
    Other zone-wide controllers (sustain, the mod wheel...) go on as MIDI, on the master channel
    and every member channel, so they reach notes whichever channel the synth applies them to.

    Without note events the MIDI is passed through untouched, for synths that only speak MIDI or
    MPE, or when the user prefers it.

    Audio thread only, and real-time safe.
*/
class ClapNoteTranslator : private MpeDecoder::Listener
{
public:
    ClapNoteTranslator();

    /** The layout MPE Roll is sending. When it changes, held notes are ended. */
    void setOutputSettings (const OutputSettings&);

    /** What the synth's note port accepts. */
    void setDialects (bool synthTakesNoteEvents, bool synthTakesMidi);

    /** True when notes go out as CLAP note events and expressions. */
    bool isUsingNoteEvents() const noexcept     { return useNoteEvents; }

    /** Appends the CLAP version of a block of MIDI. */
    void translate (const juce::MidiBuffer& midi, ClapEventList& out, int numSamples);

    /** Ends every note this translator started (e.g. before the synth is deactivated). */
    void endAllNotes (ClapEventList& out, uint32_t time);

    /** Forgets everything without sending anything. */
    void reset();

    /** The synth was (re)activated, so its voices are gone: forget the notes, but keep what the
        zones are set to (bend, pressure...), which still applies to the next notes.
    */
    void forgetNotes();

    int getNumHeldNotes() const noexcept;

    static InputSettings decoderSettingsFor (const OutputSettings&);

private:
    static constexpr int numExpressions = 7;    // CLAP_NOTE_EXPRESSION_VOLUME .. PRESSURE

    struct Note
    {
        uint32_t decoderId = 0;
        int32_t clapId = -1;
        int key = 0, channel = 1;
        bool inUse = false, started = false;
        bool stopPending = false;                           // its NOTE_OFF didn't fit: sent next block
        float pitch = 0.0f;                                 // the note's own bend, in semitones
        std::array<double, numExpressions> values {};       // the note's own values
        std::array<bool, numExpressions> known {};
    };

    using ExpressionValues = std::array<double, numExpressions>;
    using ExpressionFlags = std::array<bool, numExpressions>;

    MpeDecoder decoder;
    OutputSettings output;
    bool settingsChanged = true;
    bool takesNoteEvents = true, takesMidi = true, useNoteEvents = true;
    bool passedThroughMidi = false;                         // since the last reconfiguration

    std::array<Note, MpeDecoder::maxNotes> notes;

    // Zone-wide state, indexed by the channel it arrived on (a master channel, or with a
    // single channel, that channel).
    std::array<float, 17> zonePitch {};
    std::array<ExpressionValues, 17> zoneValues {};
    std::array<ExpressionFlags, 17> zoneKnown {};

    // While translating one message.
    ClapEventList* target = nullptr;
    const juce::MidiMessage* current = nullptr;
    uint32_t time = 0;
    bool forwardAsMidi = false, forwardToZone = false;

    void reconfigure (ClapEventList& out);
    void handleEvent (const MpeDecoder::Event&) override;
    void handleZoneEvent (const MpeDecoder::Event&);

    Note* findNote (uint32_t decoderId) noexcept;
    int scopeChannelOf (const Note&) const noexcept;
    int expressionForController (int controller) const noexcept;
    bool zoneControllerGoesAsMidi (int expression) const noexcept;
    static double expressionValue (int expression, float normalised) noexcept;
    static double combine (int expression, double noteValue, double zoneValue) noexcept;
    double tuningOf (const Note&) const noexcept;

    bool sendNote (uint16_t type, const Note&, double velocity);
    void sendExpression (const Note&, int expression, double value);
    void sendCombined (const Note&, int expression);
    void setExpression (Note&, int expression, double value);
    void sendMidi (const juce::MidiMessage&);
    void sendToZone (const juce::MidiMessage&);
    void retryPendingStops();
    bool isProtocolMessage (const juce::MidiMessage&) const noexcept;
};

} // namespace mperoll
