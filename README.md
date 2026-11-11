<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/banner-dark.png">
    <img src="docs/images/banner-light.png" alt="MPE Roll" width="640">
  </picture>
</h1>

An MPE piano roll for FL Studio. MPE Roll is an instrument plugin (VST3, plus AU on macOS and a
standalone app) with its own piano roll. You load your synth *inside* it (VST3, VST2 or CLAP), so
every note gets its own pitch glide, pressure, timbre and controllers. FL Studio's own piano roll
can't send those to a third-party synth (VST, VST3 or CLAP): its slide notes, per-note pitch and
Mod X / Mod Y work with FL's own synths only.

For FL's native synths (FLEX, Sytrus, Harmor, 3xOsc...), which can't receive MPE at all, MPE Roll
writes a piano roll script. The script rebuilds the clip in FL's own piano roll using slide notes,
fine pitch and Mod X / Mod Y.

![MPE Roll's editor in its FL Studio skin](docs/images/editor-fl-studio-skin.png)

> **Status:** early version. The MPE engine, MIDI files, recording and the FL script are covered by
> automated tests, and every change is built and tested on Windows, macOS and Linux. The plugin is
> checked end to end against VST3, VST2 and CLAP test synths. Release 0.1.0 has been **tested in
> FL Studio on Windows 11** (26.1.7) with Analog Lab (VST2 and VST3), Omnisphere 3, Surge XT as
> CLAP, the installer and the script bridge, and release 0.2.0 in the same FL as a VST3 (the
> installer over 0.1.0, the update check, *Report a bug*, the script bridge, Analog Lab's undo
> history and the Channel Rack switch); see [Known limitations](#known-limitations).

## What it does

**Every MPE expression, per note**

| Expression | Sent to the synth as |
| --- | --- |
| Strike (note-on velocity) | Note On velocity, optionally 14-bit with a CC88 prefix |
| Lift (release velocity) | Note Off velocity, optionally 14-bit with a CC88 prefix |
| Glide (pitch) | 14-bit pitch bend on the note's channel, any range up to ±96 semitones |
| Vibrato (depth drawn per note, speed in Hz per clip) | Played by MPE Roll as part of the note's pitch bend, so it needs the same bend range setup as glides (in Plain MIDI, the newest note's) |
| Press (pressure) | Channel pressure, poly aftertouch or a CC of your choice |
| Slide (timbre) | CC74 (or any CC), optionally 14-bit with an LSB controller |
| Any other controller | That CC on the note's own channel: pan, volume, expression, vibrato rate/depth... |

**CLAP synths get it natively.** A CLAP synth is played with CLAP notes and *note expressions*
instead of MPE MIDI, so there are no channels or bend ranges to set up in the synth:

| MPE Roll lane | CLAP note expression |
| --- | --- |
| Glide (plus zone pitch bend) | Tuning, in semitones |
| Press (plus zone pressure) | Pressure |
| Slide (moved by zone timbre) | Brightness |
| Per-note CC7 / CC10 / CC1 / CC11 | Volume / pan / vibrato / expression |

Other zone-wide controllers, such as the sustain pedal or the mod wheel, reach the synth as MIDI on
the master channel and every member channel, so they apply to the notes whichever way the synth
handles channels. (A synth that doesn't take MIDI gets zone CC7/10/1/11 combined into its notes
instead.) Synths that only take MIDI get MPE as usual, and *MPE settings › Note expressions (CLAP,
VST3)* switches to MPE MIDI if you prefer it.

**VST3 synths are asked what they take.** A VST3 synth gets MIDI pitch bend, pressure and
controllers only on the channels it maps them on (that's how VST3 works, and JUCE's host drops the
rest). MPE Roll asks each VST3 synth which channels those are. One that maps them on every channel,
as JUCE-built synths such as Vital and Surge XT do, is played with MPE over MIDI. One that maps
pitch bend on MIDI channel 1 only but takes **VST3 note expressions** is played with those instead:
each note on channel 1 with its own ID, its glide as tuning, and its pressure and timbre as the
synth's own types for MPE's pressure and Y (plus volume, pan, vibrato and expression where it takes
them). One that takes neither can't get each note's own expression, and *MPE settings* says so and
suggests *Plain MIDI*. MPE Roll asks again every few seconds, so a synth whose own MPE switch
changes this is followed.

**Known synths come with directions.** MPE Roll knows the MPE of some 600 instruments from 170
makers, from an audit of what the major stores sell (October 2026): for one with MPE (Serum and
Serum 2, Pigments and the other Arturia instruments, Omnisphere 3, Vital, Diva, Phase Plant, Surge
XT, Equator2, Cherry Audio, SWAM, Icarus...), *MPE settings* and the status line say where its MPE
switch is; for one without (Analog Lab, ZENOLOGY and the Roland Cloud instruments, Massive X,
Sylenth1, Spire, Zebra2, Omnisphere 2, Waves...), MPE Roll offers *Plain MIDI* or *One copy per
note* when you load it. [docs/MPE_SYNTHS.md](docs/MPE_SYNTHS.md) lists 673 instruments, with where
to turn MPE on and sources; those MPE Roll doesn't know itself are mostly libraries that play
inside another plugin, such as Kontakt, which MPE Roll sees as that plugin.

**MPE for any synth: one copy per note.** For a synth without MPE (Serum 1.1, Analog Lab, Kontakt,
Nexus...), MPE Roll can load several copies of it (8 to start with, up to 32) and play each note on
a copy of its own, so every note keeps its own glide, vibrato, pressure and timbre. Copy 1 is the
synth you edit: its parameters reach the other copies at once, and a new preset as soon as the synth
reports it, its window closes, or you ask, without cutting notes that are playing. Quiet copies
sleep, the rest are played on several CPU cores (one set of threads for every MPE Roll), keyswitches
and modifier keys can go to every copy, and a copy that crashes (on Windows) or throws an error is
switched off instead of taking FL with it. Copies stop loading when memory runs low, and after a
crash while loading them they aren't loaded again until you ask. See
[docs/FL_STUDIO.md](docs/FL_STUDIO.md#one-copy-per-note-mpe-for-any-synth).

**Zone-wide (master channel) lanes:** pitch bend with its own range, pressure, timbre and any
controller, including the sustain, sostenuto, soft, legato and hold pedals.

**The MPE details handled for you:** lower, upper or split zones, channel rotation
(least-recently-released first), what to do when channels run out (share or steal), the MPE
Configuration Message and pitch-bend ranges, each note's starting state sent *before* its Note On,
and a plain-MIDI mode for synths that don't do MPE.

**Editing**
- FL's piano roll tools, with FL's keys, on an icon toolbar (hover for the words): **Draw (P)**
  click to add, drag to move or resize, right-click to delete, Ctrl-drag to select, Shift-drag to
  duplicate, Alt to bypass snap; **Paint (B)** drags out a run of snapped notes; **Delete (D)**;
  **Slice (C)** cuts a note where you click, or every note a vertical drag crosses, each note's
  glide and expression cut with it; **Select (E)** boxes or clicks notes.
- **Glide tool (G):** draw pitch bends straight onto notes. Glides snap to whole semitones and the
  grid (Alt draws freely).
- **Tools menu, as in FL's piano roll and with FL's keys:** quantize (Alt+Q; Ctrl+Q quick), chop
  (Alt+U; Ctrl+U quick), glue (Ctrl+G), arpeggiate (Alt+A), strum (Alt+S), flam (Alt+F), legato
  (Ctrl+L), articulate (Alt+L), limit (Alt+K), flip (Alt+Y), randomize (Alt+R) and scale levels
  (Alt+X). A tool's dialog previews on the clip as you change it, through the synth; Accept keeps it
  as one undo step. With nothing selected a tool works on every note, as in FL.
- **Expression lane:** draw, line or erase any of the lanes above, on selected notes or every note
  under the pen. The vibrato lane shows on the notes as a wavy line.
- FL's editing keys: undo/redo, copy/paste, duplicate (Ctrl+B), deselect (Ctrl+D), invert the
  selection (Shift+I), quick quantize (Ctrl+Q; Shift+Q starts only), discard lengths (Shift+D),
  transpose (↑/↓ a semitone, Ctrl an octave), nudge (←/→).
- 16 clip slots. Each plays in sync with FL (**Follow FL**) or is launched by notes from FL's piano
  roll (**Note trigger**: C0 plays slot 1, C#0 slot 2...).
- **Ten skins**, FL Studio's look by default, any colour of them changeable, and a background
  picture of your own behind the grid.

**Your synth, any format**
- Hosts VST3, VST2 and CLAP instruments (plus AU on macOS), with a plugin scanner and a crash-safe
  blacklist. The synth's own window opens from *Edit synth*.
- **Automate the synth from FL:** the synth's parameters show up as MPE Roll's parameters, named
  after them (*Synth 1* to *Synth 1024*). Move a knob in the synth's window, then use FL's
  *Tools › Last tweaked* to make an automation clip or link a controller. The link survives saving,
  even if the synth's parameters are reordered by an update.

**In and out**
- **Record** from an MPE controller. *MPE settings › Controller* has ready-made setups for ROLI
  Seaboard and LUMI, LinnStrument, Haken Continuum and ContinuuMini (including MPE+ high resolution),
  Expressive E Osmose, Ableton Push 3, Keith McMillen K-Board Pro 4 and QuNexus, Embodme Erae,
  Sensel Morph and Joué, and keyboards with poly aftertouch. Each shows how to set the controller
  up. Takes overdub into the clip as one undo step.
- **MIDI files:** import any MIDI file (MPE or not), export MPE MIDI files, or drag the *File*
  button straight into FL.
- **Export audio:** *File › Export audio as WAV / MP3* plays the clip through a copy of your synth
  (same sound, faster than real time, without touching the one playing) and writes a 24-bit WAV or a
  320 kbps MP3, including the synth's release tail.
- **Send to FL piano roll:** the bridge for FL's native synths (see below).
- The MPE stream also goes to the plugin's MIDI output, so you can route it elsewhere in FL.

## Downloading a build

**Releases.** The [latest release](https://github.com/freener0x/MPE-Roll/releases/latest) has the
Windows installer (`MPE-Roll-Setup-<version>.exe`), the Windows files (`MPE-Roll-<version>-Windows.zip`)
and the macOS bundles (`MPE-Roll-<version>-macOS.zip`) attached, with notes on what changed since the
one before ([CHANGELOG.md](CHANGELOG.md) keeps them all). From 0.2.0 on, a release also carries an
experimental Linux build (`MPE-Roll-<version>-Linux.tar.gz`) and `SHA256SUMS.txt`, the checksums of
the files ([how to check a download](docs/UNSIGNED_BUILDS.md#verify-what-you-downloaded)). For the
releases after 0.2.0, GitHub also keeps a signed record that the files were built by this
repository's release workflow (an artifact attestation, signed by the workflow through Sigstore),
which `gh attestation verify` checks a download against; the same page says how.

**Releasing (maintainers).** Set the version in `CMakeLists.txt` (`project(MPE_ROLL VERSION
…)`, three numbers such as `0.3.0`), rename the *[Unreleased]* section of `CHANGELOG.md` to that
version and the day, merge, then either tag that commit `v<version>` and push the tag, or open the
*Release* workflow under *Actions* and *Run workflow* on `main`, which tags main's commit for you.
The workflow checks the tag against the version and that `CHANGELOG.md` has a section for it,
builds everything, runs the unit tests, the installer's test and Apple's AU validator, and
publishes the release with that section in its notes. A tag with a suffix (`v0.3.0-beta1`, or a
suffix typed into *Run workflow*) is marked a pre-release; it needs the section for its version too
(`## [0.3.0]` for `v0.3.0-beta1`). The suffix is letters and digits with dots between (`beta1`,
`rc.1`), and the build runs as `0.3.0-beta1`, so its update check offers 0.3.0 once that's out.
The update check reads versions only in these shapes, so the workflow refuses others.

Before it publishes, the workflow signs the artifact attestation for the files (through Sigstore,
with the identity GitHub gives the run) and GitHub stores it; that needs nothing set up. After a
release that isn't a pre-release, its *Announce* job tells people, where the repository is set up
for it. Both of these are optional, and a release goes out the same without them:

- **Discord.** Add a repository secret named `DISCORD_RELEASE_WEBHOOK` (*Settings › Secrets and
  variables › Actions › New repository secret*) holding the webhook address of the Discord
  channel to post in (it starts `https://discord.com/api/webhooks/`; Discord's help article
  *Intro to Webhooks* says how to make one). The job then posts the release's name, its link, the
  start of its first changes from `CHANGELOG.md` and the website's address there. Without the
  secret it posts nothing. Anyone who has the address can post in that channel, so keep it
  nowhere but in the secret.
- **GitHub Discussions.** Turn Discussions on (*Settings › General › Features*) and keep their
  *Announcements* category: the job opens a discussion "MPE Roll \<version\>" there with the
  version's `CHANGELOG.md` section. Without Discussions or that category it opens none and says
  so. It needs no secret. This hasn't run yet: GitHub lets only maintainers post in
  *Announcements*, and the first release will show whether it accepts the workflow's own token.

An announcement that fails doesn't fail the release: the run stays green (the *Announce* job is
allowed to fail, and shows as failed on the run's page if it does), and the run's summary page
says what went wrong. To try again, re-run the *Announce* job alone from the run's page; a
discussion isn't opened twice, but a Discord post that went through is posted again. A run that
replaces the files of a release already out doesn't announce it again. The workflow uses no other
secret: the rest is done with the token GitHub gives each run. No release has been made since the
attestation and the announcements were added: their steps were tried with stand-ins for GitHub and
Discord, and the first release will show how they do for real.

**Builds between releases.** Every code change is built by GitHub Actions (a change to the
documentation alone builds nothing).
Open the repository's **Actions** tab (you need to be signed in to GitHub), pick the latest successful
**Build** run, and download **MPE-Roll-Windows-Setup-build-N** (the installer) or
**MPE-Roll-Windows-build-N** (the files, to copy by hand) under *Artifacts* (N is the run's number,
shown as #N beside it), **MPE-Roll-macOS-build-N** for a Mac, or **MPE-Roll-Linux-build-N**
(experimental) for Linux. Each build names itself at the bottom of the File menu and of *MPE
settings*, e.g. "MPE Roll 0.1.0, build 63, f2ea3c4, 2026-10-26": the run number, the commit and the
day it was built. If that isn't the build you downloaded, the DAW loaded an older copy (see the FL
Studio guide's troubleshooting table).

**The Windows installer.** Close FL Studio and run `MPE-Roll-Setup-<version>.exe` from the release
(a build between releases comes zipped: unzip it and run `MPE-Roll-Setup-build-N.exe`).
It isn't signed, so Windows may say *Windows protected your PC*: choose *More info*, then *Run
anyway*. It installs the VST3 in `C:\Program Files\Common Files\VST3` and, unless you untick them,
the VST2 `MPE Roll.dll` (with `MPE Roll Scanner.exe` beside it) in a folder you choose, and the
standalone app in `C:\Program Files\MPE Roll` with a Start menu entry. Before installing it looks
for other copies of MPE Roll: the ones FL Studio's plugin database lists (for the Windows account
that runs the installer; copies inside FL's own folder aren't looked for), and any in the plugin
folders (`Common Files\VST3` and `VST2`, `Program Files\VST3`, the Steinberg folders and
`Program Files\VSTPlugins`, up to four folders down; the VST2 folder you choose and the registry's
`VSTPluginsPath`, two down). It removes the ones you leave ticked once any program holding them is
closed, so FL can't load an old build by mistake. Run a newer installer over it to update: 0.1.0's
installer was run over an earlier build this way on Windows 11, and CI installs, upgrades and
uninstalls every build's installer. To uninstall, use Windows' *Settings › Apps › Installed apps*;
your settings, scans and plugin log in `%APPDATA%\MPE Roll` stay.

**The Windows files**, to copy by hand instead:
- `MPE Roll.vst3`, the VST3 plugin: copy the whole folder to `C:\Program Files\Common Files\VST3`.
- `MPE Roll.dll`, the same plugin as VST2: put it in a VST plugin folder that FL Studio scans (the
  folders are listed under *Options › Manage plugins*). Install either one, or both; used together in
  one project, each keeps its own list of scanned synths and the last to save it wins.
- `MPE Roll Scanner.exe`: MPE Roll checks plugins with it in a separate process, so a plugin that
  crashes while MPE Roll scans for synths can't take FL Studio down. The `.vst3` folder already
  holds a copy; put this one next to `MPE Roll.dll` (unless the `.vst3` is installed too), and
  replace it whenever you update MPE Roll, since a scanner from another version can't check plugins.
- `MPE Roll.exe`, the standalone app, with `MPE Roll.pdb` beside it. Keep the two together: if the
  app crashes, it saves a report to `%APPDATA%\MPE Roll\Crash report.txt`, and the `.pdb` lets the
  report name the code involved.

**The macOS bundles.** A release's `MPE-Roll-<version>-macOS.zip` holds a folder `MPE Roll`; a
build between releases (**MPE-Roll-macOS-build-N**) holds one more zip, `MPE-Roll-bundles.zip`,
with that folder inside. Open the zip that holds the folder with Finder (or `ditto -x -k <zip> .`),
which keeps the app's permissions; other unzip tools can lose them, and the app then won't start.
Inside are `MPE Roll.vst3` (for `/Library/Audio/Plug-Ins/VST3`), `MPE Roll.component` (for
`/Library/Audio/Plug-Ins/Components`) and the standalone `MPE Roll.app`. Nobody has run the macOS
build yet: CI builds it on every code change, but no person has opened it on a Mac. Testers are
wanted: please say how it went, working or not, in an
[issue](https://github.com/freener0x/MPE-Roll/issues).

Or let a script do it. In Terminal,

```
curl -fsSL https://freener0x.github.io/MPE-Roll/install-macos.sh | bash
```

downloads the latest release's zip (0.2.0 or later), checks it against the release's
`SHA256SUMS.txt`, and copies the three bundles into your own Library folders
(`~/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/Components` and `~/Applications`),
with no password and nothing else changed; it prints what it did and the command that undoes it.
A file fetched this way should get no quarantine flag, and the script takes the flag off its
copies if they have one, so there should be none for you to remove. The script is
[docs/install-macos.sh](docs/install-macos.sh); to read it before running it, to install another
release or to install from a zip you already have, see
[the install script](docs/UNSIGNED_BUILDS.md#macos-the-install-script). GitHub's macOS runner
runs it, and looks for the flag on a file fetched this way, whenever the code changes; in the first
such run (10 November 2026) the file carried no flag and the script installed both that build and
release 0.2.0. That runner is the only Mac it has run on: no person has run it on a Mac of their
own.

**The Linux build** (experimental): `MPE-Roll-<version>-Linux.tar.gz` from 0.2.0 on, or
**MPE-Roll-Linux-build-N** between releases (a zip with `MPE-Roll-Linux.tar.gz` inside). Unpacked
with `tar -xzf`, it's a folder `MPE Roll` with `MPE Roll.vst3` (copy it to `~/.vst3`), the
standalone app and the scanner. FL Studio has no Linux version, and no Linux host has run this
build yet; it's built on Ubuntu 24.04 with the libraries listed under [Building](#building).
Reports from Bitwig, Reaper or Ardour users are welcome. Its update check (below) can't reach
GitHub: JUCE's networking on Linux has no HTTPS, so it says so when asked and sends nothing.

These builds aren't code signed. Windows may show a SmartScreen warning for the installer and the
standalone app, and on macOS you need to remove the quarantine flag from the unzipped `MPE Roll`
folder before a DAW will load the bundles (`xattr -dr com.apple.quarantine ~/Downloads/"MPE Roll"`
for the folder unzipped in Downloads; it covers all three), unless the install script above put
them in place.
[docs/UNSIGNED_BUILDS.md](docs/UNSIGNED_BUILDS.md) says what each system shows, how to get past
it, and how to check a download.

**Update check (off by default).** MPE Roll can tell you when a new release is out. Turn it on
under *MPE settings › Updates*, or ask once with *File › Check for updates now…*. When it checks,
MPE Roll sends one plain HTTPS request to GitHub for the latest release (api.github.com, or on
Windows github.com/freener0x/MPE-Roll/releases/latest if that fails). Nothing about you, your
computer or your project goes with it beyond what every web request carries: your IP address, the
plugin's name and version in the request's User-Agent header (`MPE Roll/0.2.0`, say), and the
headers your system adds to any request (on a Mac, the languages it prefers, and cookies for
github.com if the host program keeps any). MPE Roll keeps only whether the check is on, the time of
the last check and how it went, the version it found and any version you chose to skip, in a
settings file of its own (`%APPDATA%\MPE Roll\Update check.settings` on Windows), which every MPE
Roll you run goes by: turned off in one, the check is off in those already open too, and a check
waiting for GitHub's answer is called off. Turned on, it checks at most once a day, as MPE Roll
opens; a newer release shows as an *Update to 0.3.0* button (with the version found) beside the
status line until you install or skip it, and the button opens the release's page in your browser.
MPE Roll downloads and installs nothing itself, never offers a pre-release, and
*File › Skip version 0.3.0* keeps it quiet until a newer one is out. A check that gets no answer
says nothing unless you asked for it. 0.2.0 and later have it, release 0.1.0 doesn't.
[docs/PRIVACY.md](docs/PRIVACY.md#the-update-check-off-by-default) has the details.

## Building

You need CMake 3.22+ and a C++20 compiler. JUCE 9, the CLAP headers and the LAME MP3 encoder are
downloaded automatically (`-DMPEROLL_MP3=OFF` builds without MP3 export, and without LAME). The
downloaded JUCE gets a fix to its VST3 hosting from `cmake/PatchJuce.cmake`; with your own JUCE
checkout (`-DFETCHCONTENT_SOURCE_DIR_JUCE=...`), run `cmake -P path/to/PatchJuce.cmake` in it.

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
libxcursor-dev libxi-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev`), then
`cmake -S . -B build -G Ninja && cmake --build build`.

Options: `-DMPEROLL_HOST_VST2=OFF` and `-DMPEROLL_HOST_CLAP=OFF` leave out VST2 or CLAP hosting, and
`-DMPEROLL_BUILD_DEV_TOOLS=ON` adds the test harness and test synths.

## Using it in FL Studio

The short version is below. [docs/FL_STUDIO.md](docs/FL_STUDIO.md) has the full walkthrough, and
[docs/MPE_IN_FL_STUDIO.md](docs/MPE_IN_FL_STUDIO.md) answers "Does FL Studio support MPE?": what
FL's own piano roll does for one note, what it doesn't, and what was and wasn't checked.

1. **Add it:** *Options › Manage plugins › Find installed plugins*, then add **MPE Roll** to the
   Channel Rack like any instrument.
2. **Load your synth:** click *Load synth…*. The first time, choose *Scan for plugins…*, or pick a
   `.vst3`, VST2 `.dll` or `.clap` file with *Load plugin file…*. *Edit synth* opens the synth's own
   window.
3. **Turn on MPE in the synth** and set its MPE pitch-bend range to **48** (or set MPE Roll's range
   to match the synth's in *MPE settings*). Synths that honour the MPE Configuration Message, like
   JUCE-based and ROLI Equator synths, set themselves up. CLAP synths that support note expressions
   need no setup at all.
4. **Write notes** in MPE Roll's piano roll and press play in FL. In *Follow FL* mode the clip follows
   the song or pattern position. In *Note trigger* mode, put notes in FL's piano roll for MPE Roll's
   channel to launch clip slots.

**Recording an MPE controller:** in FL's MIDI settings, enable the controller and give it a
**Port** number. In MPE Roll's wrapper settings (the gear icon), set the **MIDI input port** to the
same number. Pick your controller under *MPE settings › Controller*, arm *Rec* in MPE Roll and play
FL.

**FL's native synths:** click *Send to FL piano roll*. MPE Roll saves `MPE Roll Import.pyscript`
into FL's scripts folder, `<User data folder>\FL Studio\Settings\Piano roll scripts` (normally under
`Documents\Image-Line`). In the piano roll of a native synth's channel, run it from the piano roll
menu › *Tools* (FL Studio 21.1 or newer):

- Each overlapping note gets its own colour, because slide notes only bend notes of the same colour.
- Glides become chains of slide notes, and a note's starting offset becomes fine pitch.
- Pressure and timbre become Mod X / Mod Y. Map those to whatever you like in the synth's per-note
  modulation.
- Velocity, release and per-note pan carry over. Zone pitch bend can be folded into every note, and
  the sustain pedal into note lengths.

## Known limitations

[docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md), the website's Known issues page, has these too, with
what to do about each one meanwhile and where it stands, and lists the issues 0.2.0 fixed. To report
something, *File › Report a bug…* in MPE Roll opens the issue form on GitHub with the build filled
in, and puts the plugin log on the clipboard to paste into it. Nothing is sent until you submit the
form on GitHub.

- **Tested in FL Studio on Windows 11** (26.1.7, releases 0.1.0 and 0.2.0). MPE Roll loads in the
  Channel Rack and plays Arturia Analog Lab, as VST2 and as VST3 (zone-wide lanes; per-note lanes with *Plain
  MIDI*), Spectrasonics Omnisphere 3 over MPE (per-note glides, pressure and timbre included, once
  MPE is on in Omnisphere's System page) and Surge XT as CLAP (with CLAP note expressions, nothing
  set up in the synth). *Send to FL piano roll* asked where to save, because 0.1.0 writes the script
  into FL's folder by itself only when `Documents\Image-Line\FL Studio\Settings\Piano roll scripts`
  already exists where Windows keeps Documents (0.2.0 and later look for FL's folder and save there
  themselves); saved there, the script is listed under the piano roll menu's *Tools* and runs, and
  the glides land in tune, so the fine-pitch unit the script uses (tenths of a semitone) is
  confirmed. The release installer upgraded an earlier build. The standalone app loads synths,
  clicks land where they're aimed after the window is reopened, and a scan that meets FL's own
  plugins says they can't be hosted. The rest of what's said here about FL (MIDI port routing and
  recording, slide notes carrying Mod X/Y ramps) follows Image-Line's documentation and forum
  reports, not tests in FL. Release 0.2.0 was tested in the same FL, as a VST3: the installer
  upgraded 0.1.0 (Installed apps names *MPE Roll* as the publisher, and a project saved with 0.1.0
  opens with its clip), *Send to FL piano roll* saves the script straight into FL's scripts folder,
  *Check for updates now…* asks GitHub and says it's up to date, *Report a bug…* opens the form,
  and Analog Lab, as VST2 and as VST3, no longer fills FL's undo history while notes glide. Please
  open an issue with what you find.
- **Nobody has run the macOS build yet**, nor the experimental Linux build in a Linux host. CI
  builds both and runs the tests on macOS and Linux with every code change, but no person has
  opened the macOS bundles on a Mac. Testers wanted: please say how it went, working or not, in an
  [issue](https://github.com/freener0x/MPE-Roll/issues).
- **VST3 note expressions are new.** They're tested automatically with a small non-JUCE VST3 test
  synth that maps pitch bend on channel 1 only, on Linux and Windows, but not yet with a commercial
  synth that takes them. Serum 2's guide says it takes note expressions with its own MPE switch off.
  Omnisphere 3 with its MPE off is played that way (*VST3 notes on channel 1*), but no per-note
  expression comes out of it over that path: it doesn't take VST3 note expressions, so its MPE has
  to be on (FL Studio 26.1.7, Windows 11).
- **One copy per note is new.** It's tested automatically with a small synth without MPE (VST3 and
  VST2, on Linux and Windows, where a copy crashing or throwing on purpose is switched off and never
  called again). In FL Studio 26.1.7 it plays Analog Lab (VST3), FabFilter Twin 3, Serum 1, Kontakt 8
  and Arcade with each note's own glides (a user's report); in all five the glides stopped at 2
  semitones, the synth's own pitch-bend range, until it's raised in the synth (Analog Lab's *Settings*
  tab, for instruments that have one) and *Zone bend range* set to match, which the status line now
  says when a glide goes past it. Nexus is untried. A synth may mind having many copies of itself
  (licence checks, memory); please report what you find.
- **The update check is new** (in 0.2.0 and later, off unless you turn it on). It's tested
  automatically against a stand-in for GitHub's answers, in the Linux and Windows builds' checks,
  and has asked GitHub itself from Windows 11 (in FL Studio 26.1.7, with 0.2.0), but not yet from
  macOS, nor through a proxy; the experimental Linux build can't ask GitHub at all (no HTTPS
  there), and says so. On a Mac it asks
  GitHub's API alone (Privacy says why), so a network that has used up the API's 60 requests an
  hour can't check there until the hour is out: *File › Check for updates now…* then says GitHub's
  limit of requests from your network is used up. If it says it couldn't check for any other
  reason while your browser reaches github.com, please report it with the *Last check* line from
  *MPE settings › Updates*.
- **CLAP and VST2 hosting are new.** Both are tested automatically with small test synths on
  Linux, and compiled for Windows and macOS. In FL Studio 26.1.7 on Windows 11, Surge XT's CLAP
  plays with CLAP note expressions and opens its own window separately from MPE Roll's, which says
  so and shows a *Show Surge XT* button that brings it up; no other CLAP synth's window has been
  tried. If a CLAP synth's window can't be shown at all, MPE Roll shows a message and the synth
  still plays. Please report synths that misbehave.
- VST2 hosting uses [FST](third_party/fst/README.md), a clean-room reimplementation of the VST2
  headers, because Steinberg's SDK can't be redistributed (with the opcodes FST hadn't documented
  filled in). It has been tested with a JUCE-built VST2 synth and, in FL Studio 26.1.7, with Analog
  Lab's VST2, so please report other VST2 synths that misbehave.
- The controller presets are based on each maker's documentation, not tested on the hardware.
- FL shows synth parameter slots by number until a synth is loaded. If FL keeps old names after you
  change synths, reopen the project.
- **The Channel Rack switch stops MPE Roll only while FL's notes say when it plays.** In plain
  *Follow FL* mode MPE Roll plays the clip from FL's song position, not from FL's notes, and FL
  Studio 26.1.7 sends no All Notes Off when MPE Roll's channel is switched off (the plugin log shows
  none), so the clip plays on. Turn on *MPE settings › Playback › Follow FL only while FL holds a
  note* and give MPE Roll's channel a note in FL's piano roll for as long as the clip should sound:
  the switch then stops it (checked with 0.2.0 in FL 26.1.7), and patterns and the playlist decide
  when it plays. Muting MPE Roll's mixer track silences it in either case. In *Note trigger* mode,
  where FL's notes start and stop the clips, the switch should work the same way, but that hasn't
  been tried (details in the
  [FL Studio guide](docs/FL_STUDIO.md#stopping-mpe-roll-with-the-channel-rack-switch)).
- **FL's plugin list can keep the old maker name.** From 0.2.0 MPE Roll gives its maker as *MPE
  Roll*, where 0.1.0 gave *Glowing Pancake*, and CI checks what its VST3 and VST2 give a host
  (listed, loaded, and in the VST3's `moduleinfo.json`). Yet after the installer upgraded 0.1.0,
  FL Studio 26.1.7's *Manage plugins* list went on naming *Glowing Pancake* as the VST3's vendor
  (and no vendor for the VST2), even once FL's entry for it in the plugin database had been deleted
  and the plugins scanned again with *Verify plugins*, while the installed VST3's own
  `moduleinfo.json` named *MPE Roll*. FL's plugin wrapper had kept an empty registry entry under
  the old name: removing it and scanning again made the list name *MPE Roll*. Only the name shown
  is wrong: MPE Roll loads and plays. The FL Studio guide's
  [Removing the old maker name from FL](docs/FL_STUDIO.md#removing-the-old-maker-name-from-fl)
  has the steps.
- If FL also sends your controller to the *selected* Channel Rack channel, notes can play twice.
  Keep another channel selected while recording.
- FL's native synths only get what FL's piano roll can express: one colour per overlapping note, at
  most 16 at a time.

## How MPE Roll is made

MPE Roll is designed and tested in FL Studio by its maintainer, and written with an AI coding
assistant (Claude, which is why most commits name it as their author). Every change is built and
checked on Windows, macOS and Linux by an automated test suite: unit tests of the MPE engine, an
end-to-end harness that plays clips through VST3, VST2 and CLAP test synths, and an install,
upgrade and uninstall test of the Windows installer. Releases 0.1.0 and 0.2.0 were also tested by
hand in FL Studio 26.1.7 on Windows 11 (see [Known limitations](#known-limitations)).

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
  ControllerPresets setups for popular MPE controllers
  NoteTools         FL's piano roll tools (chop, arpeggiate, strum, glue...) on notes and their curves
  CopyRouter        one copy per note: shares an MPE stream out over copies of a synth, one note each
source/plugin/    The plugin: processor, hosted synth, automation slots, document/undo, the editor UI.
  CopyWorkers       plays a synth's copies on several cores (the audio thread helps, never waits idle)
  CopySafety        catches a copy that crashes (Windows) or throws, and how much memory is free
  clap/             CLAP hosting: plugin format, instance, and the MPE to note expression translator
tests/            Unit tests, plus a Python stand-in for FL's flpianoroll module that runs the
                  generated scripts.
tools/            Developer harness and tiny MPE test synths: VST3 and VST2 (JUCE), CLAP (plain CLAP
                  API, pitch only from note expressions), a VST3 one on the plain VST3 interfaces
                  that maps pitch bend on channel 1 only and takes note expressions, and a plain one
                  without MPE (VST3 and VST2) for one copy per note. Needs -DMPEROLL_BUILD_DEV_TOOLS=ON.
                  make_donate_page.py writes DONATE.md, the website's Support and share pages and
                  its front page's front matter (docs/donate.html, docs/share.html,
                  docs/index.html), the QR codes and .github/FUNDING.yml from docs/donate.json.
                  make_synth_list.py writes docs/MPE_SYNTHS.md and source/plugin/KnownSynths.h, the
                  synths MPE Roll knows the MPE of, from docs/synths.json (CI checks they match).
                  shoot_images.sh and make_demo.sh draw the website's pictures and its demo film
                  with the harness, and check_site.py checks the built website. check_spdx.py
                  checks that the files of the plugin's code, tests, tools, build, installer and
                  CI workflows start with MPE Roll's SPDX header (see License; CI runs it).
third_party/fst/  Clean-room VST2 headers (GPL-3.0-or-later), and two include shims of MPE Roll's
                  that point JUCE's VST2 hosting at them (pluginterfaces/vst2.x/).
third_party/lame/ The build configuration for the LAME MP3 encoder (LAME itself is downloaded).
cmake/            Fixes and additions applied to the downloaded JUCE: an empty VST3 MIDI mapping, the
                  hosted VST3's edit controller, and VST3 note events handed over in the MIDI buffer.
```

Run the tests with `ctest --test-dir build`. With the dev tools enabled, `MpeRollHarness selftest`
and `uitest` exercise the full plugin, and `render <test synth> out.wav`, `params <test synth>`,
`clapdialect <test synth.clap>` and `vst3dialect <note expression synth.vst3> <test synth.vst3>`
check that per-note glides, parameter automation and CLAP and VST3 note expressions reach a hosted
synth, and `export <test synth> <folder>` exports a clip as WAV and MP3 and reads both back. CI runs
all of them.

## Support MPE Roll

MPE Roll is free and open source, with no ads and no tracking ([docs/PRIVACY.md](docs/PRIVACY.md)
says what it keeps on your computer, and what its update check, off unless you turn it on, sends).
If it helps you, a donation keeps it going: see
[DONATE.md](DONATE.md) (Cash App, or Bitcoin, Ethereum, Solana and Monero by QR code). Inside the
plugin, *MPE settings › Support MPE Roll* (the link at the foot of the settings) opens the same
details in a window of its own, QR codes and Copy buttons included, with nothing to load from the
web. The page is also on MPE Roll's website, <https://freener0x.github.io/MPE-Roll/donate.html>.
Beside the link, *Share MPE Roll* opens the website's share page, with posts ready for X, Instagram
and Facebook.

The website (<https://freener0x.github.io/MPE-Roll/>) is the `docs/` folder, which GitHub Pages
builds with Jekyll in GitHub's Cayman theme (*Settings › Pages › Build and deployment*: *Deploy from a
branch*, `main`, `/docs`). The front page is laid out by `docs/_layouts/home.html` from the words in
`docs/_data/features.yml`, edited by hand, and the pictures `docs/_data/screenshots.yml` lists:
`tools/shoot_images.sh` draws those of the editor and `tools/make_demo.sh` the demo film, both with
the harness (`MpeRollHarness shoot` and `demo`), and the two from FL Studio are screenshots taken in
FL Studio 26.1.7 on Windows 11. `tools/make_donate_page.py` makes the Support page, the share page
(`docs/share.html`, posts ready for X, Instagram and Facebook) and the front page's front matter
and donation paragraph; the "Does FL Studio support MPE?" page, the FL Studio guide, the list of
synths and the Known issues, Unsigned builds and Privacy pages become pages as they are, and
`docs/install-macos.sh` is served as the file it is. `docs/_config.yml` says how it's put together,
gives each guide its description for search engines, and holds the steps for Google Search Console.
CI builds the site and runs `tools/check_site.py` on it: nothing may be loaded from another site,
no link into the site may lead nowhere, `sitemap.xml` may list only pages that are there, the front
page's software data must parse and name addresses that exist, and every page must have a
description of its own, of 160 characters at most. Its pages link to the repository and its licence;
should the repository ever be made private, set `"repoPublic": false` in `docs/donate.json` and run
`tools/make_donate_page.py`, and the website then links to nothing in it, since visitors would get
a 404 there.

## More from the maker

[stonedmango.com](https://stonedmango.com) is a free, open-source key and mode finder that runs in
the browser; your music never leaves your device. The website's front page links to it too, in its
last band (the `more` block of `docs/_data/features.yml`).

## License

MPE Roll is an independent project, not made by or affiliated with Image-Line. FL Studio is
Image-Line's trademark, and the other product names here belong to their owners.

Copyright 2026 The MPE Roll contributors. MPE Roll's own files are licensed under the
AGPL-3.0-only, the open-source licence JUCE is available under: see [LICENSE](LICENSE). The files of
the plugin's code, its tests, tools, build, installer and CI workflows say so in their first
comment, an SPDX header (`SPDX-FileCopyrightText: 2026 The MPE Roll contributors` and
`SPDX-License-Identifier: AGPL-3.0-only`). They are the files in `source/`, `tests/`, `tools/`,
`installer/`, `cmake/` and `third_party/lame/`, the two include shims in
`third_party/fst/pluginterfaces/vst2.x/`, `CMakeLists.txt`, the scanner's resource script and the
workflows. CI checks them with `tools/check_spdx.py`, whose `--fix` adds a missing header.

`third_party/fst/` holds the FST headers, which keep FST's licence (GPL-3.0-or-later, see
[third_party/fst](third_party/fst/README.md)); the two shims beside them, and the LAME build
configuration in `third_party/lame/`, were written for MPE Roll. MPE Roll builds against the CLAP
headers (MIT). MP3 export uses [LAME](https://lame.sourceforge.io) 3.100 (LGPL-2.0-or-later), built
from its unchanged source as a static library; MPE Roll's own source being available is what lets
anyone rebuild it with another version of LAME.
