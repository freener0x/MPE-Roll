#include "TestHelpers.h"
#include "core/NoteTools.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace mperoll
{

using namespace test;

class NoteToolsTests : public juce::UnitTest
{
public:
    NoteToolsTests() : juce::UnitTest ("NoteTools", "MPE Roll") {}

    static bool isSorted (const MpeClip& clip)
    {
        for (size_t i = 1; i < clip.notes.size(); ++i)
            if (clip.notes[i - 1].start > clip.notes[i].start)
                return false;

        return true;
    }

    static std::vector<int> keysOf (const MpeClip& clip)
    {
        std::vector<int> keys;

        for (const auto& n : clip.notes)
            keys.push_back (n.key);

        return keys;
    }

    /** A C major triad, each note with its own velocity so the arpeggio's source can be told apart. */
    static std::set<uint32_t> addTriad (MpeClip& clip, double start = 0.0, double length = 1.0)
    {
        return { clip.addNote (makeNote (60, start, length, 0.9f)),
                 clip.addNote (makeNote (64, start, length, 0.8f)),
                 clip.addNote (makeNote (67, start, length, 0.7f)) };
    }

    void runTest() override
    {
        beginTest ("Targets are the given ids in clip order, or every note");
        {
            MpeClip clip;
            const auto late = clip.addNote (makeNote (60, 2.0, 1.0));
            const auto early = clip.addNote (makeNote (62, 0.0, 1.0));
            const auto middle = clip.addNote (makeNote (64, 1.0, 1.0));

            expect ((notetools::targets (clip, {}) == std::vector<uint32_t> { early, middle, late }));
            expect ((notetools::targets (clip, { late, early }) == std::vector<uint32_t> { early, late }));
            expect ((notetools::targets (clip, { late, 999u }) == std::vector<uint32_t> { late }));
            expect (notetools::targets (clip, { 999u }).empty());
        }

        beginTest ("Split keeps each piece's part of a curve, re-timed to its start");
        {
            MpeClip clip;
            auto glide = makeNote (60, 1.0, 1.0, 0.7f);
            glide.lift = 0.3f;
            glide.lanes.pitch = { { 0.0, 0.0f }, { 1.0, 2.0f } };
            const auto id = clip.addNote (glide);

            expectEquals ((int) notetools::splitNote (clip, id, 0.0), 0);
            expectEquals ((int) notetools::splitNote (clip, id, -0.5), 0);
            expectEquals ((int) notetools::splitNote (clip, id, 1.0), 0);
            expectEquals ((int) notetools::splitNote (clip, id, 1.5), 0);
            expectEquals ((int) notetools::splitNote (clip, 999u, 0.5), 0);
            expectEquals ((int) clip.notes.size(), 1);

            const auto rightId = notetools::splitNote (clip, id, 0.5);
            expect (rightId != 0 && rightId != id);
            expectEquals ((int) clip.notes.size(), 2);

            const auto* left = clip.findNote (id);
            const auto* right = clip.findNote (rightId);
            expectEquals (left->start, 1.0);
            expectEquals (left->length, 0.5);
            expectEquals (right->start, 1.5);
            expectEquals (right->length, 0.5);

            expectEquals ((int) left->lanes.pitch.size(), 2);
            expectEquals (left->lanes.pitch.getPoints().back().beat, 0.5);
            expectWithinAbsoluteError (left->lanes.pitch.valueAt (0.0, 9.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (left->lanes.pitch.valueAt (0.5, 9.0f), 1.0f, 1.0e-6f);

            expectEquals ((int) right->lanes.pitch.size(), 2);
            expectEquals (right->lanes.pitch.getPoints().front().beat, 0.0);
            expectEquals (right->lanes.pitch.getPoints().back().beat, 0.5);
            expectWithinAbsoluteError (right->lanes.pitch.valueAt (0.0, 9.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (right->lanes.pitch.valueAt (0.5, 9.0f), 2.0f, 1.0e-6f);

            expectEquals (right->key, 60);
            expectEquals (right->strike, 0.7f);
            expectEquals (right->lift, 0.3f);
            expectEquals (left->strike, 0.7f);
            expect (isSorted (clip));
        }

        beginTest ("Slice cuts only the notes the line passes through");
        {
            MpeClip clip;
            const auto crossing = clip.addNote (makeNote (60, 0.0, 2.0));
            const auto starting = clip.addNote (makeNote (62, 1.0, 0.5));
            const auto later = clip.addNote (makeNote (64, 3.0, 1.0));

            const auto pieces = notetools::sliceAt (clip, {}, 1.0);
            expectEquals ((int) pieces.size(), 1);
            expectEquals ((int) clip.notes.size(), 4);
            expectEquals (clip.findNote (crossing)->length, 1.0);
            expectEquals (clip.findNote (starting)->length, 0.5);
            expectEquals (clip.findNote (later)->length, 1.0);

            const auto* piece = clip.findNote (*pieces.begin());
            expectEquals (piece->key, 60);
            expectEquals (piece->start, 1.0);
            expectEquals (piece->length, 1.0);

            expect (notetools::sliceAt (clip, { later }, 1.0).empty());
            expect (notetools::sliceAt (clip, { starting }, 3.5).empty());
            expectEquals ((int) notetools::sliceAt (clip, { later }, 3.5).size(), 1);
            expectEquals ((int) clip.notes.size(), 5);
            expect (isSorted (clip));
        }

        beginTest ("Chop makes pieces that carry their part of a pressure curve");
        {
            MpeClip clip;
            auto swell = makeNote (60, 0.0, 1.0);
            swell.lanes.pressure = { { 0.0, 0.0f }, { 1.0, 1.0f } };
            const auto id = clip.addNote (swell);

            notetools::ChopOptions options;
            options.length = 0.25;
            options.gate = 1.0f;

            const auto pieces = notetools::chop (clip, {}, options);
            expectEquals ((int) pieces.size(), 4);
            expect (pieces.count (id) == 1);
            expectEquals ((int) clip.notes.size(), 4);

            for (int i = 0; i < 4; ++i)
            {
                const auto& piece = clip.notes[(size_t) i];
                expect (pieces.count (piece.id) == 1);
                expectEquals (piece.start, i * 0.25);
                expectEquals (piece.length, 0.25);
                expectWithinAbsoluteError (piece.lanes.pressure.valueAt (0.0, 9.0f), (float) i * 0.25f, 1.0e-6f);
                expectWithinAbsoluteError (piece.lanes.pressure.valueAt (0.25, 9.0f), (float) (i + 1) * 0.25f, 1.0e-6f);
            }

            // The remainder becomes the last piece, the gate shortens every piece, and a note no
            // longer than a piece is only gated.
            MpeClip mixed;
            mixed.addNote (makeNote (60, 0.0, 1.1));
            mixed.addNote (makeNote (64, 2.0, 0.2));
            options.gate = 0.5f;

            const auto gated = notetools::chop (mixed, {}, options);
            expectEquals ((int) gated.size(), 6);
            expectEquals ((int) mixed.notes.size(), 6);
            expectWithinAbsoluteError (mixed.notes[0].length, 0.125, 1.0e-9);
            expectWithinAbsoluteError (mixed.notes[4].start, 1.0, 1.0e-9);
            expectWithinAbsoluteError (mixed.notes[4].length, 0.05, 1.0e-9);
            expectEquals (mixed.notes[5].start, 2.0);
            expectWithinAbsoluteError (mixed.notes[5].length, 0.1, 1.0e-9);
            expect (isSorted (mixed));

            // Only the chosen note is chopped.
            MpeClip chosen;
            const auto chopped = chosen.addNote (makeNote (60, 0.0, 1.0));
            const auto kept = chosen.addNote (makeNote (62, 1.0, 1.0));
            options.gate = 1.0f;
            expectEquals ((int) notetools::chop (chosen, { chopped }, options).size(), 4);
            expectEquals ((int) chosen.notes.size(), 5);
            expectEquals (chosen.findNote (kept)->length, 1.0);
        }

        beginTest ("Arpeggiate cycles a chord's pitches in each pattern");
        {
            using Pattern = notetools::ArpeggiateOptions::Pattern;
            notetools::ArpeggiateOptions options;
            options.step = 0.25;

            auto run = [&] (Pattern pattern, int range, const std::vector<int>& expectedKeys)
            {
                MpeClip clip;
                const auto chord = addTriad (clip);
                options.pattern = pattern;
                options.range = range;

                const auto made = notetools::arpeggiate (clip, {}, options);
                expectEquals ((int) made.size(), (int) expectedKeys.size());
                expectEquals ((int) clip.notes.size(), (int) expectedKeys.size());
                expect (keysOf (clip) == expectedKeys);

                for (auto id : chord)
                    expect (clip.findNote (id) == nullptr && made.count (id) == 0);

                for (size_t i = 0; i < clip.notes.size(); ++i)
                {
                    expect (made.count (clip.notes[i].id) == 1);
                    expectEquals (clip.notes[i].start, (double) i * options.step);
                    expectEquals (clip.notes[i].length, options.step);
                    expect (! clip.notes[i].lanes.hasAnyData());
                }

                expect (isSorted (clip));
                return clip;
            };

            const auto up = run (Pattern::up, 1, { 60, 64, 67, 60 });
            expectEquals (up.notes[0].strike, 0.9f);
            expectEquals (up.notes[1].strike, 0.8f);
            expectEquals (up.notes[2].strike, 0.7f);
            expectEquals (up.notes[3].strike, 0.9f);

            run (Pattern::down, 1, { 67, 64, 60, 67 });
            run (Pattern::upDown, 1, { 60, 64, 67, 64 });
            run (Pattern::downUp, 1, { 67, 64, 60, 64 });

            options.step = 0.125;
            const auto wide = run (Pattern::up, 2, { 60, 64, 67, 72, 76, 79, 60, 64 });
            expectEquals (wide.notes[3].strike, 0.9f);   // the C an octave up plays like the C
            expectEquals (wide.notes[5].strike, 0.7f);
            run (Pattern::upDown, 2, { 60, 64, 67, 72, 76, 79, 76, 72 });
            options.step = 0.25;
            options.range = 1;
            options.pattern = Pattern::up;

            // The gate shortens each step, a lone note is repeated, and overlapping notes make one
            // chord lasting from the first start to the last end.
            MpeClip gated;
            addTriad (gated);
            options.gate = 0.5f;
            notetools::arpeggiate (gated, {}, options);
            expectEquals ((int) gated.notes.size(), 4);
            expectEquals (gated.notes[0].length, 0.125);
            options.gate = 1.0f;

            MpeClip lone;
            lone.addNote (makeNote (72, 2.0, 1.0));
            options.step = 0.5;
            expectEquals ((int) notetools::arpeggiate (lone, {}, options).size(), 2);
            expect ((keysOf (lone) == std::vector<int> { 72, 72 }));
            expectEquals (lone.notes[1].start, 2.5);

            MpeClip overlapping;
            overlapping.addNote (makeNote (60, 0.0, 1.0));
            overlapping.addNote (makeNote (64, 0.5, 1.0));
            notetools::arpeggiate (overlapping, {}, options);
            expect ((keysOf (overlapping) == std::vector<int> { 60, 64, 60 }));
            expectEquals (overlapping.notes[2].end(), 1.5);
            options.step = 0.25;

            // Random picks from the chord only, and the same seed picks the same.
            options.pattern = Pattern::random;
            options.seed = 42;
            MpeClip randomA, randomB;
            addTriad (randomA, 0.0, 4.0);
            addTriad (randomB, 0.0, 4.0);
            notetools::arpeggiate (randomA, {}, options);
            notetools::arpeggiate (randomB, {}, options);
            expect (randomA == randomB);
            expectEquals ((int) randomA.notes.size(), 16);

            for (const auto& n : randomA.notes)
                expect (n.key == 60 || n.key == 64 || n.key == 67);

            // Only the given notes are arpeggiated.
            MpeClip partial;
            const auto chord = addTriad (partial);
            const auto bystander = partial.addNote (makeNote (48, 0.0, 1.0));
            options.pattern = Pattern::up;
            notetools::arpeggiate (partial, chord, options);
            expectEquals ((int) partial.notes.size(), 5);
            expect (partial.findNote (bystander) != nullptr);
            expect (isSorted (partial));
        }

        beginTest ("Strum spreads notes starting together in pitch order");
        {
            MpeClip clip;
            const auto c = clip.addNote (makeNote (60, 0.0, 1.0, 0.8f));
            const auto e = clip.addNote (makeNote (64, 0.0, 1.0, 0.8f));
            const auto g = clip.addNote (makeNote (67, 0.0, 1.0, 0.8f));
            const auto lone = clip.addNote (makeNote (72, 2.0, 1.0, 0.8f));

            notetools::StrumOptions options;
            options.time = 0.1;
            options.upwards = true;
            options.velocityFall = 0.5f;
            options.keepEnds = true;

            const auto touched = notetools::strum (clip, {}, options);
            expect ((touched == std::set<uint32_t> { c, e, g }));
            expectEquals (clip.findNote (c)->start, 0.0);
            expectWithinAbsoluteError (clip.findNote (e)->start, 0.1, 1.0e-9);
            expectWithinAbsoluteError (clip.findNote (g)->start, 0.2, 1.0e-9);
            expectEquals (clip.findNote (c)->length, 1.0);
            expectWithinAbsoluteError (clip.findNote (e)->length, 0.9, 1.0e-9);
            expectWithinAbsoluteError (clip.findNote (g)->length, 0.8, 1.0e-9);
            expectWithinAbsoluteError (clip.findNote (g)->end(), 1.0, 1.0e-9);
            expectWithinAbsoluteError (clip.findNote (c)->strike, 0.8f, 1.0e-6f);
            expectWithinAbsoluteError (clip.findNote (e)->strike, 0.6f, 1.0e-6f);
            expectWithinAbsoluteError (clip.findNote (g)->strike, 0.4f, 1.0e-6f);
            expectEquals (clip.findNote (lone)->start, 2.0);
            expectEquals (clip.findNote (lone)->length, 1.0);
            expect (isSorted (clip));

            MpeClip down;
            const auto c2 = down.addNote (makeNote (60, 0.0, 1.0));
            const auto e2 = down.addNote (makeNote (64, 0.0, 1.0));
            const auto g2 = down.addNote (makeNote (67, 0.0, 1.0));
            options.upwards = false;
            options.keepEnds = false;
            options.velocityFall = 0.0f;
            notetools::strum (down, { c2, e2, g2 }, options);
            expectEquals (down.findNote (g2)->start, 0.0);
            expectWithinAbsoluteError (down.findNote (e2)->start, 0.1, 1.0e-9);
            expectWithinAbsoluteError (down.findNote (c2)->start, 0.2, 1.0e-9);
            expectEquals (down.findNote (c2)->length, 1.0);
            expectEquals (down.findNote (c2)->strike, 0.8f);
            expect ((keysOf (down) == std::vector<int> { 67, 64, 60 }));

            // A note pushed past its own end keeps the shortest length rather than vanishing.
            MpeClip tiny;
            const auto low = tiny.addNote (makeNote (60, 0.0, 0.05));
            const auto high = tiny.addNote (makeNote (64, 0.0, 0.05));
            options.upwards = true;
            options.keepEnds = true;
            notetools::strum (tiny, {}, options);
            expectEquals (tiny.findNote (low)->length, 0.05);
            expectWithinAbsoluteError (tiny.findNote (high)->length, 1.0e-3, 1.0e-12);
        }

        beginTest ("Flam puts grace notes before each note");
        {
            MpeClip clip;
            auto target = makeNote (60, 1.0, 1.0, 0.8f);
            target.lift = 0.4f;
            target.lanes.pitch = { { 0.0, 1.0f } };
            const auto main = clip.addNote (target);
            const auto atStart = clip.addNote (makeNote (64, 0.0, 1.0, 0.8f));

            notetools::FlamOptions options;
            options.time = 0.0625;
            options.velocity = 0.5f;
            options.count = 2;

            const auto graces = notetools::flam (clip, {}, options);
            expectEquals ((int) graces.size(), 2);
            expectEquals ((int) clip.notes.size(), 4);
            expectEquals (clip.findNote (main)->start, 1.0);
            expectEquals (clip.findNote (atStart)->start, 0.0);

            std::set<double> starts;

            for (auto id : graces)
            {
                const auto* grace = clip.findNote (id);
                expect (grace != nullptr && id != main && id != atStart);
                expectEquals (grace->key, 60);
                expectWithinAbsoluteError (grace->strike, 0.4f, 1.0e-6f);
                expectEquals (grace->lift, 0.4f);
                expectWithinAbsoluteError (grace->length, 0.05625, 1.0e-9);
                expect (! grace->lanes.hasAnyData());
                starts.insert (grace->start);
            }

            expect ((starts == std::set<double> { 0.875, 0.9375 }));
            expect (isSorted (clip));

            expect (notetools::flam (clip, { atStart }, options).empty());
            expectEquals ((int) clip.notes.size(), 4);
        }

        beginTest ("Glue joins touching notes of one pitch and continues the curve");
        {
            MpeClip clip;
            auto first = makeNote (60, 0.0, 1.0);
            first.lanes.pressure = { { 0.0, 0.0f }, { 1.0, 1.0f } };
            auto second = makeNote (60, 1.0, 1.0);
            second.lanes.pressure = { { 0.0, 1.0f }, { 1.0, 0.0f } };
            second.lanes.get (LaneId::cc (74)) = { { 0.5, 0.25f } };

            const auto a = clip.addNote (first);
            const auto b = clip.addNote (second);
            const auto otherKey = clip.addNote (makeNote (62, 1.0, 1.0));
            const auto apart = clip.addNote (makeNote (60, 3.0, 1.0));

            const auto survivors = notetools::glue (clip, {}, {});
            expect ((survivors == std::set<uint32_t> { a, otherKey, apart }));
            expectEquals ((int) clip.notes.size(), 3);
            expect (clip.findNote (b) == nullptr);

            const auto* glued = clip.findNote (a);
            expectEquals (glued->start, 0.0);
            expectEquals (glued->length, 2.0);
            expectEquals ((int) glued->lanes.pressure.size(), 3);
            expectWithinAbsoluteError (glued->lanes.pressure.valueAt (0.5, 9.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (glued->lanes.pressure.valueAt (1.0, 9.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (glued->lanes.pressure.valueAt (1.5, 9.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (glued->lanes.pressure.valueAt (2.0, 9.0f), 0.0f, 1.0e-6f);

            const auto* cc = glued->lanes.find (LaneId::cc (74));
            expect (cc != nullptr && cc->size() == 1);
            expectWithinAbsoluteError (cc->getPoints().front().beat, 1.5, 1.0e-9);
            expectEquals (clip.findNote (otherKey)->length, 1.0);
            expect (isSorted (clip));

            notetools::GlueOptions wide;
            wide.gap = 1.0;
            notetools::glue (clip, {}, wide);
            expectEquals ((int) clip.notes.size(), 2);
            expectEquals (clip.findNote (a)->length, 4.0);

            // Overlapping notes end where the later one did, and only the chosen notes join.
            MpeClip overlapping;
            const auto under = overlapping.addNote (makeNote (60, 0.0, 2.0));
            overlapping.addNote (makeNote (60, 1.0, 0.5));
            const auto over = overlapping.addNote (makeNote (60, 1.5, 2.0));
            const auto left = overlapping.addNote (makeNote (60, 3.0, 1.0));
            notetools::glue (overlapping, { under, over }, {});
            expectEquals ((int) overlapping.notes.size(), 3);
            expectEquals (overlapping.findNote (under)->length, 3.5);
            expect (overlapping.findNote (left) != nullptr);
        }

        beginTest ("Legato reaches the next start, chords together");
        {
            MpeClip clip;
            const auto a = clip.addNote (makeNote (60, 0.0, 0.5));
            const auto b = clip.addNote (makeNote (64, 0.0, 0.25));
            const auto c = clip.addNote (makeNote (62, 1.0, 0.5));
            auto long1 = makeNote (65, 2.0, 2.0);
            long1.lanes.pitch = { { 0.0, 0.0f }, { 2.0, 2.0f } };
            const auto d = clip.addNote (long1);
            const auto e = clip.addNote (makeNote (67, 3.0, 0.25));

            const auto touched = notetools::legato (clip, {}, {});
            expect ((touched == std::set<uint32_t> { a, b, c, d, e }));
            expectEquals (clip.findNote (a)->length, 1.0);
            expectEquals (clip.findNote (b)->length, 1.0);
            expectEquals (clip.findNote (c)->length, 1.0);
            expectEquals (clip.findNote (d)->length, 1.0);
            expectEquals (clip.findNote (e)->length, 0.25);

            const auto& trimmed = clip.findNote (d)->lanes.pitch;
            expectEquals ((int) trimmed.size(), 2);
            expectEquals (trimmed.getPoints().back().beat, 1.0);
            expectWithinAbsoluteError (trimmed.valueAt (1.0, 9.0f), 1.0f, 1.0e-6f);
            expect (isSorted (clip));

            MpeClip gapped;
            const auto g1 = gapped.addNote (makeNote (60, 0.0, 0.5));
            const auto g2 = gapped.addNote (makeNote (62, 1.0, 0.5));
            const auto g3 = gapped.addNote (makeNote (64, 2.0, 0.5));
            notetools::LegatoOptions overlap;
            overlap.overlap = -0.1;
            notetools::legato (gapped, { g1, g3 }, overlap);
            expectWithinAbsoluteError (gapped.findNote (g1)->length, 1.9, 1.0e-9);   // g2 is not in the set
            expectEquals (gapped.findNote (g2)->length, 0.5);
            expectEquals (gapped.findNote (g3)->length, 0.5);
        }

        beginTest ("Articulate scales lengths and trims curves");
        {
            MpeClip clip;
            auto glide = makeNote (60, 0.0, 1.0);
            glide.lanes.pitch = { { 0.0, 0.0f }, { 1.0, 2.0f } };
            const auto a = clip.addNote (glide);
            const auto b = clip.addNote (makeNote (62, 2.0, 0.1));

            notetools::ArticulateOptions options;
            options.lengthFactor = 0.5f;
            options.minimumLength = 0.0625;

            const auto touched = notetools::articulate (clip, {}, options);
            expect ((touched == std::set<uint32_t> { a, b }));
            expectEquals (clip.findNote (a)->length, 0.5);
            expectEquals (clip.findNote (b)->length, 0.0625);

            const auto& pitch = clip.findNote (a)->lanes.pitch;
            expectEquals ((int) pitch.size(), 2);
            expectEquals (pitch.getPoints().back().beat, 0.5);
            expectWithinAbsoluteError (pitch.valueAt (0.5, 9.0f), 1.0f, 1.0e-6f);

            options.lengthFactor = 4.0f;
            notetools::articulate (clip, { a }, options);
            expectEquals (clip.findNote (a)->length, 2.0);
            expectEquals ((int) clip.findNote (a)->lanes.pitch.size(), 2);
            expectEquals (clip.findNote (b)->length, 0.0625);
            expect (isSorted (clip));
        }

        beginTest ("Limit folds keys into the range by octaves, widening a narrow one");
        {
            MpeClip clip;
            const std::vector<int> keys { 36, 60, 84, 85, 48, 72, 0, 127 };

            for (size_t i = 0; i < keys.size(); ++i)
                clip.addNote (makeNote (keys[i], (double) i, 0.5));

            notetools::LimitOptions options;
            options.lowKey = 48;
            options.highKey = 72;

            const auto touched = notetools::limit (clip, {}, options);
            expectEquals ((int) touched.size(), (int) keys.size());
            expect ((keysOf (clip) == std::vector<int> { 48, 60, 72, 61, 48, 72, 48, 67 }));

            MpeClip narrow;
            const auto low = narrow.addNote (makeNote (48, 0.0, 1.0));
            const auto high = narrow.addNote (makeNote (73, 1.0, 1.0));
            const auto near = narrow.addNote (makeNote (50, 2.0, 1.0));
            const auto outside = narrow.addNote (makeNote (20, 3.0, 1.0));
            options.lowKey = 60;
            options.highKey = 60;
            notetools::limit (narrow, { low, high, near }, options);
            expectEquals (narrow.findNote (low)->key, 60);
            expectEquals (narrow.findNote (high)->key, 61);
            expectEquals (narrow.findNote (near)->key, 62);
            expectEquals (narrow.findNote (outside)->key, 20);

            for (auto id : { low, high, near })
                expect (narrow.findNote (id)->key >= 54 && narrow.findNote (id)->key <= 66);

            expect (isSorted (narrow));
        }

        beginTest ("Flip mirrors pitches and reverses time with the lanes");
        {
            auto build = [] (MpeClip& clip)
            {
                auto glide = makeNote (60, 0.0, 1.0);
                glide.lanes.pitch = { { 0.0, 0.0f }, { 1.0, 2.0f } };
                glide.lanes.pressure = { { 0.25, 0.5f } };
                return std::vector<uint32_t> { clip.addNote (glide),
                                               clip.addNote (makeNote (64, 1.0, 1.0)),
                                               clip.addNote (makeNote (67, 2.0, 0.5)) };
            };

            MpeClip vertical;
            auto ids = build (vertical);
            notetools::FlipOptions options;
            options.vertical = true;
            options.horizontal = false;
            const auto touched = notetools::flip (vertical, {}, options);
            expect ((touched == std::set<uint32_t> (ids.begin(), ids.end())));
            expect ((keysOf (vertical) == std::vector<int> { 67, 63, 60 }));
            expectEquals (vertical.notes[0].start, 0.0);

            MpeClip horizontal;
            ids = build (horizontal);
            options.vertical = false;
            options.horizontal = true;
            notetools::flip (horizontal, {}, options);
            expectEquals (horizontal.findNote (ids[0])->start, 1.5);
            expectEquals (horizontal.findNote (ids[1])->start, 0.5);
            expectEquals (horizontal.findNote (ids[2])->start, 0.0);
            expectEquals (horizontal.findNote (ids[0])->length, 1.0);
            expect ((keysOf (horizontal) == std::vector<int> { 67, 64, 60 }));
            expect (isSorted (horizontal));

            const auto& reversed = horizontal.findNote (ids[0])->lanes;
            expectEquals ((int) reversed.pitch.size(), 2);
            expectWithinAbsoluteError (reversed.pitch.valueAt (0.0, 9.0f), 2.0f, 1.0e-6f);
            expectWithinAbsoluteError (reversed.pitch.valueAt (0.5, 9.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (reversed.pitch.valueAt (1.0, 9.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (reversed.pressure.getPoints().front().beat, 0.75, 1.0e-9);

            MpeClip both;
            ids = build (both);
            options.vertical = true;
            notetools::flip (both, {}, options);
            expect ((keysOf (both) == std::vector<int> { 60, 63, 67 }));
            expectEquals (both.notes[0].start, 0.0);
            expectEquals (both.notes[2].start, 1.5);

            // A subset mirrors around its own middle; a single key is unchanged.
            MpeClip subset;
            ids = build (subset);
            options.horizontal = false;
            notetools::flip (subset, { ids[0], ids[1] }, options);
            expect ((keysOf (subset) == std::vector<int> { 64, 60, 67 }));
            notetools::flip (subset, { ids[2] }, options);
            expectEquals (subset.findNote (ids[2])->key, 67);
        }

        beginTest ("Randomize is reproducible and stays within bounds");
        {
            MpeClip clip;

            for (int i = 0; i < 24; ++i)
            {
                auto n = makeNote (36 + i * 3, i * 0.5, 0.5, 0.5f);

                if (i % 2 == 1)
                    n.lanes.pressure = { { 0.0, 0.5f }, { 0.25, 0.9f } };

                clip.addNote (n);
            }

            notetools::RandomizeOptions options;
            options.seed = 7;
            options.strike = 1.0f;
            options.lift = 1.0f;
            options.pressure = 0.5f;
            options.timbre = 0.5f;
            options.pitch = 12;
            options.pitchChance = 0.5f;
            options.length = 0.9f;
            options.start = 0.5;
            options.minimumLength = 0.0625;

            auto first = clip, second = clip, third = clip;
            const auto touched = notetools::randomize (first, {}, options);
            notetools::randomize (second, {}, options);
            options.seed = 8;
            notetools::randomize (third, {}, options);

            expectEquals ((int) touched.size(), 24);
            expect (first == second);
            expect (! (first == third));
            expect (! (first == clip));
            expectEquals ((int) first.notes.size(), 24);
            expect (isSorted (first));
            expect (isSorted (third));

            bool anyKeyMoved = false, anyStrikeMoved = false;

            for (const auto& n : first.notes)
            {
                const auto which = "note " + juce::String ((int) n.id);
                expect (n.strike >= 0.0f && n.strike <= 1.0f, which + " strike");
                expect (n.lift >= 0.0f && n.lift <= 1.0f, which + " lift");
                expect (n.start >= 0.0, which + " start");
                expect (n.length >= 0.0625, which + " length");
                expect (n.key >= 0 && n.key <= 127, which + " key");
                expect (! n.lanes.pressure.isEmpty(), which + " pressure");
                expectEquals ((int) n.lanes.timbre.size(), 1, which + " timbre");
                expectEquals (n.lanes.timbre.getPoints().front().beat, 0.0, which + " timbre time");

                for (const auto& p : n.lanes.pressure.getPoints())
                    expect (p.value >= 0.0f && p.value <= 1.0f, which + " pressure value");

                for (const auto& p : n.lanes.timbre.getPoints())
                    expect (p.value >= 0.0f && p.value <= 1.0f, which + " timbre value");

                const auto* original = clip.findNote (n.id);
                anyKeyMoved = anyKeyMoved || n.key != original->key;
                anyStrikeMoved = anyStrikeMoved || std::abs (n.strike - original->strike) > 1.0e-6f;

                // A lane with data keeps its shape: both points move together.
                if (original->lanes.pressure.size() == 2)
                    expectEquals ((int) n.lanes.pressure.size(), 2, which + " pressure shape");
            }

            expect (anyKeyMoved && anyStrikeMoved);

            // Nothing moves when every amount is zero, and only the chosen notes move otherwise.
            auto untouched = clip;
            notetools::randomize (untouched, {}, {});
            expect (untouched == clip);

            auto partial = clip;
            options.start = 0.0;
            notetools::randomize (partial, { clip.notes[0].id }, options);
            expect (! (partial.notes[0] == clip.notes[0]));

            for (size_t i = 1; i < clip.notes.size(); ++i)
                expect (partial.notes[i] == clip.notes[i]);
        }

        beginTest ("Scale levels scales and clips velocities and lanes");
        {
            MpeClip clip;
            auto a = makeNote (60, 0.0, 1.0, 0.5f);
            a.lift = 0.5f;
            a.lanes.pressure = { { 0.0, 0.4f } };
            a.lanes.timbre = { { 0.0, 0.75f } };
            auto b = makeNote (62, 1.0, 1.0, 0.8f);
            b.lanes.timbre = { { 0.0, 0.25f } };
            const auto idA = clip.addNote (a);
            const auto idB = clip.addNote (b);

            notetools::ScaleLevelsOptions options;
            options.strikeMultiplier = 1.5f;
            options.strikeOffset = 0.1f;
            options.liftMultiplier = 0.5f;
            options.pressureMultiplier = 2.0f;
            options.timbreMultiplier = 2.0f;

            const auto touched = notetools::scaleLevels (clip, {}, options);
            expect ((touched == std::set<uint32_t> { idA, idB }));

            const auto* first = clip.findNote (idA);
            const auto* second = clip.findNote (idB);
            expectWithinAbsoluteError (first->strike, 0.85f, 1.0e-6f);
            expectWithinAbsoluteError (first->lift, 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (first->lanes.pressure.valueAt (0.0, 9.0f), 0.8f, 1.0e-6f);
            expectWithinAbsoluteError (first->lanes.timbre.valueAt (0.0, 9.0f), 1.0f, 1.0e-6f);
            expectEquals (second->strike, 1.0f);
            expect (second->lanes.pressure.isEmpty());
            expectWithinAbsoluteError (second->lanes.timbre.valueAt (0.0, 9.0f), 0.0f, 1.0e-6f);

            notetools::scaleLevels (clip, { idB }, options);
            expectWithinAbsoluteError (first->strike, 0.85f, 1.0e-6f);
            expectEquals (second->strike, 1.0f);
        }

        beginTest ("Quantize moves starts and ends towards the grid");
        {
            notetools::QuantizeOptions options;
            options.grid = 0.25;
            options.strength = 1.0f;
            options.starts = true;
            options.ends = false;

            MpeClip clip;
            const auto a = clip.addNote (makeNote (60, 0.3, 0.6));
            const auto b = clip.addNote (makeNote (62, 0.1, 0.1));
            const auto touched = notetools::quantize (clip, {}, options);
            expect ((touched == std::set<uint32_t> { a, b }));
            expectEquals (clip.findNote (a)->start, 0.25);
            expectEquals (clip.findNote (a)->length, 0.6);
            expectEquals (clip.findNote (b)->start, 0.0);
            expect (isSorted (clip));

            MpeClip half;
            const auto h = half.addNote (makeNote (60, 0.3, 0.6));
            options.strength = 0.5f;
            notetools::quantize (half, {}, options);
            expectWithinAbsoluteError (half.findNote (h)->start, 0.275, 1.0e-9);
            expectEquals (half.findNote (h)->length, 0.6);

            MpeClip ends;
            auto curved = makeNote (60, 0.3, 0.6);
            curved.lanes.pitch = { { 0.0, 0.0f }, { 0.6, 1.0f } };
            const auto e1 = ends.addNote (curved);
            const auto e2 = ends.addNote (makeNote (62, 0.2, 0.05));
            auto shrinking = makeNote (64, 1.0, 0.6);
            shrinking.lanes.pitch = { { 0.0, 0.0f }, { 0.6, 1.0f } };
            const auto e3 = ends.addNote (shrinking);
            options.strength = 1.0f;
            options.ends = true;
            notetools::quantize (ends, {}, options);
            expectEquals (ends.findNote (e1)->start, 0.25);
            expectWithinAbsoluteError (ends.findNote (e1)->length, 0.75, 1.0e-9);
            expectEquals ((int) ends.findNote (e1)->lanes.pitch.size(), 2);   // lengthened: untouched
            expectEquals (ends.findNote (e2)->start, 0.25);
            expectWithinAbsoluteError (ends.findNote (e2)->length, 0.25, 1.0e-9);   // never below the grid
            expectEquals (ends.findNote (e3)->start, 1.0);
            expectWithinAbsoluteError (ends.findNote (e3)->length, 0.5, 1.0e-9);
            expectWithinAbsoluteError (ends.findNote (e3)->lanes.pitch.getPoints().back().beat, 0.5, 1.0e-9);
            expect (isSorted (ends));

            // Ends alone leave the starts where they are.
            MpeClip endsOnly;
            const auto o = endsOnly.addNote (makeNote (60, 0.3, 0.6));
            options.starts = false;
            notetools::quantize (endsOnly, { o }, options);
            expectEquals (endsOnly.findNote (o)->start, 0.3);
            expectWithinAbsoluteError (endsOnly.findNote (o)->end(), 1.0, 1.0e-9);
        }

        beginTest ("Every operation leaves the clip sorted by start");
        {
            auto build = []
            {
                MpeClip clip;
                clip.addNote (makeNote (60, 0.0, 1.0));
                clip.addNote (makeNote (64, 0.0, 1.5));
                clip.addNote (makeNote (67, 0.0, 0.5));
                clip.addNote (makeNote (62, 1.3, 0.9));
                clip.addNote (makeNote (60, 2.1, 0.4));
                clip.addNote (makeNote (72, 2.1, 1.0));
                clip.addNote (makeNote (48, 3.7, 2.0));
                clip.addNote (makeNote (120, 5.0, 0.3));
                return clip;
            };

            notetools::RandomizeOptions shake;
            shake.start = 2.0;
            shake.length = 0.5f;
            shake.pitch = 24;

            notetools::FlipOptions flipBoth;
            flipBoth.horizontal = true;

            notetools::QuantizeOptions coarse;
            coarse.grid = 1.0;
            coarse.ends = true;

            notetools::GlueOptions wide;
            wide.gap = 2.0;

            std::vector<std::pair<juce::String, std::function<void (MpeClip&)>>> operations
            {
                { "sliceAt",     [] (MpeClip& c) { notetools::sliceAt (c, {}, 0.7); } },
                { "chop",        [] (MpeClip& c) { notetools::chop (c, {}, {}); } },
                { "arpeggiate",  [] (MpeClip& c) { notetools::arpeggiate (c, {}, {}); } },
                { "strum",       [] (MpeClip& c) { notetools::strum (c, {}, {}); } },
                { "flam",        [] (MpeClip& c) { notetools::flam (c, {}, {}); } },
                { "glue",        [&] (MpeClip& c) { notetools::glue (c, {}, wide); } },
                { "legato",      [] (MpeClip& c) { notetools::legato (c, {}, {}); } },
                { "articulate",  [] (MpeClip& c) { notetools::articulate (c, {}, {}); } },
                { "limit",       [] (MpeClip& c) { notetools::limit (c, {}, {}); } },
                { "flip",        [&] (MpeClip& c) { notetools::flip (c, {}, flipBoth); } },
                { "randomize",   [&] (MpeClip& c) { notetools::randomize (c, {}, shake); } },
                { "scaleLevels", [] (MpeClip& c) { notetools::scaleLevels (c, {}, {}); } },
                { "quantize",    [&] (MpeClip& c) { notetools::quantize (c, {}, coarse); } },
            };

            for (const auto& [label, operation] : operations)
            {
                auto clip = build();
                operation (clip);
                expect (isSorted (clip), label);
                expect (! clip.notes.empty(), label);
            }
        }
    }
};

static NoteToolsTests noteToolsTests;

} // namespace mperoll
