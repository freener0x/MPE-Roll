#include "TestHelpers.h"
#include "core/MpeEncoder.h"
#include "plugin/clap/ClapNoteTranslator.h"

namespace mperoll
{

class ClapNoteTranslatorTests : public juce::UnitTest
{
public:
    ClapNoteTranslatorTests() : juce::UnitTest ("ClapNoteTranslator", "MPE Roll") {}

    struct Translated
    {
        std::vector<clap_event_note_t> notes;
        std::vector<clap_event_note_expression_t> expressions;
        std::vector<clap_event_midi_t> midi;
        std::vector<uint32_t> times;    // every event, in order

        std::vector<double> valuesOf (int expression) const
        {
            std::vector<double> values;

            for (const auto& e : expressions)
                if (e.expression_id == expression)
                    values.push_back (e.value);

            return values;
        }

        int count (uint16_t type) const
        {
            int n = 0;

            for (const auto& e : notes)
                n += e.header.type == type ? 1 : 0;

            return n;
        }
    };

    static Translated run (ClapNoteTranslator& translator, const juce::MidiBuffer& midi, int numSamples = 512)
    {
        ClapEventList list;
        translator.translate (midi, list, numSamples);

        Translated result;

        for (uint32_t i = 0; i < list.size(); ++i)
        {
            const auto* header = list.get (i);
            result.times.push_back (header->time);

            if (header->type == CLAP_EVENT_NOTE_ON || header->type == CLAP_EVENT_NOTE_OFF)
                result.notes.push_back (*reinterpret_cast<const clap_event_note_t*> (header));
            else if (header->type == CLAP_EVENT_NOTE_EXPRESSION)
                result.expressions.push_back (*reinterpret_cast<const clap_event_note_expression_t*> (header));
            else if (header->type == CLAP_EVENT_MIDI)
                result.midi.push_back (*reinterpret_cast<const clap_event_midi_t*> (header));
        }

        return result;
    }

    static int bend (float semitones, float range)
    {
        return MpeEncoder::semitonesToPitchBend (semitones, range);
    }

    void runTest() override
    {
        beginTest ("An MPE note becomes a CLAP note with its expression");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::pitchWheel (2, bend (2.0f, 48.0f)), 10);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 74, 100), 10);
            midi.addEvent (juce::MidiMessage::channelPressureChange (2, 40), 10);
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 10);
            midi.addEvent (juce::MidiMessage::pitchWheel (2, bend (12.0f, 48.0f)), 200);
            midi.addEvent (juce::MidiMessage::channelPressureChange (2, 80), 200);
            midi.addEvent (juce::MidiMessage::noteOff (2, 60, (juce::uint8) 64), 400);

            const auto r = run (t, midi);
            expect (t.isUsingNoteEvents());
            expectEquals ((int) r.notes.size(), 2);
            expectEquals ((int) r.midi.size(), 0, "nothing is left over as MIDI");

            const auto& on = r.notes.front();
            const auto& off = r.notes.back();
            expectEquals ((int) on.header.type, (int) CLAP_EVENT_NOTE_ON);
            expectEquals ((int) on.key, 60);
            expectEquals ((int) on.channel, 1);
            expectEquals ((int) on.header.time, 10);
            expectWithinAbsoluteError (on.velocity, 100.0 / 127.0, 1.0e-6);
            expect (on.note_id >= 0);
            expectEquals ((int) off.header.type, (int) CLAP_EVENT_NOTE_OFF);
            expectEquals (off.note_id, on.note_id, "the note off names the same note");
            expectEquals ((int) off.header.time, 400);

            const auto tuning = r.valuesOf (CLAP_NOTE_EXPRESSION_TUNING);
            expectEquals ((int) tuning.size(), 2);
            expectWithinAbsoluteError (tuning[0], 2.0, 0.01);
            expectWithinAbsoluteError (tuning[1], 12.0, 0.01);

            const auto pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
            expectEquals ((int) pressure.size(), 2);
            expectWithinAbsoluteError (pressure[0], 40.0 / 127.0, 1.0e-4);
            expectWithinAbsoluteError (pressure[1], 80.0 / 127.0, 1.0e-4);

            const auto brightness = r.valuesOf (CLAP_NOTE_EXPRESSION_BRIGHTNESS);
            expectEquals ((int) brightness.size(), 1);
            expectWithinAbsoluteError (brightness[0], 100.0 / 127.0, 1.0e-4);

            for (const auto& e : r.expressions)
                expectEquals (e.note_id, on.note_id);

            // The note's initial expression follows its note on, at the same time.
            expectEquals ((int) r.expressions.front().header.time, 10);

            for (size_t i = 1; i < r.times.size(); ++i)
                expect (r.times[i] >= r.times[i - 1], "events stay in time order");

            expectEquals (t.getNumHeldNotes(), 0);
        }

        beginTest ("The master channel's bend tunes every note in its zone");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (3, bend (1.0f, 48.0f)), 0);
            midi.addEvent (juce::MidiMessage::noteOn (3, 64, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, bend (-2.0f, 2.0f)), 100);

            const auto r = run (t, midi);
            const auto tuning = r.valuesOf (CLAP_NOTE_EXPRESSION_TUNING);
            expectEquals ((int) tuning.size(), 3);
            expectWithinAbsoluteError (tuning[0], 1.0, 0.01);    // E4's own bend at its start
            expectWithinAbsoluteError (tuning[1], -2.0, 0.01);   // C4: master bend only
            expectWithinAbsoluteError (tuning[2], -1.0, 0.01);   // E4: its own bend plus the master's
            expectEquals ((int) r.midi.size(), 0, "the master bend isn't also sent as MIDI");
        }

        beginTest ("Zone-wide controllers go on as MIDI, or onto the notes for synths without MIDI");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 5);
            midi.addEvent (juce::MidiMessage::channelPressureChange (1, 90), 6);
            midi.addEvent (juce::MidiMessage::programChange (1, 3), 7);

            auto r = run (t, midi);
            expectEquals ((int) r.midi.size(), 3);
            expectEquals ((int) r.midi[0].data[0], 0xb0);
            expectEquals ((int) r.midi[0].data[1], 64);
            expectEquals ((int) r.midi[0].data[2], 127);
            expectEquals ((int) r.midi[0].header.time, 5);
            expectEquals ((int) r.midi[1].data[0], 0xd0);
            expectEquals ((int) r.midi[2].data[0], 0xc0);
            expectEquals ((int) r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE).size(), 0);

            ClapNoteTranslator clapOnly;
            clapOnly.setDialects (true, false);
            r = run (clapOnly, midi);
            expectEquals ((int) r.midi.size(), 0);
            expectEquals ((int) r.notes.size(), 1);
            const auto pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
            expectEquals ((int) pressure.size(), 1, "zone pressure reaches the note instead");
            expectWithinAbsoluteError (pressure[0], 90.0 / 127.0, 1.0e-4);
        }

        beginTest ("Per-note volume, pan, vibrato and expression controllers become note expressions");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 10, 0), 0);
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 7, 127), 10);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 10, 64), 11);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 1, 127), 12);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 11, 0), 13);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 20, 99), 14);

            const auto r = run (t, midi);
            const auto pan = r.valuesOf (CLAP_NOTE_EXPRESSION_PAN);
            expectEquals ((int) pan.size(), 2);
            expectWithinAbsoluteError (pan[0], 0.0, 1.0e-9);
            expectWithinAbsoluteError (pan[1], 0.5, 1.0e-9);
            expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_VOLUME).at (0), 1.0, 1.0e-9);
            expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_VIBRATO).at (0), 1.0, 1.0e-9);
            expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_EXPRESSION).at (0), 0.0, 1.0e-9);

            expectEquals ((int) r.midi.size(), 1, "a controller with no note expression goes on as MIDI");
            expectEquals ((int) r.midi[0].data[1], 20);
        }

        beginTest ("Pressure sent on a controller still arrives as pressure");
        {
            OutputSettings s;
            s.pressureTarget = OutputSettings::PressureTarget::controller;
            s.pressureController = 11;
            ClapNoteTranslator t;
            t.setOutputSettings (s);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (2, 11, 127), 10);

            const auto r = run (t, midi);
            expectEquals ((int) r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE).size(), 1);
            expectEquals ((int) r.valuesOf (CLAP_NOTE_EXPRESSION_EXPRESSION).size(), 0);
        }

        beginTest ("Single channel mode: bends tune every note, poly aftertouch presses one");
        {
            OutputSettings s;
            s.mode = OutputSettings::Mode::singleChannel;
            s.pressureTarget = OutputSettings::PressureTarget::polyAftertouch;
            ClapNoteTranslator t;
            t.setOutputSettings (s);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::pitchWheel (1, bend (1.0f, 2.0f)), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::aftertouchChange (1, 60, 127), 10);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, bend (-1.0f, 2.0f)), 20);

            const auto r = run (t, midi);
            const auto tuning = r.valuesOf (CLAP_NOTE_EXPRESSION_TUNING);
            expectEquals ((int) tuning.size(), 4);
            expectWithinAbsoluteError (tuning[0], 1.0, 0.01);
            expectWithinAbsoluteError (tuning[1], 1.0, 0.01);
            expectWithinAbsoluteError (tuning[2], -1.0, 0.01);
            expectWithinAbsoluteError (tuning[3], -1.0, 0.01);

            int pressed = 0;

            for (const auto& e : r.expressions)
                if (e.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
                    pressed += e.key == 60 ? 1 : 100;

            expectEquals (pressed, 1, "only the pressed key gets pressure");
        }

        beginTest ("MIDI goes through untouched for MIDI-only synths, or when asked");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::pitchWheel (2, 9000), 3);
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 3);
            midi.addEvent (juce::MidiMessage::noteOff (2, 60, (juce::uint8) 0), 30);

            ClapNoteTranslator midiOnly;
            midiOnly.setDialects (false, true);
            auto r = run (midiOnly, midi);
            expect (! midiOnly.isUsingNoteEvents());
            expectEquals ((int) r.notes.size(), 0);
            expectEquals ((int) r.midi.size(), 3);
            expectEquals ((int) r.midi[0].data[0], 0xe1);
            expectEquals ((int) r.midi[2].header.time, 30);

            OutputSettings s;
            s.clapNoteExpressions = false;
            ClapNoteTranslator preferMidi;
            preferMidi.setOutputSettings (s);
            r = run (preferMidi, midi);
            expectEquals ((int) r.midi.size(), 3);

            ClapNoteTranslator noChoice;
            noChoice.setOutputSettings (s);
            noChoice.setDialects (true, false);
            r = run (noChoice, midi);
            expect (noChoice.isUsingNoteEvents(), "a synth that only takes CLAP notes gets them anyway");
            expectEquals ((int) r.notes.size(), 2);
        }

        beginTest ("Changing the layout ends held notes in the dialect they started in");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            run (t, midi);
            expectEquals (t.getNumHeldNotes(), 1);

            OutputSettings s;
            s.memberBendRange = 24.0f;
            t.setOutputSettings (s);
            auto r = run (t, {});
            expectEquals (r.count (CLAP_EVENT_NOTE_OFF), 1);
            expectEquals (t.getNumHeldNotes(), 0);

            // Notes started as MIDI are silenced with All Notes Off on the way to note events.
            s.clapNoteExpressions = false;
            t.setOutputSettings (s);
            run (t, midi);
            s.clapNoteExpressions = true;
            t.setOutputSettings (s);
            r = run (t, {});
            expectEquals ((int) r.midi.size(), 16);
            expectEquals ((int) r.midi[0].data[1], 123);
        }

        beginTest ("It reads what MpeEncoder writes, MPE+ and wide bend ranges included");
        {
            for (const bool mpePlus : { false, true })
            {
                OutputSettings s;
                s.memberBendRange = 96.0f;
                s.mpePlus = mpePlus;

                MpeEncoder enc;
                enc.setSettings (s);
                ClapNoteTranslator t;
                t.setOutputSettings (s);

                juce::MidiBuffer midi;
                enc.writeConfiguration (midi, 0);
                enc.prepareNote (midi, 0, 1, 60);
                enc.setPitch (midi, 0, 1, -7.25f);
                enc.setTimbre (midi, 0, 1, 0.3f);
                enc.startNote (midi, 0, 1, 0.9f);
                enc.setPitch (midi, 50, 1, 31.5f);
                enc.setPressure (midi, 60, 1, 0.61f);
                enc.stopNote (midi, 100, 1, 0.2f);

                const auto r = run (t, midi);
                expectEquals ((int) r.notes.size(), 2);
                expectEquals ((int) r.midi.size(), 0, "configuration messages aren't passed on");

                const auto tuning = r.valuesOf (CLAP_NOTE_EXPRESSION_TUNING);
                expect (tuning.size() >= 2);
                expectWithinAbsoluteError (tuning[0], -7.25, mpePlus ? 0.001 : 0.02);
                expectWithinAbsoluteError (tuning[1], 31.5, mpePlus ? 0.001 : 0.02);
                expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_BRIGHTNESS).at (0), 0.3, 0.01);
                expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE).back(), 0.61, 0.01);
                expectWithinAbsoluteError (r.notes[0].velocity, 0.9, 0.01);
            }
        }

        beginTest ("The event list stops at its capacity instead of allocating");
        {
            ClapEventList list;
            list.reserve (sizeof (clap_event_note_t) * 2, 8);

            clap_event_note_t note {};
            note.header = makeClapHeader<clap_event_note_t> (CLAP_EVENT_NOTE_ON, 0);
            expect (list.push (note));
            expect (list.push (note));
            expect (! list.push (note));
            expectEquals ((int) list.size(), 2);
            expectEquals ((int) list.getInputEvents()->size (list.getInputEvents()), 2);
            expect (list.getInputEvents()->get (list.getInputEvents(), 1) != nullptr);
            expect (list.getInputEvents()->get (list.getInputEvents(), 2) == nullptr);
        }
    }
};

static ClapNoteTranslatorTests clapNoteTranslatorTests;

} // namespace mperoll
