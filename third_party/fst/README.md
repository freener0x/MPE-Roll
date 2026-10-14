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
