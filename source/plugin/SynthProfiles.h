#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace mperoll
{

/** What MPE Roll knows about a particular synth's MPE: whether it has any, and how to turn it on.
    From the makers' manuals and support pages, checked in October 2026; see docs/FL_STUDIO.md.
*/
struct SynthProfile
{
    enum class Mpe
    {
        none,           // no MPE: each note's own expression can't reach it, Plain MIDI is the way
        needsSwitch,    // MPE, off until it's turned on in the synth
        followsSetup    // MPE that turns itself on from MPE Roll's setup messages
    };

    juce::String name;      // as the user knows it: "Serum 2"
    Mpe mpe = Mpe::needsSwitch;
    juce::String advice;    // what to do in the synth, in a sentence or two

    /** Which synth a description is, if MPE Roll knows it. Matched on the name and maker. */
    static std::optional<SynthProfile> find (const juce::PluginDescription&);
};

} // namespace mperoll
