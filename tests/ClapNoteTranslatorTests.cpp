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

        beginTest ("Zone pressure reaches the notes; other zone controllers go to every channel of the zone");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 5);
            midi.addEvent (juce::MidiMessage::channelPressureChange (1, 90), 6);
            midi.addEvent (juce::MidiMessage::programChange (1, 3), 7);

            auto r = run (t, midi);
            expectEquals ((int) r.midi.size(), 17, "the pedal on the master and all 15 member channels, then the program change");
            expectEquals ((int) r.midi[0].data[0], 0xb0);
            expectEquals ((int) r.midi[0].data[1], 64);
            expectEquals ((int) r.midi[0].data[2], 127);
            expectEquals ((int) r.midi[0].header.time, 5);
            expectEquals ((int) r.midi[1].data[0], 0xb1, "including the channel the note is on");
            expectEquals ((int) r.midi[15].data[0], 0xbf);
            expectEquals ((int) r.midi[16].data[0], 0xc0);

            auto pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
            expectEquals ((int) pressure.size(), 1, "zone pressure becomes the note's pressure, not MIDI");
            expectWithinAbsoluteError (pressure[0], 90.0 / 127.0, 1.0e-4);

            ClapNoteTranslator clapOnly;
            clapOnly.setDialects (true, false);
            r = run (clapOnly, midi);
            expectEquals ((int) r.midi.size(), 0);
            expectEquals ((int) r.notes.size(), 1);
            pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
            expectEquals ((int) pressure.size(), 1);
            expectWithinAbsoluteError (pressure[0], 90.0 / 127.0, 1.0e-4);
        }

        beginTest ("A zone's values combine with each note's own, as MpeEncoder writes them");
        {
            for (const bool synthTakesMidi : { true, false })
            {
                OutputSettings s;
                MpeEncoder enc;
                enc.setSettings (s);
                ClapNoteTranslator t;
                t.setOutputSettings (s);
                t.setDialects (true, synthTakesMidi);

                juce::MidiBuffer midi;
                enc.setZonePressure (midi, 0, 0.5f);
                enc.setZoneTimbre (midi, 0, 0.7f);
                enc.prepareNote (midi, 0, 1, 60);
                enc.setPressure (midi, 0, 1, 0.2f);
                enc.setTimbre (midi, 0, 1, 0.6f);
                enc.startNote (midi, 0, 1, 0.8f);
                enc.setZonePressure (midi, 100, 0.6f);

                const auto r = run (t, midi);
                const auto pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
                const auto brightness = r.valuesOf (CLAP_NOTE_EXPRESSION_BRIGHTNESS);
                expectEquals ((int) pressure.size(), 2);
                expectWithinAbsoluteError (pressure[0], 0.7, 0.02, "the note's pressure plus the zone's");
                expectWithinAbsoluteError (pressure[1], 0.8, 0.02, "and again when the zone's changes");
                expectEquals ((int) brightness.size(), 1);
                expectWithinAbsoluteError (brightness[0], 0.8, 0.02, "timbre: the zone's moves the note's from the middle");

                for (const auto& m : r.midi)
                    expect ((m.data[0] & 0xf0) != 0xd0, "zone pressure isn't also sent as MIDI");
            }
        }

        beginTest ("A controller with no note expression goes out as MIDI, never the note it came with");
        {
            OutputSettings s;
            s.zones.lowerMembers = 1;   // so both notes use the same channel
            MpeEncoder enc;
            enc.setSettings (s);
            ClapNoteTranslator t;
            t.setOutputSettings (s);

            juce::MidiBuffer midi;
            enc.prepareNote (midi, 0, 1, 60);
            enc.setController (midi, 0, 1, 71, 0.8f);
            enc.startNote (midi, 0, 1, 0.9f);
            enc.stopNote (midi, 100, 1, 0.5f);
            enc.prepareNote (midi, 200, 2, 62);     // the encoder resets CC71 on the channel first
            enc.startNote (midi, 200, 2, 0.9f);
            enc.stopNote (midi, 300, 2, 0.5f);

            const auto r = run (t, midi);
            int midiNotes = 0;
            std::vector<int> cc71;

            for (const auto& m : r.midi)
            {
                const auto status = m.data[0] & 0xf0;
                midiNotes += (status == 0x90 || status == 0x80) ? 1 : 0;

                if (status == 0xb0 && m.data[1] == 71)
                    cc71.push_back (m.data[2]);
            }

            expectEquals (midiNotes, 0, "no note goes out as MIDI");
            expectEquals ((int) cc71.size(), 2, "the controller goes out before each note");
            expectEquals (cc71.at (0), MpeEncoder::toSevenBit (0.8f));
            expectEquals (r.count (CLAP_EVENT_NOTE_ON), 2);
            expectEquals (r.count (CLAP_EVENT_NOTE_OFF), 2);
            expectEquals (t.getNumHeldNotes(), 0);
        }

        beginTest ("Poly aftertouch sent before a note gives it its starting pressure");
        {
            for (const auto mode : { OutputSettings::Mode::singleChannel, OutputSettings::Mode::mpe })
            {
                OutputSettings s;
                s.mode = mode;
                s.pressureTarget = OutputSettings::PressureTarget::polyAftertouch;
                MpeEncoder enc;
                enc.setSettings (s);
                ClapNoteTranslator t;
                t.setOutputSettings (s);

                juce::MidiBuffer midi;
                enc.prepareNote (midi, 0, 1, 60);
                enc.setPressure (midi, 0, 1, 0.8f);
                enc.startNote (midi, 0, 1, 0.9f);

                const auto r = run (t, midi);
                const auto pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
                expect (! pressure.empty(), "the note starts with pressure");

                if (! pressure.empty())
                    expectWithinAbsoluteError (pressure.back(), 0.8, 0.01);
            }
        }

        beginTest ("Reactivating the synth keeps the zone's bend for the next notes");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::pitchWheel (1, bend (2.0f, 2.0f)), 0);
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            run (t, midi);

            t.forgetNotes();
            expectEquals (t.getNumHeldNotes(), 0);

            juce::MidiBuffer next;
            next.addEvent (juce::MidiMessage::noteOn (3, 64, (juce::uint8) 100), 0);
            const auto r = run (t, next);
            const auto tuning = r.valuesOf (CLAP_NOTE_EXPRESSION_TUNING);
            expectEquals ((int) tuning.size(), 1);
            expectWithinAbsoluteError (tuning.at (0), 2.0, 0.01);
        }

        beginTest ("Tuning stays within the range CLAP defines");
        {
            OutputSettings s;
            s.memberBendRange = 96.0f;
            s.masterBendRange = 96.0f;
            ClapNoteTranslator t;
            t.setOutputSettings (s);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::pitchWheel (1, bend (90.0f, 96.0f)), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (2, bend (90.0f, 96.0f)), 0);
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);

            const auto tuning = run (t, midi).valuesOf (CLAP_NOTE_EXPRESSION_TUNING);
            expectEquals ((int) tuning.size(), 1);
            expectWithinAbsoluteError (tuning.at (0), 120.0, 1.0e-9);
        }

        beginTest ("A note off that doesn't fit in the block is sent at the start of the next");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (2, 60, (juce::uint8) 64), 10);

            ClapEventList tiny;
            tiny.reserve (sizeof (clap_event_note_t), 1);
            t.translate (midi, tiny, 512);
            expectEquals ((int) tiny.size(), 1, "only the note on fitted");
            expectEquals (t.getNumHeldNotes(), 1, "so the note is still owed its note off");

            const auto r = run (t, {});
            expectEquals (r.count (CLAP_EVENT_NOTE_OFF), 1);
            expectEquals ((int) r.notes.at (0).header.time, 0);
            expectEquals (t.getNumHeldNotes(), 0);
        }

        beginTest ("A flood of expression leaves room for note offs");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);

            for (int i = 0; i < 400; ++i)
                midi.addEvent (juce::MidiMessage::channelPressureChange (2, i % 128), 1 + i);

            midi.addEvent (juce::MidiMessage::noteOff (2, 60, (juce::uint8) 64), 500);

            ClapEventList small;
            small.reserve (64 * sizeof (clap_event_note_expression_t), 64);
            t.translate (midi, small, 512);

            int offs = 0;

            for (uint32_t i = 0; i < small.size(); ++i)
                offs += small.get (i)->type == CLAP_EVENT_NOTE_OFF ? 1 : 0;

            expectEquals (offs, 1);
            expectEquals (t.getNumHeldNotes(), 0);
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
            expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_VOLUME).at (0), 1.27 * 1.27, 1.0e-9);
            expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_VIBRATO).at (0), 1.0, 1.0e-9);
            expectWithinAbsoluteError (r.valuesOf (CLAP_NOTE_EXPRESSION_EXPRESSION).at (0), 0.0, 1.0e-9);

            expectEquals ((int) r.midi.size(), 1, "a controller with no note expression goes on as MIDI");
            expectEquals ((int) r.midi[0].data[1], 20);

            juce::MidiBuffer standard;
            standard.addEvent (juce::MidiMessage::controllerEvent (3, 7, 100), 0);
            standard.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100), 0);
            expectWithinAbsoluteError (run (t, standard).valuesOf (CLAP_NOTE_EXPRESSION_VOLUME).at (0), 1.0, 1.0e-9,
                                       "CC7 at its usual 100 leaves the volume as it is");
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

            // What was passed through as MIDI is undone on the way to note events: notes, and the
            // bends and pressure that now become note expressions, on every channel. Controllers
            // are left alone (MpeEncoder resends them; Reset All Controllers would undo them).
            s.clapNoteExpressions = false;
            t.setOutputSettings (s);
            run (t, midi);
            s.clapNoteExpressions = true;
            t.setOutputSettings (s);
            r = run (t, {});
            expectEquals ((int) r.midi.size(), 48);
            expectEquals ((int) r.midi[0].data[1], 123);
            expectEquals ((int) r.midi[1].data[0], 0xe0);
            expectEquals ((int) r.midi[1].data[2], 0x40, "pitch bend back to the centre");
            expectEquals ((int) r.midi[2].data[0], 0xd0);

            for (const auto& m : r.midi)
                expect (! ((m.data[0] & 0xf0) == 0xb0 && m.data[1] == 121), "no Reset All Controllers");
        }

        beginTest ("A settings change keeps the zone's values and sends no controller resets");
        {
            ClapNoteTranslator t;
            juce::MidiBuffer first;
            first.addEvent (juce::MidiMessage::controllerEvent (1, 74, 102), 0);   // zone timbre 0.8
            first.addEvent (juce::MidiMessage::controllerEvent (1, 1, 89), 0);    // zone mod wheel, as MIDI
            first.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0);
            run (t, first);

            OutputSettings s;
            s.defaultTimbre = 0.4f;         // anything that isn't the zone layout
            t.setOutputSettings (s);

            juce::MidiBuffer next;
            next.addEvent (juce::MidiMessage::controllerEvent (3, 74, 64), 0);    // the note's own timbre, 0.5
            next.addEvent (juce::MidiMessage::noteOn (3, 64, (juce::uint8) 100), 0);
            const auto r = run (t, next);

            const auto brightness = r.valuesOf (CLAP_NOTE_EXPRESSION_BRIGHTNESS);
            expectEquals ((int) brightness.size(), 1);
            expectWithinAbsoluteError (brightness.at (0), 0.8, 0.02, "the zone's timbre still moves the new note");

            for (const auto& m : r.midi)
                expect (! ((m.data[0] & 0xf0) == 0xb0 && m.data[1] == 121), "the synth's mod wheel isn't reset");
        }

        beginTest ("A key's poly aftertouch ends with its note");
        {
            OutputSettings s;
            s.mode = OutputSettings::Mode::singleChannel;
            s.pressureTarget = OutputSettings::PressureTarget::polyAftertouch;
            MpeEncoder enc;
            enc.setSettings (s);
            ClapNoteTranslator t;
            t.setOutputSettings (s);

            juce::MidiBuffer midi;
            enc.prepareNote (midi, 0, 1, 60);
            enc.setPressure (midi, 0, 1, 0.8f);
            enc.startNote (midi, 0, 1, 0.9f);
            enc.stopNote (midi, 100, 1, 0.5f);
            enc.prepareNote (midi, 200, 2, 60);      // the same key again, with no pressure of its own
            enc.startNote (midi, 200, 2, 0.9f);

            const auto r = run (t, midi);
            expectEquals (r.count (CLAP_EVENT_NOTE_ON), 2);
            const auto second = r.notes.back().note_id;
            int pressureOnSecond = 0;

            for (const auto& e : r.expressions)
                if (e.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE && e.note_id == second)
                    ++pressureOnSecond;

            expectEquals (pressureOnSecond, 0, "the second note doesn't inherit the first one's pressure");
        }

        beginTest ("Plain MIDI: the newest note's channel pressure reaches the synth's notes");
        {
            OutputSettings s;
            s.mode = OutputSettings::Mode::singleChannel;
            MpeEncoder enc;
            enc.setSettings (s);
            ClapNoteTranslator t;
            t.setOutputSettings (s);

            juce::MidiBuffer midi;
            enc.prepareNote (midi, 0, 1, 60);
            enc.setPressure (midi, 0, 1, 0.8f);
            enc.startNote (midi, 0, 1, 0.9f);
            enc.setZonePressure (midi, 10, 0.1f);

            const auto r = run (t, midi);
            const auto pressure = r.valuesOf (CLAP_NOTE_EXPRESSION_PRESSURE);
            expectEquals ((int) pressure.size(), 2);

            if (pressure.size() == 2)
            {
                expectWithinAbsoluteError (pressure[0], 0.8, 0.01, "the note starts with its pressure");
                expectWithinAbsoluteError (pressure[1], 0.9, 0.01, "the zone's is added");
            }
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
            list.reserve (sizeof (clap_event_note_t) * 2, 16);

            clap_event_note_t note {};
            note.header = makeClapHeader<clap_event_note_t> (CLAP_EVENT_NOTE_ON, 0);
            expect (list.push (note));
            expect (! list.push (note), "the last room is kept for essential events");
            expect (list.push (note, ClapEventList::Priority::essential));
            expect (! list.push (note, ClapEventList::Priority::essential));
            expectEquals ((int) list.size(), 2);
            expectEquals ((int) list.getInputEvents()->size (list.getInputEvents()), 2);
            expect (list.getInputEvents()->get (list.getInputEvents(), 1) != nullptr);
            expect (list.getInputEvents()->get (list.getInputEvents(), 2) == nullptr);
        }
    }
};

static ClapNoteTranslatorTests clapNoteTranslatorTests;

} // namespace mperoll
