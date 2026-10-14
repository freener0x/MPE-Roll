#pragma once

#include "MpeSettings.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <bitset>
#include <cstdint>

namespace mperoll
{

/** Interprets incoming MIDI as MPE and reports note-level events.

    - Messages on a zone's master channel are zone-wide.
    - Channel messages on any other channel apply to every note sounding on that channel (which,
      with an MPE controller, is exactly one note - so they become per-note expression).
    - Poly aftertouch always becomes per-note pressure.
    - MPE Configuration Messages and pitch bend sensitivity RPNs update the zone layout and ranges.
    - CC88 high resolution velocity prefixes are applied to the following Note On/Off.

    A new note is reported as notePrepare, then its initial expression, then noteStart, mirroring
    the order MpeEncoder needs to reproduce it. Real-time safe.
*/
class MpeDecoder
{
public:
    struct Event
    {
        enum class Type : uint8_t
        {
            notePrepare, noteStart, noteStop,
            notePitch, notePressure, noteTimbre, noteController,
            zonePitch, zonePressure, zoneTimbre, zoneController
        };

        Type type = Type::notePrepare;
        uint32_t noteId = 0;
        int key = 0;
        int channel = 0;
        int controller = 0;
        float value = 0.0f;     // semitones for pitch, 0..1 otherwise (strike/lift for start/stop)
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void handleEvent (const Event&) = 0;
    };

    static constexpr int maxNotes = 256;

    MpeDecoder();

    void setSettings (const InputSettings& newSettings);
    const InputSettings& getSettings() const noexcept   { return settings; }

    /** The current layout, which may have been changed by configuration messages. */
    const ZoneLayout& getZones() const noexcept         { return zones; }

    void process (const juce::MidiMessage& message, Listener& listener);

    /** Ends every held note (e.g. when the transport stops). */
    void stopAllNotes (Listener& listener);

    /** Forgets all notes and channel state without reporting anything. */
    void reset();

    int getNumHeldNotes() const noexcept;

private:
    struct ChannelState
    {
        int pitchBend = 8192;
        float bendRange = 2.0f;
        int pressure = -1;
        int timbreMsb = -1;
        int timbreLsb = 0;
        std::array<int8_t, 128> controllers;
        int rpnMsb = 127, rpnLsb = 127;
        bool nrpnSelected = false;
        int dataMsb = -1;
        int velocityPrefix = -1;

        void reset (float range) noexcept;
    };

    struct HeldNote
    {
        uint32_t id = 0;
        int channel = 0;
        int key = 0;
        bool inUse = false;
    };

    InputSettings settings;
    ZoneLayout zones;
    std::array<ChannelState, 17> channels;
    std::array<HeldNote, maxNotes> notes;
    uint32_t nextId = 1;

    bool isZoneWide (int channel) const noexcept;
    float defaultBendRange (int channel) const noexcept;
    float timbreValue (const ChannelState&) const noexcept;
    float channelPitch (int channel) const noexcept;

    void handleNoteOn (int channel, int key, int velocity, Listener&);
    void handleNoteOff (int channel, int key, int velocity, Listener&);
    void handleController (int channel, int controller, int value, Listener&);
    void handleDataEntry (int channel, Listener&);
    void applyConfigurationMessage (int channel, int members);
    void stopNotes (int channel, bool wholeZone, Listener&);

    template <typename Fn>
    void forEachNoteOnChannel (int channel, Fn&& fn);

    void emit (Listener&, Event::Type, uint32_t id, int key, int channel, float value, int controller = 0);
};

} // namespace mperoll
