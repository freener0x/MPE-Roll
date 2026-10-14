#include "TestHelpers.h"
#include "core/MpeEncoder.h"

namespace mperoll
{

using namespace test;

class MpeEncoderTests : public juce::UnitTest
{
public:
    MpeEncoderTests() : juce::UnitTest ("MpeEncoder", "MPE Roll") {}

    void runTest() override
    {
        beginTest ("Configuration: MCM then bend ranges on master and members");
        {
            MpeEncoder enc;
            juce::MidiBuffer out;
            enc.writeConfiguration (out, 0);
            const auto m = collect (out);

            // MCM: CC101 0, CC100 6, CC6 15, null RPN
            expect (m[0].message.isControllerOfType (101) && m[0].message.getControllerValue() == 0 && m[0].message.getChannel() == 1);
            expect (m[1].message.isControllerOfType (100) && m[1].message.getControllerValue() == 6);
            expect (m[2].message.isControllerOfType (6) && m[2].message.getControllerValue() == 15);
            expect (m[3].message.isControllerOfType (101) && m[3].message.getControllerValue() == 127);

            // Master bend range 2 on channel 1, member range 48 on channels 2..16.
            expect (m[7].message.isControllerOfType (6) && m[7].message.getControllerValue() == 2 && m[7].message.getChannel() == 1);
            const auto memberRanges = countIf (m, [] (const juce::MidiMessage& msg)
                                               { return msg.isControllerOfType (6) && msg.getControllerValue() == 48; });
            expectEquals (memberRanges, 15);
        }

        beginTest ("Initial expression is sent before the Note On");
        {
            MpeEncoder enc;
            juce::MidiBuffer out;
            enc.prepareNote (out, 10, 1, 60);
            enc.setPitch (out, 10, 1, 12.0f);
            enc.setPressure (out, 10, 1, 0.25f);
            enc.startNote (out, 10, 1, 0.8f);
            const auto m = collect (out);

            expect (m.front().message.isPitchWheel());
            expectEquals (m.front().message.getPitchWheelValue(), 8192 + 2048);
            expect (m.back().message.isNoteOn());
            expectEquals (m.back().message.getChannel(), 2);
            expectEquals ((int) m.back().message.getVelocity(), 102);

            // The note has no timbre of its own, so the default goes out before the note.
            expectEquals (countIf (m, [] (const juce::MidiMessage& msg)
                                   { return msg.isControllerOfType (74) && msg.getControllerValue() == 64; }), 1);
            expectEquals (countIf (m, [] (const juce::MidiMessage& msg)
                                   { return msg.isChannelPressure() && msg.getChannelPressureValue() == 32; }), 1);
        }

        beginTest ("Channels rotate, least recently released first");
        {
            OutputSettings s;
            s.zones = { 3, 0 };
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;

            for (MpeEncoder::Handle h = 1; h <= 3; ++h)
            {
                enc.prepareNote (out, 0, h, 60 + (int) h);
                enc.startNote (out, 0, h, 0.5f);
            }

            expectEquals (enc.getChannel (1), 2);
            expectEquals (enc.getChannel (2), 3);
            expectEquals (enc.getChannel (3), 4);

            enc.stopNote (out, 0, 2, 0.5f);
            enc.stopNote (out, 0, 1, 0.5f);
            enc.prepareNote (out, 0, 4, 70);
            enc.startNote (out, 0, 4, 0.5f);
            expectEquals (enc.getChannel (4), 3);   // released before channel 2
        }

        beginTest ("Expression is deduplicated once a note is sounding");
        {
            MpeEncoder enc;
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 60);
            enc.startNote (out, 0, 1, 0.5f);
            out.clear();

            enc.setPitch (out, 0, 1, 1.0f);
            enc.setPitch (out, 1, 1, 1.0f);
            enc.setTimbre (out, 2, 1, 0.75f);
            enc.setTimbre (out, 3, 1, 0.75f);
            expectEquals (out.getNumEvents(), 2);
        }

        beginTest ("Overflow: share a channel or steal the oldest note");
        {
            OutputSettings s;
            s.zones = { 2, 0 };
            MpeEncoder share;
            share.setSettings (s);
            juce::MidiBuffer out;

            for (MpeEncoder::Handle h = 1; h <= 3; ++h)
            {
                share.prepareNote (out, 0, h, 60 + (int) h);
                share.startNote (out, 0, h, 0.5f);
            }

            expectEquals (share.getNumActiveVoices(), 3);
            expect (share.getChannel (3) == 2 || share.getChannel (3) == 3);

            s.overflow = OutputSettings::Overflow::stealOldest;
            MpeEncoder steal;
            steal.setSettings (s);
            out.clear();

            for (MpeEncoder::Handle h = 1; h <= 3; ++h)
            {
                steal.prepareNote (out, 0, h, 60 + (int) h);
                steal.startNote (out, 0, h, 0.5f);
            }

            expectEquals (steal.getNumActiveVoices(), 2);
            expect (! steal.isPlaying (1));
            expectEquals (steal.getChannel (3), 2);
            expectEquals (countIf (collect (out), [] (const juce::MidiMessage& m) { return m.isNoteOff(); }), 1);
        }

        beginTest ("High resolution velocity uses a CC88 prefix");
        {
            OutputSettings s;
            s.highResVelocity = true;
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 60);
            enc.startNote (out, 0, 1, 0.75f);
            const auto m = collect (out);

            const auto& prefix = m[m.size() - 2].message;
            const auto& noteOn = m.back().message;
            expect (prefix.isControllerOfType (88));
            expectEquals ((noteOn.getVelocity() << 7) | prefix.getControllerValue(), 12287);
        }

        beginTest ("14-bit timbre sends MSB then LSB");
        {
            OutputSettings s;
            s.timbreLsbController = 106;
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 60);
            enc.setTimbre (out, 0, 1, 0.3f);
            const auto m = collect (out);

            expect (m[0].message.isControllerOfType (74));
            expect (m[1].message.isControllerOfType (106));
            const auto v14 = (m[0].message.getControllerValue() << 7) | m[1].message.getControllerValue();
            expectEquals (v14, MpeEncoder::toFourteenBit (0.3f));
        }

        beginTest ("Zone-wide pitch uses the master channel and range");
        {
            MpeEncoder enc;
            juce::MidiBuffer out;
            enc.setZonePitch (out, 0, 1.0f);
            const auto m = collect (out);
            expectEquals ((int) m.size(), 1);
            expectEquals (m[0].message.getChannel(), 1);
            expectEquals (m[0].message.getPitchWheelValue(), 8192 + 4096);
        }

        beginTest ("Zone controllers can be reset without touching notes");
        {
            MpeEncoder enc;
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 60);
            enc.setPitch (out, 0, 1, 2.0f);
            enc.startNote (out, 0, 1, 0.5f);
            enc.setZonePitch (out, 0, 1.0f);
            enc.setZoneController (out, 0, 64, 1.0f);
            out.clear();

            enc.resetZoneControllers (out, 0);
            const auto m = collect (out);
            expectEquals ((int) m.size(), 2);
            expect (m[0].message.isPitchWheel() && m[0].message.getChannel() == 1 && m[0].message.getPitchWheelValue() == 8192);
            expect (m[1].message.isControllerOfType (64) && m[1].message.getControllerValue() == 0);
            expect (enc.isPlaying (1));
        }

        beginTest ("MPE+ sends a CC87 prefix just before timbre, pressure and bend");
        {
            OutputSettings s;
            s.mpePlus = true;
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 60);
            enc.setTimbre (out, 0, 1, 0.5f);
            enc.setPressure (out, 0, 1, 0.5f);
            enc.setPitch (out, 0, 1, 0.5f * 48.0f / 8191.0f);   // half a bend step above centre
            const auto m = collect (out);

            expect (m[0].message.isControllerOfType (87) && m[0].message.getControllerValue() == 64);
            expect (m[1].message.isControllerOfType (74) && m[1].message.getControllerValue() == 63);
            expect (m[2].message.isControllerOfType (87) && m[2].message.getControllerValue() == 64);
            expect (m[3].message.isChannelPressure() && m[3].message.getChannelPressureValue() == 63);
            expect (m[4].message.isControllerOfType (87) && m[4].message.getControllerValue() == 64);
            expect (m[5].message.isPitchWheel() && m[5].message.getPitchWheelValue() == 8192);
        }

        beginTest ("Alternative pressure targets");
        {
            OutputSettings s;
            s.pressureTarget = OutputSettings::PressureTarget::polyAftertouch;
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 61);
            enc.setPressure (out, 0, 1, 1.0f);
            const auto m = collect (out);
            expect (m[0].message.isAftertouch() && m[0].message.getNoteNumber() == 61 && m[0].message.getAfterTouchValue() == 127);
        }

        beginTest ("Single channel mode: poly aftertouch and last-note pitch");
        {
            OutputSettings s;
            s.mode = OutputSettings::Mode::singleChannel;
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;

            enc.prepareNote (out, 0, 1, 60);
            enc.startNote (out, 0, 1, 0.5f);
            enc.prepareNote (out, 0, 2, 64);
            enc.startNote (out, 0, 2, 0.5f);
            expectEquals (enc.getChannel (1), 1);
            expectEquals (enc.getChannel (2), 1);
            out.clear();

            enc.setPressure (out, 0, 1, 0.5f);
            enc.setPitch (out, 0, 1, 1.0f);   // not the lead note: ignored
            enc.setPitch (out, 0, 2, 1.0f);
            const auto m = collect (out);
            expectEquals ((int) m.size(), 2);
            expect (m[0].message.isAftertouch() && m[0].message.getNoteNumber() == 60);
            expect (m[1].message.isPitchWheel() && m[1].message.getPitchWheelValue() == 8192 + 4096);
        }

        beginTest ("Two zones split the keyboard");
        {
            OutputSettings s;
            s.zones = { 7, 7 };
            s.splitKey = 60;
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 48);
            enc.prepareNote (out, 0, 2, 72);
            expectEquals (enc.getChannel (1), 2);
            expectEquals (enc.getChannel (2), 15);
        }

        beginTest ("A fresh note resets per-note controllers left by the previous note");
        {
            OutputSettings s;
            s.zones = { 1, 0 };
            MpeEncoder enc;
            enc.setSettings (s);
            juce::MidiBuffer out;
            enc.prepareNote (out, 0, 1, 60);
            enc.setController (out, 0, 1, 10, 1.0f);
            enc.startNote (out, 0, 1, 0.5f);
            enc.stopNote (out, 0, 1, 0.5f);
            out.clear();

            enc.prepareNote (out, 0, 2, 62);
            enc.startNote (out, 0, 2, 0.5f);
            expectEquals (countIf (collect (out), [] (const juce::MidiMessage& m)
                                   { return m.isControllerOfType (10) && m.getControllerValue() == 64; }), 1);
        }

        beginTest ("Pitch bend conversion is exact at the extremes");
        {
            expectEquals (MpeEncoder::semitonesToPitchBend (48.0f, 48.0f), 16383);
            expectEquals (MpeEncoder::semitonesToPitchBend (-48.0f, 48.0f), 0);
            expectEquals (MpeEncoder::semitonesToPitchBend (0.0f, 48.0f), 8192);
            expectWithinAbsoluteError (MpeEncoder::pitchBendToSemitones (16383, 48.0f), 48.0f, 1.0e-5f);
            expectWithinAbsoluteError (MpeEncoder::pitchBendToSemitones (0, 48.0f), -48.0f, 1.0e-5f);
        }
    }
};

static MpeEncoderTests mpeEncoderTests;

} // namespace mperoll
