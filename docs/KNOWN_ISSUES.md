# Known issues
<!-- Mirrors the README's "Known limitations" list: when one changes, change the other too. -->

What's known not to work yet, or to work differently from what you'd expect, in release 0.2.0 and
the builds made since. For each: what you'll see, what to do meanwhile, and where it stands.
Problems with a particular setup (a synth that won't load, no sound, glides that are too big) are in
the [FL Studio guide's troubleshooting table](FL_STUDIO.md#troubleshooting). Anything not here:
please [open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose); the tracker has a
form each for a synth that won't load, for sound or expression that doesn't arrive, for other bugs,
and for ideas, and asks for the build line from the bottom of MPE Roll's File menu.

The issues that 0.2.0 fixed are listed at the end of this page.

- **Tested in FL Studio on Windows 11 only.** Release 0.1.0 was tested in FL Studio 26.1.7 on
  Windows 11: the installer (over an earlier build too), Analog Lab as VST2 and as VST3 (zone-wide
  lanes work; per-note lanes need *Plain MIDI* or *One copy per note*, since Analog Lab has no
  MPE), Omnisphere 3 over MPE, Surge XT as CLAP, and *Send to FL piano roll*. Release 0.2.0 was
  tested in the same FL as a VST3: the installer over 0.1.0, *Send to FL piano roll* saving into
  FL's scripts folder by itself, the update check, *Report a bug…*, Analog Lab (VST2 and VST3) with
  FL's undo history, and the Channel Rack switch (below). Nothing has been tried in another host,
  and MIDI port routing and recording, and slide notes carrying Mod X / Mod Y ramps in FL, follow
  Image-Line's documentation and user reports. *What to do:* try it, and report what you find, good or bad. *Status:* more reports
  wanted.
- **Switching MPE Roll's channel off in the Channel Rack doesn't stop its sound in plain *Follow
  FL* mode** (the default). In that mode MPE Roll plays its clip from FL's transport, not from
  notes, and FL Studio 26.1.7 sends a channel switched off nothing at all (no All Notes Off: checked
  with 0.2.0), so MPE Roll can't tell and the clip plays on. *What to do:* turn on *MPE settings ›
  Playback › Follow FL only while FL holds a note* and give MPE Roll's channel a note in FL's piano
  roll for as long as the clip should sound; the switch then stops it (checked with 0.2.0 in FL
  26.1.7), and patterns and the playlist decide when it plays. Or mute MPE Roll's mixer track,
  which works in either case. *Status:* by design, FL's. In *Note trigger* mode, where FL's notes
  start and stop the clips, the switch should work the same way; that hasn't been tried. The FL
  Studio guide's section *Stopping MPE Roll with the Channel Rack switch* has the details.
- **FL's plugin list can keep the old maker name** (seen after the 0.2.0 installer upgraded 0.1.0).
  From 0.2.0 MPE Roll gives its maker as *MPE Roll*, where 0.1.0 gave *Glowing Pancake*, and CI
  checks what its VST3 and VST2 tell a host. Yet FL Studio 26.1.7's *Options › Manage plugins* list
  went on naming *Glowing Pancake* as the VST3's vendor, even after FL's entry for it in its plugin
  database (`Documents\Image-Line\FL Studio\Presets\Plugin database\Installed\Generators\VST3`) was
  deleted and the plugins scanned again with *Verify plugins*. MPE Roll loads and plays as it
  should; only the name shown is old. *What to do:* FL's *Manage plugins* window lets you change
  the vendor it shows. If yours names *Glowing Pancake* too, please say what
  `C:\Program Files\Common Files\VST3\MPE Roll.vst3\Contents\Resources\moduleinfo.json` gives as
  its `"Vendor"` (open it in Notepad), in an issue. *Status:* looking into where FL keeps the old
  name.
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
- **The update check is new** (in 0.2.0 and later, off unless you turn it on under
  *MPE settings › Updates*). It's tested automatically against a stand-in for GitHub's answers, in
  the Linux and Windows builds' checks, and has asked GitHub itself from Windows 11 (in FL Studio
  26.1.7, with 0.2.0, which it found up to date), but not yet from macOS, nor through a proxy; the
  experimental Linux build can't ask GitHub at all (no HTTPS there),
  and says so. On a Mac it asks GitHub's API alone, not the release page as well (the
  [Privacy](PRIVACY.md) page says why), so a network that has used up the API's 60 requests an
  hour can't check there until the hour is out: *File › Check for updates now…* then says GitHub's
  limit of requests from your network is used up. *What to do:* if it says it couldn't check for
  any other reason while your browser reaches github.com, please report it with the *Last check*
  line from *MPE settings › Updates*. The
  [latest release](https://github.com/freener0x/MPE-Roll/releases/latest) is always on GitHub.
  *Status:* works on Windows; waiting on a test on macOS.
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

## Fixed in 0.2.0

Both were seen in release 0.1.0 in FL Studio 26.1.7, and both fixes were checked there with 0.2.0
(MPE Roll as a VST3), besides MPE Roll's own tests: if you still see either in 0.2.0, please
[open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose).

- **With Analog Lab, FL's undo history fills with *Knob tweak (channel1 pitch bend)* and the like
  while notes glide** (release 0.1.0). Analog Lab gives each MIDI channel's pitch bend a parameter
  of its own, which follows every bend MPE Roll sends, and 0.1.0 passes those moves on to FL as
  edits. The sound isn't affected, but your own edits get buried among those entries. 0.2.0 keeps
  those moves from FL when the synth is a VST2 or a VST3 (Analog Lab as both, in FL Studio
  26.1.7); the [troubleshooting table](FL_STUDIO.md#troubleshooting)'s row on it says how to see
  whether any still get through with another synth.
- **Send to FL piano roll asks where to save** (release 0.1.0). It writes `MPE Roll Import.pyscript`
  straight into `Documents\Image-Line\FL Studio\Settings\Piano roll scripts` only when that folder
  exists already; otherwise a save dialog opens at Documents, and the box afterwards reads as if all
  were well wherever you saved. 0.2.0 saves it into FL's scripts folder itself, making that folder
  when needed, asks where to save only when FL's folder isn't on the computer, and warns, with a
  *Show file* button, when the file lands anywhere else. On Windows it looks for the user data
  folder where FL's installer records it; whether that follows a folder moved in FL's settings
  hasn't been tried, so if the box names a folder outside the user data folder *Options › File
  settings* shows, move the file into that one's `FL Studio\Settings\Piano roll scripts`.
