# Using MPE Roll in FL Studio

FL Studio's piano roll can't send per-note expression to plugins. MPE Roll gets around that by being
the instrument FL sees: it holds its own MPE piano roll and plays a synth hosted inside it.

> These steps follow Image-Line's documentation and user reports. MPE Roll hasn't been checked inside
> FL Studio yet. If something here doesn't match what you see, please open an issue.

## 1. Install

1. Build the VST3 (see the README), or download a build.
2. Copy `MPE Roll.vst3` to `C:\Program Files\Common Files\VST3` (macOS: `/Library/Audio/Plug-Ins/VST3`).
3. In FL Studio: *Options › Manage plugins › Find installed plugins*.
4. Add **MPE Roll** to the Channel Rack like any other instrument.

## 2. Load a synth

1. Open MPE Roll and click **Load synth…**.
2. The first time, choose **Scan for plugins…**. MPE Roll scans the standard VST3 folders, and a
   plugin that crashes during the scan is skipped next time. You can also use **Load plugin file…**
   to pick a `.vst3` directly.
3. Click **Edit synth** to open the synth's own window.
4. **Turn on MPE in the synth.** Most MPE-capable synths have an MPE switch in their settings or
   global menu. Set the synth's MPE pitch-bend range to **48 semitones**, or change MPE Roll's
   *Per-note bend range* (in **MPE settings**) to match the synth. Synths that follow the MPE
   Configuration Message pick up MPE Roll's setup automatically. Use *Send MPE setup now* after
   changing a synth's MPE mode.

The synth and its settings are saved inside your FL project. If you open the project on a computer
that doesn't have that synth, MPE Roll shows **Missing: <synth name>** and keeps its settings, so
nothing is lost when you save.

**Synth without MPE?** Set *Synth expects* to **Plain MIDI (one channel)**. Pressure becomes poly
aftertouch, and the most recent note's glide drives the channel pitch bend.

## 3. Write and play

- **Add notes:** click in the grid. Drag to move, drag the right edge to resize. Right-click (or
  right-drag) deletes.
- **Selection:** Ctrl-drag to select, Ctrl-click to add or remove a note, Ctrl+A for everything.
  Shift-drag duplicates.
- **Glide tool (G):** drag across a note to draw its pitch bend. Shift snaps to semitones,
  right-drag erases, double-click clears. Press **E** to go back to editing.
- **Expression lane** (bottom): pick a lane from the menu. Lanes include velocity, release, pitch,
  pressure and timbre, any per-note controller, and zone-wide pitch bend, pressure, timbre, sustain
  and other pedals and controllers. *Draw* replaces only the stretch you draw over, *Line* draws a
  straight line, *Erase* removes points. Edits apply to the selected notes, or to every note under
  the pen if none are selected.
- **Zoom/scroll:** Ctrl+wheel zooms time, Alt+wheel zooms keys, Shift+wheel scrolls sideways. Drag
  the green handle on the ruler to change the clip length.
- **Keys:** ↑/↓ transpose (Shift: octave), ←/→ nudge, Q quantizes, Ctrl+C/X/V, Ctrl+D or Ctrl+B
  duplicates, Ctrl+Z / Ctrl+Y undo and redo.

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

1. Put the controller in MPE mode (lower zone; most controllers default to this).
2. FL Studio: *Options › MIDI settings*. Enable the controller as an input and give it a **Port**
   number, for example 10.
3. MPE Roll: open the plugin's wrapper settings (the gear icon in the plugin window's title bar). Set
   its **MIDI input port** to the same number. FL then sends the controller's raw MIDI, on all its
   channels, to MPE Roll.
4. Arm **Rec** in MPE Roll and start FL's transport. Each take is overdubbed into the clip, and one
   Ctrl+Z removes the whole take.

If notes play twice, FL is also sending the controller to the *selected* Channel Rack channel.
Select a different channel while you play.

MPE Roll follows the controller's MPE Configuration Messages automatically. If your controller uses
a different bend range or zone, set it under *MPE settings › Input from controller / FL*.

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

- **MIDI file ▾ › Import:** reads any MIDI file. MPE files keep their per-note expression; plain
  files import as notes with zone-wide pitch bend and controllers. You can also drop a `.mid` file
  onto MPE Roll.
- **MIDI file ▾ › Export:** writes an MPE MIDI file, with the zone setup at the start, for other DAWs
  and MPE-aware tools.
- **Drag the MIDI file button** into FL (playlist, piano roll or a channel) to drop the clip as a
  MIDI file. FL's own MIDI import keeps the notes but not per-note expression.
- MPE Roll's **MIDI output** carries the same MPE stream it sends to the hosted synth. In the
  wrapper settings, set an output port to send it to another plugin's input port.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| No sound | Load a synth (*Load synth…*), and check that the synth itself makes sound in its own window. |
| Glides are much too big or too small | The synth's MPE bend range doesn't match MPE Roll's *Per-note bend range* (48 by default). |
| All notes bend together | The synth isn't in MPE mode. Turn MPE on in the synth, or switch MPE Roll to *Plain MIDI*. |
| Recording catches nothing | The controller's Port in FL's MIDI settings must match MPE Roll's MIDI input port, and FL must be playing. |
| Notes play twice while recording | FL also sends the controller to the selected channel. Select another channel. |
| "Missing: <synth>" on the synth button | That synth isn't installed (or scanned) on this computer. Install it, scan, and pick it again. |
