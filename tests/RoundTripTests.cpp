#include "TestHelpers.h"
#include "core/ClipBuilder.h"
#include "core/MidiFileIO.h"
#include "core/MpeDecoder.h"
#include "core/MpeEncoder.h"

namespace mperoll
{

using namespace test;

/** Encoding a clip to MIDI and decoding it again must give back the same performance. */
class RoundTripTests : public juce::UnitTest
{
public:
    RoundTripTests() : juce::UnitTest ("Round trip", "MPE Roll") {}

    static MpeClip makeExpressiveClip()
    {
        MpeClip clip;
        clip.name = "Round trip";
        clip.length = 8.0;

        auto a = makeNote (60, 0.0, 2.0, 0.9f);
        a.lift = 0.25f;
        a.lanes.pitch = { { 0.0, 0.0f }, { 1.0, 2.0f }, { 2.0, -1.5f } };
        a.lanes.pressure = { { 0.0, 0.0f }, { 0.5, 0.8f }, { 2.0, 0.2f } };
        a.lanes.timbre = { { 0.0, 0.3f }, { 2.0, 0.9f } };
        a.lanes.get (LaneId::cc (10)) = { { 0.0, 0.1f }, { 2.0, 0.9f } };   // per-note pan
        clip.addNote (a);

        auto b = makeNote (64, 0.5, 1.0, 0.4f);
        b.lift = 0.75f;
        b.lanes.pitch = { { 0.0, 0.5f } };    // microtonal, static
        clip.addNote (b);

        auto c = makeNote (67, 0.5, 3.0, 0.6f);   // overlaps both, plain
        clip.addNote (c);

        auto d = makeNote (72, 4.0, 0.25, 1.0f);
        d.lanes.pitch = { { 0.0, -12.0f }, { 0.25, 0.0f } };   // scoop up an octave
        clip.addNote (d);

        clip.zone.pitch = { { 0.0, 0.0f }, { 4.0, 1.0f } };
        clip.zone.get (LaneId::cc (64)) = { { 0.0, 1.0f }, { 3.0, 1.0f }, { 3.0001, 0.0f } };
        return clip;
    }

    void compareClips (const MpeClip& original, const MpeClip& decoded, float velocityTolerance)
    {
        expectEquals ((int) decoded.notes.size(), (int) original.notes.size());

        if (decoded.notes.size() != original.notes.size())
            return;

        constexpr float pitchTolerance = 48.0f / 8192.0f * 2.0f + 0.006f;
        constexpr float valueTolerance = 1.0f / 127.0f + 0.003f;

        for (size_t i = 0; i < original.notes.size(); ++i)
        {
            const auto& o = original.notes[i];
            const auto& r = decoded.notes[i];
            const auto label = "note " + juce::String ((int) i);

            expectEquals (r.key, o.key, label);
            expectWithinAbsoluteError (r.start, o.start, 2.0 / 960.0, label);
            expectWithinAbsoluteError (r.length, o.length, 2.0 / 960.0, label);
            expectWithinAbsoluteError (r.strike, o.strike, velocityTolerance, label);
            expectWithinAbsoluteError (r.lift, o.lift, velocityTolerance, label);

            for (double t = 0.0; t < o.length - 0.02; t += o.length / 37.0)
            {
                expectWithinAbsoluteError (r.lanes.pitch.valueAt (t, 0.0f), o.lanes.pitch.valueAt (t, 0.0f), pitchTolerance, label + " pitch");
                expectWithinAbsoluteError (r.lanes.pressure.valueAt (t, 0.0f), o.lanes.pressure.valueAt (t, 0.0f), valueTolerance, label + " pressure");
                expectWithinAbsoluteError (r.lanes.timbre.valueAt (t, 0.5f), o.lanes.timbre.valueAt (t, 0.5f), valueTolerance, label + " timbre");

                if (const auto* pan = o.lanes.find (LaneId::cc (10)))
                {
                    const auto* decodedPan = r.lanes.find (LaneId::cc (10));
                    expect (decodedPan != nullptr, label + " pan lane");

                    if (decodedPan != nullptr)
                        expectWithinAbsoluteError (decodedPan->valueAt (t, 0.5f), pan->valueAt (t, 0.5f), valueTolerance, label + " pan");
                }
            }
        }

        for (double t = 0.0; t < 4.0; t += 0.1)
            expectWithinAbsoluteError (decoded.zone.pitch.valueAt (t, 0.0f), original.zone.pitch.valueAt (t, 0.0f), 0.002f, "zone pitch");

        const auto* sustain = decoded.zone.find (LaneId::cc (64));
        expect (sustain != nullptr && sustain->valueAt (1.0, 0.0f) > 0.99f && sustain->valueAt (3.5, 1.0f) < 0.01f, "sustain pedal");
    }

    void runTest() override
    {
        beginTest ("Clip -> MPE MIDI file -> clip");
        {
            const auto original = makeExpressiveClip();

            juce::MemoryOutputStream stream;
            expect (MidiFileIO::writeClip (original, {}, {}, stream));

            juce::MemoryInputStream input (stream.getData(), stream.getDataSize(), false);
            const auto imported = MidiFileIO::importClip (input, {});
            expect (imported.ok);
            expectEquals (imported.detectedZones.lowerMembers, 15);
            expectEquals (imported.clip.name, original.name);
            compareClips (original, imported.clip, 1.0f / 127.0f);
        }

        beginTest ("High resolution velocity, 14-bit timbre and a custom zone layout survive the round trip");
        {
            const auto original = makeExpressiveClip();

            OutputSettings out;
            out.zones = { 6, 0 };
            out.memberBendRange = 24.0f;
            out.highResVelocity = true;
            out.timbreLsbController = 106;

            InputSettings in;
            in.timbreLsbController = 106;

            juce::MemoryOutputStream stream;
            expect (MidiFileIO::writeClip (original, out, {}, stream));
            juce::MemoryInputStream input (stream.getData(), stream.getDataSize(), false);
            const auto imported = MidiFileIO::importClip (input, in);

            expectEquals (imported.detectedZones.lowerMembers, 6);
            compareClips (original, imported.clip, 1.0f / 16383.0f + 1.0e-5f);
        }

        beginTest ("Haken MPE+ high resolution survives the round trip");
        {
            const auto original = makeExpressiveClip();

            OutputSettings out;
            out.mpePlus = true;
            out.memberBendRange = 96.0f;
            InputSettings in;
            in.mpePlus = true;

            juce::MemoryOutputStream stream;
            expect (MidiFileIO::writeClip (original, out, {}, stream));
            juce::MemoryInputStream input (stream.getData(), stream.getDataSize(), false);
            const auto imported = MidiFileIO::importClip (input, in);
            compareClips (original, imported.clip, 1.0f / 127.0f);
        }

        beginTest ("Plain (non-MPE) MIDI files import as notes with zone-wide expression");
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100).withTimeStamp (0));
            seq.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80).withTimeStamp (0));
            seq.addEvent (juce::MidiMessage::pitchWheel (1, 16383).withTimeStamp (480));
            seq.addEvent (juce::MidiMessage::noteOff (1, 60).withTimeStamp (960));
            seq.addEvent (juce::MidiMessage::noteOff (1, 64).withTimeStamp (960));

            juce::MidiFile file;
            file.setTicksPerQuarterNote (480);
            file.addTrack (seq);

            const auto imported = MidiFileIO::importClip (file, {});
            expectEquals ((int) imported.clip.notes.size(), 2);
            expectWithinAbsoluteError (imported.clip.notes[0].length, 2.0, 1.0e-9);
            expect (imported.clip.notes[0].lanes.pitch.isEmpty());
            expectWithinAbsoluteError (imported.clip.zone.pitch.valueAt (1.5, 0.0f), 2.0f, 1.0e-3f);
        }

        beginTest ("Live recording through the decoder and builder");
        {
            // Simulates a controller: a note that bends up while pressure swells.
            MpeDecoder decoder;
            ClipBuilder builder;

            auto send = [&] (double beat, const juce::MidiMessage& m)
            {
                builder.setTime (beat, beat);
                decoder.process (m, builder);
            };

            send (1.0, juce::MidiMessage::pitchWheel (2, 8192));
            send (1.0, juce::MidiMessage::noteOn (2, 62, (juce::uint8) 100));

            for (int i = 1; i <= 100; ++i)
            {
                const auto beat = 1.0 + i * 0.01;
                send (beat, juce::MidiMessage::pitchWheel (2, MpeEncoder::semitonesToPitchBend ((float) i * 0.02f, 48.0f)));
                send (beat, juce::MidiMessage::channelPressureChange (2, i));
            }

            send (2.0, juce::MidiMessage::noteOff (2, 62, (juce::uint8) 50));
            builder.finish();

            const auto notes = builder.takeNotes();
            expectEquals ((int) notes.size(), 1);
            expectWithinAbsoluteError (notes[0].start, 1.0, 1.0e-9);
            expectWithinAbsoluteError (notes[0].length, 1.0, 1.0e-9);
            expectWithinAbsoluteError (notes[0].lanes.pitch.valueAt (1.0, 0.0f), 2.0f, 0.01f);
            expect (notes[0].lanes.pitch.size() < 10);   // a straight ramp simplifies down
            expectWithinAbsoluteError (notes[0].lanes.pressure.valueAt (0.5, 0.0f), 50.0f / 127.0f, 0.01f);
        }
    }
};

static RoundTripTests roundTripTests;

} // namespace mperoll
