#include "TestHelpers.h"
#include "core/FlScriptExporter.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>

namespace
{
    /** Writes a sample FL script plus a JSON file describing what running it should produce. */
    int writeFlSample (const juce::File& target)
    {
        using namespace mperoll;

        MpeClip clip;
        clip.name = "Sample";
        clip.length = 8.0;

        auto glide = test::makeNote (60, 0.0, 2.0, 0.9f);
        glide.lanes.pitch = { { 0.0, 0.0f }, { 0.5, 0.0f }, { 1.5, 3.0f } };
        glide.lanes.pressure = { { 0.0, 0.0f }, { 1.0, 1.0f }, { 2.0, 0.2f } };
        glide.lanes.timbre = { { 0.0, 0.2f }, { 2.0, 0.8f } };
        glide.lanes.get (LaneId::cc (10)) = { { 0.0, 0.2f } };
        clip.addNote (glide);

        clip.addNote (test::makeNote (64, 0.25, 1.0, 0.5f));

        auto micro = test::makeNote (67, 0.5, 1.5, 0.7f);
        micro.lanes.pitch = { { 0.0, -0.3f } };
        clip.addNote (micro);

        auto vibrato = test::makeNote (72, 3.0, 2.0, 0.6f);

        for (int i = 0; i <= 200; ++i)
            vibrato.lanes.pitch.append (i * 0.01, 0.4f * (float) std::sin (i * 0.25));

        clip.addNote (vibrato);

        auto scoop = test::makeNote (48, 5.0, 1.0, 1.0f);
        scoop.lanes.pitch = { { 0.0, -14.0f }, { 0.25, 0.0f } };   // beyond FL's fine pitch range
        clip.addNote (scoop);

        FlScriptExporter::Options options;
        options.title = "Sample";
        const auto result = FlScriptExporter::generate (clip, options);

        int moving = 0;

        for (const auto& n : result.notes)
            for (const auto& s : n.slides)
                moving += s.pitchMoves ? 1 : 0;

        auto* expectations = new juce::DynamicObject();
        expectations->setProperty ("notes", (int) result.notes.size());
        expectations->setProperty ("slides", result.numSlides);
        expectations->setProperty ("moving_slides", moving);

        if (! target.replaceWithText (result.script)
            || ! target.withFileExtension (".expect.json").replaceWithText (juce::JSON::toString (juce::var (expectations))))
            return 1;

        std::printf ("Wrote %s (%d notes, %d slides)\n", target.getFullPathName().toRawUTF8(),
                     (int) result.notes.size(), result.numSlides);
        return 0;
    }
}

int main (int argc, char** argv)
{
    if (argc == 3 && juce::String (argv[1]) == "--write-fl-sample")
        return writeFlSample (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("MPE Roll");

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
