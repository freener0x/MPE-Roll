#include "SynthProfiles.h"

namespace mperoll
{

namespace
{
    using Mpe = SynthProfile::Mpe;

    bool startsWithAny (const juce::String& s, std::initializer_list<const char*> prefixes)
    {
        for (auto* p : prefixes)
            if (s.startsWith (p))
                return true;

        return false;
    }

    SynthProfile noMpe (const juce::String& name, const juce::String& why = {})
    {
        return { name, Mpe::none, (why.isNotEmpty() ? why : name + " has no MPE.") + " Set Synth expects to Plain MIDI." };
    }

    std::optional<SynthProfile> cherryAudio (const juce::String& n, const juce::String& name)
    {
        // Cherry Audio's own list of which of its instruments have MPE.
        if (startsWithAny (n, { "dco-106", "dreamsynth", "memorymode", "mercury-4", "miniverse", "ps-20", "esq-1",
                                "octave cat", "sines" }))
            return SynthProfile { name, Mpe::needsSwitch,
                                  "Turn on MPE in it: Settings (gear icon) > Interface > Enable MPE mode, then set the "
                                  "bend range on its MPE tab to 48, MPE Roll's per-note bend range." };

        if (startsWithAny (n, { "elka-x", "ca2600", "eight voice", "lowdown bass", "quadra", "surrealistic mg-1" }))
            return noMpe (name, name + " has no MPE, Cherry Audio says.");

        return std::nullopt;
    }
}

std::optional<SynthProfile> SynthProfile::find (const juce::PluginDescription& d)
{
    const auto name = d.name.trim();
    const auto n = name.toLowerCase();
    const auto maker = d.manufacturerName.toLowerCase();

    // Xfer
    if (n.startsWith ("serum fx"))
        return std::nullopt;

    if (n.startsWith ("serum2") || n.startsWith ("serum 2"))
        return SynthProfile { "Serum 2", Mpe::needsSwitch,
                              "Check MPE is on in Serum 2: MENU > MPE Enabled, with MPE Bend Range 48 on its Global "
                              "page, MPE Roll's per-note bend range." };

    if (n.startsWith ("serum") && (maker.contains ("xfer") || maker.isEmpty() || n == "serum"))
        return SynthProfile { "Serum", Mpe::needsSwitch,
                              "Turn on MPE in Serum: MENU (top bar) > MPE Enabled, and set the MPE bend range at the "
                              "bottom of that menu to 48, MPE Roll's per-note bend range." };

    // Arturia
    if (n.startsWith ("analog lab"))
        return noMpe (name, "Arturia says Analog Lab itself has no MPE. For each note's own expression, load the "
                            "instrument itself instead: Pigments, or a V Collection instrument with an Enable MPE setting.");

    if (n.startsWith ("pigments"))
        return SynthProfile { "Pigments", Mpe::needsSwitch,
                              "Turn on MPE in Pigments: Settings (top toolbar) > Enable MPE, with Bend Range 48, MPE "
                              "Roll's per-note bend range." };

    if (maker.contains ("arturia"))
        return SynthProfile { name, Mpe::needsSwitch,
                              "Many Arturia V Collection instruments have MPE: gear icon > Settings tab > Enable MPE, "
                              "with Bend Range 48. One without that setting has no MPE: set Synth expects to Plain MIDI." };

    // Roland Cloud
    if (n.startsWith ("zenology") || maker.contains ("roland"))
        return noMpe (name, "Roland's ZENOLOGY and Roland Cloud instruments have no MPE.");

    // Spectrasonics
    if (n.startsWith ("omnisphere"))
    {
        const auto major = d.version.upToFirstOccurrenceOf (".", false, false).getIntValue();

        if (major == 2 || major == 1)
            return noMpe ("Omnisphere " + juce::String (major), "Omnisphere " + juce::String (major)
                          + " has no MPE (Omnisphere 3 has). Load the patch into Part 1.");

        return SynthProfile { "Omnisphere", Mpe::needsSwitch,
                              "Turn on MPE in Omnisphere 3: System page (gear icon) > MPE on, with MPE Pitch Bend Range "
                              "48, and load the patch into Part 1 only (Omnisphere 2 has no MPE: use Plain MIDI)." };
    }

    // Vital
    if (n == "vital" || maker.contains ("vital audio"))
        return SynthProfile { "Vital", Mpe::needsSwitch,
                              "Turn on MPE in Vital: ADVANCED tab > MPE ENABLED. It's saved with each preset, so loading "
                              "one can turn it off again. Vital's per-note bend range is fixed at 48, as MPE Roll's is." };

    // u-he
    if (n.startsWith ("diva"))
        return SynthProfile { "Diva", Mpe::needsSwitch,
                              "Diva has MPE from 1.4.8, for polyphonic presets: set its Pitch Bend Up and Down to MPE. "
                              "Its CLAP version needs nothing set up: MPE Roll plays it with CLAP note expressions." };

    if (n.startsWith ("zebra2") || n.startsWith ("zebra 2"))
        return noMpe ("Zebra2", "Zebra2 has no MPE (Zebra 3 has).");

    if (n.startsWith ("zebra3") || n.startsWith ("zebra 3"))
        return SynthProfile { "Zebra 3", Mpe::needsSwitch,
                              "Zebra 3 has MPE. Its CLAP version needs nothing set up: MPE Roll plays it with CLAP note "
                              "expressions." };

    // Kilohearts
    if (n.startsWith ("phase plant"))
        return SynthProfile { "Phase Plant", Mpe::needsSwitch,
                              "Turn on MPE in Phase Plant's settings, with Bend Range 48 and its MIDI channel set to Any." };

    // Native Instruments
    if (n.startsWith ("massive"))
        return noMpe (name);

    if (n.startsWith ("kontakt"))
        return noMpe (name, "Kontakt has no MPE mode of its own (a few libraries have one: if yours does, turn it on there).");

    // Others without MPE
    if (n.startsWith ("sylenth"))
        return noMpe ("Sylenth1");

    if (n.startsWith ("spire"))
        return noMpe ("Spire");

    if (maker == "waves")
        return noMpe (name, "Waves synths have no MPE.");

    // Others with MPE
    if (n.startsWith ("surge xt") || n == "surge")
        return SynthProfile { "Surge XT", Mpe::followsSetup,
                              "Surge XT turns MPE on by itself from MPE Roll's setup (MPE settings > Send MPE setup now, "
                              "if it hasn't). Its CLAP version is played with CLAP note expressions." };

    if (n.startsWith ("equator"))
        return SynthProfile { name, Mpe::needsSwitch,
                              "Choose MPE mode (not Standard) in Equator2's MIDI settings, with a per-note bend range of 48." };

    if (n.startsWith ("swam") || maker.contains ("audio modeling"))
        return SynthProfile { name, Mpe::needsSwitch,
                              "Set its MIDI Profile to MPE (MIDI settings). SWAM instruments play one note at a time." };

    if (maker.contains ("expressive e") || n.startsWith ("noisy") || n.startsWith ("imagine"))
        return SynthProfile { name, Mpe::needsSwitch,
                              "Use its MPE presets, or turn MPE on in its settings, with a per-note bend range of 48." };

    if (maker.contains ("cherry audio"))
        return cherryAudio (n, name);

    return std::nullopt;
}

} // namespace mperoll
