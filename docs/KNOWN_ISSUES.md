# Known issues
<!-- Mirrors the README's "Known limitations" list: when one changes, change the other too. -->

What's known not to work yet, or to work differently from what you'd expect, in release 0.1.0 and
the builds made since. For each: what you'll see, what to do meanwhile, and where it stands.
Problems with a particular setup (a synth that won't load, no sound, glides that are too big) are in
the [FL Studio guide's troubleshooting table](FL_STUDIO.md#troubleshooting). Anything not here:
please [open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose); the tracker has a
form each for a synth that won't load, for sound or expression that doesn't arrive, for other bugs,
and for ideas, and asks for the build line from the bottom of MPE Roll's File menu.

The next release is 0.2.0. "Fixed in the next release" means the fix is on the main branch now, so
a build made since (see the README's
[Downloading a build](https://github.com/freener0x/MPE-Roll#downloading-a-build)) has it already.

- **Tested in FL Studio on Windows 11 only.** Release 0.1.0 was tested in FL Studio 26.1.7 on
  Windows 11: the installer (over an earlier build too), Analog Lab as VST2 and as VST3 (zone-wide
  lanes work; per-note lanes need *Plain MIDI* or *One copy per note*, since Analog Lab has no
  MPE), Omnisphere 3 over MPE, Surge XT as CLAP, and *Send to FL piano roll*. Nothing has been tried
  in another host, and MIDI port routing and recording, and slide notes carrying Mod X / Mod Y ramps
  in FL, follow Image-Line's documentation and user reports. *What to do:* try it, and report what
  you find, good or bad. *Status:* more reports wanted.
- **With Analog Lab, FL's undo history fills with *Knob tweak (channel1 pitch bend)* and the like
  while notes glide** (release 0.1.0). Analog Lab gives each MIDI channel's pitch bend a parameter
  of its own, which follows every bend MPE Roll sends, and 0.1.0 passes those moves on to FL as
  edits. The sound isn't affected, but your own edits get buried among those entries. *What to do:*
  use a build made since 3 November 2026, which keeps those moves from FL whatever the synth's
  format (the troubleshooting table's row on it says how to see whether any still get through), or
  wait for 0.2.0. *Status:* fixed in the next release, 0.2.0 (on main since 3 November 2026).
- **Switching MPE Roll's channel off in the Channel Rack doesn't stop its sound** (release 0.1.0,
  seen in *Follow FL* mode, the default). FL's channel switch stops the notes FL sends a channel,
  and in *Follow FL* mode MPE Roll plays its clip from FL's transport, not from notes, so the clip
  plays on. *What to do:* mute MPE Roll's mixer track, or stop FL (turning the channel's volume
  knob down silences it too). *Status:* addressed on main for 0.2.0, waiting on a test in FL. An
  All Notes Off or All Sound Off from FL now stops the clip until the transport restarts, the song
  jumps or loops, or FL sends a note, and *MPE settings › Playback › Follow FL only while FL holds a
  note* lets FL's notes, and so patterns and the playlist, decide when it sounds. Whether the
  switch itself then stops it depends on FL sending All Notes Off, or ending the channel's notes,
  when the channel is switched off, which hasn't been checked; the plugin log notes the first All
  Notes Off FL sends. The FL Studio guide's section *Stopping MPE Roll with the Channel Rack
  switch* has the details.
- **Omnisphere 3 needs its MPE switch on.** Omnisphere 3's VST3 doesn't take VST3 note
  expressions, so with MPE off in its System page (*MPE settings › Synth is played with* then
  begins "VST3 notes on channel 1") no per-note expression comes out of it. *What to do:* turn MPE
  on in Omnisphere's System page; MPE Roll then plays it as "MPE over MIDI", per-note glides,
  pressure and timbre included (checked in FL Studio 26.1.7). *Status:* by design, Omnisphere's.
- **Surge XT's CLAP opens its own window.** In FL Studio 26.1.7, *Edit synth* on Surge XT's CLAP
  opens Surge's window as a separate window over MPE Roll's, with the message "Surge XT opened its
  window separately." and a *Show Surge XT* button in MPE Roll that brings it up again. The synth
  plays with CLAP note expressions with nothing set up in it. *What to do:* use the button.
  *Status:* by design for CLAP synths that can't be embedded. Other CLAP synths' windows haven't
  been tried; please report one whose window doesn't appear at all.
- **The macOS build has never been run by a person.** GitHub Actions builds the VST3, AU and
  standalone app on every code change and runs the unit tests on macOS, but nobody has opened the
  bundles on a Mac: not the plug-ins in a DAW, not the app, not the Gatekeeper dialogs that the
  [unsigned builds page](UNSIGNED_BUILDS.md) describes. *What to do:* if you have a Mac, try the
  release zip following that page and report what happened, whether it worked or not, with the
  exact text of any dialog. *Status:* testers wanted.
- **The Linux build is experimental.** From 0.2.0 on, a release carries
  `MPE-Roll-<version>-Linux.tar.gz` (the VST3, the standalone app and the scanner), and the builds
  between releases carry the same files. CI runs the unit tests and MPE Roll's own test harness on
  Linux, but FL Studio has no Linux version and no Linux host has run the build. *What to do:* if
  you try it in a Linux host (Bitwig, Reaper, Ardour...), please report what happened, whether it
  worked or not. *Status:* testers wanted.
- **Send to FL piano roll asks where to save** (release 0.1.0). MPE Roll writes
  `MPE Roll Import.pyscript` straight into `Documents\Image-Line\FL Studio\Settings\Piano roll
  scripts` only when that folder exists already; otherwise a save dialog opens at Documents, and
  the box afterwards reads as if all were well wherever you saved. *What to do:* save it into
  `C:\Users\<you>\Documents\Image-Line\FL Studio\Settings\Piano roll scripts`, creating the folder
  if need be (if you've moved FL's user data folder, the `FL Studio\Settings\Piano roll scripts`
  inside that one). FL then lists the script under the piano roll's *Tools* scripts and runs it,
  and the glides land in tune (checked in FL Studio 26.1.7). A script saved anywhere else isn't
  listed by FL: move it there. *Status:* fixed in the next release, 0.2.0 (on main): MPE Roll
  finds FL's folder, makes the scripts folder when needed and saves there itself, asks only when
  FL's folder isn't on the computer, and warns, with a *Show file* button, when the file lands
  anywhere else.
- **VST3 note expressions are new.** They're tested automatically with a small test synth that maps
  pitch bend on channel 1 only (Linux and Windows), but with no commercial synth yet: Omnisphere 3
  doesn't take them (above), and Serum 2's guide says it does, with its own MPE switch off. *What
  to do:* if a VST3 synth played with note expressions doesn't glide, see the troubleshooting table
  and please send the plugin log. *Status:* reports wanted.
- **One copy per note is new.** It's tested automatically with a small synth without MPE (VST3 and
  VST2, on Linux and Windows, where a copy crashing or throwing on purpose is switched off and never
  called again). In FL Studio 26.1.7 it plays Analog Lab (VST3), FabFilter Twin 3, Serum 1,
  Kontakt 8 and Arcade with each note's own glides (a user's report); in all five the glides
  stopped at 2 semitones, the synth's own pitch-bend range, until it's raised in the synth and
  *Zone bend range* set to match. Nexus is untried. A synth may mind having many copies of itself
  (licence checks, memory). *What to do:* set the synth's bend range (in copy 1's window) and *Zone
  bend range* in *MPE settings* to the same value; the status line says so when a glide goes past
  it. *Status:* by design (the ranges); reports on other synths wanted.
- **CLAP and VST2 hosting are new.** Both are tested automatically with small test synths on Linux
  and compiled for Windows and macOS; the real synths tried are Surge XT's CLAP and Analog Lab's
  VST2, in FL Studio 26.1.7 on Windows 11. VST2 hosting uses FST, a clean-room reimplementation of
  the VST2 headers (with the opcodes it hadn't documented filled in), because Steinberg's SDK can't
  be redistributed. *What to do:* report synths that misbehave, with their format. *Status:*
  reports wanted.
- **The controller presets come from documentation.** The setups under *MPE settings › Controller*
  (ROLI Seaboard and LUMI, LinnStrument, Haken Continuum and ContinuuMini, Expressive E Osmose,
  Ableton Push 3, Keith McMillen K-Board Pro 4 and QuNexus, Embodme Erae, Sensel Morph and Joué,
  and keyboards with poly aftertouch) follow each maker's documentation and haven't been tested on
  the hardware. *What to do:* if recording catches nothing or the wrong lanes, check the
  controller's port in FL's MIDI settings against MPE Roll's input port first, then report what the
  controller sends. *Status:* reports wanted.
- **FL shows synth parameter slots by number until a synth is loaded** (*Synth 1* to *Synth 1024*).
  If FL keeps the old names after you change synths, reopen the project. *Status:* by design, FL's.
- **Notes can play twice while recording** if FL also sends your controller to the selected Channel
  Rack channel. *What to do:* keep another channel selected while recording. *Status:* by design,
  FL's routing.
- **FL's native synths only get what FL's piano roll can express.** *Send to FL piano roll*
  rebuilds the clip in FL's own piano roll with slide notes, fine pitch and Mod X / Mod Y: one
  colour per overlapping note, at most 16 at a time, and pressure and timbre arrive as Mod X / Mod Y
  for you to map in the synth. FL's synths can't be hosted inside MPE Roll (a scan says so when it
  meets them). *Status:* by design, FL's.
