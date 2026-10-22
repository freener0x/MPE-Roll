# MPE in software instruments

<!-- Made by tools/make_synth_list.py from docs/synths.json. Change those, not this page. -->

Which instruments for Windows have MPE, and where to turn it on: 24 instruments from 16 makers, sold by the major stores (Plugin Boutique, Sweetwater, Thomann, Best Service, KVR Marketplace, Native Instruments, Splice, ADSR, Audio Plugin Deals) or given away. Checked October 2026 against the makers' manuals, support pages and release notes, and the MPE lists of controller makers. The sources are in the last column. Found a mistake, or an instrument that's missing? Please open an issue.

12 have MPE, 2 have it with limits, 10 don't, 0 never mention it and 0 are unclear.

## Reading the list

- **Yes**: it has MPE. Turn it on where the list says, and set its per-note bend range to MPE Roll's *Per-note bend range* (48 unless you change it), or MPE Roll's to match it.
- **Yes, limited**: it has MPE with a limit the notes spell out (one note at a time, some presets only...).
- **No**: its maker says it has no MPE. In MPE Roll's *MPE settings*, set *Synth expects* to *Plain MIDI (one channel)*: the newest note's glides, pressure and timbre then drive it, and every held note follows them.
- **Not mentioned**: its maker never mentions MPE for it, so it almost certainly has none. Use *Plain MIDI* as for **No**.
- **Unclear**: the sources disagree. Try MPE first; if held notes bend together, use *Plain MIDI*.
- **\***: not yet confirmed by a second, independent check.

MPE Roll knows these instruments: when you load one, its status line and *MPE settings* say where its MPE switch is, and for one without MPE it offers to switch to *Plain MIDI*. A VST3 that takes VST3 note expressions, or a CLAP that takes CLAP note expressions, can get each note's expression that way, with nothing to set up in the instrument; *MPE settings › Synth is played with* says which way MPE Roll plays it. [FL_STUDIO.md](FL_STUDIO.md#7-notes-on-particular-synths) has more on some of them.

Only instruments are listed: effects don't play notes, so MPE doesn't apply to them.

## Makers

[Arturia](#arturia) · [Audio Modeling](#audio-modeling) · [Cherry Audio](#cherry-audio) · [Expressive E](#expressive-e) · [Kilohearts](#kilohearts) · [LennarDigital](#lennardigital) · [Native Instruments](#native-instruments) · [Reveal Sound](#reveal-sound) · [Roland](#roland) · [ROLI](#roli) · [Spectrasonics](#spectrasonics) · [Surge Synth Team](#surge-synth-team) · [u-he](#u-he) · [Vital Audio](#vital-audio) · [Waves](#waves) · [Xfer Records](#xfer-records)

## Arturia

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Analog Lab | VST2, VST3, AAX, Standalone | **No** | *Plain MIDI*. For each note's own expression, load the instrument itself: Pigments, or a V Collection instrument with an Enable MPE setting. |  | [support.arturia.com](https://support.arturia.com/hc/en-us/articles/4793918378908-MPE-Compatibility) |
| Arturia V Collection instruments | VST2, VST3, AAX, Standalone | Yes, limited | Gear icon › Settings tab › Enable MPE. Bend range: Bend Range next to it; set 48. | Many have MPE; one without that setting has none | [support.arturia.com](https://support.arturia.com/hc/en-us/articles/4793918378908-MPE-Compatibility) |
| Pigments | VST2, VST3, AAX, Standalone | Yes | Settings (top toolbar) › Enable MPE. Bend range: Bend Range next to it; set 48. |  | [support.arturia.com](https://support.arturia.com/hc/en-us/articles/4793918378908-MPE-Compatibility) |

## Audio Modeling

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| SWAM instruments | VST2, VST3, AAX, Standalone | Yes, limited | MIDI settings › MIDI Profile: MPE. | One note at a time | [static.audiomodeling.com](https://static.audiomodeling.com/manuals/brass/SWAM%20Solo%20Brass%20-%20User%20Manual%20-%20v3.6.0.pdf) |

## Cherry Audio

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Elka-X | VST2, VST3, AAX, Standalone | **No** | *Plain MIDI*. |  | [cherryaudio.kayako.com](https://cherryaudio.kayako.com/article/631-does-the-elka-x-synthesizer-have-mpe-support) |
| Memorymode | VST2, VST3, AAX, Standalone | Yes | Settings (gear icon) › Interface › Enable MPE mode. Bend range: Its MPE tab; set 48. |  | [cherryaudio.kayako.com](https://cherryaudio.kayako.com/article/539-cherry-audio-mpe-enabled-instruments) |

## Expressive E

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Noisy and Imagine | VST3, AAX, Standalone | Yes | Its MPE presets, or MPE on in its settings. |  | [expressivee.com](https://www.expressivee.com/88-noisy-two) |

## Kilohearts

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Phase Plant | VST2, VST3, CLAP, AAX, Standalone | Yes | Its settings › MPE on, MIDI channel Any. Bend range: Bend Range next to it; set 48. |  | [kilohearts.com](https://kilohearts.com/blog/expressive_performance_with_mpe_in_phase_plant) |

## LennarDigital

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Sylenth1 | VST2, VST3, AAX | **No** | *Plain MIDI*. |  | [lennardigital.com](https://www.lennardigital.com/sylenth1/whatsnew.php) |

## Native Instruments

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Kontakt | VST2, VST3, AAX, Standalone | **No** | *Plain MIDI*. |  | [docs.native-instruments.com](https://docs.native-instruments.com/ni-tech-manuals/ksp-manual/en/version-history) |
| Massive X | VST2, VST3, AAX | **No** | *Plain MIDI*. |  | [kvraudio.com](https://www.kvraudio.com/forum/viewtopic.php?t=545907&start=1185) |

## Reveal Sound

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Spire | VST2, VST3, AAX | **No** | *Plain MIDI*. |  | [kvraudio.com](https://www.kvraudio.com/forum/viewtopic.php?p=8899985) |

## Roland

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| ZENOLOGY and Roland Cloud instruments | VST3, AAX | **No** | *Plain MIDI*. |  | [roland.com](https://www.roland.com/dk/products/rc_zenology/) |

## ROLI

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Equator2 | VST3, AAX, Standalone | Yes | MIDI settings › MPE mode (not Standard). Bend range: Per-note bend range in the same settings; set 48. |  | [support.roli.com](https://support.roli.com/support/solutions/articles/36000447444-changing-midi-settings-in-equator2) |

## Spectrasonics

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Omnisphere 2 | VST2, VST3, AAX, Standalone | **No** | *Plain MIDI*. Load the patch into Part 1. |  | [support.spectrasonics.net](https://support.spectrasonics.net/manual/Omnisphere3/30/en/topic/mpe-midi-polyphonic-expression) |
| Omnisphere 3 | VST2, VST3, AAX, Standalone | Yes | System page (gear icon) › MPE on; load the patch into Part 1 only. Bend range: MPE Pitch Bend Range on the System page; set 48. |  | [support.spectrasonics.net](https://support.spectrasonics.net/manual/Omnisphere3/30/en/topic/system-mpe) |

## Surge Synth Team

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Surge XT | VST3, CLAP, Standalone | Yes | Turns itself on from MPE Roll's setup (MPE settings › Send MPE setup now). CLAP version: CLAP note expressions, nothing to set up. |  | [github.com](https://github.com/surge-synthesizer/surge/blob/main/src/common/SurgeSynthesizer.cpp) |

## u-he

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Diva | VST2, VST3, CLAP, AAX | Yes | Pitch Bend Up and Down set to MPE (polyphonic presets). CLAP version: CLAP note expressions, nothing to set up. | MPE from 1.4.8 | [u-he.com](https://u-he.com/news/2024/08/mpe-released/) |
| Zebra 3 | VST3, CLAP, AAX | Yes | On without a switch. CLAP version: CLAP note expressions, nothing to set up. |  | [u-he.com](https://u-he.com/products/plug-ins/zebra3/releases/3.0.1/) |
| Zebra2 | VST2, VST3, AAX | **No** | *Plain MIDI*. |  | [kvraudio.com](https://www.kvraudio.com/forum/viewtopic.php?p=7627708) |

## Vital Audio

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Vital | VST2, VST3, CLAP, Standalone | Yes | ADVANCED tab › MPE ENABLED (saved with each preset). Bend range: Fixed at 48. |  | [github.com](https://github.com/mtytel/vital/blob/main/src/synthesis/framework/voice_handler.cpp) |

## Waves

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Waves synths | VST2, VST3, AAX | **No** | *Plain MIDI*. |  | [waves.com](https://www.waves.com/codex-synth-overview) |

## Xfer Records

| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |
| --- | --- | --- | --- | --- | --- |
| Serum | VST2, VST3, AAX | Yes | MENU (top bar) › MPE Enabled. Bend range: MPE bend range at the bottom of the same menu; set 48. | Serum 1.x; MPE since 1.2x | [synthtopia.com](https://www.synthtopia.com/content/2019/04/18/serum-synth-now-mpe-compatible/) |
| Serum 2 | VST3, AAX | Yes | MENU › MPE Enabled (usually on). Bend range: MPE Bend Range on its Global page; set 48. Takes VST3 note expressions with MPE off. |  | [xferrecords.com](https://xferrecords.com/manual/serum-2) |
