#include "TestHelpers.h"
#include "core/ControllerPresets.h"
#include "core/MpeDecoder.h"

namespace mperoll
{

struct EventLog : MpeDecoder::Listener
{
    std::vector<MpeDecoder::Event> events;

    void handleEvent (const MpeDecoder::Event& e) override { events.push_back (e); }

    int count (MpeDecoder::Event::Type type) const
    {
        int n = 0;

        for (const auto& e : events)
            n += e.type == type ? 1 : 0;

        return n;
    }

    const MpeDecoder::Event* last (MpeDecoder::Event::Type type) const
    {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->type == type)
                return &*it;

        return nullptr;
    }
};

class MpeDecoderTests : public juce::UnitTest
{
public:
    MpeDecoderTests() : juce::UnitTest ("MpeDecoder", "MPE Roll") {}

    static void rpn (MpeDecoder& d, EventLog& log, int channel, int parameter, int msb, int lsb = -1)
    {
        d.process (juce::MidiMessage::controllerEvent (channel, 101, parameter >> 7), log);
        d.process (juce::MidiMessage::controllerEvent (channel, 100, parameter & 127), log);
        d.process (juce::MidiMessage::controllerEvent (channel, 6, msb), log);

        if (lsb >= 0)
            d.process (juce::MidiMessage::controllerEvent (channel, 38, lsb), log);
    }

    void runTest() override
    {
        using Type = MpeDecoder::Event::Type;

        beginTest ("MPE Configuration Messages change the zones");
        {
            MpeDecoder d;
            EventLog log;
            rpn (d, log, 1, 6, 7);
            expectEquals (d.getZones().lowerMembers, 7);
            rpn (d, log, 16, 6, 9);
            expectEquals (d.getZones().upperMembers, 9);
            expectEquals (d.getZones().lowerMembers, 5);    // newest zone wins on overlap
            expect (log.events.empty());
        }

        beginTest ("State sent before the Note On becomes the note's initial expression");
        {
            MpeDecoder d;
            EventLog log;
            d.process (juce::MidiMessage::pitchWheel (2, 8192 + 8191 / 48), log);
            d.process (juce::MidiMessage::controllerEvent (2, 74, 100), log);
            d.process (juce::MidiMessage::channelPressureChange (2, 30), log);
            d.process (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 90), log);

            expect (log.events[0].type == Type::notePrepare);
            expect (log.events.back().type == Type::noteStart);
            expectWithinAbsoluteError (log.last (Type::notePitch)->value, 1.0f, 0.01f);
            expectWithinAbsoluteError (log.last (Type::noteTimbre)->value, 100.0f / 127.0f, 1.0e-5f);
            expectWithinAbsoluteError (log.last (Type::notePressure)->value, 30.0f / 127.0f, 1.0e-5f);
            expectWithinAbsoluteError (log.last (Type::noteStart)->value, 90.0f / 127.0f, 1.0e-5f);
        }

        beginTest ("Member channel messages are per note, master channel messages are zone-wide");
        {
            MpeDecoder d;
            EventLog log;
            d.process (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), log);
            d.process (juce::MidiMessage::noteOn (3, 64, (juce::uint8) 100), log);
            log.events.clear();

            d.process (juce::MidiMessage::pitchWheel (3, 16383), log);
            expectEquals (log.count (Type::notePitch), 1);
            expectEquals (log.last (Type::notePitch)->key, 64);
            expectWithinAbsoluteError (log.last (Type::notePitch)->value, 48.0f, 1.0e-4f);

            d.process (juce::MidiMessage::pitchWheel (1, 0), log);
            expectWithinAbsoluteError (log.last (Type::zonePitch)->value, -2.0f, 1.0e-4f);

            d.process (juce::MidiMessage::controllerEvent (1, 64, 127), log);
            expectEquals (log.last (Type::zoneController)->controller, 64);

            d.process (juce::MidiMessage::noteOff (3, 64, (juce::uint8) 32), log);
            expectWithinAbsoluteError (log.last (Type::noteStop)->value, 32.0f / 127.0f, 1.0e-5f);
            expectEquals (d.getNumHeldNotes(), 1);
        }

        beginTest ("Pitch bend sensitivity on a member channel applies to the whole zone");
        {
            MpeDecoder d;
            EventLog log;
            rpn (d, log, 2, 0, 24);
            d.process (juce::MidiMessage::noteOn (5, 60, (juce::uint8) 100), log);
            d.process (juce::MidiMessage::pitchWheel (5, 16383), log);
            expectWithinAbsoluteError (log.last (Type::notePitch)->value, 24.0f, 1.0e-4f);

            rpn (d, log, 1, 0, 12, 50);
            d.process (juce::MidiMessage::pitchWheel (1, 16383), log);
            expectWithinAbsoluteError (log.last (Type::zonePitch)->value, 12.5f, 1.0e-4f);
        }

        beginTest ("Poly aftertouch, CC88 velocity prefix and channel mode messages");
        {
            MpeDecoder d;
            EventLog log;
            d.process (juce::MidiMessage::controllerEvent (2, 88, 127), log);
            d.process (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 64), log);
            expectWithinAbsoluteError (log.last (Type::noteStart)->value, (float) ((64 << 7) | 127) / 16383.0f, 1.0e-6f);

            d.process (juce::MidiMessage::aftertouchChange (2, 60, 127), log);
            expectWithinAbsoluteError (log.last (Type::notePressure)->value, 1.0f, 1.0e-6f);

            d.process (juce::MidiMessage::noteOn (3, 62, (juce::uint8) 64), log);
            d.process (juce::MidiMessage::allNotesOff (1), log);   // on the master: the whole zone
            expectEquals (d.getNumHeldNotes(), 0);
        }

        beginTest ("Single channel input treats channel messages as zone-wide");
        {
            InputSettings s;
            s.mode = InputSettings::Mode::singleChannel;
            MpeDecoder d;
            d.setSettings (s);
            EventLog log;
            d.process (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), log);
            d.process (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), log);
            d.process (juce::MidiMessage::pitchWheel (1, 16383), log);
            expectEquals (log.count (Type::notePitch), 0);
            expectWithinAbsoluteError (log.last (Type::zonePitch)->value, 2.0f, 1.0e-4f);

            d.process (juce::MidiMessage::aftertouchChange (1, 64, 64), log);
            expectEquals (log.last (Type::notePressure)->key, 64);
        }

        beginTest ("MPE+ prefixes add resolution to timbre, pressure and bend");
        {
            InputSettings s;
            s.mpePlus = true;
            MpeDecoder d;
            d.setSettings (s);
            EventLog log;
            d.process (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), log);

            d.process (juce::MidiMessage::controllerEvent (2, 87, 64), log);
            d.process (juce::MidiMessage::controllerEvent (2, 74, 63), log);
            expectWithinAbsoluteError (log.last (Type::noteTimbre)->value, 0.5f, 1.0e-6f);

            d.process (juce::MidiMessage::controllerEvent (2, 87, 64), log);
            d.process (juce::MidiMessage::channelPressureChange (2, 63), log);
            expectWithinAbsoluteError (log.last (Type::notePressure)->value, 0.5f, 1.0e-6f);

            d.process (juce::MidiMessage::controllerEvent (2, 87, 64), log);
            d.process (juce::MidiMessage::pitchWheel (2, 8192), log);
            expectWithinAbsoluteError (log.last (Type::notePitch)->value, 0.5f * 48.0f / 8191.0f, 1.0e-6f);

            // Without a prefix the plain 7-bit value applies, and CC87 never shows up as a lane.
            d.process (juce::MidiMessage::controllerEvent (2, 74, 127), log);
            expectWithinAbsoluteError (log.last (Type::noteTimbre)->value, 1.0f, 1.0e-6f);
            expectEquals (log.count (Type::noteController), 0);
        }

        beginTest ("Controller presets are recognised until they're customised");
        {
            const auto& presets = controllerPresets();
            expect (presets.size() >= 8);

            for (size_t i = 0; i < presets.size(); ++i)
            {
                expectEquals (findControllerPreset (presets[i].input), (int) i, presets[i].name);

                auto changed = presets[i].input;
                changed.memberBendRange = 12.0f;
                expectEquals (findControllerPreset (changed), -1);

                MpeDecoder d;
                d.setSettings (presets[i].input);
                EventLog log;
                d.process (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), log);
                expectEquals (log.count (Type::noteStart), 1, presets[i].name);
            }
        }

        beginTest ("14-bit timbre combines MSB and LSB");
        {
            InputSettings s;
            s.timbreLsbController = 106;
            MpeDecoder d;
            d.setSettings (s);
            EventLog log;
            d.process (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), log);
            d.process (juce::MidiMessage::controllerEvent (2, 74, 38), log);
            d.process (juce::MidiMessage::controllerEvent (2, 106, 51), log);
            expectWithinAbsoluteError (log.last (Type::noteTimbre)->value, (float) ((38 << 7) | 51) / 16383.0f, 1.0e-6f);
        }
    }
};

static MpeDecoderTests mpeDecoderTests;

} // namespace mperoll
