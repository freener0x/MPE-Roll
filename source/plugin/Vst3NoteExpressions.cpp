#include "Vst3NoteExpressions.h"
#include "Vst3NoteEvents.h"

#if MPEROLL_HOST_CLAP
 #include "clap/ClapNoteTranslator.h"
#endif

namespace mperoll
{

Vst3NoteExpressions::Vst3NoteExpressions()
{
   #if MPEROLL_HOST_CLAP
    translator = std::make_unique<ClapNoteTranslator>();
    events = std::make_unique<ClapEventList>();
   #endif

    for (auto& t : typeIds)     t = Vst3Support::noType;
    for (auto& s : scaled)      s = false;
    for (auto& m : minimum)     m = 0.0;
    for (auto& m : maximum)     m = 1.0;
}

Vst3NoteExpressions::~Vst3NoteExpressions() = default;

void Vst3NoteExpressions::setSupport (const Vst3Support& support) noexcept
{
    for (size_t i = 0; i < (size_t) numExpressions; ++i)
    {
        typeIds[i] = support.typeIds[i];
        scaled[i] = support.scaled[i];
        minimum[i] = support.minimum[i];
        maximum[i] = support.maximum[i];
    }

    takesTuning = support.takesTuning();
    midiMissesNotes = support.midiMissesNotes();
}

bool Vst3NoteExpressions::isUsedWith (const OutputSettings& settings) const noexcept
{
   #if MPEROLL_HOST_CLAP
    return takesTuning.load() && midiMissesNotes.load()
        && settings.mode == OutputSettings::Mode::mpe && settings.clapNoteExpressions;
   #else
    juce::ignoreUnused (settings);
    return false;
   #endif
}

void Vst3NoteExpressions::prepare (int maximumBlockSize)
{
    // A note event takes a few dozen bytes in a MidiBuffer, and a block can carry hundreds.
    translated.ensureSize ((size_t) juce::jmax (1 << 15, maximumBlockSize * 64));
}

void Vst3NoteExpressions::reset()
{
   #if MPEROLL_HOST_CLAP
    translator->reset();
   #endif

    translating = false;

    for (auto& channel : held)
        channel.reset();
}

void Vst3NoteExpressions::forgetNotes()
{
   #if MPEROLL_HOST_CLAP
    translator->forgetNotes();
   #endif

    for (auto& channel : held)
        channel.reset();
}

juce::MidiBuffer& Vst3NoteExpressions::process (juce::MidiBuffer& midi, int numSamples, const OutputSettings& settings) noexcept
{
   #if MPEROLL_HOST_CLAP
    const auto use = isUsedWith (settings);

    if (! use && ! translating)
    {
        trackHeld (midi);
        return midi;
    }

    translated.clear();
    events->clear();

    // Notes that went out as MIDI are ended here: the translator only ends notes it started.
    if (use && ! translating)
        endHeld (translated);

    // Going back to MIDI, the translator ends its notes and passes this block's MIDI on, once.
    translator->setOutputSettings (settings);
    translator->setDialects (use, true);
    translator->translate (midi, *events, numSamples);
    convert();

    if (! use)
        trackHeld (midi);

    translating = use;
    return translated;
   #else
    juce::ignoreUnused (numSamples, settings);
    return midi;
   #endif
}

void Vst3NoteExpressions::trackHeld (const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto* data = metadata.data;

        if (metadata.numBytes < 3)
            continue;

        const auto status = data[0] & 0xf0;
        const auto channel = (size_t) (data[0] & 0x0f);
        const auto key = (size_t) (data[1] & 0x7f);

        if (status == 0x90 && data[2] > 0)
            held[channel].set (key);
        else if (status == 0x80 || status == 0x90)
            held[channel].reset (key);
    }
}

void Vst3NoteExpressions::endHeld (juce::MidiBuffer& out) noexcept
{
    for (size_t channel = 0; channel < held.size(); ++channel)
    {
        for (size_t key = 0; key < 128; ++key)
        {
            if (held[channel].test (key))
            {
                const uint8_t off[3] { (uint8_t) (0x80 | channel), (uint8_t) key, 0 };
                out.addEvent (off, 3, 0);
            }
        }

        held[channel].reset();
    }
}

double Vst3NoteExpressions::valueFor (int expression, double clapValue) const noexcept
{
    const auto x = (size_t) expression;
    auto value = clapValue;

    // CLAP's units to VST3's normalised values.
    if (expression == Vst3Support::tuning)
        value = clapValue / 240.0 + 0.5;    // semitones; VST3's tuning spans +/-120
    else if (expression == 0)
        value = clapValue / 4.0;            // a gain up to 4 (+12 dB); VST3's volume has 0.25 for 0 dB

    value = juce::jlimit (0.0, 1.0, value);

    // A synth's own type for pressure or timbre gets 0..1 spread over the range it gives.
    if (scaled[x].load())
        return minimum[x].load() + value * (maximum[x].load() - minimum[x].load());

    return juce::jlimit (minimum[x].load(), maximum[x].load(), value);
}

void Vst3NoteExpressions::convert() noexcept
{
   #if MPEROLL_HOST_CLAP
    for (uint32_t i = 0; i < events->size(); ++i)
    {
        const auto* header = events->get (i);

        if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;

        const auto time = (int) header->time;

        switch (header->type)
        {
            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            {
                const auto& e = *reinterpret_cast<const clap_event_note_t*> (header);
                Vst3NoteEvents::addNote (translated, time, header->type == CLAP_EVENT_NOTE_ON,
                                         0, e.key, e.velocity, (uint32_t) e.note_id);
                break;
            }

            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                const auto& e = *reinterpret_cast<const clap_event_note_expression_t*> (header);

                if (e.expression_id < 0 || e.expression_id >= numExpressions)
                    break;

                const auto type = typeIds[(size_t) e.expression_id].load();

                if (type != Vst3Support::noType)
                    Vst3NoteEvents::addExpression (translated, time, (uint32_t) e.note_id, type,
                                                   valueFor (e.expression_id, e.value));

                break;
            }

            case CLAP_EVENT_MIDI:
            {
                const auto& e = *reinterpret_cast<const clap_event_midi_t*> (header);
                const auto size = juce::jlimit (1, 3, juce::MidiMessage::getMessageLengthFromFirstByte (e.data[0]));
                translated.addEvent (e.data, size, time);
                break;
            }

            default:
                break;
        }
    }
   #endif
}

} // namespace mperoll
