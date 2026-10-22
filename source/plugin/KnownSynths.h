#pragma once

// Made by tools/make_synth_list.py from docs/synths.json. Change those, not this file.
// The instruments MPE Roll knows the MPE of, checked October 2026: the same list as docs/MPE_SYNTHS.md.

#include <array>

namespace mperoll::knownsynths
{
    enum class Mpe { yes, followsSetup, no, undocumented };

    // Names and makers in lower case letters and digits, several separated by '|'. A synth is this one
    // when its name starts with one of the names (any name, if there are none) and not with one of the
    // excludes, its maker contains one of the makers (or is one, for those starting with '='), and its
    // major version is one of the versions. The first that fits is the one.
    struct Synth
    {
        const char* name;
        const char* names;
        const char* excludes;
        const char* makers;
        const char* versions;
        Mpe mpe;
        const char* advice;
        const char* clapAdvice;     // added in builds that host CLAP plugins
    };

    inline constexpr std::array<Synth, 24> synths { {
        { "Omnisphere 2", "omnisphere", "", "", "1|2", Mpe::no,
          "Omnisphere 2 has no MPE (Omnisphere 3 has). Load the patch into Part 1. Set Synth expects to Plain MIDI.",
          "" },
        { "", "elkax|ca2600|eightvoice|lowdownbass|quadra|surrealisticmg1", "", "cherryaudio", "", Mpe::no,
          "It has no MPE, Cherry Audio says. Set Synth expects to Plain MIDI.",
          "" },
        { "Omnisphere 3", "omnisphere", "", "", "", Mpe::yes,
          "Turn on MPE in Omnisphere 3: System page (gear icon) > MPE on, with MPE Pitch Bend Range 48, and load the patch into Part 1 only (Omnisphere 2 has no MPE: use Plain MIDI).",
          "" },
        { "Phase Plant", "phaseplant", "", "", "", Mpe::yes,
          "Turn on MPE in Phase Plant's settings, with Bend Range 48 and its MIDI channel set to Any.",
          "" },
        { "", "dco106|dreamsynth|memorymode|mercury4|miniverse|ps20|esq1|octavecat|sines", "", "cherryaudio", "", Mpe::yes,
          "Turn on MPE in it: Settings (gear icon) > Interface > Enable MPE mode, then set the bend range on its MPE tab to 48, MPE Roll's per-note bend range.",
          "" },
        { "Analog Lab", "analoglab", "", "", "", Mpe::no,
          "Arturia says Analog Lab itself has no MPE. For each note's own expression, load the instrument itself: Pigments, or a V Collection instrument with an Enable MPE setting. Set Synth expects to Plain MIDI.",
          "" },
        { "Pigments", "pigments", "", "", "", Mpe::yes,
          "Turn on MPE in Pigments: Settings (top toolbar) > Enable MPE, with Bend Range 48, MPE Roll's per-note bend range.",
          "" },
        { "Massive X", "massive", "", "", "", Mpe::no,
          "Massive X has no MPE. Set Synth expects to Plain MIDI.",
          "" },
        { "Kontakt", "kontakt", "", "", "", Mpe::no,
          "Kontakt has no MPE mode of its own (a few libraries have one: if yours does, turn it on there). Set Synth expects to Plain MIDI.",
          "" },
        { "Sylenth1", "sylenth", "", "", "", Mpe::no,
          "Sylenth1 has no MPE. Set Synth expects to Plain MIDI.",
          "" },
        { "Surge XT", "surgext|surge", "", "", "", Mpe::followsSetup,
          "Surge XT turns MPE on by itself from MPE Roll's setup (MPE settings > Send MPE setup now, if it hasn't).",
          " Its CLAP version needs nothing set up when Note expressions are on." },
        { "Equator2", "equator", "", "", "", Mpe::yes,
          "Choose MPE mode (not Standard) in Equator2's MIDI settings, with a per-note bend range of 48.",
          "" },
        { "Serum 2", "serum2", "", "", "", Mpe::yes,
          "Check MPE is on in Serum 2: MENU > MPE Enabled, with MPE Bend Range 48 on its Global page, MPE Roll's per-note bend range.",
          "" },
        { "Zebra2", "zebra2", "", "", "", Mpe::no,
          "Zebra2 has no MPE (Zebra 3 has). Set Synth expects to Plain MIDI.",
          "" },
        { "Zebra 3", "zebra3", "", "", "", Mpe::yes,
          "Zebra 3 has MPE.",
          " Its CLAP version needs nothing set up when Note expressions are on in MPE settings." },
        { "Serum", "serum", "serumfx|serumfx|serum2", "", "", Mpe::yes,
          "Turn on MPE in Serum: MENU (top bar) > MPE Enabled, and set the MPE bend range at the bottom of that menu to 48, MPE Roll's per-note bend range.",
          "" },
        { "Vital", "vital", "", "vital|matttytel", "", Mpe::yes,
          "Turn on MPE in Vital: ADVANCED tab > MPE ENABLED. It's saved with each preset, so loading one can turn it off again. Vital's per-note bend range is fixed at 48, as MPE Roll's is.",
          "" },
        { "Spire", "spire", "", "", "", Mpe::no,
          "Spire has no MPE. Set Synth expects to Plain MIDI.",
          "" },
        { "Diva", "diva", "", "", "", Mpe::yes,
          "Diva has MPE from 1.4.8, for polyphonic presets: set its Pitch Bend Up and Down to MPE.",
          " Its CLAP version needs nothing set up when Note expressions are on in MPE settings." },
        { "", "", "", "arturia", "", Mpe::yes,
          "Many Arturia V Collection instruments have MPE: gear icon > Settings tab > Enable MPE, with Bend Range 48. One without that setting has no MPE: set Synth expects to Plain MIDI.",
          "" },
        { "", "", "", "roland", "", Mpe::no,
          "Roland's ZENOLOGY and Roland Cloud instruments have no MPE. Set Synth expects to Plain MIDI.",
          "" },
        { "", "", "", "=waves", "", Mpe::no,
          "Waves synths have no MPE. Set Synth expects to Plain MIDI.",
          "" },
        { "", "", "", "audiomodeling", "", Mpe::yes,
          "Set its MIDI Profile to MPE (MIDI settings). SWAM instruments play one note at a time.",
          "" },
        { "", "", "", "expressivee", "", Mpe::yes,
          "Use its MPE presets, or turn MPE on in its settings, with a per-note bend range of 48.",
          "" },
    } };
}
