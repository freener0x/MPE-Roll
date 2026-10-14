# MPE Roll

An MPE piano roll for FL Studio. MPE Roll is an instrument plugin (VST3, plus AU on macOS and a
standalone app) with its own piano roll. You load your synth *inside* it, so every note gets its own
pitch glide, pressure, timbre and controllers. FL Studio's own piano roll can't send per-note
expression to plugins.

For FL's native synths (FLEX, Sytrus, Harmor, 3xOsc...), which can't receive MPE at all, MPE Roll
writes a piano roll script. The script rebuilds the clip in FL's own piano roll using slide notes,
fine pitch and Mod X / Mod Y.

![MPE Roll editor](docs/images/editor.png)

> **Status:** early version. The MPE engine, MIDI files, recording and the FL script are covered by
> automated tests. The plugin has been checked end to end against a JUCE MPE synth. It has **not
> been tried inside FL Studio yet**. See [Known limitations](#known-limitations).

## What it does

**Every MPE expression, per note**

| Expression | Sent to the synth as |
| --- | --- |
| Strike (note-on velocity) | Note On velocity, optionally 14-bit with a CC88 prefix |
| Lift (release velocity) | Note Off velocity, optionally 14-bit with a CC88 prefix |
| Glide (pitch) | 14-bit pitch bend on the note's channel, any range up to ±96 semitones |
| Press (pressure) | Channel pressure, poly aftertouch or a CC of your choice |
| Slide (timbre) | CC74 (or any CC), optionally 14-bit with an LSB controller |
| Any other controller | That CC on the note's own channel: pan, volume, expression, vibrato rate/depth... |

**Zone-wide (master channel) lanes:** pitch bend with its own range, pressure, timbre and any
controller, including the sustain, sostenuto, soft, legato and hold pedals.

**The MPE details handled for you:** lower, upper or split zones, channel rotation
(least-recently-released first), what to do when channels run out (share or steal), the MPE
Configuration Message and pitch-bend ranges, each note's starting state sent *before* its Note On,
and a plain-MIDI mode for synths that don't do MPE.

**Editing**
- FL-style note editing: click to add, drag to move or resize, right-click to delete, Ctrl-drag to
  select, Shift-drag to duplicate, Alt to bypass snap.
- **Glide tool (G):** draw pitch bends straight onto notes (Shift snaps to semitones).
- **Expression lane:** draw, line or erase any of the lanes above, on selected notes or every note
  under the pen.
- Undo/redo, copy/paste, duplicate (Ctrl+D/B), quantize (Q), transpose (↑/↓), nudge (←/→).
- 16 clip slots. Each plays in sync with FL (**Follow FL**) or is launched by notes from FL's piano
  roll (**Note trigger**: C0 plays slot 1, C#0 slot 2...).

**In and out**
- **Record** from an MPE controller (Seaboard, LinnStrument, Osmose, Push 3, Continuum...). Takes
  overdub into the clip as one undo step.
- **MIDI files:** import any MIDI file (MPE or not), export MPE MIDI files, or drag the *MIDI file*
  button straight into FL.
- **Send to FL piano roll:** the bridge for FL's native synths (see below).
- The MPE stream also goes to the plugin's MIDI output, so you can route it elsewhere in FL.

## Building

You need CMake 3.22+ and a C++20 compiler. JUCE 9 is downloaded automatically.

**Windows** (Visual Studio 2022 or 2026 with "Desktop development with C++"):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MpeRoll_VST3
```

The plugin ends up in `build\MpeRoll_artefacts\Release\VST3\MPE Roll.vst3`. Copy that folder to
`C:\Program Files\Common Files\VST3`. Or configure with `-DMPEROLL_COPY_PLUGIN_AFTER_BUILD=ON` (from
an administrator prompt) to have it copied for you.

**macOS** (Xcode): `cmake -S . -B build -G Xcode`, then build the `MpeRoll_VST3` or `MpeRoll_AU`
scheme.

**Linux:** install JUCE's dependencies (`libasound2-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxi-dev libfreetype-dev libfontconfig1-dev`), then
`cmake -S . -B build -G Ninja && cmake --build build`.

## Using it in FL Studio

The short version is below. [docs/FL_STUDIO.md](docs/FL_STUDIO.md) has the full walkthrough.

1. **Add it:** *Options › Manage plugins › Find installed plugins*, then add **MPE Roll** to the
   Channel Rack like any instrument.
2. **Load your synth:** click *Load synth…*. The first time, choose *Scan for plugins…*, or browse to
   a `.vst3` file. *Edit synth* opens the synth's own window.
3. **Turn on MPE in the synth** and set its MPE pitch-bend range to **48** (or set MPE Roll's range
   to match the synth's in *MPE settings*). Synths that honour the MPE Configuration Message, like
   JUCE-based and ROLI Equator synths, set themselves up.
4. **Write notes** in MPE Roll's piano roll and press play in FL. In *Follow FL* mode the clip follows
   the song or pattern position. In *Note trigger* mode, put notes in FL's piano roll for MPE Roll's
   channel to launch clip slots.

**Recording an MPE controller:** in FL's MIDI settings, enable the controller and give it a
**Port** number. In MPE Roll's wrapper settings (the gear icon), set the **MIDI input port** to the
same number. Arm *Rec* in MPE Roll and play FL.

**FL's native synths:** click *Send to FL piano roll*. MPE Roll writes `MPE Roll Import.pyscript`
into `Documents\Image-Line\FL Studio\Settings\Piano roll scripts`. In the piano roll of a native
synth's channel, run it from the Scripts menu:

- Each overlapping note gets its own colour, because slide notes only bend notes of the same colour.
- Glides become chains of slide notes, and a note's starting offset becomes fine pitch.
- Pressure and timbre become Mod X / Mod Y. Map those to whatever you like in the synth's per-note
  modulation.
- Velocity, release and per-note pan carry over. Zone pitch bend can be folded into every note, and
  the sustain pedal into note lengths.

## Known limitations

- **Not yet tested inside FL Studio.** Development and testing so far ran on Linux. FL-specific
  behaviour (MIDI port routing, the script bridge's fine-pitch units, slide notes carrying Mod X/Y
  ramps) follows Image-Line's documentation and forum reports, not hands-on checks. Please open an
  issue with what you find.
- The fine-pitch unit FL uses in scripts isn't confirmed. The generated script uses tenths of a
  semitone; if glides land at the wrong pitch, change `PITCHOFS_PER_SEMITONE` at the top of the
  script to `100`.
- Only VST3 synths can be hosted (plus AU on macOS). VST2 and CLAP aren't supported yet.
- The hosted synth's parameters aren't exposed to FL for automation yet. Automate them inside the
  synth, or use the per-note and zone controller lanes.
- If FL also sends your controller to the *selected* Channel Rack channel, notes can play twice.
  Keep another channel selected while recording.
- FL's native synths only get what FL's piano roll can express: one colour per overlapping note, at
  most 16 at a time.

## How it's built

```
source/core/      The MPE engine. Only needs juce_core/juce_audio_basics, so it's fully unit tested.
  MpeClip           notes, expression curves, clip slots' data model
  MpeEncoder        notes -> MPE MIDI (zones, channel allocation, MCM/RPN, CC88, 14-bit timbre...)
  MpeDecoder        MIDI -> notes (controllers, files, FL), including MCM/RPN parsing
  ClipPlayer        sample-accurate playback with looping and note chasing
  ClipBuilder       turns decoded MIDI into notes and curves (recording, file import)
  MidiFileIO        Standard MIDI File import/export
  FlScriptExporter  the FL Studio piano roll script bridge
source/plugin/    The plugin: processor, hosted synth, document/undo, and the editor UI.
tests/            Unit tests, plus a Python stand-in for FL's flpianoroll module that runs the
                  generated scripts.
tools/            Developer harness and a tiny MPE test synth (-DMPEROLL_BUILD_DEV_TOOLS=ON).
```

Run the tests with `ctest --test-dir build`. With the dev tools enabled, `MpeRollHarness selftest`,
`uitest` and `render "<path to MPE Roll Test Synth.vst3>" out.wav` exercise the full plugin. The
render check confirms that per-note glides reach a hosted MPE synth.

## License

AGPL-3.0, the open-source licence JUCE is available under. See [LICENSE](LICENSE).
