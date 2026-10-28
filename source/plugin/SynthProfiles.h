#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace mperoll
{

/** What MPE Roll knows about a particular synth's MPE: whether it has any, and how to turn it on.
    From docs/synths.json, which also makes docs/MPE_SYNTHS.md (tools/make_synth_list.py).
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
    juce::String bendRange; // where its own pitch-bend range is set and what it allows (Plain MIDI, One copy per note), if known
    bool certain = true;    // false for one whose maker never mentions MPE: it most likely has none

    /** "has no MPE", or "most likely has no MPE" when its maker just never mentions any. */
    juce::String describeNone() const   { return certain ? "has no MPE" : "most likely has no MPE"; }

    /** Which synth a description is, if MPE Roll knows it. Matched on the name, maker and version. */
    static std::optional<SynthProfile> find (const juce::PluginDescription&);
};

} // namespace mperoll
