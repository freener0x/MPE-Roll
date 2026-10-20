# FST (Free Studio Technology)

Clean-room headers for the VST2 plugin interface, written without the Steinberg VST SDK. MPE Roll uses
them so it can host VST2 synths. They come from FST 0.123 by IOhannes m zmölnig (IEM), as packaged by
Debian (`fst-dev`). Upstream: <https://git.iem.at/zmoelnig/FST>.

Licensed under the GNU General Public License, version 3 or later (see `LICENSE`). That is compatible
with MPE Roll's AGPL-3.0 licence. FST's authors ask that projects using it are released under a
compatible open-source licence.

`pluginterfaces/vst2.x/` holds two small shims that point the include path JUCE uses at FST, quieten
its notes about undocumented symbols, and give placeholder names to the MIDI key name query that JUCE
declares but MPE Roll never uses.

Local changes to `fst.h`:

- An `FST_QUIET` switch that turns `FST_WARNING` into nothing.
- `t_fstSpeakerArrangement::speakers` holds eight entries instead of being an open-ended array. Hosts,
  JUCE's among them, size that struct on the assumption that it already has room for eight speakers,
  so with the open-ended array they wrote past the end of the block when loading any VST2 plugin.
- A stray second semicolon after the `t_fstHostOpcode` enum is removed (clang warns about it).
- The host and effect opcodes FST hadn't yet documented had placeholder values (100000 and up), so
  JUCE's VST2 host never recognised a plugin asking for, say, its directory or an idle call, and
  asked plugins for their tail or bypass with numbers they didn't know. They now have their real
  values, as published in other free hosts' VST2 headers (the VeSTige headers in LMMS and Carla,
  and the `vst` Rust crate) and consistent with the opcodes FST already knew. The shim checks at
  compile time that every opcode JUCE uses has a real value.
- The pointer-sized integer is 64 bits on every 64-bit Windows target (`_WIN64`), ARM64 included,
  not only on x64.
- The output pin flags (`kVstPinIsActive`, `kVstPinIsStereo`, `kVstPinUseSpeaker`) were 0, so MPE
  Roll's VST2 build told hosts its outputs were inactive mono pins. The program name length is 24,
  not 25. In `t_fstTimeInfo` the SMPTE offset and frame rate are the two fields before
  `samplesToNextClock` (FST had called them `currentBar` and `magic`, and put the SMPTE fields after
  `flags`, past the end of the struct hosts pass). The same sources agree on all three, and the shim
  checks them at compile time.
- `t_fstPinProperties` ends with 48 reserved bytes, making it 128 bytes as hosts and plugins size it.
  Without them, a plugin filling the struct wrote past the end of the host's copy: JUCE's VST2 plugin
  code writes one byte past `shortLabel`, which crashed MPE Roll's VST2 host on Windows.
