#pragma once

#include "ClapEventList.h"
#include "core/MpeDecoder.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace mperoll
{

/** Turns the MPE MIDI stream MPE Roll sends its synth into CLAP events.

    With note events, each MPE note becomes a CLAP note with its own ID, and its per-note
    expression becomes CLAP note expressions: pitch bend (plus the zone's master bend) becomes
    tuning, pressure becomes pressure, the timbre controller becomes brightness, and per-note
    CC7, CC10, CC1 and CC11 become volume, pan, vibrato and expression. Zone-wide controllers
    (sustain, the mod wheel on the master channel...) have no CLAP equivalent, so they're passed
    on as MIDI when the synth accepts it.

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
        float pitch = 0.0f;                                 // the note's own bend, in semitones
        std::array<double, numExpressions> values {};       // held until the note starts
        std::array<bool, numExpressions> known {};
    };

    using ExpressionValues = std::array<double, numExpressions>;
    using ExpressionFlags = std::array<bool, numExpressions>;

    MpeDecoder decoder;
    OutputSettings output;
    bool settingsChanged = true;
    bool takesNoteEvents = true, takesMidi = true, useNoteEvents = true;
    bool midiNotesMaySound = false;

    std::array<Note, MpeDecoder::maxNotes> notes;

    // Zone-wide state, indexed by the channel it arrived on (a master channel, or with a
    // single channel, that channel).
    std::array<float, 17> zonePitch {};
    std::array<ExpressionValues, 17> zoneValues {};
    std::array<ExpressionFlags, 17> zoneKnown {};

    // While translating one message.
    ClapEventList* target = nullptr;
    uint32_t time = 0;
    bool forwardAsMidi = false;

    void reconfigure (ClapEventList& out);
    void handleEvent (const MpeDecoder::Event&) override;
    void handleZoneEvent (const MpeDecoder::Event&);

    Note* findNote (uint32_t decoderId) noexcept;
    int scopeChannelOf (const Note&) const noexcept;
    int expressionForController (int controller) const noexcept;
    static double expressionValue (int expression, float normalised) noexcept;

    void sendNote (uint16_t type, const Note&, double velocity);
    void sendExpression (const Note&, int expression, double value);
    void setExpression (Note&, int expression, double value);
    void sendMidi (const juce::MidiMessage&);
    bool isProtocolMessage (const juce::MidiMessage&) const noexcept;
};

} // namespace mperoll
