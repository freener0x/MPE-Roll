# Using MPE Roll in FL Studio

FL Studio's piano roll can't send per-note expression to plugins. MPE Roll gets around that by being
the instrument FL sees: it holds its own MPE piano roll and plays a synth hosted inside it.

> These steps follow Image-Line's documentation and user reports. Release 0.1.0 has been tested in
> FL Studio 26.1.7 on Windows 11: the installer (over an earlier build too), Analog Lab as VST2 and
> as VST3, Omnisphere 3 over MPE and Surge XT as CLAP (section 7), and *Send to FL piano roll*
> (section 5). Release 0.2.0 was tested in the same FL as a VST3: the installer over 0.1.0, *Send
> to FL piano roll*, the update check, *Report a bug…*, Analog Lab with FL's undo history (section
> 7) and the Channel Rack switch (section 3). The rest rests on the documentation and reports, not
> on tests in FL. If something here doesn't match what you see, please open an issue.

## 1. Install

1. Download the [latest release](https://github.com/freener0x/MPE-Roll/releases/latest): the Windows
   installer, the Windows files to copy by hand, or the macOS bundles. Or take a build made since
   (see [Downloading a build](https://github.com/freener0x/MPE-Roll#downloading-a-build)), or build
   it yourself.
2. On Windows, close FL Studio and run the installer, `MPE-Roll-Setup-<version>.exe` (a build between
   releases comes zipped, with `MPE-Roll-Setup-build-N.exe` inside). If Windows says
   *Windows protected your PC*, choose *More info*, then *Run anyway*: the downloads aren't
   code-signed, and [Unsigned builds](UNSIGNED_BUILDS.md) says what Windows and macOS show, and why.
   For the VST2, pick one of the folders listed under *Options › Manage plugins*. The installer
   offers to remove older copies of MPE Roll it finds in the plugin folders; leave them ticked
   unless a project of yours still needs one. To install by hand instead, copy the whole
   `MPE Roll.vst3` folder to `C:\Program Files\Common Files\VST3` (macOS:
   `/Library/Audio/Plug-Ins/VST3`, once the download mark is off the unzipped `MPE Roll` folder:
   `xattr -dr com.apple.quarantine ~/Downloads/"MPE Roll"`). On Windows you can use `MPE Roll.dll`,
   the VST2 version, instead: put it in one of the plugin folders listed under *Options › Manage
   plugins*, with `MPE Roll Scanner.exe` beside it (see step 2 below). When you update, close FL
   Studio first (Windows can't replace a plugin file that FL has loaded). With the installer, run
   the newer one over the old (0.1.0's, and 0.2.0's over 0.1.0, were checked this way on Windows
   11); by hand, replace every
   copy you installed before, the `.dll` too if you used it. No one has run the macOS bundles yet;
   if you do, please report how it went.
3. In FL Studio: *Options › Manage plugins › Find installed plugins*. The bottom of MPE Roll's File
   menu names the build that's loaded, e.g. "MPE Roll 0.1.0, release v0.1.0, fd8e433, 2026-11-01"
   (a build between releases says "build 63" and its commit instead of the release). From 0.2.0,
   MPE Roll gives its maker as *MPE Roll*, where earlier builds gave *Glowing Pancake* (the
   repository's old name). If FL still shows the old name after you update, tick *Verify plugins*
   and *Rescan previously verified plugins* there, as FL's manual advises after an update, and click
   *Find installed plugins* again. In FL Studio 26.1.7, after the installer upgraded 0.1.0, the list
   went on naming *Glowing Pancake* as the VST3's vendor even after FL's entry for it in the plugin
   database was deleted and the plugins scanned again with *Verify plugins*. MPE Roll loads and
   plays as it should: see *FL's plugin list names Glowing Pancake as MPE Roll's maker* under
   Troubleshooting. If new features are missing too, FL has found an older copy: see *After an
   update, new features are missing* under Troubleshooting. MPE Roll's plugin IDs, which
   hosts save in projects, don't depend on the name, and neither does its settings folder
   (`%APPDATA%\MPE Roll` on Windows).
4. Add **MPE Roll** to the Channel Rack like any other instrument.
5. In MPE Roll's wrapper settings (the gear icon in the plugin window's title bar), leave *Send pitch
   bend range* off so FL doesn't send its own bend range on top of MPE Roll's. If you use FL's *Smart
   disable*, turn it off for MPE Roll: MPE Roll plays its own clip, so FL may think it's idle.
6. **Update check (off by default).** MPE Roll can tell you when a new release is out. Turn it on
   under *MPE settings › Updates*, or ask once with *File › Check for updates now…*. When it checks,
   MPE Roll sends one plain HTTPS request to GitHub for the latest release (api.github.com, or on
   Windows github.com/freener0x/MPE-Roll/releases/latest if that fails). Nothing about you, your
   computer or your project goes with it beyond what every web request carries: your IP address,
   the plugin's name and version in the request's User-Agent header (`MPE Roll/0.2.0`, say), and
   the headers your system adds to any request (on a Mac, the languages it prefers, and cookies for
   github.com if FL keeps any). MPE Roll keeps only whether the check is on, the time of the last
   check and how it went, the version it found and any version you chose to skip, in a settings
   file of its own, which the VST2 and the VST3 both go by, even when both are open in one project.
   Turned on, it checks at most once a day, as MPE Roll's window opens. A newer release shows as an
   *Update to 0.3.0* button (with the version found) beside the status line until you install or
   skip it, and the button opens the release's page: download the installer there and run it as in
   step 2. *File › Skip version 0.3.0* keeps it quiet until a newer one is out. 0.2.0 and later
   have it; [Privacy](PRIVACY.md#the-update-check-off-by-default) has the details.

## 2. Load a synth

1. Open MPE Roll and click **Load synth…**.
2. The first time, choose **Scan for plugins…**. MPE Roll scans the standard VST3, VST2 and CLAP
   folders (for CLAP, `C:\Program Files\Common Files\CLAP`). If your VST2 plugins live somewhere
   else, add that folder in the scanner's options. You can also use **Load plugin file…** to pick a
   `.vst3`, `.dll` (VST2) or `.clap` file directly.

   MPE Roll looks inside each plugin file with **MPE Roll Scanner**, a separate program, so a plugin
   that crashes or hangs while it's being looked at only stops the scanner. Such a plugin is listed
   at the end of the scan and skipped afterwards, and *Load plugin file…* refuses it with a message
   instead of crashing FL. Each file gets a scanner of its own, so nothing a plugin leaves behind
   can take the next file down with it. The scanner comes inside the `.vst3` folder; with the `.dll`, keep
   `MPE Roll Scanner.exe` next to it, and replace it when you update MPE Roll: a scanner from an older
   version can't check plugins, and MPE Roll says so rather than loading them unchecked. Without the
   scanner, plugins are looked at inside FL, where a crashing one takes FL down. *Load plugin file…*
   shows *Checking…* while the scanner looks at the file; FL stays responsive meanwhile. Once the
   check passes, *Loading…* takes its place, and loading the synth itself holds FL's window until the
   synth is ready, as it would when the synth is loaded in FL directly.

   The list of plugins found is kept on this computer and shared by every MPE Roll in the same FL
   process (a bridged MPE Roll, the standalone app, and the `.dll` used alongside the `.vst3` each
   keep a copy of their own), so a scan in one channel shows up in the others' menus too.
3. Click **Edit synth** to open the synth's own window.
4. **Turn on MPE in the synth.** Most MPE-capable synths have an MPE switch in their settings or
   global menu. Set the synth's MPE pitch-bend range to **48 semitones**, or change MPE Roll's
   *Per-note bend range* (in **MPE settings**) to match the synth. Synths that follow the MPE
   Configuration Message pick up MPE Roll's setup automatically. Use *Send MPE setup now* after
   changing a synth's MPE mode.

**CLAP synths** that support CLAP note expressions skip step 4: MPE Roll plays each note as a CLAP
note with its own tuning, pressure and brightness (plus volume, pan, vibrato and expression from
per-note CC7, CC10, CC1 and CC11), so there are no channels or bend ranges to match. The top of
**MPE settings** shows how the loaded synth is being played. If a CLAP synth responds better to MPE,
turn off **Note expressions (CLAP, VST3)** there.

**VST3 synths** only get MIDI pitch bend, pressure and controllers on the MIDI channels they map them
on; the rest is dropped (that's how VST3 works). MPE Roll asks each VST3 synth which channels those
are, and again every few seconds:
- **Every channel** (JUCE-built synths such as Vital and Surge XT, Omnisphere 3 with its MPE on):
  MPE over MIDI, as above.
- **Channel 1 only, but it takes VST3 note expressions:** MPE Roll plays it with those, as Cubase
  and Bitwig do: every note on channel 1 with an ID of its own, its glide as tuning, and its pressure
  and timbre as whatever the synth takes for MPE's pressure and Y. *MPE settings* says *VST3 notes on
  channel 1...*. Serum 2's guide says it takes note expressions with its own MPE switch off.
  Omnisphere 3 with its MPE off reads this way too, but no per-note expression comes out of it over
  this path: turn its MPE on (section 7).
- **Channel 1 only, and no note expressions:** each note's own expression can't reach it. *MPE
  settings* says so, and MPE Roll offers *Plain MIDI* when you load it. If the synth also comes as
  VST2 or CLAP, load that instead: VST2 synths get MIDI on every channel.

The plugin log (*Load synth… › Show the plugin log*) records what each VST3 synth answered.

**Known synths:** for the synths in section 7, *MPE settings* and the status line say where the
synth's MPE switch is, and for those without MPE (Analog Lab, ZENOLOGY...) MPE Roll offers *Plain
MIDI* when you load them. *Keep MPE* (Escape) leaves everything as it is.

The synth and its settings are saved inside your FL project. If you open the project on a computer
that doesn't have that synth, MPE Roll shows **Missing: \<synth name\>** and keeps its settings, so
nothing is lost when you save.

**Synth without MPE?** Set *Synth expects* to **Plain MIDI (one channel)**. The most recent note's
glide, pressure and timbre drive the whole channel, so set *Zone bend range* to the synth's own bend
range. Glide and pressure are added to the zone-wide lanes, and after the last note ends they stay
where it left them, so its release doesn't jump. Timbre (CC74) is only sent once a note has a timbre
lane, so a synth with its filter on CC74 keeps the preset's setting otherwise. Pressure goes out as
*Pressure sent as* says: channel aftertouch (the default, added to the zone-wide *Channel pressure*
lane), a controller (back to its usual value when you change this setting), or poly aftertouch,
which keeps each note's own pressure for synths that respond to it.

**To hear real per-note expression**, use a synth with MPE. Two free ones: *Surge XT* (load its
`.clap` and MPE Roll plays it with CLAP note expressions, nothing to set up, as checked in FL Studio
26.1.7 on Windows 11, where its window opens separately from MPE Roll's, with a *Show Surge XT*
button to bring it up; with its VST3, turn on MPE in its menu) and *Vital* (turn on MPE in its
settings). Or play any synth with one copy per note, below.

### One copy per note: MPE for any synth

Set *Synth expects* to **One copy per note (any synth)** (*MPE settings*, or the button in the box
MPE Roll shows when you load a synth without MPE). MPE Roll then loads several copies of the synth,
8 to start with, and plays each note on a copy of its own as Plain MIDI. Each copy hears one note,
so that note's glide, vibrato, pressure and timbre are its own, even in a synth with no MPE at all.
The copies load one at a time, so a slow synth (Analog Lab, a big Kontakt library) takes a while;
the status line counts them.

**Set it up in the synth (copy 1).** The synth's window (the toolbar's synth button) is copy 1's.
Set its sound there; MPE Roll gives the other copies the same.
1. **Pitch-bend range.** Set the synth's own bend range (up and down) to a value it allows, 12 or
   24 say, and *Zone bend range* in *MPE settings* to the same: each copy's glides are scaled to
   it, and stop at its edge. MPE Roll also sends the range (RPN 0), which some synths follow, but
   most keep their own, 2 semitones to start with. Check it: draw a one-octave glide and listen
   that it lands an octave up. When a glide goes past *Zone bend range*, the status line says so
   for a minute (*Glides stop at 2 semitones, Serum's own pitch-bend range, but one reaches 12: set
   Serum's range to 12 and Zone bend range to 12*), and *Synth is played with* in *MPE settings*
   says where the setting is in a synth MPE Roll knows (the table below has them too).
2. **Pressure and timbre.** Each copy gets its note's pressure as channel aftertouch (*Pressure sent
   as*) and its timbre as CC74. A synth uses them only where its preset routes them: route
   *Aftertouch* in its mod matrix, and MIDI-learn CC74 to a control (filter cutoff, a macro). Only
   copy 1 has a window to learn in, and a note's own lanes reach only its copy, so learn with
   *Plain MIDI* before choosing copies, or from the *Zone timbre* lane, which reaches every copy.
3. **Voices.** Each copy plays one note at a time, so turn off the synth's mono, legato and glide
   (portamento), or a copy glides from whatever note it played last. Leave polyphony at 2 or more,
   so a copy's last note can finish its release when the copy is used again. Arpeggiators and
   chord patterns only ever see one note per copy: chords don't arpeggiate, though a one-note
   pattern still runs.

**Settings** (*MPE settings › One copy per note*):
- **Copies** (2 to 32, 8 to start with): one for each note held at once, plus a few for release
  tails. Up to 15 notes held at once each keep their own expression. Past 15, *Out of channels*
  applies whatever the number of copies: *Share a channel* still gives the new note a copy, but its
  glides and pressure move with another note's; *Steal oldest note* ends the oldest note. So
  copies past 15 only give release tails room. With fewer copies than notes, when every copy holds
  one, *Share a channel* puts the new note in the copy holding the fewest and *Steal oldest note*
  ends the oldest.
- **Keys to every copy**: keys that don't play notes but change how the synth plays, so every copy
  must hear them. *Keyswitches (C0-B2)* for Kontakt libraries with keyswitches low on the keyboard;
  *Output Arcade Samplers* (or *Output Arcade Samplers, an octave lower*) for the Playable Pitch
  keys and modifiers of Arcade's Samplers (its Instruments need *None*); or type your own: note names as on MPE Roll's
  keyboard (middle C is C5), ranges, `black` or `white` for those keys only, commas between
  (`C0-B2`, `C0-B3, black C4-C6`).
- **Sleep while quiet** (on): a copy that holds no note and has been quiet for a second stops being
  processed until a note comes for it, like FL's *Smart disable*. Copy 1 doesn't sleep while its
  window is open. Turn it off for a synth that makes sound on its own between notes.
- **Use several CPU cores** (on): FL plays a plugin on one core, so MPE Roll plays its copies on
  threads of its own, one per core (up to 8), at audio priority. The sound is the same either way;
  turn it off if a synth misbehaves (crackles, or crashes) with it.
- **Follow copy 1's sound** (on): parameter changes in copy 1 (its knobs, FL automation of *Synth*
  parameters) reach the other copies before their next block. A new preset, or other changes that
  aren't parameters, reach them as copy 1's whole state: within a second of the synth reporting the
  change, when copy 1's window closes, every 1.5 seconds while it's open (for a synth whose state
  is quick to get, and not while you're turning its knobs), and when a copy finishes loading after
  a change. The copies are given it one at a time, each once it holds no note and its last note
  has died away (with none such, one that has held no note for 10 seconds), so no note is cut short; for
  that moment new notes go to the other copies. Each copy is
  asked for its own state first and only given copy 1's if it differs (the plugin log says "had its
  sound already" otherwise). Keys held for every copy (a keyswitch, Arcade's modifiers) are played
  to it again afterwards. Off: only when you ask, with **Give every copy copy 1's sound**.
- **Load the copies again**: after loading stopped short (below), or a copy crashed.

**Running smoothly, and not crashing:**
- Each copy takes the CPU and memory of one instance of the synth. Effects inside the synth
  (reverb, delay, chorus) run in every copy: switch them off in the synth and put one on FL's mixer
  track instead. Fewer unison voices save the most CPU.
- Copies stop loading while the computer has less than 1 GB of memory free (or twice what the
  last copy took), and *MPE settings* says so. Close other programs, or ask for fewer copies.
- If FL closes while copies are loading (a synth that crashes as the fifth copy loads, say), MPE
  Roll doesn't load copies of that synth on its own again, so the project opens: *Load the copies
  again* tries once more.
- On Windows, a copy that crashes while it plays is switched off and the rest play on, instead of
  taking FL with it; *MPE settings* says which. On every system, a copy that throws an error it
  doesn't catch is switched off the same way. A switched-off copy is never called again, not even
  by its own timers, and is let go only when FL closes. Save your project soon after: a synth that
  crashed once may again. (Copy 1, the synth itself, isn't covered: it's what you edit, and it
  always plays on FL's own thread, as it would outside MPE Roll.)
- Every copy is given a transport (tempo, position) even when the host gives none: some synths
  crash without one.
- In FL: keep *Options › Audio settings › Multithreaded generator processing* on, and MPE Roll's
  *Allow threaded processing* (wrapper › *Processing*) on. If a synth crackles inside MPE Roll, tick
  *Use fixed size buffers* there (Output strongly recommends it for Arcade in FL, crackles or not).
- An audio export loads its own copies, so it needs as much memory again while it runs. Its
  progress window counts them as they load (*Cancel* stops it), and the export stops with a message
  if fewer than asked for load, rather than rendering a file with notes missing. It renders on
  threads at normal priority, so FL keeps playing smoothly meanwhile.
- Every MPE Roll in FL shares one set of threads for its copies, so ten MPE Rolls in copy mode
  don't start ten sets (and fight over the cores). The threads help up to four MPE Rolls' blocks at
  once; a fifth at the same moment plays its own copies on FL's thread for that block.

**The synths you'll likely try** (from their makers' manuals and user reports):

| Synth | Bend range (set the same as *Zone bend range*) | Pressure, timbre | Watch out for |
| --- | --- | --- | --- |
| Serum 1 before 1.23 (1.1.5, say), which has no MPE, so copies are its way to per-note glides | The two small boxes at the bottom left, by the pitch wheel: up and down in semitones (2 and −2 to start with; the down box takes a negative number). Set 24 and −24, the most they take, and *Zone bend range* to 24. They change with the preset: check them after loading one | *MATRIX* tab › a row with source *Aftertouch* (it has no drag tile). For CC74: right-click a knob › *MIDI Learn*, then play the *Zone timbre* lane (it reaches every copy, copy 1 included), or learn it with *Plain MIDI* before choosing *One copy per note*. The knob keeps CC 74, saved with the preset | *VOICING*: MONO off, PORTA 0, POLY 2 (right-click POLY to lock it). Serum has MPE of its own from 1.23 (*MENU › MPE Enabled*), working with MPE Roll from 1.26b8: with such a Serum, use that instead of copies (the newest 1.x in Xfer's update log is 1.36b8, October 2023). Keeping copies, untick *MPE Enabled*: with it on, Serum bends past its bend boxes (a user report). If the copies still bend too far, MPE is on by default in your Serum: turn that off in its preferences (1.36b8: save *User/Default.fxp* with it off), then *Load the copies again*. Avoid 1.23 builds before 1.23b8 ("multiple instances not playing back") |
| Analog Lab (Pro, called V before 5.8; Intro, Play) | The *Settings* tab (right-hand panel) changes with the preset's instrument; for instruments that have one (Augmented VOICES, say) it holds the pitch-bend range: set it there and *Zone bend range* to the same (a user got deeper glides this way). A preset whose instrument shows none keeps its own, usually 2: leave *Zone bend range* at 2 for it | Channel aftertouch reaches presets whose instrument uses it. Analog Lab can't reassign aftertouch itself (Arturia lists it as reserved), so changing what it does needs the full instrument. CC74 is the *Brightness* macro on Arturia's controller setups, or MIDI-learn it | Slow to load (5–25 s a copy, the first slowest; user reports). Pick single, polyphonic presets without glide, not Multis. Keep CC28 and CC29 (they step presets by default) and program changes out of lanes, and bank select CC0 and CC32 (they choose what a program change recalls). A copy in demo mode stops after 20 minutes |
| Kontakt 7, 8 | Each instrument's own: many NI libraries, the Factory Library among them, have *PB Range* (Up, Down) on their settings or options page (the cog), up to 12 by user reports; in the full Kontakt, Edit (wrench) › *Edit All Groups* › the Source module's *Pitch Bend* modulation. Kontakt Player can't edit groups, so a library without the setting keeps its own | Most libraries ignore aftertouch: set *Pressure sent as* to CC1 or CC11; right-click a knob › *Learn MIDI CC# Automation* for CC74 | Copies share no samples: memory × copies (lower the preload in *Options › Memory*). Keyswitches: *Keys to every copy*. Turn off the library's own legato and glide, and *Options › Engine › Multiprocessor support* (plug-in). A library in DEMO mode goes silent in every copy |
| FabFilter Twin 3 (no MPE as of 3.x) | No bend-range box: pitch bend is a MIDI modulation source routed to *Master Tune*, and the slot's slider sets how far, about 0.16 for 2 semitones (the default) and in proportion from there (about 0.96 for an octave): check with a one-octave glide. Saved with the preset | *Aftertouch* and *Mod Wheel* are MIDI sources in the same modulation section (drag one onto a knob); for CC74, a MIDI source set to that controller, or MIDI-learn | Turn its glide (portamento) off and keep polyphony at 2 or more. Reported playing well with copies in FL Studio 26 |
| reFX Nexus 5 | Modulation page › *Pitch Bend*: Max +24, Min −24 (negative), *Speed* 127 so glides aren't smoothed. Saved with the preset | Mod matrix: source *at+* to a destination; CC74 moves the filter cutoff already | Portamento *Mode* off; *Voices* 4 or more; avoid arpeggiator and trance-gate presets. Update to 5.1.9 or later (fixes a crash when several instances close) |
| Output Arcade (2.x) | None documented: draw a glide and listen; if nothing bends, only MIDI-learned controls vary per note | Right-click a macro › *LEARN MIDI*, then play the *Zone timbre* lane (CC74, and it reaches copy 1), or learn with *Plain MIDI* first. For pressure, set *Pressure sent as* to a CC and learn that the same way (learning aftertouch isn't documented) | **Samplers:** *Keys to every copy* › *Output Arcade Samplers*, so the modifiers and Playable Pitch keys reach every copy. Output gives no note numbers, so check first: with *Plain MIDI* and Arcade's window open, FL C4 should light Arcade's first sample key (its C2); if FL C3 does, choose *Output Arcade Samplers, an octave lower*. Turn *Hold* off on the sample keys (pressed again, a key goes to another copy, so it can't stop a held loop), and *Legato* can't line samples up across copies. **Instruments:** *Keys to every copy* › *None*. Log in and download the kit in copy 1 before turning copies on. Output asks for 8 GB of RAM (16 recommended), and one user measured about 2 GB an instance: start with 4 copies. *Use fixed size buffers* in FL |

### Automating the synth from FL

The synth's parameters become MPE Roll's own parameters, *Synth 1* to *Synth 1024*, each named after
the synth parameter it controls. FL automates them like any plugin parameter:

1. Click **Edit synth** and move the knob you want to automate in the synth's window.
2. In FL, choose *Tools › Last tweaked › Create automation clip* (or *Link to controller*).

You can also find them under *Browse parameters* in MPE Roll's plugin wrapper menu. The link is saved
by parameter, so it still points at the right knob after a synth update reorders its parameters.
Loading a different synth reassigns the slots.

## 3. Write and play

The tools are FL's piano roll tools, with FL's keys, on an icon toolbar: hover a button for its
name and key.

- **Draw tool (P), add notes:** click in the grid. Drag to move, drag the right edge to resize.
  Right-click (or right-drag) deletes with any tool but Glide.
- **Selection:** Ctrl-drag to select, Ctrl-click to add or remove a note, Ctrl+A for everything,
  Ctrl+D for nothing. Shift-drag duplicates. The **Select tool (E)** does the same with a plain
  drag: a box selects what it covers, a click selects just that note (Ctrl adds), and a click on
  empty space selects nothing.
- **Delete tool (D):** click or drag across notes to delete them.
- **Slice tool (C):** click a note to cut it in two where you click, on the grid (Alt bypasses
  snap), or press and drag up or down to draw a cut line across several notes and cut them all at
  that beat. A note's glide, pressure, timbre and controller curves are cut with it, so each piece
  keeps its own part and the sound carries on across the cut.
- **Paint tool (B):** drag across the grid to lay down notes one after another, each as long as the
  last note you placed or resized, starting on the grid (hold **Alt** to start off the grid). Move up
  or down while dragging to change key. Start on an existing note to carry on in its rhythm and
  length. Going back over painted notes doesn't double them, a whole stroke is one undo, and the
  painted notes stay selected so ↑/↓ or ←/→ move them straight away. Right-drag deletes.
- **Glide tool (G):** press on a note where the glide should start and drag to the pitch it should
  reach. With **Snap glide** on (the default), the glide is a straight line that lands on a whole
  semitone and starts and ends on the grid, and a label shows the interval and the note it lands on.
  Hold **Alt** to draw freely instead (or turn Snap glide off). Drag to the left of where you pressed
  to scoop up or down into it. Right-drag erases, double-click clears. Press **P** to go back to
  drawing.
- **Expression lane** (bottom): pick a lane from the menu. Lanes include velocity, release, pitch,
  pressure and timbre, any per-note controller, and zone-wide pitch bend, pressure, timbre, sustain
  and other pedals and controllers. *Draw* replaces only the stretch you draw over, *Line* draws a
  straight line, *Erase* removes points. Edits apply to the selected notes, or to every note under
  the pen if none are selected.
- **Vibrato:** pick *Vibrato* in the lane menu and draw how deep it is, up to a semitone either way.
  Ramp it up to have the vibrato come in late. MPE Roll plays it by wobbling each note's own pitch
  bend, so it needs the same setup as glides: the synth in MPE mode with its per-note bend range
  matching MPE Roll's *Per-note bend range*, or *Plain MIDI* with *Zone bend range* matching the
  synth, where only the newest note's vibrato is heard. For a synth without MPE, such as Analog Lab,
  use *Plain MIDI* or the zone-wide *Vibrato* lane, which wobbles the master channel's pitch bend at
  the zone range. When a note with vibrato ends, it lets go at its glide's pitch, so the release tail
  rings in tune. The speed (in Hz, the same at any tempo) is set next to the lane menu and applies to
  the whole clip. Notes with vibrato show a wavy line. *Send to FL piano roll* turns it into slide
  notes. (The *Vibrato Rate / Depth* controllers, CC76 to CC78, are different: they only do something
  if the synth maps them.)
- **Zoom/scroll:** Ctrl+wheel zooms time, Alt+wheel zooms keys, Shift+wheel scrolls sideways. Drag
  the green handle on the ruler to change the clip length.
- **Keys, as in FL:** ↑/↓ (or Shift+↑/↓) transpose a semitone, Ctrl+↑/↓ an octave, ←/→ (or
  Shift+←/→) nudge by the snap, Ctrl+C/X/V, Ctrl+B duplicates, Ctrl+D deselects, Shift+I inverts
  the selection, Shift+D shrinks notes to the snap length, Ctrl+Q and Shift+Q quantize, Ctrl+Z /
  Ctrl+Y undo and redo, Delete deletes. P, B, D, C and E pick the Draw, Paint, Delete, Slice and
  Select tools, and G the Glide tool. (FL's Mute tool and muted notes aren't in MPE Roll.)

### Tools: FL's piano roll tools

The wrench button opens the same tools FL's piano roll has, with FL's keys. Each works on the
selected notes, or on every note when nothing is selected, as in FL. A tool with a dialog previews
its result on the clip as you change the settings (you hear it through the synth), and *Accept*
(Return) keeps it as one undo step while *Cancel* (Escape, or a click elsewhere) puts the notes
back. The quick versions apply a tool at once with the settings it was last used with.

| Tool | Key | What it does |
|---|---|---|
| Quick quantize | Ctrl+Q | Lands note starts and ends on the grid (the snap value); Shift+Q lands starts only, as in FL |
| Quantize... | Alt+Q | Grid, strength, starts and/or ends |
| Quick chop | Ctrl+U | Cuts each note into pieces at the grid |
| Chop... | Alt+U | Piece length and gate (how much of each piece sounds) |
| Glue | Ctrl+G | Joins notes of one pitch that touch or overlap into one (the dialog adds a gap to close) |
| Arpeggiate... | Alt+A | Each chord becomes a run of its notes: pattern (up, down, up and down, down and up, random), step, gate, range in octaves |
| Strum... | Alt+S | Spreads each chord's notes in time: time between notes, direction, velocity fall, keep ends |
| Flam... | Alt+F | A grace note (or several) just before each note: time, velocity, count |
| Quick legato | Ctrl+L | Lengthens each note to where the next one starts (the dialog sets an overlap or gap) |
| Articulate... | Alt+L | Scales note lengths (staccato below 100 %, legato above), with a minimum |
| Limit... | Alt+K | Moves notes by octaves into a key range |
| Flip... | Alt+Y | Mirrors pitches (vertical) or reverses time (horizontal) within the notes |
| Randomize... | Alt+R | Shakes velocity, release, pressure, timbre, pitch, length and start by chance (the seed re-rolls) |
| Scale levels... | Alt+X | Scales and offsets velocities, and scales pressure and timbre lanes |

A note's glide and expression curves go with it through every tool: a chopped or sliced note's
pieces each keep their part, a shortened note keeps what it still plays, and a reversed note's
curves run backwards.

### Skins and background

*MPE settings › Look*:
- **Skin:** ten looks to pick from: MPE Roll, FL Studio, Midnight, Synthwave, Solarized, Paper (light),
  Forest, Ember, Ocean and High contrast. MPE Roll opens in *FL Studio* until you pick another
  (0.1.0 opened in *MPE Roll*).
- **Colours:** every colour of the skin (grid rows and lines, notes, selected notes, glide, pressure,
  timbre, text, buttons, keys...) has a swatch. Click it to pick your own; it changes as you drag.
  *Back to the skin's colours* undoes your changes. Picking another skin starts from its colours.
- **Background picture:** *Choose a picture...* puts a PNG, JPEG or GIF behind the grid and the
  expression lane, scaled to cover them. *Picture strength* sets how much of it shows through. A
  file that can't be read as a picture is turned down, and the picture you had stays.

The look is kept on this computer and shared by every MPE Roll window, in every project.

### Two ways to play clips

**Follow FL** (default): the selected clip slot plays in sync with FL's transport.
- In pattern mode it follows the pattern position.
- In song mode it follows the song position. A looping clip repeats for as long as the song plays;
  turn *Loop* off to play the clip once from bar 1.

**Note trigger:** MPE Roll has 16 clip slots, and notes from FL's own piano roll launch them. By
default **C0** plays slot 1, **C#0** slot 2, and so on (change the first key in *MPE settings ›
Playback*).
- A trigger note plays its clip from the start for as long as the note lasts, looping if the note is
  longer than the clip.
- Put trigger notes in patterns and arrange those patterns in the playlist as usual.
- Notes outside the trigger range play the hosted synth directly.

### Stopping MPE Roll with the Channel Rack switch

In *Follow FL* mode MPE Roll plays the clip from FL's song or pattern position, not from FL's notes.
The Channel Rack switch only stops FL's notes to the channel, and FL Studio 26.1.7 sends no All
Notes Off when it does (the plugin log shows none, with 0.2.0), so in plain *Follow FL* mode the
clip plays on with the channel switched off. Muting MPE Roll's mixer track silences it in either
mode. To have the switch stop it, turn on *Follow FL only while FL holds a note* (the second point
below) and give MPE Roll's channel a note for as long as the clip should sound: switching the
channel off then stops it (checked with 0.2.0 in FL 26.1.7).

- **All Notes Off from FL.** When FL sends MPE Roll All Notes Off (CC 123) or All Sound Off
  (CC 120) while the song plays (FL 26.1.7 doesn't for a channel switched off), MPE Roll stops
  the clip's notes, puts its controllers back at rest, and stays silent until you stop and start
  the transport, the song position jumps, or FL sends a note. Each time a looping pattern or song
  starts over counts as a jump, so in pattern mode the clip comes back with the next loop. Picking
  another clip slot in MPE Roll doesn't end the silence. One that arrives just as the transport
  starts or jumps is FL tidying up: it ends the notes sounding then but leaves the clip playing,
  so a channel switched off before you press Play isn't silenced this way. With the transport
  stopped it only ends the notes. The first time one silences the clip, the plugin log (*Load
  synth… › Show the plugin log*) says so: "The host sent All Notes Off (CC 123): playback paused
  until the transport restarts" (or All Sound Off, CC 120).
- **Follow FL only while FL holds a note** (*MPE settings › Playback*, Follow FL mode only). Give
  MPE Roll's channel a note in FL's piano roll and arrange its pattern in the playlist as usual:
  the clip then sounds only while such a note lasts, at the song position, so patterns and the
  playlist decide when it plays, and so does the Channel Rack switch: switched off, the channel's
  note ends and the clip stops. Without such a note the clip stays silent, whether the channel is
  on or off. Any key on any channel counts, notes that touch (one
  ending where the next starts) keep the clip playing without a break, and with *Chase notes* on,
  the clip's notes already under the playhead start with it. FL sends a note when the playhead
  reaches its start, so a song started in the middle of one may stay silent until the next. When
  the last one ends, or FL sends All Notes Off, the clip's notes stop and its zone-wide bend,
  pressure and pedals go back to rest, as when the transport stops. Switching the option on ends
  any notes FL was playing through MPE Roll.

  Those notes aren't played, but with *Rec* armed they're recorded like any other, so they become
  notes of the clip, which plays them from then on like the rest: switch the option off while you
  record, or delete them from the clip afterwards. Notes from a controller played through FL
  (section 4) count as FL's too: with the option on they only open the gate, so you hear the clip
  while you hold a key, and nothing of what you play.

In *Note trigger* mode FL's notes start and stop the clips, so the switch should stop MPE Roll there
as well, as it ends the note in the mode above (not tried in *Note trigger* mode yet). From 0.2.0
an All Notes Off or All Sound Off from FL also stops a clip a note started, as the note's end
would, and the plugin log notes the first: "The host sent All Notes Off (CC 123): the clip playing
in Note trigger mode stopped".

## 4. Record from an MPE controller

1. Put the controller in MPE mode (lower zone; most controllers default to this). In MPE Roll, pick
   your controller under *MPE settings › Input from controller / FL › Controller*. The *Setup* line
   below it says what to set on the controller itself, and the preset sets the matching bend range
   and zones. For a Haken Continuum or ContinuuMini this also turns on MPE+ high resolution.
2. FL Studio: *Options › MIDI settings*. Enable the controller as an input and give it a **Port**
   number, for example 10.
3. MPE Roll: open the plugin's wrapper settings (the gear icon in the plugin window's title bar). Set
   its **MIDI input port** to the same number. FL then sends the controller's raw MIDI, on all its
   channels, to MPE Roll.
4. Arm **Rec** in MPE Roll and start FL's transport. Each take is overdubbed into the clip, and one
   Ctrl+Z removes the whole take.

If notes play twice, FL is also sending the controller to the *selected* Channel Rack channel.
Select a different channel while you play.

Switch *Follow FL only while FL holds a note* (section 3) off while you record. With it on, the
controller's notes reach MPE Roll as FL's notes do, so they only open the gate: you hear the clip
while you hold a key, and nothing of what you play. FL's own notes that open the gate would be
recorded into the take too.

MPE Roll follows the controller's MPE Configuration Messages automatically. If your controller isn't
listed, or uses a different bend range or zone, choose *Generic MPE* and adjust the settings below
it.

## 5. FL's native synths: Send to FL piano roll

FL Studio's own synths (Harmor, Sytrus, Toxic Biohazard, FLEX, 3xOsc and the rest) come in
Image-Line's own plugin format, which no other host can load
([Image-Line: native plugins](https://support.image-line.com/action/knowledgebase?ans=504)), and FL
Studio installs only those native versions
([Image-Line: no VST versions in the install](https://support.image-line.com/action/knowledgebase?ans=589)).
So MPE Roll can't host them: *Load plugin file…* on `Harmor_x64.dll` says so, and a scan of FL's
`Plugins\Fruity` folder lists them as files that failed to load.

Image-Line used to sell separate VST versions of some of them (Harmor, Sytrus, Toxic Biohazard…) and
stopped in 2021. If you bought a VST registration back then, you can still download and unlock it
from your Image-Line account (*Account › Other Downloads* / *My Licenses*), and MPE Roll loads it as
an ordinary VST2 synth. None of Image-Line's synths supports MPE, so set *Synth expects* to *Plain
MIDI* for them ([Image-Line on MPE](https://support.image-line.com/action/knowledgebase?ans=506)).
FLEX was never sold as a VST. Image-Line says it plans to release them as VSTs again, without a date
([Image-Line: VST versions](https://support.image-line.com/action/knowledgebase?id=79&ans=494)).

For everyone else, the way to play them from MPE Roll is FL's own piano roll. The native synths
can't receive MPE; the closest they get to per-note expression is slide notes, fine pitch, and the
Mod X / Mod Y, pan and release note properties, which *Send to FL piano roll* writes for you.

1. Write or record the clip in MPE Roll, then click **Send to FL piano roll** (the FL button on the
   toolbar).
2. MPE Roll saves `MPE Roll Import.pyscript` straight into FL's scripts folder,
   `<User data folder>\FL Studio\Settings\Piano roll scripts`: normally
   `C:\Users\<you>\Documents\Image-Line\FL Studio\Settings\Piano roll scripts` (macOS:
   `~/Documents/Image-Line/FL Studio/Settings/Piano roll scripts`), and makes the
   `Piano roll scripts` folder if FL hasn't yet. On Windows it looks for the user data folder where
   FL's installer records it. If you moved yours (*Options › File settings › User data folder*)
   and the box names a different folder from the one shown there, move the file into
   `FL Studio\Settings\Piano roll scripts` inside the folder FL shows. Only when FL's folder isn't
   on this computer does MPE Roll ask where to save, and then it warns you if the file lands
   anywhere else, since FL lists scripts from that folder only. (Release 0.1.0 writes there by
   itself only when the folder already exists where Windows keeps Documents; otherwise it asks where
   to save, as it did in the test of 0.1.0: pick that folder, and create the last part of it if FL
   hasn't yet.)
3. In FL Studio (21.1 or newer), open the piano roll of a native synth's channel, open the piano
   roll menu (top left) › **Tools**, and run **MPE Roll Import** from the scripts listed there
   (checked in FL Studio 26.1.7 on Windows 11). If it isn't listed yet, close and reopen the menu;
   the first time, restart FL. *Ctrl+Alt+Y* runs the last script again.
4. Choose the options in the script's dialog:
   - *Existing notes*: replace them or keep them.
   - *Pitch glides*: slide notes, starting pitch only, or off.
   - *Pressure* → Mod X, Mod Y, velocity or off; *Timbre* → Mod Y, Mod X or off.
   - *Mod ramps on slide notes*: put pressure and timbre ramps on the slide notes too.
   - *Group slides with their note*: so you can move them together.

How the clip is translated:
- Every overlapping note gets its own colour, because FL's slide notes only bend notes of the same
  colour. FL has 16 colours, so if more than 16 notes overlap, some share; MPE Roll tells you when
  that happens.
- A glide becomes a chain of slide notes, and a note's starting offset becomes fine pitch.
- Zone-wide pitch bend is folded into every note's glide.
- Notes held by the sustain pedal are lengthened to match.
- Map Mod X / Mod Y in the synth's per-note modulation to whatever pressure and timbre should
  control.

FL Studio 26.1.7 takes the tenths of a semitone the script writes: the glides landed in tune
(checked on Windows 11). If glides land at the wrong pitch in your version of FL, open the script in
a text editor and change `PITCHOFS_PER_SEMITONE = 10` to `100`.

## 6. MIDI files and other plugins

- **File ▾ › Export audio as WAV / MP3:** plays the clip once through a copy of your synth (same
  plugin, same sound) faster than real time and writes it, release tail included, at FL's tempo: a
  24-bit WAV at FL's sample rate, or a 320 kbps MP3 at 48 kHz or below, as MP3 goes no higher (a
  project at 88.2 or 96 kHz gives a 44.1 or 48 kHz MP3). The synth playing live isn't interrupted, and the
  file opens in your file browser when it's done. Big sample libraries take a moment to load the copy.
  Cancel stops the export and leaves no file behind, unless the file had just been finished when you
  pressed it: then it's kept and opens as usual. To render with FL's own mixer and effects, use
  FL's *Export* as usual instead.
- **File ▾ › Import:** reads any MIDI file. MPE files keep their per-note expression; plain
  files import as notes with zone-wide pitch bend and controllers. You can also drop a `.mid` file
  onto MPE Roll.
- **File ▾ › Export MPE MIDI file:** writes an MPE MIDI file, with the zone setup at the start, for
  other DAWs and MPE-aware tools.
- **Drag the File button** into FL (playlist, piano roll or a channel) to drop the clip as a
  MIDI file. FL's own MIDI import keeps the notes but not per-note expression.
- MPE Roll's **MIDI output** carries the same MPE stream it sends to the hosted synth. In the
  wrapper settings, set an output port to send it to another plugin's input port.

## 7. Notes on particular synths

These come from the makers' documentation and user reports, and from tests in MPE Roll in FL Studio
26.1.7 on Windows 11 (release 0.1.0): Analog Lab, as VST2 and as VST3, loads and plays (zone-wide
lanes work, per-note ones need *Plain MIDI* as below; in 0.1.0 its channel pitch-bend parameters
fill FL's undo history while notes glide, see Troubleshooting); Omnisphere 3's VST3 plays over MPE,
per-note lanes included, once MPE is on in its System page, and with its MPE off, when MPE Roll
plays it with *VST3 notes on channel 1*, no per-note expression comes out of it; Surge XT's CLAP
plays with CLAP note expressions with nothing set up in it, and opens its own window separately.
Analog Lab and Omnisphere don't come as CLAP, so the note-expression path doesn't apply to them.

MPE in popular synths (from the makers' manuals and support pages, October 2026). MPE Roll knows
these, and says the same in *MPE settings*. [MPE_SYNTHS.md](MPE_SYNTHS.md) has the full list, with
sources. Set the synth's per-note bend range to **48**, MPE
Roll's *Per-note bend range*, or change MPE Roll's to match.

| Synth | MPE | Where to turn it on |
| --- | --- | --- |
| Serum (1.x) | From 1.23 (use 1.26b8 or later) | *MENU* (top bar) › *MPE Enabled*; MPE bend range at the bottom of that menu. Earlier builds: *One copy per note* |
| Serum 2 (VST3 only) | Yes | *MENU* › *MPE Enabled* (usually on); *MPE Bend Range* on its Global page. Takes VST3 note expressions with MPE off |
| Pigments | Yes | Gear icon (top toolbar) › *Settings* tab › *MPE Settings* › *MPE: Enabled*, *Bend Range* |
| Arturia V Collection | Many | Gear icon › *Settings* tab › *Enable MPE*; one without that setting has none |
| Analog Lab (Pro, V, Intro, Play) | **No** | *One copy per note* or *Plain MIDI*, or load the instrument itself (Pigments, a V Collection instrument) |
| Omnisphere 3 | Yes | *System* page (gear icon) › MPE on, *MPE Pitch Bend Range*; patch in Part 1 only |
| Omnisphere 2 | **No** | *One copy per note* or *Plain MIDI*, patch in Part 1 |
| Vital | Yes | *ADVANCED* tab › *MPE ENABLED* (saved per preset); per-note range fixed at 48 |
| Diva (1.4.8 and later) | Yes | *MAIN* section (bottom of the panel) › *MPE* button, polyphonic presets only; the CLAP version needs nothing |
| Zebra 3 | Yes | *MPE* button (*Perform* page), *Pitch Bend Up/Down* (*Pitches* panel) › *MPE*; the CLAP version needs nothing |
| Zebra2, ZebraHZ | **No** | *One copy per note* or *Plain MIDI* |
| Phase Plant | Yes | No switch: MIDI channel *Any* (not 1), *Bend Range* 48 in its voice settings; pressure and timbre act where the patch routes its MPE modulators |
| Surge XT | Yes | Turns itself on from MPE Roll's setup (else *MPE* in its header); the CLAP version needs nothing (checked in FL Studio 26.1.7; its window opens separately, and *Show Surge XT* brings it up) |
| Equator2 | Yes | *Mod Panel* › *Note Configuration* › *MPE* mode, *Note Pitch Bend* 48 |
| Cherry Audio (DCO-106, Dreamsynth, Memorymode, Mercury-4, Miniverse, PS-20, Octave Cat, Sines...) | Yes | *Settings* (gear) › *Interface* › *Enable MPE mode*; bend range on its *MPE* tab |
| SWAM | Yes, one note at a time | MIDI settings › *MIDI Profile*: *MPE* |
| Noisy, Imagine | Yes | Its MPE presets, or MPE on in its settings |
| ZENOLOGY, Roland Cloud | **No** | *One copy per note* or *Plain MIDI* |
| Massive X, Sylenth1, Spire, Waves synths | **No** | *One copy per note* or *Plain MIDI* |
| Kontakt 8 | Only some libraries | No switch of its own: libraries scripted for MPE (Ashlight, for one) have their own setting |
| Tone2 Icarus | Yes | *SETUP* page › *force MPE* (Icarus 2.5: *Enable MPE support*) |

**Xfer Serum and Serum 2**
- Serum 1 has MPE from 1.23 (March 2019), off until you tick *MPE Enabled* in its main *MENU*;
  set the MPE bend range at the bottom of that menu to MPE Roll's *Per-note bend range* (48). Use
  1.26b8 or later: earlier builds ignored channel 16, cut a note played on two channels at once, and
  could crash on the bend range MPE Roll sends. The newest 1.x in Xfer's update log is 1.36b8
  (October 2023). An older
  Serum (1.1.5, say) has no *MPE Enabled* item: update it from your Xfer account, or play it with
  *One copy per note* (section 2), which needs no MPE in Serum. Its VST2 (`Serum_x64.dll`) gets MPE's
  per-channel MIDI whatever the VST3 does.
- Serum 2 comes as VST3 (and AAX) only. Xfer's guide: with its MPE switch on it takes per-channel
  MIDI, and with it off it takes VST3 note expressions. Which of those reach it through MPE Roll
  depends on the channels its VST3 maps pitch bend on, which MPE Roll asks it: *MPE settings › Synth
  is played with* says *MPE over MIDI* (keep Serum 2's MPE on) or *VST3 notes on channel 1* (turn
  Serum 2's MPE off). If it says the notes can't be reached one by one, please send the plugin log.

**Roland ZENOLOGY and Roland Cloud instruments**
- No MPE: play them with *Plain MIDI*.
- They're VST3 only on Windows (Roland dropped VST2 in February 2026), installed in
  `C:\Program Files\Common Files\VST3\Roland\`, and need Roland Cloud Manager: sign in there and
  check the instrument is authorised (and that its RCMService is running) before scanning. One that
  isn't authorised doesn't start when MPE Roll looks inside it, and MPE Roll now says so instead of
  listing nothing. If it still won't load, *Copy details* in the box that says so (and the plugin
  log) has what happened.

**Spectrasonics Omnisphere**
- Authorize it once in its standalone app or directly in FL before loading it inside MPE Roll.
- A parameter only reaches MPE Roll's *Synth* slots after you right-click it in Omnisphere and choose
  **Enable Host Automation**.
- **Omnisphere 3** has MPE (3.0.1c and later include MPE fixes), and plays over MPE in MPE Roll once
  it's on, per-note glides, pressure and timbre included (checked with the VST3 in FL Studio 26.1.7
  on Windows 11, release 0.1.0, where *Synth is played with* reads *MPE over MIDI*). MPE on is
  required: with it off, MPE Roll plays it with *VST3 notes on channel 1*, and no per-note
  expression comes out of it that way, because Omnisphere 3 doesn't take VST3 note expressions
  (checked the same way). If Omnisphere stays silent instead, its Part 1 is hearing MIDI channel 1
  only while MPE Roll sends the notes on other channels: MPE Roll points this out once it has
  played Omnisphere three seconds of notes, short or long, without a sound (after giving it ten
  seconds to load its sounds) and offers *Use Plain MIDI*; *Keep MPE* is the default. Its status
  line keeps saying *No sound from Omnisphere yet* until it hears it.
  - Turn MPE on in its **System** page (the SYSTEM button in its header), which has an MPE section.
    Spectrasonics' manual: with MPE on, all 8 Parts are set to MIDI channel *Any*. Set its *MPE Pitch
    Bend Range* to MPE Roll's *Per-note bend range* (48 by default). If Omnisphere doesn't offer 48,
    set MPE Roll to a value it does offer; don't count on Omnisphere following MPE Roll's setup
    messages.
  - Load the patch into **Part 1** only. With MPE on, every Part listens on all channels, so any
    other loaded Part plays as well.
  - Still silent with MPE on? Watch Omnisphere's MIDI indicator while MPE Roll plays. If it stays
    dark, Omnisphere isn't getting the notes at all: try the VST2 `Omnisphere.dll` with *Load plugin
    file…*. If it lights, set *Lower zone channels* to 7 in *MPE settings*, so notes stay on channels
    2–8, in case this version ignores channels 9–16.
  - Omnisphere's *MPE Y* is timbre (CC74) and *MPE Z* is pressure. Patches in the factory *MPE*
    category already use them. For other patches, route MPE Y or MPE Z yourself (right-click a
    control › *Modulate*). Keep *Pressure sent as* on **Channel pressure**.
  - If held notes bend together through the VST3 version, try the VST2 `Omnisphere.dll` with *Load
    plugin file…*.
- **Omnisphere 2** has no MPE. Use *Plain MIDI (one channel)* with the patch in Part 1, and set *Zone
  bend range* to the patch's own bend range (*Bend* on its main page). Pressure arrives as channel
  aftertouch, which patches use through Omnisphere's *Aftertouch* modulation source. For timbre,
  MIDI-learn CC74 to a control.
- For each note's own glides with Omnisphere 2, use *One copy per note* (section 2). Or, untested:
  load the same patch into Parts 1–8, give
  them MIDI channels 2–9 and the same bend range (up to 24), then in MPE Roll choose *Synth expects:
  MPE*, *Lower zone channels: 8* and that range as *Per-note bend range*. Each note then plays on its
  own Part with its own bend and pressure. Only 8 notes get a Part each, and zone-wide lanes,
  sustain included, don't reach the Parts.

**Arturia Analog Lab**
- Arturia says Analog Lab itself isn't MPE compatible. (Their individual instruments, such as
  Pigments, are.) Set *Synth expects* to **Plain MIDI (one channel)**. In Analog Lab's Settings, set
  *MIDI Channel* to *All* or 1.
- What to expect: left on MPE (MPE Roll's default), per-note glides and vibrato are scaled for a
  48-semitone range and Analog Lab applies them with its own, usually 2, so they barely move; only
  the zone-wide lanes work. With *Plain MIDI* the newest note's glide, vibrato, pressure and timbre
  work, but every held note follows them. With *One copy per note* (section 2) each note has its
  own, at the cost of an Analog Lab per copy (each takes a while to load).
- In release 0.1.0, FL's undo history fills with *Knob tweak (channel1 pitch bend)* and the like
  while notes glide: Analog Lab gives each MIDI channel's pitch bend a parameter of its own, which
  follows MPE Roll's bends, and 0.1.0 passes those moves on to FL as edits. The sound isn't
  affected, but your own edits get buried among those entries. 0.2.0 keeps those moves from FL,
  with Analog Lab as a VST2 and as a VST3 (checked in FL Studio 26.1.7, and by MPE Roll's own tests
  with test synths of both formats; Troubleshooting has the details).
- Set *Zone bend range* to the preset's pitch-bend range (often 2). The newest note's glide bends the
  whole channel and stops at the edge of that range. Analog Lab's *Settings* tab shows the range for
  instruments that have one (Augmented VOICES, say): raise it there, and *Zone bend range* with it.
- Users report Analog Lab responding to channel aftertouch, and nothing says it takes poly
  aftertouch, so keep *Pressure sent as* on **Channel pressure**. The newest note's pressure then
  goes out as aftertouch, added to the zone-wide *Channel pressure* lane.
- The newest note's timbre is sent as CC74 (once any note has a timbre lane). Whether that changes
  the sound depends on the *MIDI Controller* map in Analog Lab's settings; with *Generic MIDI
  Controller*, MIDI-learn CC74 to a control.
- From FL you can automate the macros and the effects. An engine parameter generally can't be
  automated until it's assigned to one of Analog Lab's controls (user reports, matching the
  manual's rule for MIDI learn).
- Keep CC28 and CC29 (they step presets by default) and program changes out of lanes, and bank
  select (CC0, CC32), which picks what a program change recalls.

## Troubleshooting

What's known not to work yet, and where each fix stands, is on the [Known issues](KNOWN_ISSUES.md)
page. If what you see isn't there or below, *Report a bug…* in MPE Roll's File menu opens the issue
form on GitHub with the build filled in, and puts the plugin log on the clipboard to paste into it.
Your browser opens only when you click it, and nothing is sent until you submit the form on GitHub.

| Problem | Fix |
| --- | --- |
| FL doesn't list MPE Roll | Check where the files went: the whole `MPE Roll.vst3` folder in `C:\Program Files\Common Files\VST3`, or `MPE Roll.dll` in one of the plugin folders listed under *Options › Manage plugins* (the installer puts the VST2 where you told it to). Then click *Find installed plugins*. |
| After an update, new features (or the toolbar's icons) are missing | FL is loading an older MPE Roll. The bottom of the File menu (and of *MPE settings*) names the build that's loaded, e.g. "release v0.1.0" or "build 63": compare it with the download's name (`MPE-Roll-0.1.0-Windows.zip`, `MPE-Roll-Windows-build-63`). Close FL and run the installer, which finds the older copies FL's plugin database lists (outside FL's own folder) and any in the usual plugin folders. Without the installer, or for a copy it didn't find, look in every folder listed under *Options › Manage plugins* for `MPE Roll.vst3`, `MPE Roll.dll` and `MPE Roll Scanner.exe`, and replace or delete the old ones. A project or channel made with the `.dll` keeps loading the `.dll`, even with a newer `.vst3` installed. Then *Find installed plugins*. |
| FL's plugin list names *Glowing Pancake* as MPE Roll's maker | 0.1.0 gave that name (the repository's old one); 0.2.0 and later give *MPE Roll*, which CI checks in the VST3 and the VST2 (as listed, as loaded, and in the VST3's `moduleinfo.json`). In FL Studio 26.1.7, after the installer upgraded 0.1.0, *Options › Manage plugins* went on naming *Glowing Pancake* as the VST3's vendor (and no vendor for the VST2), even after a scan with *Verify plugins* ticked and with FL's entry for it (`Documents\Image-Line\FL Studio\Presets\Plugin database\Installed\Generators\VST3\MPE Roll.nfo` and `.fst`) deleted first. Only the name shown is old: MPE Roll loads and plays. Image-Line's manual says the vendor shown can be changed in *Manage plugins*. Where FL keeps the old name isn't known yet: if you see it, please say in an issue what `C:\Program Files\Common Files\VST3\MPE Roll.vst3\Contents\Resources\moduleinfo.json` gives as `"Vendor"` (open it in Notepad), and the path *Manage plugins* shows for MPE Roll. |
| *Couldn't check for updates: …* in the status line | You asked (*File › Check for updates now…*, or *Check now* in *MPE settings › Updates*) and no release came back: you're offline, a firewall or proxy blocks api.github.com and github.com, or GitHub is limiting requests from your network (60 an hour from one address; on Windows MPE Roll then asks the release page instead, which isn't limited that way, and on a Mac the check has to wait for the next hour, and the status line says *GitHub's limit of … requests an hour from your network is used up*). The daily check says nothing when this happens; *MPE settings › Updates › Last check* shows how the last one went. The releases are at github.com/freener0x/MPE-Roll/releases. |
| Scanning for plugins crashes FL | MPE Roll Scanner isn't installed, so plugins were looked at inside FL. It's inside the `.vst3` folder; with the `.dll`, put `MPE Roll Scanner.exe` next to it. |
| "… crashed while MPE Roll was checking it" | That plugin crashes when it's loaded outside its usual host, so MPE Roll won't load it (FL would go down with it). Try updating the plugin, and please report which one it is. |
| Per-note lanes do nothing, zone-wide ones work | The synth isn't in MPE mode, or has none. *MPE settings › Synth is played with* says which, and where the synth's MPE switch is if MPE Roll knows it (section 7). Turn MPE on in the synth, or set *Synth expects* to *Plain MIDI*, where the newest note's expression drives the whole synth. A VST3 synth that takes pitch bend on channel 1 only can't get each note's own expression over MIDI: MPE Roll plays it with VST3 note expressions if it takes them, and otherwise says so (section 2). |
| MPE Roll crashes | The standalone app saves a report to `%APPDATA%\MPE Roll\Crash report.txt` and says so. In FL, FL's own crash message names the file that crashed. Please include either when you report it. |
| No sound | Load a synth (*Load synth…*), and check that the synth itself makes sound in its own window. |
| No sound from a loaded synth; "No sound from …" appears | The synth isn't in MPE mode, so it only listens on channel 1, and MPE Roll plays notes on channels 2–16; or it was still loading its sounds. Turn MPE on in the synth (Omnisphere 3: its *System* page), or click *Use Plain MIDI*. Only that button changes anything: *Keep MPE* takes Return and Escape both, and closing the box keeps MPE too. MPE Roll asks once each time a synth is loaded or a project is opened: it gives the synth ten seconds to load, then asks once three seconds of notes, short or long, have gone without a sound, and not at all if the synth has sounded first. Until it hears the synth, the status line says *No sound from … yet* as well. *Synth is played with* in *MPE settings* shows the channels. |
| A synth's window opens with its title bar above the top of the screen, or its controls out of reach | MPE Roll opens the synth's window on the screen MPE Roll is on, with the title bar inside the work area (above the taskbar); a synth whose window can be resized is shrunk to fit. If one still ends up out of reach (some synths resize themselves after opening, for example when a preset loads), choose *Load synth… › Bring the synth's window back on screen*. A synth whose window can't be resized and is taller than the screen keeps its title bar on screen and runs off the bottom: lower its own size or zoom setting (most have one), or Windows' display scale. |
| A synth won't load | MPE Roll says why in a box. For a VST2 or VST3 it finds out first, which takes a moment (*finding out why…* in the status line; if the plugin opens a window of its own meanwhile, such as a licence or sign-in, answer it): Windows' own reason when Windows refuses the file (a DLL it needs is missing, it isn't a 64-bit plugin…), or else the step that failed when MPE Roll Scanner retraced the load one step at a time in a separate process. The plugin may have changed since the scan (an update): *Load synth… › Load plugin file…* on it reads it afresh, where a scan skips a plugin it has listed already. Its component may have refused to start. Or it loads fine there: MPE Roll then loads it once more by itself (a plugin may wait for a licence check the first time), and if that fails too, something in FL's process stops it, such as a second install of the same plugin already loaded (when you pick another install of the loaded synth, MPE Roll unloads the current one first). Click *Report it* to open the *A synth won't load* form on GitHub with the build and the synth filled in and the details on the clipboard to paste into it, or *Copy details* to paste them anywhere: they name the plugin, its format and version, the error, and the end of MPE Roll's plugin log, which holds each step. *Load synth… › Show the plugin log* opens the log itself (`%APPDATA%\MPE Roll\Plugin log.txt`), which records every scan and load with how long it took. A plugin that *didn't start when MPE Roll looked inside it* usually isn't activated or authorised yet: open it once directly in FL, or sign in to its maker's app (Roland Cloud Manager for ZENOLOGY), then try again. One that *took too long* may be waiting for a sign-in window the first time. |
| MPE Roll keeps playing with its channel switched off in the Channel Rack | In *Follow FL* mode MPE Roll plays the clip from FL's song position, not from FL's notes, which are all the switch stops: FL Studio 26.1.7 sends no All Notes Off when the channel is switched off. Turn on *MPE settings › Playback › Follow FL only while FL holds a note* and give the channel a note in FL's piano roll for as long as the clip should sound: the switch then stops it (checked with 0.2.0 in FL 26.1.7), but without such a note the clip stays silent. Or mute MPE Roll's mixer track. Section 3, *Stopping MPE Roll with the Channel Rack switch*, has the details. In *Note trigger* mode FL's notes start and stop the clips, so the switch should stop them as it ends the note with the option above (not tried in *Note trigger* mode yet). |
| Clicks, timing drift or CPU spikes | In MPE Roll's wrapper settings, turn on *Use fixed size buffers* (Troubleshooting tab). |
| Mouse clicks land away from the pointer after reopening the window | In FL, MPE Roll draws its window in software (*MPE settings › Window › Window drawing: Automatic*), which avoids this. If it still happens, try the other *Window drawing* choices and tell us what the *Window* line next to it says. |
| Glides are much too big or too small | The synth's MPE bend range doesn't match MPE Roll's *Per-note bend range* (48 by default). |
| All notes bend together | The synth isn't in MPE mode. Turn MPE on in the synth, or switch MPE Roll to *Plain MIDI*. |
| Recording catches nothing | The controller's Port in FL's MIDI settings must match MPE Roll's MIDI input port, and FL must be playing. |
| Notes play twice while recording | FL also sends the controller to the selected channel. Select another channel. |
| "Missing: \<synth\>" on the synth button | That synth isn't installed (or scanned) on this computer. Install it, scan, and pick it again. |
| "Not loaded: \<synth\>" on the synth button when a project opens | FL Studio (or MPE Roll) closed while that synth was loading last time, so MPE Roll didn't load it again on its own; *MPE settings › Synth is played with* says so too. *Load synth… ›* its name loads it once more. If FL closes again, the plugin crashes on loading here: try its other format (VST2 or VST3), or keep a single install of it (two installs of one plugin in FL can trip over each other), and please report it. |
| A CLAP synth plays but doesn't glide, press or brighten | It may not support CLAP note expressions. Turn off *Note expressions (CLAP, VST3)* in *MPE settings* and turn on MPE in the synth instead. |
| A VST3 synth played with VST3 note expressions doesn't glide | Its own MPE switch may be on (Serum 2 takes note expressions with it off). Or turn off *Note expressions (CLAP, VST3)* to send MPE over MIDI, and please send the plugin log. |
| A CLAP synth's window is empty or opens separately | Some CLAP synths can't be embedded in MPE Roll's window and open their own; MPE Roll says so (*Surge XT opened its window separately.*) and shows a *Show Surge XT* button that brings the window up again. Surge XT's CLAP does this in FL Studio 26.1.7 on Windows 11, and plays fine. Please report synths whose window doesn't appear at all. |
| An automation clip moves the wrong knob | The clip belongs to an MPE Roll slot, and slots are reassigned when you load a different synth. Re-link it with *Last tweaked*. |
| FL's undo history fills with *Knob tweak (channel1 pitch bend)* and the like while notes glide | Some synths give each MIDI channel's pitch bend (or wheel, or aftertouch) a parameter of its own, which moves with every bend MPE Roll sends: Analog Lab's and Kontakt's are named "channel1 pitch bend" to "channel16 pitch bend". Release 0.1.0 passes Analog Lab's on to FL as edits (seen in FL Studio 26.1.7 on Windows 11); the sound isn't affected. From 0.2.0 on, MPE Roll doesn't pass such moves on to FL: a parameter named after a channel's or controller's bend or wheel, one the synth's VST3 MIDI mapping moves, and anything the synth reports while it plays a block with bends in it, is the synth following its notes, not an edit. MPE Roll's own tests check this with VST2 and VST3 test synths, and in FL Studio 26.1.7 Analog Lab's no longer reach FL's undo history, as VST2 or as VST3. If entries still appear in 0.2.0 or a later build, *Load synth… › Show the plugin log*: a line "…parameter changes reported by the synth passed on to the host as edits" names the parameters that got through; please send it with the synth's name and format (VST2 or VST3). |
| Vibrato barely moves, or only on the newest note | Vibrato rides on the note's pitch bend, so it needs what glides need: MPE on in the synth with the bend ranges matching, or *Plain MIDI* with *Zone bend range* set to the synth's, where only the newest note's is heard. A synth without MPE can also take the zone-wide *Vibrato* lane. |
| Harmor, Sytrus or another FL synth can't be scanned or loaded | FL's own synths come in Image-Line's format, which only FL can load, so a scan leaves `Harmor_x64.dll` and the like out and names them in a box when it's done, and *Load plugin file…* says what they are. Use *Send to FL piano roll* (section 5). Image-Line's separate VST versions, if you own one, load as ordinary VST2 synths with *Plain MIDI*. |
| *MPE Roll Import* isn't in FL's piano roll *Tools* menu | FL lists scripts from one folder only, `<User data folder>\FL Studio\Settings\Piano roll scripts`, and only files ending in `.pyscript`. In FL, *Options › File settings › User data folder* shows where the user data folder is on this computer (if Windows keeps Documents in OneDrive, FL's is often still `C:\Users\<you>\Documents\Image-Line`). The box after *Send to FL piano roll* and the status line say where the file went; if that's anywhere else, the box says so, and its *Show file* button opens the folder so you can move the file. After adding a script, close and reopen the menu; the first time, restart FL. Piano roll scripts need FL Studio 21.1 or newer. |
| "The MPE Roll Scanner installed here is older than this MPE Roll" | The `MPE Roll Scanner.exe` next to `MPE Roll.dll` is from an older version, so it can't check plugins, and MPE Roll won't load them unchecked. Copy the scanner from the download that this MPE Roll came in (it's also inside the `.vst3` folder) over the old one. |
| A synth scanned in one MPE Roll channel is missing from another's menu, or the scanned list is gone after a restart | Every MPE Roll of the same kind in FL shares one list and one settings file on this computer (`MPE Roll.settings`), so this shouldn't happen any more. Mixing `MPE Roll.vst3` and `MPE Roll.dll` in one project, a bridged (*Make bridged*) MPE Roll or the standalone app alongside can still overwrite the list: the last to save wins, and scanning again brings it back. If it happens otherwise, please tell us. |
| One copy per note (or Plain MIDI): glides stop short at 2 semitones, or overshoot | Each copy bends within the synth's own pitch-bend range, 2 semitones in most synths until you change it, so set it (in copy 1's window) and *Zone bend range* in *MPE settings* to the same value, up and down. When a glide goes past *Zone bend range*, the status line says so for a minute, with the number to set; *Synth is played with* in *MPE settings* says where the setting is in Serum, Analog Lab, Kontakt, FabFilter Twin 3, Nexus and Arcade (section 2's table has them too). Many synths save the range with each preset, so check it after picking a new one. |
| One copy per note: some notes sound like the old preset | The copies get copy 1's new sound when the synth reports the change, when its window closes, or every 1.5 seconds while it's open; a copy holding a note gets it when the note ends. Close the synth's window, or click *Give every copy copy 1's sound* in *MPE settings*. If *Follow copy 1's sound* is off, only that button does it. |
| One copy per note: fewer copies than asked for | *MPE settings › One copy per note* says why: too little memory free (close other programs, or ask for fewer), a copy that wouldn't load, or loading cut short by a crash last time. *Load the copies again* tries again. |
| One copy per note: a copy "crashed … and was switched off" | The synth crashed in that copy, and MPE Roll stopped using it so FL carried on. Save your project, then *Load the copies again*; if it happens again, try fewer copies or turn off *Use several CPU cores*, and please report the synth. |
| One copy per note: CPU is high | Each copy runs a whole synth. Fewer copies, fewer unison voices, effects moved from the synth to FL's mixer, and *Sleep while quiet* on all help. Keep FL's *Multithreaded generator processing* and MPE Roll's *Use several CPU cores* on. |
| One copy per note: a keyswitch, or Arcade's modifiers, act on one note only | Those keys must reach every copy: set *Keys to every copy* (*Keyswitches*, *Output Arcade Samplers*, or the keys typed in). |
| One copy per note: some of Arcade's loops play on every copy at once, or some modifiers and pitch keys change one loop only | The *Output Arcade Samplers* keys are an octave off for your Arcade: choose the other one (*…, an octave lower* or back), and check as in section 2's table. With an Arcade Instrument, choose *None*. |
