#include "core/ExpressionCurve.h"
#include "core/MpeClip.h"
#include "core/MpeSettings.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace mperoll
{

class ExpressionCurveTests : public juce::UnitTest
{
public:
    ExpressionCurveTests() : juce::UnitTest ("ExpressionCurve", "MPE Roll") {}

    void runTest() override
    {
        beginTest ("Interpolates linearly and holds the end values");
        {
            ExpressionCurve c { { 1.0, 0.0f }, { 3.0, 1.0f } };
            expectEquals (c.valueAt (0.0, 9.0f), 0.0f);
            expectWithinAbsoluteError (c.valueAt (2.0, 9.0f), 0.5f, 1.0e-6f);
            expectEquals (c.valueAt (5.0, 9.0f), 1.0f);
            expectEquals (ExpressionCurve().valueAt (1.0, 0.25f), 0.25f);
        }

        beginTest ("Points stay sorted and same-time points are replaced");
        {
            ExpressionCurve c;
            c.addPoint (2.0, 0.2f);
            c.addPoint (1.0, 0.1f);
            c.addPoint (3.0, 0.3f);
            c.addPoint (2.0, 0.9f);
            expectEquals ((int) c.size(), 3);
            expectEquals (c.getPoints()[1].value, 0.9f);

            c.append (2.5, 0.5f);   // out of order append falls back to a sorted insert
            expectEquals (c.getPoints()[2].beat, 2.5);
        }

        beginTest ("Ranges can be removed and replaced");
        {
            ExpressionCurve c { { 0.0, 0.0f }, { 1.0, 1.0f }, { 2.0, 0.0f }, { 3.0, 1.0f } };
            c.removeRange (0.5, 2.0);
            expectEquals ((int) c.size(), 2);

            c.replaceRange (0.0, 3.0, { { 0.0, 0.5f }, { 1.5, 0.75f } });
            expectEquals ((int) c.size(), 2);
            expectEquals (c.valueAt (1.5, 0.0f), 0.75f);
        }

        beginTest ("Simplify keeps the shape and removes redundant points");
        {
            ExpressionCurve line;

            for (int i = 0; i <= 100; ++i)
                line.append (i * 0.01, (float) i * 0.01f);

            line.simplify (0.001f);
            expectEquals ((int) line.size(), 2);

            ExpressionCurve flat;

            for (int i = 0; i <= 50; ++i)
                flat.append (i * 0.1, 0.3f);

            flat.simplify (0.001f);
            expectEquals ((int) flat.size(), 1);

            ExpressionCurve vibrato;

            for (int i = 0; i <= 400; ++i)
                vibrato.append (i * 0.01, (float) std::sin (i * 0.05));

            const auto original = vibrato;
            vibrato.simplify (0.01f);
            expect (vibrato.size() < original.size() / 3);

            for (int i = 0; i <= 400; ++i)
                expectWithinAbsoluteError (vibrato.valueAt (i * 0.01, 0.0f), original.valueAt (i * 0.01, 0.0f), 0.0101f);
        }

        beginTest ("Averages are time weighted");
        {
            ExpressionCurve ramp { { 0.0, 0.0f }, { 1.0, 1.0f } };
            expectWithinAbsoluteError (ramp.averageOver (0.0, 1.0, 0.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (ramp.averageOver (0.0, 2.0, 0.0f), 0.75f, 1.0e-6f);
        }

        beginTest ("Trim keeps the shape inside the range");
        {
            ExpressionCurve c { { 0.0, 0.0f }, { 4.0, 4.0f } };
            c.trimTo (1.0, 3.0);
            expectEquals ((int) c.size(), 2);
            expectWithinAbsoluteError (c.valueAt (1.0, 0.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (c.valueAt (3.0, 0.0f), 3.0f, 1.0e-6f);
        }

        beginTest ("Clips serialise and deserialise losslessly");
        {
            MpeClip clip;
            clip.name = "Test clip";
            clip.length = 8.0;
            clip.looping = false;

            MpeNote n;
            n.key = 64;
            n.start = 1.25;
            n.length = 0.5;
            n.strike = 0.123f;
            n.lift = 0.9f;
            n.lanes.pitch = { { 0.0, 0.0f }, { 0.5, 2.5f } };
            n.lanes.pressure = { { 0.1, 0.4f } };
            n.lanes.get (LaneId::cc (10)) = { { 0.0, 0.25f } };
            n.lanes.vibrato = { { 0.0, 0.0f }, { 0.4, 0.6f } };
            clip.addNote (n);
            clip.zone.get (LaneId::cc (64)) = { { 0.0, 1.0f }, { 4.0, 0.0f } };
            clip.zone.vibrato = { { 2.0, 0.3f } };
            clip.vibratoRate = 7.5;

            MpeClip restored;
            expect (MpeClip::fromMemoryBlock (clip.toMemoryBlock(), restored));
            expect (restored == clip);

            juce::MemoryBlock garbage ("not a clip", 10);
            MpeClip untouched;
            expect (! MpeClip::fromMemoryBlock (garbage, untouched));
        }

        beginTest ("Clips saved before vibrato still load");
        {
            // Version 1: one note with a pitch curve and controller 10, and no zone lanes.
            juce::MemoryOutputStream out;
            out.writeInt (0x4d505243);
            out.writeInt (1);
            out.writeString ("Old");
            out.writeDouble (8.0);
            out.writeBool (true);
            out.writeInt (2);
            out.writeInt (1);
            out.writeInt (1);
            out.writeInt (62);
            out.writeDouble (0.5);
            out.writeDouble (1.0);
            out.writeFloat (0.7f);
            out.writeFloat (0.4f);
            out.writeInt (2);
            out.writeDouble (0.0); out.writeFloat (0.0f);
            out.writeDouble (1.0); out.writeFloat (3.0f);
            out.writeInt (0);
            out.writeInt (0);
            out.writeInt (1);
            out.writeInt (10);
            out.writeInt (1);
            out.writeDouble (0.0); out.writeFloat (0.25f);

            for (int i = 0; i < 3; ++i)
                out.writeInt (0);

            out.writeInt (0);

            MpeClip old;
            expect (MpeClip::fromMemoryBlock (out.getMemoryBlock(), old));
            expect (old.name == "Old" && old.notes.size() == 1 && old.notes[0].key == 62);
            expect (old.notes[0].lanes.pitch.size() == 2 && old.notes[0].lanes.find (LaneId::cc (10)) != nullptr);
            expect (old.notes[0].lanes.vibrato.isEmpty() && old.zone.vibrato.isEmpty());
            expectEquals (old.vibratoRate, defaultVibratoRate);
        }

        beginTest ("Settings round trip through XML");
        {
            EngineSettings s;
            s.output.zones = { 7, 7 };
            s.output.memberBendRange = 24.0f;
            s.output.pressureTarget = OutputSettings::PressureTarget::polyAftertouch;
            s.output.timbreLsbController = 106;
            s.output.highResVelocity = true;
            s.input.mode = InputSettings::Mode::singleChannel;
            s.playback.mode = PlaybackSettings::Mode::noteTrigger;
            s.playback.triggerBaseKey = 24;

            const auto xml = s.toXml();
            expect (EngineSettings::fromXml (xml.get()) == s);
        }

        beginTest ("Zone layouts follow the MPE specification");
        {
            expect ((ZoneLayout { 15, 3 }.normalised() == ZoneLayout { 15, 0 }));
            expect ((ZoneLayout { 7, 10 }.normalised() == ZoneLayout { 7, 7 }));

            const ZoneLayout split { 5, 6 };
            expect (split.isMasterChannel (1));
            expect (split.isMasterChannel (16));
            expect (split.isMemberChannel (6));
            expect (! split.isMemberChannel (7));
            expect (split.isMemberChannel (10));
            expectEquals (split.zoneOfChannel (8), -1);
            expectEquals (split.memberChannel (1, 0), 15);
        }
    }
};

static ExpressionCurveTests expressionCurveTests;

} // namespace mperoll
