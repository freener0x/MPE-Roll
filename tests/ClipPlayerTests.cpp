#include "TestHelpers.h"
#include "core/ClipPlayer.h"

namespace mperoll
{

using namespace test;

class ClipPlayerTests : public juce::UnitTest
{
public:
    ClipPlayerTests() : juce::UnitTest ("ClipPlayer", "MPE Roll") {}

    static constexpr double sampleRate = 48000.0;
    static constexpr double bps = 2.0 / sampleRate;    // 120 bpm

    struct Rig
    {
        MpeEncoder encoder;
        ClipPlayer player;
        std::vector<TimedMessage> messages;   // absolute sample positions

        void render (const MpeClip& clip, double startBeat, int startSample, int numSamples, bool loop)
        {
            juce::MidiBuffer out;
            player.renderBlock (clip, encoder, out, startBeat, bps, 0, numSamples, loop);

            for (auto& m : collect (out))
            {
                m.sample += startSample;
                messages.push_back (m);
            }
        }

        // Plays from beat 0 in fixed blocks, computing each block's start from the sample counter.
        void play (const MpeClip& clip, int totalSamples, int blockSize, bool loop)
        {
            for (int s = 0; s < totalSamples; s += blockSize)
                render (clip, s * bps, s, std::min (blockSize, totalSamples - s), loop);
        }

        std::vector<int> noteOnSamples (int key = -1) const
        {
            std::vector<int> result;

            for (const auto& m : messages)
                if (m.message.isNoteOn() && (key < 0 || m.message.getNoteNumber() == key))
                    result.push_back (m.sample);

            return result;
        }

        std::vector<int> noteOffSamples() const
        {
            std::vector<int> result;

            for (const auto& m : messages)
                if (m.message.isNoteOff())
                    result.push_back (m.sample);

            return result;
        }
    };

    void runTest() override
    {
        beginTest ("Notes start and stop on the exact sample");
        {
            MpeClip clip;
            clip.addNote (makeNote (60, 1.0, 1.0));
            Rig rig;
            rig.play (clip, 96000, 512, false);

            expect ((rig.noteOnSamples() == std::vector<int> { 24000 }));
            expect ((rig.noteOffSamples() == std::vector<int> { 48000 }));
        }

        beginTest ("No note is lost or doubled across block boundaries");
        {
            MpeClip clip;

            for (int i = 0; i < 64; ++i)
                clip.addNote (makeNote (60 + (i % 12), i * 0.125, 0.1));

            for (int blockSize : { 1, 7, 64, 333, 512, 4096 })
            {
                Rig rig;
                rig.play (clip, (int) (8.5 / bps), blockSize, false);
                expectEquals ((int) rig.noteOnSamples().size(), 64, "block size " + juce::String (blockSize));
                expectEquals ((int) rig.noteOffSamples().size(), 64, "block size " + juce::String (blockSize));
            }
        }

        beginTest ("Looping clips retrigger at the loop point and cut notes running past it");
        {
            MpeClip clip;
            clip.length = 2.0;
            clip.addNote (makeNote (60, 0.0, 0.5));
            clip.addNote (makeNote (62, 1.5, 4.0));   // runs past the loop end
            Rig rig;
            rig.play (clip, (int) (6.0 / bps), 480, true);

            expect ((rig.noteOnSamples (60) == std::vector<int> { 0, 48000, 96000 }));
            expectEquals ((int) rig.noteOnSamples (62).size(), 3);
            expectEquals ((int) rig.noteOffSamples().size(), 6);
        }

        beginTest ("Notes under the playhead are chased when playback starts mid-note");
        {
            MpeClip clip;
            auto n = makeNote (60, 0.0, 2.0);
            n.lanes.pitch = { { 0.0, 0.0f }, { 2.0, 4.0f } };
            clip.addNote (n);

            Rig rig;
            rig.render (clip, 1.0, 0, 256, false);
            expect ((rig.noteOnSamples() == std::vector<int> { 0 }));

            // The chased note starts with the pitch it has at beat 1 (+2 semitones of 48).
            int initialBend = -1;

            for (const auto& m : rig.messages)
                if (m.message.isPitchWheel() && initialBend < 0)
                    initialBend = m.message.getPitchWheelValue();

            expectEquals (initialBend, MpeEncoder::semitonesToPitchBend (2.0f, 48.0f));

            juce::MidiBuffer ignored;
            rig.player.setChaseNotes (false);
            rig.player.stop (rig.encoder, ignored, 0);
            rig.messages.clear();
            rig.render (clip, 1.0, 0, 256, false);
            expect (rig.noteOnSamples().empty());
        }

        beginTest ("Expression curves are streamed while the note plays");
        {
            MpeClip clip;
            auto n = makeNote (60, 0.0, 1.0);
            n.lanes.pitch = { { 0.0, 0.0f }, { 1.0, 12.0f } };
            n.lanes.pressure = { { 0.0, 0.0f }, { 0.5, 1.0f }, { 1.0, 0.0f } };
            clip.addNote (n);

            Rig rig;
            rig.player.setControlInterval (48);
            rig.play (clip, 24000, 512, false);

            int lastBend = 0, bends = 0, pressures = 0, maxPressure = 0;

            for (const auto& m : rig.messages)
            {
                if (m.message.isPitchWheel())
                {
                    expect (m.message.getPitchWheelValue() >= lastBend);
                    lastBend = m.message.getPitchWheelValue();
                    ++bends;
                }

                if (m.message.isChannelPressure())
                {
                    ++pressures;
                    maxPressure = std::max (maxPressure, m.message.getChannelPressureValue());
                }
            }

            expect (bends > 100);
            expect (std::abs (lastBend - MpeEncoder::semitonesToPitchBend (12.0f, 48.0f)) < 8);
            expect (pressures > 50);
            expectEquals (maxPressure, 127);
        }

        beginTest ("Zone lanes go to the master channel");
        {
            MpeClip clip;
            clip.zone.get (LaneId::cc (64)) = { { 0.0, 1.0f }, { 1.0, 0.0f } };
            Rig rig;
            rig.play (clip, 48000, 512, false);

            expect (countIf (rig.messages, [] (const juce::MidiMessage& m)
                             { return m.isControllerOfType (64) && m.getChannel() == 1; }) > 2);
        }

        beginTest ("Notes removed by an edit are stopped");
        {
            MpeClip clip;
            clip.addNote (makeNote (60, 0.0, 4.0));
            Rig rig;
            rig.render (clip, 0.0, 0, 512, false);
            expectEquals (rig.player.getNumActiveNotes(), 1);

            MpeClip edited = clip;
            edited.notes.clear();
            rig.render (edited, 512 * bps, 512, 512, false);
            expectEquals (rig.player.getNumActiveNotes(), 0);
            expectEquals ((int) rig.noteOffSamples().size(), 1);
        }
    }
};

static ClipPlayerTests clipPlayerTests;

} // namespace mperoll
