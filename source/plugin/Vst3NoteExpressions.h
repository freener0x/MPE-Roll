#pragma once

#include "Vst3Support.h"
#include "core/MpeSettings.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <bitset>
#include <memory>

namespace mperoll
{

class ClapNoteTranslator;
class ClapEventList;

/** Plays a VST3 synth with VST3 note expressions when MPE over MIDI can't reach its notes one by
    one but it takes note expressions (see Vst3Support): each MPE note becomes a VST3 note with an
    ID of its own, its bend becomes tuning, and its pressure and timbre become the types the synth
    maps MPE's pressure and Y to, with volume, pan, vibrato and expression from per-note CC7, CC10,
    CC1 and CC11 where the synth takes them. The zone's bend and pressure are combined into every
    note, as an MPE synth would combine them; its other controllers (the sustain pedal...) go on as
    MIDI. Every note goes on MIDI channel 1, told apart by its ID, so a synth listening on channel 1
    only hears them all.

    It's MPE Roll's MPE to CLAP translator, with its CLAP events handed to JUCE's VST3 host as
    Vst3NoteEvents. Anything else (a synth MIDI reaches note by note, Plain MIDI, note expressions
    turned off in the settings) goes through as it is.

    setSupport() and isUsedWith() on any thread; the rest on the audio thread (or with it held
    off), real-time safe except prepare().
*/
class Vst3NoteExpressions
{
public:
    Vst3NoteExpressions();
    ~Vst3NoteExpressions();

    /** What the loaded synth takes. */
    void setSupport (const Vst3Support&) noexcept;

    /** Whether notes go as note expressions with these settings. */
    bool isUsedWith (const OutputSettings&) const noexcept;

    /** Whether this build can play VST3 note expressions at all: it uses the CLAP translator. */
    static constexpr bool isAvailable() noexcept
    {
       #if MPEROLL_HOST_CLAP
        return true;
       #else
        return false;
       #endif
    }

    /** Room for a block of this size. Not real-time safe. */
    void prepare (int maximumBlockSize);

    /** Another synth: forget everything. */
    void reset();

    /** The synth was stopped and started again, so its voices are gone. */
    void forgetNotes();

    /** The MIDI to hand JUCE's VST3 host for a block: `midi` itself, or what it became. */
    juce::MidiBuffer& process (juce::MidiBuffer& midi, int numSamples, const OutputSettings&) noexcept;

private:
    static constexpr int numExpressions = Vst3Support::numExpressions;

   #if MPEROLL_HOST_CLAP
    std::unique_ptr<ClapNoteTranslator> translator;
    std::unique_ptr<ClapEventList> events;
   #endif

    juce::MidiBuffer translated;
    bool translating = false;                       // the translator had notes to look after last block
    std::array<std::bitset<128>, 16> held;          // notes that went as MIDI and haven't ended

    std::atomic<bool> takesTuning { false }, midiMissesNotes { false };
    std::array<std::atomic<uint32_t>, numExpressions> typeIds;
    std::array<std::atomic<bool>, numExpressions> scaled;
    std::array<std::atomic<double>, numExpressions> minimum, maximum;

    void trackHeld (const juce::MidiBuffer&) noexcept;
    void endHeld (juce::MidiBuffer&) noexcept;
    void convert (bool onChannelOne) noexcept;
    double valueFor (int expression, double clapValue) const noexcept;
};

} // namespace mperoll
