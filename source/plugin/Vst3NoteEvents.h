#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace mperoll::Vst3NoteEvents
{

/*  VST3 note events, handed to JUCE's VST3 host inside a MIDI buffer.

    JUCE builds a hosted VST3's event list from MIDI alone, so it can't give a synth note IDs or
    note expressions. MPE Roll's patch to JUCE (cmake/PatchJuce.cmake, with the decoder in
    cmake/juce/MpeRollVst3Events.inc) turns these SysEx messages back into VST3 note on/off events
    with note IDs and note expression values, at the same sample position. They're SysEx under the
    non-commercial manufacturer ID, 0x7d, followed by "MRV", every value in 7-bit groups, most
    significant first:

        F0 7D 'M' 'R' 'V' 01 channel pitch velocity(2) noteId(4) F7          note on
        F0 7D 'M' 'R' 'V' 02 channel pitch velocity(2) noteId(4) F7          note off
        F0 7D 'M' 'R' 'V' 03 noteId(4) typeId(5) value(5) F7                 note expression value

    velocity is 0..16383 for 0..1, a note ID 0..2^28-1, and a value 0..0xffffffff for 0..1.
    Adding them doesn't allocate once the buffer has room. Any thread.
*/

constexpr int noteEventSize = 15, expressionEventSize = 21;
constexpr uint32_t maxNoteId = (1u << 28) - 1;

inline void putBits (uint8_t* at, uint64_t value, int bytes) noexcept
{
    for (int i = bytes - 1; i >= 0; --i)
    {
        at[i] = (uint8_t) (value & 0x7f);
        value >>= 7;
    }
}

inline uint64_t getBits (const uint8_t* at, int bytes) noexcept
{
    uint64_t value = 0;

    for (int i = 0; i < bytes; ++i)
        value = (value << 7) | (uint64_t) (at[i] & 0x7f);

    return value;
}

inline bool isEvent (const uint8_t* data, int size) noexcept
{
    return data != nullptr && size >= 7 && data[0] == 0xf0 && data[1] == 0x7d
        && data[2] == 'M' && data[3] == 'R' && data[4] == 'V' && data[size - 1] == 0xf7;
}

/** Adds a note on (or off) with a note ID; channel is 0..15. */
inline void addNote (juce::MidiBuffer& buffer, int samplePosition, bool isOn,
                     int channel, int pitch, double velocity, uint32_t noteId)
{
    std::array<uint8_t, noteEventSize> m { 0xf0, 0x7d, 'M', 'R', 'V', (uint8_t) (isOn ? 1 : 2) };
    m[6] = (uint8_t) juce::jlimit (0, 15, channel);
    m[7] = (uint8_t) juce::jlimit (0, 127, pitch);
    putBits (m.data() + 8, (uint64_t) juce::roundToInt (juce::jlimit (0.0, 1.0, velocity) * 16383.0), 2);
    putBits (m.data() + 10, noteId & maxNoteId, 4);
    m[14] = 0xf7;
    buffer.addEvent (m.data(), (int) m.size(), samplePosition);
}

/** Adds a note expression value (normalised 0..1, as VST3 note expressions are) for a note ID. */
inline void addExpression (juce::MidiBuffer& buffer, int samplePosition, uint32_t noteId, uint32_t typeId, double value)
{
    std::array<uint8_t, expressionEventSize> m { 0xf0, 0x7d, 'M', 'R', 'V', 3 };
    putBits (m.data() + 6, noteId & maxNoteId, 4);
    putBits (m.data() + 10, typeId, 5);
    putBits (m.data() + 15, (uint64_t) std::llround (juce::jlimit (0.0, 1.0, value) * 4294967295.0), 5);
    m[20] = 0xf7;
    buffer.addEvent (m.data(), (int) m.size(), samplePosition);
}

} // namespace mperoll::Vst3NoteEvents
