#include "TestHelpers.h"
#include "core/FlScriptExporter.h"

namespace mperoll
{

using namespace test;

class FlScriptExporterTests : public juce::UnitTest
{
public:
    FlScriptExporterTests() : juce::UnitTest ("FlScriptExporter", "MPE Roll") {}

    void runTest() override
    {
        beginTest ("Overlapping notes get their own colours, later notes reuse them");
        {
            MpeClip clip;
            clip.addNote (makeNote (60, 0.0, 2.0));
            clip.addNote (makeNote (64, 0.5, 2.0));
            clip.addNote (makeNote (67, 1.0, 2.0));
            clip.addNote (makeNote (72, 4.0, 1.0));

            const auto result = FlScriptExporter::generate (clip, {});
            expectEquals (result.notes[0].colour, 0);
            expectEquals (result.notes[1].colour, 1);
            expectEquals (result.notes[2].colour, 2);
            expectEquals (result.notes[3].colour, 0);
            expectEquals (result.numColourConflicts, 0);
        }

        beginTest ("More than 16 overlapping notes are reported as conflicts");
        {
            MpeClip clip;

            for (int i = 0; i < 18; ++i)
                clip.addNote (makeNote (40 + i, 0.0, 1.0));

            expectEquals (FlScriptExporter::generate (clip, {}).numColourConflicts, 2);
        }

        beginTest ("Glides become slide notes ending on the target pitch");
        {
            MpeClip clip;
            auto n = makeNote (60, 0.0, 2.0);
            n.lanes.pitch = { { 0.0, 0.0f }, { 1.0, 2.0f }, { 2.0, 2.0f } };
            clip.addNote (n);

            const auto result = FlScriptExporter::generate (clip, {});
            const auto& slides = result.notes[0].slides;
            expect (! slides.empty());
            expect (slides.front().pitchMoves);
            expectWithinAbsoluteError (slides.front().length, 1.0, 1.0e-9);
            expectWithinAbsoluteError (slides.front().pitch, 2.0f, 1.0e-4f);

            int moving = 0;

            for (const auto& s : slides)
                moving += s.pitchMoves ? 1 : 0;

            expectEquals (moving, 1);
        }

        beginTest ("Zone pitch is folded into notes and the sustain pedal lengthens them");
        {
            MpeClip clip;
            clip.addNote (makeNote (60, 0.0, 1.0));
            clip.zone.pitch = { { 0.0, 0.0f }, { 1.0, 1.0f } };
            clip.zone.get (LaneId::cc (64)) = { { 0.0, 1.0f }, { 2.9999, 1.0f }, { 3.0, 0.0f } };

            auto result = FlScriptExporter::generate (clip, {});
            expectWithinAbsoluteError (result.notes[0].length, 3.0, 1.0e-6);
            expectWithinAbsoluteError (result.notes[0].slides.back().pitch, 1.0f, 1.0e-3f);

            FlScriptExporter::Options plain;
            plain.bakeZonePitch = false;
            plain.applySustainPedal = false;
            result = FlScriptExporter::generate (clip, plain);
            expectWithinAbsoluteError (result.notes[0].length, 1.0, 1.0e-9);
            expect (result.notes[0].slides.empty());
        }

        beginTest ("Vibrato is baked into slides at the project's tempo");
        {
            MpeClip clip;
            clip.vibratoRate = 4.0;
            auto n = makeNote (60, 0.0, 2.0);
            n.lanes.vibrato = { { 0.0, 0.5f } };
            clip.addNote (n);

            // At 120 bpm two beats are one second: four cycles, each swinging up and down by half a semitone.
            FlScriptExporter::Options options;
            options.tempoBpm = 120.0;
            const auto result = FlScriptExporter::generate (clip, options);
            int peaks = 0, troughs = 0;

            for (const auto& s : result.notes[0].slides)
            {
                peaks += s.pitch > 0.4f ? 1 : 0;
                troughs += s.pitch < -0.4f ? 1 : 0;
            }

            expectEquals (peaks, 4);
            expectEquals (troughs, 4);

            // Twice the tempo, twice the beats per cycle: half as many in the note.
            options.tempoBpm = 240.0;
            const auto faster = FlScriptExporter::generate (clip, options);
            peaks = 0;

            for (const auto& s : faster.notes[0].slides)
                peaks += s.pitch > 0.4f ? 1 : 0;

            expectEquals (peaks, 2);
        }

        beginTest ("Long notes keep their vibrato at full depth");
        {
            for (auto depth : { 0.3f, 1.0f })
            {
                MpeClip clip;
                clip.vibratoRate = 5.5;
                auto n = makeNote (60, 0.0, 16.0);
                n.lanes.vibrato = { { 0.0, depth } };
                clip.addNote (n);

                // 16 beats at 120 bpm are 8 seconds: 44 cycles, each needing a slide up and one down.
                FlScriptExporter::Options options;
                options.tempoBpm = 120.0;
                const auto result = FlScriptExporter::generate (clip, options);
                const auto label = "depth " + juce::String (depth);
                int peaks = 0, troughs = 0;

                for (const auto& s : result.notes[0].slides)
                {
                    peaks += s.pitch > depth * 0.8f ? 1 : 0;
                    troughs += s.pitch < -depth * 0.8f ? 1 : 0;
                }

                expect (peaks >= 42 && peaks <= 45, label + ": " + juce::String (peaks) + " peaks");
                expect (troughs >= 42 && troughs <= 45, label + ": " + juce::String (troughs) + " troughs");
                expect ((int) result.notes[0].slides.size() <= options.maxSlidesPerVibratoNote, label);
            }
        }

        beginTest ("A note with vibrato ends at its glide pitch");
        {
            MpeClip clip;
            clip.vibratoRate = 4.0;
            auto n = makeNote (60, 0.0, 1.1875);    // 2.375 cycles at 120 bpm: the note ends just after a peak
            n.lanes.pitch = { { 0.0, 2.0f } };
            n.lanes.vibrato = { { 0.0, 0.5f } };
            clip.addNote (n);

            FlScriptExporter::Options options;
            options.tempoBpm = 120.0;
            const auto result = FlScriptExporter::generate (clip, options);
            const auto& slides = result.notes[0].slides;
            expect (slides.size() >= 4);
            expectWithinAbsoluteError (slides.back().offset + slides.back().length, 1.1875, 1.0e-9);
            expectWithinAbsoluteError (slides.back().pitch, 2.0f, 1.0e-4f);
            expect (slides[slides.size() - 2].pitch > 2.4f, "the slide before it is a peak");
        }

        beginTest ("Peak pressure is -1 for a note without a pressure lane");
        {
            MpeClip clip;
            clip.addNote (makeNote (60, 0.0, 1.0, 0.7f));
            auto pressed = makeNote (64, 0.0, 1.0);
            pressed.lanes.pressure = { { 0.0, 0.9f } };
            clip.addNote (pressed);

            const auto result = FlScriptExporter::generate (clip, {});
            expectWithinAbsoluteError (result.notes[0].peakPressure, -1.0f, 1.0e-6f);
            expectWithinAbsoluteError (result.notes[1].peakPressure, 0.9f, 1.0e-6f);
        }

        beginTest ("Dense curves are capped to the slide budget");
        {
            MpeClip clip;
            auto n = makeNote (60, 0.0, 4.0);

            for (int i = 0; i <= 400; ++i)
                n.lanes.pitch.append (i * 0.01, (float) std::sin (i * 0.3) * 2.0f);

            clip.addNote (n);

            FlScriptExporter::Options options;
            options.maxSlidesPerNote = 20;
            const auto result = FlScriptExporter::generate (clip, options);
            expect ((int) result.notes[0].slides.size() <= 20);
            expect (! result.notes[0].slides.empty());
        }

        beginTest ("Script embeds every note");
        {
            MpeClip clip;
            clip.addNote (makeNote (60, 0.0, 1.0));
            clip.addNote (makeNote (62, 1.0, 1.0));

            FlScriptExporter::Options options;
            options.title = "Quote \" and backslash \\ test";
            const auto result = FlScriptExporter::generate (clip, options);

            expect (result.script.contains ("import flpianoroll as flp"));
            expect (result.script.contains ("def createDialog():"));
            expect (result.script.contains ("def apply(form):"));
            expect (result.script.contains ("TITLE = \"Quote \\\" and backslash \\\\ test\""));
            expect (juce::StringArray::fromLines (result.script).indexOf ("NOTES = [") >= 0);
        }
    }
};

static FlScriptExporterTests flScriptExporterTests;

} // namespace mperoll
