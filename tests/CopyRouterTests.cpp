#include "TestHelpers.h"
#include "core/CopyRouter.h"

namespace mperoll
{

using namespace test;

namespace
{
    OutputSettings copySettings (int numCopies = 4)
    {
        OutputSettings s;
        s.mode = OutputSettings::Mode::copies;
        s.copies = numCopies;
        return s;
    }

    /** Keys 0..23 go to every copy, as a loop instrument's modifier keys might. */
    OutputSettings withEveryCopyKeys (OutputSettings s)
    {
        for (int key = 0; key <= 23; ++key)
            s.everyCopyKeys.set ((size_t) key);

        return s;
    }

    /** A copy-mode stream, written by a real MpeEncoder the way MPE Roll writes it, routed to the copies. */
    struct Rig
    {
        explicit Rig (const OutputSettings& s)
        {
            stream.setSettings (s);
            router->setSettings (s, outs.data(), 0);
            clearOuts();

            for (int c = 0; c < router->getNumCopies(); ++c)
                available.set ((size_t) c);

            quiet = available;
        }

        void route()
        {
            router->route (in, outs.data(), available, quiet, asleep);
            in.clear();
        }

        void clearOuts()
        {
            for (auto& o : outs)
                o.clear();
        }

        void play (MpeEncoder::Handle note, int key, int samplePos = 0, float strike = 0.8f)
        {
            stream.prepareNote (in, samplePos, note, key);
            stream.startNote (in, samplePos, note, strike);
        }

        void release (MpeEncoder::Handle note, int samplePos = 0)
        {
            stream.stopNote (in, samplePos, note, 0.5f);
        }

        std::vector<TimedMessage> out (int copy) const      { return collect (outs[(size_t) copy]); }

        /** The copy that was sent a Note On for this key, or -1. */
        int copyWithNoteOn (int key) const
        {
            for (int c = 0; c < CopyRouter::maxCopies; ++c)
                for (const auto& m : out (c))
                    if (m.message.isNoteOn() && m.message.getNoteNumber() == key)
                        return c;

            return -1;
        }

        bool allEmpty() const
        {
            for (const auto& o : outs)
                if (! o.isEmpty())
                    return false;

            return true;
        }

        MpeEncoder stream;
        std::unique_ptr<CopyRouter> router = std::make_unique<CopyRouter>();
        std::array<juce::MidiBuffer, CopyRouter::maxCopies> outs;
        juce::MidiBuffer in;
        CopyRouter::CopySet available, quiet, asleep;
    };

    int lastPitchWheel (const std::vector<TimedMessage>& messages)
    {
        int value = -1;

        for (const auto& m : messages)
            if (m.message.isPitchWheel())
                value = m.message.getPitchWheelValue();

        return value;
    }

    int indexOf (const std::vector<TimedMessage>& messages, const std::function<bool (const juce::MidiMessage&)>& predicate)
    {
        for (size_t i = 0; i < messages.size(); ++i)
            if (predicate (messages[i].message))
                return (int) i;

        return -1;
    }

    std::function<bool (const juce::MidiMessage&)> controller (int number, int value)
    {
        return [=] (const juce::MidiMessage& m)
        {
            return m.isControllerOfType (number) && m.getControllerValue() == value;
        };
    }

    bool isNoteOn (const juce::MidiMessage& m)      { return m.isNoteOn(); }
    bool isNoteOff (const juce::MidiMessage& m)     { return m.isNoteOff(); }
}

class CopyRouterTests : public juce::UnitTest
{
public:
    CopyRouterTests() : juce::UnitTest ("CopyRouter", "MPE Roll") {}

    void runTest() override
    {
        beginTest ("Each held note gets a copy of its own, played on the Plain MIDI channel");
        {
            for (const int midiChannel : { 1, 3 })
            {
                auto s = copySettings();
                s.singleChannel = midiChannel;
                auto rig = std::make_unique<Rig> (s);

                rig->play (1, 60);
                rig->play (2, 64);
                rig->route();

                expectEquals (rig->copyWithNoteOn (60), 0);
                expectEquals (rig->copyWithNoteOn (64), 1);
                expectEquals (rig->router->getNumCopiesPlaying(), 2);

                for (int c = 0; c < CopyRouter::maxCopies; ++c)
                {
                    const auto m = rig->out (c);
                    expectEquals (countIf (m, [midiChannel] (const juce::MidiMessage& msg)
                                           { return msg.getChannel() != midiChannel; }), 0);
                    expectEquals (countIf (m, isNoteOn), c < 2 ? 1 : 0);
                }
            }
        }

        beginTest ("A glide bends its own copy alone, scaled to the Plain MIDI bend range");
        {
            for (const float range : { 2.0f, 12.0f })
            {
                auto s = copySettings();
                s.masterBendRange = range;
                auto rig = std::make_unique<Rig> (s);

                rig->play (1, 60);
                rig->play (2, 64);
                rig->route();
                rig->clearOuts();

                rig->stream.setPitch (rig->in, 10, 1, 1.0f);
                rig->route();

                const auto m = rig->out (0);
                expectEquals ((int) m.size(), 1);
                expect (m.front().message.isPitchWheel());
                expectWithinAbsoluteError (m.front().message.getPitchWheelValue(),
                                           MpeEncoder::semitonesToPitchBend (1.0f, range), 1);
                expect (rig->out (1).empty());
            }
        }

        beginTest ("Zone pitch adds to a note's own, and zone controllers reach every copy in use");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->play (1, 60);
            rig->route();
            rig->clearOuts();

            rig->stream.setPitch (rig->in, 0, 1, 1.0f);
            rig->stream.setZonePitch (rig->in, 0, 0.5f);
            rig->route();

            expectWithinAbsoluteError (lastPitchWheel (rig->out (0)), MpeEncoder::semitonesToPitchBend (1.5f, 2.0f), 2);

            for (int c = 1; c < 4; ++c)
                expectWithinAbsoluteError (lastPitchWheel (rig->out (c)), MpeEncoder::semitonesToPitchBend (0.5f, 2.0f), 1);

            for (int c = 4; c < CopyRouter::maxCopies; ++c)
                expect (rig->out (c).empty());

            rig->clearOuts();
            rig->stream.setZoneController (rig->in, 5, 1, 0.5f);
            rig->stream.setZoneController (rig->in, 5, 64, 1.0f);
            rig->route();

            for (int c = 0; c < CopyRouter::maxCopies; ++c)
            {
                const auto m = rig->out (c);
                expectEquals (countIf (m, controller (1, 64)), c < 4 ? 1 : 0);
                expectEquals (countIf (m, controller (64, 127)), c < 4 ? 1 : 0);
                expectEquals (countIf (m, [] (const juce::MidiMessage& msg) { return msg.getChannel() != 1; }), 0);
            }
        }

        beginTest ("Allocation: quiet free copies first, then free ones still sounding, oldest ended first");
        {
            auto rig = std::make_unique<Rig> (copySettings());

            for (int i = 1; i <= 4; ++i)
                rig->play ((MpeEncoder::Handle) i, 59 + i);

            rig->route();

            for (int i = 1; i <= 4; ++i)
                expectEquals (rig->copyWithNoteOn (59 + i), i - 1);

            rig->release (3, 0);   // copy 2 ends first, but is still sounding
            rig->release (1, 1);   // copy 0
            rig->release (2, 2);   // copy 1
            rig->route();
            rig->quiet.reset (2);
            rig->clearOuts();

            rig->play (5, 70);
            rig->route();
            expectEquals (rig->copyWithNoteOn (70), 0);

            rig->clearOuts();
            rig->play (6, 71);
            rig->route();
            expectEquals (rig->copyWithNoteOn (71), 1);

            rig->clearOuts();
            rig->play (7, 72);
            rig->route();
            expectEquals (rig->copyWithNoteOn (72), 2);
            expectEquals (rig->router->getNumCopiesPlaying(), 4);
        }

        beginTest ("Allocation: a copy never used comes before one that has played");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->play (1, 60);
            rig->release (1, 1);
            rig->route();
            rig->clearOuts();

            rig->play (2, 62);
            rig->route();
            expectEquals (rig->copyWithNoteOn (62), 1);
            expect (! rig->router->isBusy (0));
        }

        beginTest ("Allocation: never a copy that isn't available or is beyond the copies in use");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->available.reset();
            rig->available.set (1);
            rig->available.set (3);
            rig->available.set (5);     // not in use: only 4 copies

            for (int i = 1; i <= 3; ++i)
                rig->play ((MpeEncoder::Handle) i, 59 + i);

            rig->route();

            expectEquals (rig->copyWithNoteOn (60), 1);
            expectEquals (rig->copyWithNoteOn (61), 3);
            expectEquals (rig->copyWithNoteOn (62), 1);     // shares the copy whose newest note is oldest

            for (const int c : { 0, 2, 4, 5 })
                expect (rig->out (c).empty());
        }

        beginTest ("Allocation: with no copy available a note isn't played, start to end");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            const auto all = rig->available;
            rig->available.reset();

            rig->play (1, 60);
            rig->route();
            expect (rig->allEmpty());

            rig->available = all;
            rig->stream.setPitch (rig->in, 0, 1, 2.0f);
            rig->stream.setPressure (rig->in, 0, 1, 0.5f);
            rig->release (1, 1);
            rig->route();
            expect (rig->allEmpty());
            expect (! rig->router->isBusy (0));

            rig->play (2, 62);
            rig->route();
            expectEquals (rig->copyWithNoteOn (62), 0);
        }

        beginTest ("Sustain pedal: a note it holds keeps its copy busy until the pedal goes up");
        {
            auto rig = std::make_unique<Rig> (copySettings (2));
            rig->stream.setZoneController (rig->in, 0, 64, 1.0f);
            rig->play (1, 60);
            rig->release (1, 1);
            rig->route();

            expect (rig->router->isBusy (0));
            expectEquals (rig->router->getNumCopiesPlaying(), 0);

            rig->clearOuts();
            rig->play (2, 62);
            rig->route();
            expectEquals (rig->copyWithNoteOn (62), 1);

            rig->stream.setZoneController (rig->in, 0, 64, 0.0f);
            rig->route();
            expect (! rig->router->isBusy (0));

            rig->clearOuts();
            rig->play (3, 64);
            rig->route();
            expectEquals (rig->copyWithNoteOn (64), 0);
        }

        beginTest ("Sustain pedal: a copy only the pedal holds comes before overflow");
        {
            auto s = copySettings (2);
            s.overflow = OutputSettings::Overflow::stealOldest;
            auto rig = std::make_unique<Rig> (s);
            rig->stream.setZoneController (rig->in, 0, 64, 1.0f);
            rig->play (1, 60);
            rig->release (1, 1);
            rig->play (2, 62, 2);
            rig->route();
            rig->clearOuts();

            rig->play (3, 64);
            rig->route();
            expectEquals (rig->copyWithNoteOn (64), 0);
            expect (rig->out (1).empty());      // the held note isn't stolen
        }

        beginTest ("Overflow: steal the oldest note");
        {
            auto s = copySettings (2);
            s.overflow = OutputSettings::Overflow::stealOldest;
            auto rig = std::make_unique<Rig> (s);

            rig->play (1, 60, 0);
            rig->play (2, 62, 1);
            rig->route();
            rig->clearOuts();

            rig->play (3, 64, 2);
            rig->route();

            const auto m = rig->out (0);
            const auto off = indexOf (m, [] (const juce::MidiMessage& msg) { return msg.isNoteOff() && msg.getNoteNumber() == 60; });
            const auto on = indexOf (m, [] (const juce::MidiMessage& msg) { return msg.isNoteOn() && msg.getNoteNumber() == 64; });
            expect (off >= 0 && on > off);
            expect (rig->out (1).empty());

            // The stolen note's later events go nowhere.
            rig->clearOuts();
            rig->stream.setPitch (rig->in, 3, 1, 2.0f);
            rig->release (1, 4);
            rig->route();
            expect (rig->allEmpty());
            expectEquals (rig->router->getNumCopiesPlaying(), 2);
        }

        beginTest ("Overflow: share the copy holding the fewest notes; its newest note bends it");
        {
            auto rig = std::make_unique<Rig> (copySettings (2));

            rig->play (1, 60, 0);
            rig->play (2, 62, 1);
            rig->route();
            rig->clearOuts();

            rig->play (3, 64, 2);
            rig->play (4, 65, 3);
            rig->route();
            expectEquals (rig->copyWithNoteOn (64), 0);     // a tie: copy 0's newest note is older
            expectEquals (rig->copyWithNoteOn (65), 1);
            expectEquals (countIf (rig->out (0), isNoteOff), 0);

            rig->clearOuts();
            rig->stream.setPitch (rig->in, 4, 1, 1.0f);    // not copy 0's newest
            rig->route();
            expect (rig->out (0).empty());

            rig->stream.setPitch (rig->in, 5, 3, 1.0f);
            rig->route();
            expectWithinAbsoluteError (lastPitchWheel (rig->out (0)), MpeEncoder::semitonesToPitchBend (1.0f, 2.0f), 1);
        }

        beginTest ("Every-copy keys go to every available copy as plain notes, waking those asleep");
        {
            auto rig = std::make_unique<Rig> (withEveryCopyKeys (copySettings()));
            rig->available.reset (3);
            rig->asleep.set (1);

            // Sleeping copies are sent zone values too; the caller throws them away.
            rig->stream.setZoneController (rig->in, 0, 1, 0.5f);
            rig->route();
            rig->clearOuts();

            rig->play (1, 12, 20, 0.5f);
            rig->route();

            for (const int c : { 0, 2 })
            {
                const auto m = rig->out (c);
                expectEquals ((int) m.size(), 1);
                expect (m.front().message.isNoteOn() && m.front().message.getNoteNumber() == 12);
                expectEquals ((int) m.front().message.getVelocity(), 64);
                expectEquals (m.front().message.getChannel(), 1);
                expectEquals (m.front().sample, 20);
            }

            const auto woken = rig->out (1);
            expectEquals ((int) woken.size(), 2);
            expect (controller (1, 64) (woken[0].message));
            expect (woken[1].message.isNoteOn());
            expectEquals (woken[0].sample, 20);
            expect (! rig->asleep[1]);
            expect (rig->out (3).empty());

            for (int c = 0; c < 3; ++c)
                expect (rig->router->isBusy (c));

            expect (! rig->router->isBusy (3));
            expectEquals (rig->router->getNumCopiesPlaying(), 0);

            // A note played meanwhile gets a copy of its own, as if the key weren't there.
            rig->clearOuts();
            rig->play (2, 60, 25);
            rig->route();
            expectEquals (rig->copyWithNoteOn (60), 0);
            expectEquals (countIf (rig->out (1), isNoteOn), 0);

            // No expression for an every-copy key, and its Note Off goes where its Note On went.
            rig->clearOuts();
            rig->available.set (3);
            rig->stream.setPitch (rig->in, 30, 1, 2.0f);
            rig->release (1, 31);
            rig->route();

            for (int c = 0; c < 3; ++c)
            {
                const auto m = rig->out (c);
                expectEquals ((int) m.size(), 1);
                expect (m.front().message.isNoteOff() && m.front().message.getNoteNumber() == 12);
            }

            expect (rig->out (3).empty());
            expect (! rig->router->isBusy (1));
            expect (! rig->router->isBusy (2));
        }

        beginTest ("A sleeping copy is sent the zone state it missed before the note that wakes it");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->asleep = rig->available;

            rig->stream.setZonePitch (rig->in, 0, 1.0f);
            rig->stream.setZoneController (rig->in, 0, 1, 0.5f);
            rig->stream.setZoneController (rig->in, 0, 64, 1.0f);
            rig->route();
            rig->clearOuts();

            rig->play (1, 60, 37);
            rig->route();

            const auto m = rig->out (0);
            expect (m.size() > 3);
            expect (m[0].message.isPitchWheel());
            expectWithinAbsoluteError (m[0].message.getPitchWheelValue(), MpeEncoder::semitonesToPitchBend (1.0f, 2.0f), 1);
            expect (controller (1, 64) (m[1].message));
            expect (controller (64, 127) (m[2].message));
            expect (m.back().message.isNoteOn());

            for (const auto& t : m)
                expectEquals (t.sample, 37);

            expect (! rig->asleep[0]);
            expect (rig->asleep[1] && rig->asleep[2] && rig->asleep[3]);

            for (int c = 1; c < CopyRouter::maxCopies; ++c)
                expect (rig->out (c).empty());
        }

        beginTest ("Dropping a copy forgets its notes; introducing it sends its bend range and the zone state");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->stream.setZoneController (rig->in, 0, 1, 0.5f);
            rig->play (1, 60);
            rig->route();
            rig->clearOuts();

            rig->router->dropCopy (0);
            expect (! rig->router->isBusy (0));
            expectEquals (rig->router->getNumCopiesPlaying(), 0);

            rig->stream.setPitch (rig->in, 0, 1, 2.0f);
            rig->release (1, 1);
            rig->route();
            expect (rig->allEmpty());

            rig->router->introduce (0, rig->outs[0], 5);
            const auto m = rig->out (0);
            expectEquals ((int) m.size(), 7);
            expect (controller (101, 0) (m[0].message));
            expect (controller (100, 0) (m[1].message));
            expect (controller (6, 2) (m[2].message));
            expect (controller (38, 0) (m[3].message));
            expect (controller (1, 64) (m.back().message));
            expectEquals (m.back().sample, 5);

            rig->clearOuts();
            rig->play (2, 62);
            rig->route();
            expectEquals (rig->copyWithNoteOn (62), 0);
        }

        beginTest ("Without sending configuration, introducing a copy sends the zone state only");
        {
            auto s = copySettings();
            s.sendConfiguration = false;
            auto rig = std::make_unique<Rig> (s);
            rig->stream.setZoneController (rig->in, 0, 1, 0.5f);
            rig->route();
            rig->clearOuts();

            rig->router->introduce (2, rig->outs[2], 0);
            const auto m = rig->out (2);
            expectEquals ((int) m.size(), 1);
            expect (controller (1, 64) (m[0].message));

            rig->clearOuts();
            rig->router->writeConfiguration (rig->outs.data(), 0);
            expect (rig->allEmpty());
        }

        beginTest ("Configuration goes to every copy in use");
        {
            auto s = copySettings();
            s.masterBendRange = 12.0f;
            auto rig = std::make_unique<Rig> (s);
            rig->router->writeConfiguration (rig->outs.data(), 0);

            for (int c = 0; c < CopyRouter::maxCopies; ++c)
                expectEquals (countIf (rig->out (c), controller (6, 12)), c < 4 ? 1 : 0);
        }

        beginTest ("stopAll ends every note and every-copy key; reset forgets everything silently");
        {
            auto rig = std::make_unique<Rig> (withEveryCopyKeys (copySettings()));
            rig->play (1, 60);
            rig->play (2, 64);
            rig->play (3, 12);
            rig->route();
            rig->clearOuts();

            rig->router->stopAll (rig->outs.data(), 7);

            for (int c = 0; c < CopyRouter::maxCopies; ++c)
            {
                const auto m = rig->out (c);
                const auto ownKey = c == 0 ? 60 : c == 1 ? 64 : -1;
                expectEquals (countIf (m, [] (const juce::MidiMessage& msg) { return msg.isNoteOff() && msg.getNoteNumber() == 12; }),
                              c < 4 ? 1 : 0);
                expectEquals (countIf (m, [ownKey] (const juce::MidiMessage& msg) { return msg.isNoteOff() && msg.getNoteNumber() == ownKey; }),
                              ownKey >= 0 ? 1 : 0);

                for (const auto& t : m)
                    expectEquals (t.sample, 7);

                expect (! rig->router->isBusy (c));
            }

            // The stream's later events for those notes are ignored; new notes play as usual.
            rig->clearOuts();
            rig->stream.setPitch (rig->in, 0, 1, 1.0f);
            rig->release (1, 1);
            rig->release (2, 1);
            rig->release (3, 1);
            rig->route();
            expect (rig->allEmpty());

            rig->play (4, 67, 2);
            rig->route();
            expect (rig->copyWithNoteOn (67) >= 0);
            expectEquals (rig->router->getNumCopiesPlaying(), 1);

            rig->stream.setZoneController (rig->in, 3, 1, 0.5f);
            rig->route();
            rig->clearOuts();

            rig->router->reset();
            expect (rig->allEmpty());
            expectEquals (rig->router->getNumCopiesPlaying(), 0);
            expect (! rig->router->isBusy (0));

            // The zone values went with it: a copy woken now is sent none.
            rig->asleep = rig->available;
            rig->play (5, 69, 4);
            rig->route();
            const auto copy = rig->copyWithNoteOn (69);
            expect (copy >= 0);

            if (copy >= 0)
                expectEquals (countIf (rig->out (copy), [] (const juce::MidiMessage& msg) { return msg.isControllerOfType (1); }), 0);
        }

        beginTest ("Changing how the copies are played stops the notes first; the new bend range applies after");
        {
            auto s = copySettings();
            auto rig = std::make_unique<Rig> (s);
            rig->stream.setZonePitch (rig->in, 0, 1.0f);
            rig->play (1, 60);
            rig->route();
            rig->clearOuts();

            auto wider = s;
            wider.masterBendRange = 12.0f;
            rig->router->setSettings (wider, rig->outs.data(), 9);

            const auto m = rig->out (0);
            const auto off = indexOf (m, [] (const juce::MidiMessage& msg) { return msg.isNoteOff() && msg.getNoteNumber() == 60; });
            expect (off >= 0);

            if (off >= 0)
                expectEquals (m[(size_t) off].sample, 9);

            // Every copy in use is sent the zone pitch again, in the new range.
            for (int c = 0; c < 4; ++c)
                expectWithinAbsoluteError (lastPitchWheel (rig->out (c)), MpeEncoder::semitonesToPitchBend (1.0f, 12.0f), 1);

            // The stream follows, as MPE Roll's own encoder does.
            rig->stream.stopAllNotes (rig->in, 10);
            rig->stream.setSettings (wider);
            rig->route();
            rig->clearOuts();

            rig->play (2, 62, 11);
            rig->stream.setPitch (rig->in, 12, 2, 1.0f);
            rig->route();
            const auto copy = rig->copyWithNoteOn (62);
            expect (copy >= 0);

            if (copy >= 0)
                expectWithinAbsoluteError (lastPitchWheel (rig->out (copy)), MpeEncoder::semitonesToPitchBend (2.0f, 12.0f), 2);
        }

        beginTest ("Changing which keys go to every copy stops the notes first");
        {
            auto s = copySettings();
            auto rig = std::make_unique<Rig> (s);
            rig->play (1, 60);
            rig->route();
            rig->clearOuts();

            rig->router->setSettings (withEveryCopyKeys (s), rig->outs.data(), 3);
            expectEquals (countIf (rig->out (0), isNoteOff), 1);
            expect (! rig->router->isBusy (0));
        }

        beginTest ("Changing the number of copies keeps notes playing, and drops those of copies taken away");
        {
            auto s = copySettings();
            auto rig = std::make_unique<Rig> (s);

            for (int i = 1; i <= 4; ++i)
                rig->play ((MpeEncoder::Handle) i, 59 + i);

            rig->route();
            rig->clearOuts();

            auto more = s;
            more.copies = 6;
            rig->router->setSettings (more, rig->outs.data(), 0);
            expectEquals (rig->router->getNumCopies(), 6);
            expect (rig->allEmpty());
            expectEquals (rig->router->getNumCopiesPlaying(), 4);

            auto fewer = s;
            fewer.copies = 2;
            rig->router->setSettings (fewer, rig->outs.data(), 0);
            expectEquals (rig->router->getNumCopies(), 2);
            expect (rig->allEmpty());
            expectEquals (rig->router->getNumCopiesPlaying(), 2);
            expect (! rig->router->isBusy (2) && ! rig->router->isBusy (3));

            // The notes of the copies taken away stay silent.
            rig->stream.setPitch (rig->in, 0, 3, 1.0f);
            rig->release (3, 1);
            rig->release (4, 1);
            rig->route();
            expect (rig->allEmpty());

            auto tooMany = s;
            tooMany.copies = 1000;
            rig->router->setSettings (tooMany, rig->outs.data(), 0);
            expectEquals (rig->router->getNumCopies(), CopyRouter::maxCopies);
        }

        beginTest ("Sample positions are kept");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->play (1, 60, 100);
            rig->route();

            const auto m = rig->out (0);
            expect (! m.empty());

            for (const auto& t : m)
                expectEquals (t.sample, 100);

            rig->clearOuts();
            rig->stream.setPitch (rig->in, 250, 1, 0.5f);
            rig->release (1, 300);
            rig->route();

            const auto later = rig->out (0);
            expectEquals ((int) later.size(), 2);
            expectEquals (later.front().sample, 250);
            expect (later.back().message.isNoteOff());
            expectEquals (later.back().sample, 300);
        }

        beginTest ("Velocity reaches the copy as the stream has it");
        {
            auto rig = std::make_unique<Rig> (copySettings());
            rig->play (1, 60, 0, 0.5f);
            rig->play (2, 62, 0, 1.0f);
            rig->play (3, 64, 0, 0.001f);
            rig->route();

            auto velocityOf = [&rig] (int c)
            {
                const auto m = rig->out (c);
                const auto i = indexOf (m, isNoteOn);
                return i >= 0 ? (int) m[(size_t) i].message.getVelocity() : -1;
            };

            expectEquals (velocityOf (0), 64);
            expectEquals (velocityOf (1), 127);
            expectEquals (velocityOf (2), 1);
        }

        beginTest ("A note's own timbre reaches its copy; the stream's default for a note without one doesn't");
        {
            auto rig = std::make_unique<Rig> (copySettings (2));

            rig->stream.prepareNote (rig->in, 0, 1, 60);
            rig->stream.setTimbre (rig->in, 0, 1, 0.9f);
            rig->stream.startNote (rig->in, 0, 1, 0.8f);
            rig->route();

            const auto own = rig->out (0);
            const auto timbre = indexOf (own, controller (74, MpeEncoder::toSevenBit (0.9f)));
            expect (timbre >= 0 && timbre < indexOf (own, isNoteOn));

            rig->play (2, 62);
            rig->route();
            expectEquals (countIf (rig->out (1), [] (const juce::MidiMessage& m) { return m.isControllerOfType (74); }), 0);

            // The copy that had a timbre gets the usual one back for its next note without one.
            rig->release (1, 1);
            rig->route();
            rig->clearOuts();
            rig->play (3, 64, 2);
            rig->route();
            expectEquals (rig->copyWithNoteOn (64), 0);
            expectEquals (countIf (rig->out (0), controller (74, 64)), 1);
        }

        beginTest ("Keys to every copy are written with MPE Roll's note names, and read back");
        {
            // Middle C is C5, as on MPE Roll's keyboard and in FL Studio.
            expect (parseKeySet ("C5") == std::bitset<128> {}.set (60));
            expect (parseKeySet ("60") == parseKeySet ("C5"));
            expect (parseKeySet ("C#5, Db5") == std::bitset<128> {}.set (61));
            expectEquals (formatKeySet (parseKeySet ("C0-B2")), juce::String ("C0-B2"));
            expectEquals ((int) parseKeySet ("C0-B2").count(), 36);
            expectEquals ((int) parseKeySet ("36 to 47").count(), 12);

            // Output Arcade: everything below its loop keys, and the black keys among them.
            const auto arcade = parseKeySet ("C0-B3, black C4-C6");
            expectEquals ((int) arcade.count(), 48 + 10);
            expect (arcade[49] && arcade[70] && ! arcade[48] && ! arcade[72] && ! arcade[71]);
            expectEquals (formatKeySet (arcade), juce::String ("C0-B3, black C#4-A#5"));
            expect (parseKeySet (formatKeySet (arcade)) == arcade);

            // Two black keys alone are named one by one; nonsense is left out.
            expectEquals (formatKeySet (parseKeySet ("C#4, D#4")), juce::String ("C#4, D#4"));
            expect (parseKeySet ("H2, C99, -3, , white C5-C6").count() == 8);
            expect (formatKeySet ({}).isEmpty() && parseKeySet ({}).none());

            // Any set survives the round trip.
            juce::Random random (7);

            for (int i = 0; i < 200; ++i)
            {
                std::bitset<128> keys;

                for (int k = 0; k < 128; ++k)
                    keys.set ((size_t) k, random.nextInt (3) == 0);

                expect (parseKeySet (formatKeySet (keys)) == keys, formatKeySet (keys));
            }
        }

        beginTest ("Copy mode's stream: MPE on 15 channels, bent within the copies' range");
        {
            auto s = copySettings();
            s.masterBendRange = 24.0f;
            s.zones = { 3, 4 };
            s.mpePlus = true;
            const auto stream = copyStreamSettings (s);
            expect (stream.mode == OutputSettings::Mode::mpe);
            expect (stream.zones == ZoneLayout { 15, 0 });
            expectEquals (stream.memberBendRange, 24.0f);
            expectEquals (stream.masterBendRange, 24.0f);
            expect (! stream.mpePlus);

            const auto play = copyPlaySettings (s);
            expect (play.mode == OutputSettings::Mode::singleChannel && ! play.mpePlus);

            // Settings in other modes are left alone.
            OutputSettings mpe;
            mpe.zones = { 3, 4 };
            expect (copyStreamSettings (mpe) == mpe);

            // The settings are saved with the project.
            EngineSettings e;
            e.output = s;
            e.output.copies = 12;
            e.output.everyCopyKeys = parseKeySet ("C0-B2");
            e.output.copiesSleep = false;
            e.output.copiesUseCores = false;
            e.output.copiesFollow = false;
            const auto back = EngineSettings::fromXml (e.toXml().get());
            expect (back.output.mode == OutputSettings::Mode::copies && back.output.copies == 12);
            expect (back.output.everyCopyKeys == e.output.everyCopyKeys);
            expect (! back.output.copiesSleep && ! back.output.copiesUseCores && ! back.output.copiesFollow);
        }
    }
};

static CopyRouterTests copyRouterTests;

} // namespace mperoll
