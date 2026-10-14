#pragma once

#include <juce_core/juce_core.h>

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

    bool operator== (const ZoneLayout&) const = default;
};

//==============================================================================
/** How the piano roll talks to the hosted synth. */
struct OutputSettings
{
    enum class Mode { mpe, singleChannel };
    enum class PressureTarget { channelPressure, polyAftertouch, controller };
    enum class Overflow { shareChannel, stealOldest };

    Mode mode = Mode::mpe;
    ZoneLayout zones;
    int splitKey = 60;                  // with two zones, keys below this go to the lower zone

    float memberBendRange = 48.0f;      // semitones, per-note pitch bend range
    float masterBendRange = 2.0f;       // semitones, zone-wide pitch bend range

    PressureTarget pressureTarget = PressureTarget::channelPressure;
    int pressureController = 11;        // used when pressureTarget == controller

    int timbreController = 74;          // the MPE "third dimension"
    int timbreLsbController = -1;       // optional LSB controller for 14-bit timbre (-1 = off)

    bool highResVelocity = false;       // send CC88 high resolution velocity prefixes
    bool sendConfiguration = true;      // send MCM + pitch bend sensitivity RPNs when playback starts
    Overflow overflow = Overflow::shareChannel;

    float defaultTimbre = 0.5f;         // sent before a note that has no timbre data of its own
    int singleChannel = 1;              // output channel when mode == singleChannel

    bool operator== (const OutputSettings&) const = default;
};

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

    bool operator== (const InputSettings&) const = default;
};

struct PlaybackSettings
{
    enum class Mode { followHost, noteTrigger };

    Mode mode = Mode::followHost;
    int triggerBaseKey = 0;             // in note-trigger mode, key (base + n) launches clip slot n
    bool chaseNotes = true;             // start notes that are already under the playhead
    float controlIntervalMs = 1.0f;     // how often expression curves are sampled during playback
    bool passThroughInput = true;       // play incoming MIDI through the hosted synth

    bool operator== (const PlaybackSettings&) const = default;
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
