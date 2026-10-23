#pragma once

#include <juce_core/juce_core.h>

#include <bitset>

namespace mperoll
{

/** The MPE zone layout as defined by the MPE specification (MIDI CA-034).

    The lower zone uses channel 1 as its master channel and channels 2..(1 + lowerMembers) as
    member channels. The upper zone uses channel 16 as master and 15 down to (16 - upperMembers)
    as members. Channels outside both zones behave like ordinary MIDI channels.
*/
struct ZoneLayout
{
    int lowerMembers = 15;
    int upperMembers = 0;

    /** Clamps the member counts to what the MPE specification allows. The lower zone wins on overlap. */
    ZoneLayout normalised() const noexcept;

    bool hasLowerZone() const noexcept              { return lowerMembers > 0; }
    bool hasUpperZone() const noexcept              { return upperMembers > 0; }
    bool hasAnyZone() const noexcept                { return hasLowerZone() || hasUpperZone(); }

    /** 0 for the lower zone, 1 for the upper zone, -1 if the channel (1-16) is in neither. */
    int zoneOfChannel (int channel) const noexcept;
    bool isMasterChannel (int channel) const noexcept;
    bool isMemberChannel (int channel) const noexcept;

    int masterChannel (int zone) const noexcept     { return zone == 0 ? 1 : 16; }
    int numMembers (int zone) const noexcept        { return zone == 0 ? lowerMembers : upperMembers; }

    /** The i-th member channel (0-based) of a zone. */
    int memberChannel (int zone, int index) const noexcept;

    /** The channels notes are played on, for showing to the user: "2-16", "2-8 and 9-15". */
    juce::String describeMemberChannels() const;

    bool operator== (const ZoneLayout&) const = default;
};

//==============================================================================
/** How the piano roll talks to the hosted synth. */
struct OutputSettings
{
    /** mpe: MPE over MIDI. singleChannel: Plain MIDI on one channel. copies: one copy of the synth
        per note, each copy played with Plain MIDI (the settings for Plain MIDI apply to each copy).
    */
    enum class Mode { mpe, singleChannel, copies };
    enum class PressureTarget { channelPressure, polyAftertouch, controller };
    enum class Overflow { shareChannel, stealOldest };

    static constexpr int minCopies = 2;
    static constexpr int maxCopies = 32;

    Mode mode = Mode::mpe;
    ZoneLayout zones;
    int splitKey = 60;                  // with two zones, keys below this go to the lower zone

    float memberBendRange = 48.0f;      // semitones, per-note pitch bend range
    float masterBendRange = 2.0f;       // semitones, zone-wide pitch bend range

    PressureTarget pressureTarget = PressureTarget::channelPressure;
    int pressureController = 11;        // used when pressureTarget == controller

    int timbreController = 74;          // the MPE "third dimension"
    int timbreLsbController = -1;       // optional LSB controller for 14-bit timbre (-1 = off)
    bool mpePlus = false;               // Haken MPE+: CC87 before timbre, pressure and bend adds 7 more bits

    bool highResVelocity = false;       // send CC88 high resolution velocity prefixes
    bool sendConfiguration = true;      // send MCM + pitch bend sensitivity RPNs when playback starts
    Overflow overflow = Overflow::shareChannel;

    float defaultTimbre = 0.5f;         // sent before a note that has no timbre data of its own
    int singleChannel = 1;              // output channel when mode == singleChannel

    bool clapNoteExpressions = true;    // play CLAP synths with CLAP notes and note expressions, not MIDI

    // One copy per note (mode == copies).
    int copies = 8;                     // copies of the synth to load, minCopies..maxCopies
    std::bitset<128> everyCopyKeys;     // keys that go to every copy (keyswitches, a loop instrument's modifier keys)
    bool copiesSleep = true;            // stop processing a copy that holds no note and has gone quiet
    bool copiesUseCores = true;         // process the copies on several CPU cores at once
    bool copiesFollow = true;           // give the other copies the first copy's sound when it changes

    /** True if this key goes to every copy rather than a copy of its own. */
    bool isEveryCopyKey (int key) const noexcept
    {
        return juce::isPositiveAndBelow (key, 128) && everyCopyKeys[(size_t) key];
    }

    // Exact on purpose: these comparisons detect edits, not closeness.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const OutputSettings&) const = default;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
};

/** In copy mode, the MPE stream MpeEncoder writes for HostedSynth to share out over the copies:
    a lower zone of 15 channels with bend ranges wide enough for any glide, whatever the MPE
    settings say. Other modes are returned as they are.
*/
OutputSettings copyStreamSettings (const OutputSettings&);

/** What each copy is played with in copy mode: the Plain MIDI settings, one note per copy. */
OutputSettings copyPlaySettings (const OutputSettings&);

/** A set of keys written out the way MPE Roll names notes (middle C is C5, as in FL Studio), in
    ranges where it can: "C0-B2", or "C0-B3, black C4-C6" for the black keys between C4 and C6.
    Empty for no keys.
*/
juce::String formatKeySet (const std::bitset<128>&);

/** Reads what formatKeySet() writes, and more loosely what a user types: note names or MIDI note
    numbers, single keys or ranges ("C1-B2", "36 to 47"), each optionally "black" or "white" keys
    only, separated by commas. Whatever can't be read is left out. */
std::bitset<128> parseKeySet (const juce::String&);

/** How incoming MIDI (controllers, files, FL's own piano roll) is interpreted. */
struct InputSettings
{
    enum class Mode { mpe, singleChannel };

    Mode mode = Mode::mpe;
    ZoneLayout zones;
    bool followConfigurationMessages = true;    // let MCM messages from a controller change the zones
    float memberBendRange = 48.0f;
    float masterBendRange = 2.0f;
    float conventionalBendRange = 2.0f;         // channels outside any zone
    int timbreController = 74;
    int timbreLsbController = -1;
    bool mpePlus = false;                       // read Haken MPE+ CC87 high-resolution prefixes
    int presetIndex = 0;                        // the controller preset these came from (see ControllerPresets)

    // Exact on purpose: these comparisons detect edits, not closeness.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const InputSettings&) const = default;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
};

struct PlaybackSettings
{
    enum class Mode { followHost, noteTrigger };

    Mode mode = Mode::followHost;
    int triggerBaseKey = 0;             // in note-trigger mode, key (base + n) launches clip slot n
    bool chaseNotes = true;             // start notes that are already under the playhead
    float controlIntervalMs = 1.0f;     // how often expression curves are sampled during playback
    bool passThroughInput = true;       // play incoming MIDI through the hosted synth

    // Exact on purpose: these comparisons detect edits, not closeness.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const PlaybackSettings&) const = default;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
};

struct EngineSettings
{
    OutputSettings output;
    InputSettings input;
    PlaybackSettings playback;

    bool operator== (const EngineSettings&) const = default;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static EngineSettings fromXml (const juce::XmlElement* xml);
};

//==============================================================================
/** Default value of a controller lane when a note has no data for it (0..1). */
float defaultControllerValue (int controller) noexcept;

/** Controllers that carry protocol data (bank select, RPN/NRPN, data entry, channel mode...)
    and therefore can't be used as an expression lane.
*/
bool isReservedController (int controller) noexcept;

juce::String controllerName (int controller);

} // namespace mperoll
