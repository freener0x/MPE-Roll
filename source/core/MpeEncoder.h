#pragma once

#include "MpeSettings.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <bitset>
#include <cstdint>

namespace mperoll
{

/** Turns note-level events into an MPE (or plain single-channel MIDI) stream.

    Allocates member channels (least recently released first), sends the configuration
    messages, deduplicates controller traffic and handles running out of channels.

    Starting a note is split into prepareNote() and startNote() so that a note's initial pitch,
    pressure, timbre and controllers can be sent before its Note On, as the MPE specification
    requires. Real-time safe: nothing allocates after construction (the MidiBuffer passed in
    should have enough space reserved).
*/
class MpeEncoder
{
public:
    using Handle = uint64_t;
    static constexpr int maxVoices = 256;

    MpeEncoder();

    /** Changes the settings. Call stopAllNotes() first if the zone layout or mode changes. */
    void setSettings (const OutputSettings& newSettings);
    const OutputSettings& getSettings() const noexcept      { return settings; }

    /** Sends the MPE Configuration Message and pitch bend sensitivity for every zone. */
    void writeConfiguration (juce::MidiBuffer& out, int samplePos);

    /** Allocates a channel for a new note. May send note-offs if a voice has to be stolen. */
    void prepareNote (juce::MidiBuffer& out, int samplePos, Handle handle, int key);

    /** Sends the Note On for a prepared note, after any initial expression set since prepareNote(). */
    void startNote (juce::MidiBuffer& out, int samplePos, Handle handle, float strike);

    void stopNote (juce::MidiBuffer& out, int samplePos, Handle handle, float lift);

    void setPitch (juce::MidiBuffer& out, int samplePos, Handle handle, float semitones);
    void setPressure (juce::MidiBuffer& out, int samplePos, Handle handle, float value);
    void setTimbre (juce::MidiBuffer& out, int samplePos, Handle handle, float value);
    void setController (juce::MidiBuffer& out, int samplePos, Handle handle, int controller, float value);

    void setZonePitch (juce::MidiBuffer& out, int samplePos, float semitones);
    void setZonePressure (juce::MidiBuffer& out, int samplePos, float value);
    void setZoneTimbre (juce::MidiBuffer& out, int samplePos, float value);
    void setZoneController (juce::MidiBuffer& out, int samplePos, int controller, float value);

    /** Sends note-offs for every sounding or prepared note. */
    void stopAllNotes (juce::MidiBuffer& out, int samplePos);

    /** Returns pitch bend, pressure and pedals to their rest positions on every channel, and the
        controllers notes' lanes have set on the single channel to their usual values.
    */
    void resetControllers (juce::MidiBuffer& out, int samplePos);

    /** Returns the zone-wide (master channel) pitch bend, pressure and pedals to rest, leaving notes alone. */
    void resetZoneControllers (juce::MidiBuffer& out, int samplePos);

    /** Forgets the zone-wide values it has sent (master channels, or the single channel), so each
        is sent again the next time it's set even if it hasn't changed. For when the receiver may
        have lost them, e.g. the synth was set up afresh. Call with no notes playing.
    */
    void forgetZoneValues() noexcept;

    bool isPlaying (Handle handle) const noexcept;
    int getChannel (Handle handle) const noexcept;
    int getNumActiveVoices() const noexcept                 { return numActive; }

    static int semitonesToPitchBend (float semitones, float range) noexcept;
    static double semitonesToPitchBendFine (float semitones, float range) noexcept;
    static float pitchBendToSemitones (int value, float range) noexcept;
    static int toSevenBit (float value) noexcept;
    static int toFourteenBit (float value) noexcept;

private:
    struct Voice
    {
        Handle handle = 0;
        int channel = 0;
        int key = 0;
        bool inUse = false;
        bool started = false;
        uint64_t order = 0;
        float pitch = 0.0f;
        float pressure = 0.0f;
        float timbre = 0.5f;
        std::bitset<128> ownControllers;    // controllers this note's own lanes have set
        std::array<int8_t, 128> ownValues {};   // their last values, for when the note takes over the single channel
    };

    struct ChannelState
    {
        int numVoices = 0;
        uint64_t lastUsed = 0;
        int pitchBend = -1;     // last sent, (14-bit << 7) | MPE+ low bits
        int pressure = -1;      // last sent, (7-bit << 7) | MPE+ low bits
        int timbre = -1;
        std::array<int8_t, 128> controllers;
        std::array<int8_t, 128> polyPressure;
        std::bitset<128> controllersTouched;
        bool pitchTouched = false, pressureTouched = false, timbreTouched = false;

        void clearValues() noexcept;
    };

    OutputSettings settings;
    std::array<Voice, maxVoices> voices;
    std::array<int, maxVoices> activeList {};   // indices into voices
    int numActive = 0;
    std::array<ChannelState, 17> channels;      // 1-based
    uint64_t clock = 0;
    float zonePitch = 0.0f;                     // single-channel mode combines zone and lead pitch
    float zonePressure = 0.0f;                  // ...and, sent as channel pressure, zone and lead pressure
    float tailPitch = 0.0f, tailPressure = 0.0f; // the last released note's, standing in for a lead until the next note
    bool timbreFromNote = false;                // single channel's timbre was last set by a note, not the zone
    std::bitset<128> noteControllers;           // single channel's controllers last set by a note's lane, not the zone

    bool isSingleChannelMode() const noexcept;
    bool combinesZonePressure() const noexcept;
    int zoneForKey (int key) const noexcept;
    int chooseMemberChannel (juce::MidiBuffer&, int samplePos, int zone, int key);

    Voice* findVoice (Handle) noexcept;
    const Voice* findVoice (Handle) const noexcept;
    Voice* findVoiceOnChannel (int channel, int key) noexcept;
    Voice* oldestVoice (int zone) noexcept;
    Voice* allocateVoice (juce::MidiBuffer&, int samplePos);
    void releaseVoice (juce::MidiBuffer&, int samplePos, Voice&, float lift);
    bool isLead (const Voice&) const noexcept;
    const Voice* leadVoice() const noexcept;

    void sendLeadValues (juce::MidiBuffer&, int samplePos);
    void sendSingleChannelPitch (juce::MidiBuffer&, int samplePos, bool force);
    void sendSingleChannelPressure (juce::MidiBuffer&, int samplePos, bool force);
    void sendSingleChannelTimbre (juce::MidiBuffer&, int samplePos);
    void sendPitchBend (juce::MidiBuffer&, int samplePos, int channel, double value, bool force);
    void sendChannelPressure (juce::MidiBuffer&, int samplePos, int channel, float value, bool force);
    void sendPolyPressure (juce::MidiBuffer&, int samplePos, int channel, int key, int value, bool force);
    void sendController (juce::MidiBuffer&, int samplePos, int channel, int controller, int value, bool force);
    void sendTimbre (juce::MidiBuffer&, int samplePos, int channel, float value, bool force);
    void sendNotePressure (juce::MidiBuffer&, int samplePos, const Voice&, float value, bool force);
    void sendRpn (juce::MidiBuffer&, int samplePos, int channel, int parameter, int msb, int lsb, bool sendLsb);

    template <typename Fn>
    void forEachZoneMaster (Fn&& fn) const;
};

} // namespace mperoll
