# Using MPE Roll in FL Studio

FL Studio's piano roll can't send per-note expression to plugins. MPE Roll gets around that by being
the instrument FL sees: it holds its own MPE piano roll and plays a synth hosted inside it.

> These steps follow Image-Line's documentation and user reports. MPE Roll hasn't been checked inside
> FL Studio yet. If something here doesn't match what you see, please open an issue.

## 1. Install

1. Download a build (see [Downloading a build](../README.md#downloading-a-build)) or build it
   yourself.
2. Copy the whole `MPE Roll.vst3` folder to `C:\Program Files\Common Files\VST3` (macOS:
   `/Library/Audio/Plug-Ins/VST3`). On Windows you can use `MPE Roll.dll`, the VST2 version, instead:
   put it in one of the plugin folders listed under *Options › Manage plugins*, with
   `MPE Roll Scanner.exe` beside it (see step 2 below).
3. In FL Studio: *Options › Manage plugins › Find installed plugins*.
4. Add **MPE Roll** to the Channel Rack like any other instrument.
5. In MPE Roll's wrapper settings (the gear icon in the plugin window's title bar), leave *Send pitch
   bend range* off so FL doesn't send its own bend range on top of MPE Roll's. If you use FL's *Smart
   disable*, turn it off for MPE Roll: MPE Roll plays its own clip, so FL may think it's idle.

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
   `MPE Roll Scanner.exe` next to it. Without the scanner, plugins are looked at inside FL, where a
   crashing one takes FL down.
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
turn off **CLAP note expressions** there.

The synth and its settings are saved inside your FL project. If you open the project on a computer
that doesn't have that synth, MPE Roll shows **Missing: <synth name>** and keeps its settings, so
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
`.clap` and MPE Roll plays it with CLAP note expressions, nothing to set up; with its VST3, turn on
MPE in its menu) and *Vital* (turn on MPE in its settings).

### Automating the synth from FL

The synth's parameters become MPE Roll's own parameters, *Synth 1* to *Synth 1024*, each named after
the synth parameter it controls. FL automates them like any plugin parameter:

1. Click **Edit synth** and move the knob you want to automate in the synth's window.
2. In FL, choose *Tools › Last tweaked › Create automation clip* (or *Link to controller*).

You can also find them under *Browse parameters* in MPE Roll's plugin wrapper menu. The link is saved
by parameter, so it still points at the right knob after a synth update reorders its parameters.
Loading a different synth reassigns the slots.

## 3. Write and play

- **Add notes:** click in the grid. Drag to move, drag the right edge to resize. Right-click (or
  right-drag) deletes.
- **Selection:** Ctrl-drag to select, Ctrl-click to add or remove a note, Ctrl+A for everything.
  Shift-drag duplicates.
- **Paint tool (B):** drag across the grid to lay down notes one after another, each as long as the
  last note you placed or resized, starting on the grid (hold **Alt** to start off the grid). Move up
  or down while dragging to change key. Start on an existing note to carry on in its rhythm and
  length. Going back over painted notes doesn't double them, a whole stroke is one undo, and the
  painted notes stay selected so ↑/↓ or ←/→ move them straight away. Right-drag deletes.
- **Glide tool (G):** press on a note where the glide should start and drag to the pitch it should
  reach. With **Snap glide** on (the default), the glide is a straight line that lands on a whole
  semitone and starts and ends on the grid, and a label shows the interval and the note it lands on.
  Hold **Alt** to draw freely instead (or turn Snap glide off). Drag to the left of where you pressed
  to scoop up or down into it. Right-drag erases, double-click clears. Press **E** to go back to
  editing.
- **Expression lane** (bottom): pick a lane from the menu. Lanes include velocity, release, pitch,
  pressure and timbre, any per-note controller, and zone-wide pitch bend, pressure, timbre, sustain
  and other pedals and controllers. *Draw* replaces only the stretch you draw over, *Line* draws a
  straight line, *Erase* removes points. Edits apply to the selected notes, or to every note under
  the pen if none are selected.
- **Vibrato:** pick *Vibrato* in the lane menu and draw how deep it is, up to a semitone either way.
  Ramp it up to have the vibrato come in late. MPE Roll plays it by wobbling each note's own pitch,
  so it works with any synth, MPE or not, with no setup in the synth. The speed (in Hz, the same at
  any tempo) is set next to the lane menu and applies to the whole clip. Notes with vibrato show a
  wavy line. *Send to FL piano roll* turns it into slide notes. (The *Vibrato Rate / Depth*
  controllers, CC76 to CC78, are different: they only do something if the synth maps them.)
- **Zoom/scroll:** Ctrl+wheel zooms time, Alt+wheel zooms keys, Shift+wheel scrolls sideways. Drag
  the green handle on the ruler to change the clip length.
- **Keys:** ↑/↓ transpose (Shift: octave), ←/→ nudge, Q quantizes, Ctrl+C/X/V, Ctrl+D or Ctrl+B
  duplicates, Ctrl+Z / Ctrl+Y undo and redo. E, B and G pick the Edit, Paint and Glide tools.

### Skins and background

*MPE settings › Look*:
- **Skin:** ten looks to pick from: MPE Roll, FL Studio, Midnight, Synthwave, Solarized, Paper (light),
  Forest, Ember, Ocean and High contrast.
- **Colours:** every colour of the skin (grid rows and lines, notes, selected notes, glide, pressure,
  timbre, text, buttons, keys...) has a swatch. Click it to pick your own; it changes as you drag.
  *Back to the skin's colours* undoes your changes. Picking another skin starts from its colours.
- **Background picture:** *Choose a picture...* puts a PNG, JPEG or GIF behind the grid and the
  expression lane, scaled to cover them. *Picture strength* sets how much of it shows through.

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

MPE Roll follows the controller's MPE Configuration Messages automatically. If your controller isn't
listed, or uses a different bend range or zone, choose *Generic MPE* and adjust the settings below
it.

## 5. FL's native synths: Send to FL piano roll

FLEX, Sytrus, Harmor, 3xOsc and the other native generators can't receive MPE. The closest they get
to per-note expression is FL's own piano roll: slide notes, fine pitch, and the Mod X / Mod Y, pan
and release note properties.

1. Write or record the clip in MPE Roll, then click **Send to FL piano roll**.
2. MPE Roll writes `MPE Roll Import.pyscript` to
   `Documents\Image-Line\FL Studio\Settings\Piano roll scripts`. That's FL's user scripts folder.
3. In FL, open the piano roll of a native synth's channel and run **MPE Roll Import** from the piano
   roll's Scripts menu. If it isn't listed, reopen the menu, or restart FL the first time.
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

If glides land at the wrong pitch, open the script in a text editor and change
`PITCHOFS_PER_SEMITONE = 10` to `100`. FL's documentation isn't clear about the unit, so this step
needs checking in a real FL install.

## 6. MIDI files and other plugins

- **File ▾ › Export audio as WAV / MP3:** plays the clip once through a copy of your synth (same
  plugin, same sound) faster than real time and writes it, release tail included, as a 24-bit WAV
  or a 320 kbps MP3 at FL's tempo and sample rate. The synth playing live isn't interrupted, and the
  file opens in your file browser when it's done. Big sample libraries take a moment to load the copy.
  To render with FL's own mixer and effects, use FL's *Export* as usual instead.
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

These come from the makers' documentation and user reports, and from a first test of Analog Lab in
MPE Roll in FL Studio 26.1.7 (it loads and plays; zone-wide lanes work, per-note ones need *Plain
MIDI* as below). Neither synth comes as CLAP, so the CLAP note-expression path doesn't apply.

**Spectrasonics Omnisphere**
- Authorize it once in its standalone app or directly in FL before loading it inside MPE Roll.
- A parameter only reaches MPE Roll's *Synth* slots after you right-click it in Omnisphere and choose
  **Enable Host Automation**.
- **Omnisphere 3** has MPE (3.0.1c and later include MPE fixes):
  - Turn MPE on in its **System** page (the SYSTEM button in its header). Set its *MPE Pitch Bend
    Range* to MPE Roll's *Per-note bend range* (48 by default). If Omnisphere doesn't offer 48, set
    MPE Roll to a value it does offer; don't count on Omnisphere following MPE Roll's setup messages.
  - Load the patch into **Part 1** only. With MPE on, every Part listens on all channels, so any
    other loaded Part plays as well.
  - Omnisphere's *MPE Y* is timbre (CC74) and *MPE Z* is pressure. Patches in the factory *MPE*
    category already use them. For other patches, route MPE Y or MPE Z yourself (right-click a
    control › *Modulate*). Keep *Pressure sent as* on **Channel pressure**.
  - If held notes bend together through the VST3 version, try the VST2 `Omnisphere.dll` with *Load
    plugin file…*.
- **Omnisphere 2** has no MPE. Use *Plain MIDI (one channel)* with the patch in Part 1, and set *Zone
  bend range* to the patch's own bend range (*Bend* on its main page). Pressure arrives as channel
  aftertouch, which patches use through Omnisphere's *Aftertouch* modulation source. For timbre,
  MIDI-learn CC74 to a control.
- *Untested idea for per-note glides with Omnisphere 2:* load the same patch into Parts 1–8, give
  them MIDI channels 2–9 and the same bend range (up to 24), then in MPE Roll choose *Synth expects:
  MPE*, *Lower zone channels: 8* and that range as *Per-note bend range*. Each note then plays on its
  own Part with its own bend and pressure. Only 8 notes get a Part each, and zone-wide lanes,
  sustain included, don't reach the Parts.

**Arturia Analog Lab**
- Arturia says Analog Lab itself isn't MPE compatible. (Their individual instruments, such as
  Pigments, are.) Set *Synth expects* to **Plain MIDI (one channel)**. In Analog Lab's Settings, set
  *MIDI Channel* to *All* or 1.
- What to expect: left on MPE (MPE Roll's default), per-note glides are scaled for a 48-semitone
  range and Analog Lab applies them with its own, usually 2, so they barely move; only the zone-wide
  lanes work. With *Plain MIDI* the newest note's glide, pressure and timbre work, but every held note
  follows them. Each note having its own takes a synth with MPE.
- Set *Zone bend range* to the preset's pitch-bend range (often 2). The newest note's glide bends the
  whole channel and stops at the edge of that range.
- Analog Lab responds to channel aftertouch, not poly aftertouch, so keep *Pressure sent as* on
  **Channel pressure**. The newest note's pressure then goes out as aftertouch, added to the
  zone-wide *Channel pressure* lane.
- The newest note's timbre is sent as CC74 (once any note has a timbre lane). Whether that changes
  the sound depends on the *MIDI Controller* map in Analog Lab's settings; with *Generic MIDI
  Controller*, MIDI-learn CC74 to a control.
- From FL you can automate the macros, the assignable controls and the effects. To automate an
  engine parameter, assign it to one of Analog Lab's controls first.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| FL doesn't list MPE Roll | Check where the files went: the whole `MPE Roll.vst3` folder in `C:\Program Files\Common Files\VST3`, or `MPE Roll.dll` in one of the plugin folders listed under *Options › Manage plugins*. Then click *Find installed plugins*. |
| Scanning for plugins crashes FL | MPE Roll Scanner isn't installed, so plugins were looked at inside FL. It's inside the `.vst3` folder; with the `.dll`, put `MPE Roll Scanner.exe` next to it. |
| "… crashed while MPE Roll was checking it" | That plugin crashes when it's loaded outside its usual host, so MPE Roll won't load it (FL would go down with it). Try updating the plugin, and please report which one it is. |
| Per-note lanes do nothing, zone-wide ones work | The synth isn't in MPE mode, or has none. Turn MPE on in the synth, or set *Synth expects* to *Plain MIDI*, where the newest note's expression drives the whole synth. |
| MPE Roll crashes | The standalone app saves a report to `%APPDATA%\MPE Roll\Crash report.txt` and says so. In FL, FL's own crash message names the file that crashed. Please include either when you report it. |
| No sound | Load a synth (*Load synth…*), and check that the synth itself makes sound in its own window. |
| Clicks, timing drift or CPU spikes | In MPE Roll's wrapper settings, turn on *Use fixed size buffers* (Troubleshooting tab). |
| Mouse clicks land away from the pointer after reopening the window | In FL, MPE Roll draws its window in software (*MPE settings › Window › Window drawing: Automatic*), which avoids this. If it still happens, try the other *Window drawing* choices and tell us what the *Window* line next to it says. |
| Glides are much too big or too small | The synth's MPE bend range doesn't match MPE Roll's *Per-note bend range* (48 by default). |
| All notes bend together | The synth isn't in MPE mode. Turn MPE on in the synth, or switch MPE Roll to *Plain MIDI*. |
| Recording catches nothing | The controller's Port in FL's MIDI settings must match MPE Roll's MIDI input port, and FL must be playing. |
| Notes play twice while recording | FL also sends the controller to the selected channel. Select another channel. |
| "Missing: <synth>" on the synth button | That synth isn't installed (or scanned) on this computer. Install it, scan, and pick it again. |
| A CLAP synth plays but doesn't glide, press or brighten | It may not support CLAP note expressions. Turn off *CLAP note expressions* in *MPE settings* and turn on MPE in the synth instead. |
| A CLAP synth's window is empty or opens separately | Some CLAP synths can't be embedded and open their own window; MPE Roll says so. Please report synths whose window doesn't appear at all. |
| An automation clip moves the wrong knob | The clip belongs to an MPE Roll slot, and slots are reassigned when you load a different synth. Re-link it with *Last tweaked*. |
