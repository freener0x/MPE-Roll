# Does FL Studio support MPE?
<!-- The page for the question people type into a search engine. Its description, for search
     engines, is in _config.yml. Say here only what the README, the FL Studio guide, Known issues
     and the list of synths say, and what was and wasn't checked. -->

FL Studio can load a synth that has MPE, but its own piano roll can't write MPE for it. In FL Studio
26.1.7 on Windows 11, FL's piano roll can't give one note of a chord its own pitch glide, pressure
or timbre on a third-party synth (VST, VST3 or CLAP). Its slide notes, per-note pitch and Mod X /
Mod Y work with FL's own synths only.

[MPE Roll](https://freener0x.github.io/MPE-Roll/) is a free, open-source instrument plugin made for
that gap: FL loads MPE Roll, MPE Roll loads your synth, and you draw or record each note's
expression in MPE Roll's own piano roll.

> The answer above was checked by MPE Roll's maintainer in FL Studio 26.1.7 on Windows 11. No other
> version of FL has been tried. Not recorded with that check: which synth it was made with, and how
> the options in FL's wrapper for that synth (the gear icon in the plugin window's title bar) were
> set. So this page can't say whether one of those options changes any of it.
> [What has been tested, and what hasn't](#what-has-been-tested-and-what-hasnt) says which of the
> rest of this page was tried and which follows documentation. MPE Roll is an independent project,
> not made by or affiliated with Image-Line. If a newer FL Studio changes any of this, or you know
> a setting in FL that does, please
> [open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose) so this page can say so.

## What is MPE, and what does a synth need for it?

MPE (MIDI Polyphonic Expression) puts each note on a MIDI channel of its own, so the pitch bend,
pressure and timbre (controller 74) sent on that channel reach that note and no other. In a chord,
one note can then glide up while the others hold, or be pressed harder than the rest.

To be played that way, a synth needs:

- **An MPE mode, switched on.** Most synths with MPE have a switch for it in their settings or
  global menu. The [list of synths](MPE_SYNTHS.md) says, maker by maker, which instruments have MPE
  and where to turn it on.
- **The same per-note bend range at both ends.** MPE Roll's is 48 semitones unless you change it.
  When the synth's differs, glides come out much too big or too small.
- **Something that sends each note on a channel of its own:** an MPE controller, or a piano roll
  that keeps each note's expression apart from the others'. FL's own piano roll doesn't do that for
  a third-party synth.

Some synths take per-note expression another way, without MPE's channels: a CLAP synth through CLAP
note expressions, a VST3 synth through VST3 note expressions. MPE Roll uses those too
([below](#what-if-my-synth-has-no-mpe)).

## What can FL Studio's piano roll do for one note?

| | FL's own synths (FLEX, Sytrus, Harmor...) | Third-party synths (VST, VST3, CLAP) |
| --- | --- | --- |
| One note's own pitch glide | Slide notes and per-note pitch | No |
| One note's own pressure and timbre | Mod X / Mod Y, which you map in the synth | No |
| MPE | No: FL's own synths can't receive it | Not from FL's piano roll |

The right-hand column is what the check at the top of this page found, with the limits given
there. A slide note bends only the notes of its own colour, and FL has 16 colours, so a chord whose
notes each go their own way needs a colour for each note.

## Can FL Studio load MPE synths?

Yes. FL Studio loads third-party synths as VST and VST3 plugins, and it can load CLAP plugins
itself. Loading isn't what's missing. What's missing is in the piano roll: by the check at the top
of this page, it can't write one note's own glide, pressure or timbre for such a synth.

## How does MPE Roll add MPE to FL Studio?

MPE Roll is the instrument FL sees. It has a piano roll of its own and plays a synth loaded inside
it, in time with FL.

1. Install MPE Roll and add it to the Channel Rack like any instrument.
2. Click *Load synth…* and pick your synth: VST3, VST2 or CLAP.
3. Turn on MPE in the synth and set its MPE bend range to 48. A CLAP synth that takes CLAP note
   expressions needs nothing set up.
4. Draw notes in MPE Roll's piano roll and press play in FL. With the Glide tool (G), drag from a
   note to the pitch it should reach; the lanes under the notes hold each note's vibrato, pressure
   and timbre.

The [FL Studio guide](FL_STUDIO.md) has every step, from the installer to troubleshooting.

## What if my synth has no MPE?

MPE Roll plays a synth the way that synth understands, and the top of *MPE settings* says which
way that is:

- **MPE over MIDI**, for a synth with MPE.
- **CLAP note expressions**, for a CLAP synth that takes them, with nothing to set up in the synth.
- **VST3 note expressions**, for a VST3 synth that takes pitch bend on channel 1 only but takes
  note expressions. This has been tested with a test synth, not yet with a commercial one.
- **One copy per note**, for a synth without MPE. MPE Roll loads several copies of the synth, 8 to
  start with and up to 32, and plays each note on a copy of its own, so every note keeps its own
  glide, vibrato, pressure and timbre, at the cost of a copy's CPU and memory each. The guide's
  [One copy per note](FL_STUDIO.md#one-copy-per-note-mpe-for-any-synth) says what to set in the
  synth.
- **Plain MIDI**, also for a synth without MPE: the newest note's glide, pressure and timbre drive
  the whole synth.

## What about FL's own synths, like FLEX, Sytrus and Harmor?

They can't receive MPE, and MPE Roll can't load them: they come in Image-Line's own plugin format.
For them, *Send to FL piano roll* writes a script. Run it from the synth's piano roll (*Tools*, FL
Studio 21.1 or newer) and it rebuilds the clip there with what FL's piano roll has: glides as
chains of slide notes, a note's starting offset as fine pitch, and pressure and timbre as Mod X and
Mod Y. Each overlapping note gets a colour of its own, 16 at most at a time. In FL Studio 26.1.7 on
Windows 11 the glides landed in tune. The guide's
[Send to FL piano roll](FL_STUDIO.md#5-fls-native-synths-send-to-fl-piano-roll) has the steps.

## Can I record an MPE controller in FL Studio?

Into MPE Roll, by this route:

1. Put the controller in MPE mode, and pick it under *MPE settings › Controller* in MPE Roll. Its
   preset sets the bend range and the zones, and a *Setup* line says what to set on the controller
   itself.
2. In FL Studio's *Options › MIDI settings*, enable the controller as an input and give it a
   **Port** number.
3. In MPE Roll's wrapper settings (the gear icon in the plugin window's title bar), set the **MIDI
   input port** to the same number.
4. Arm *Rec* in MPE Roll and start FL's transport. Each take is overdubbed into the clip, and one
   undo removes it.

If notes play twice, FL is also sending the controller to the selected Channel Rack channel: select
another channel while you play. Switch *Follow FL only while FL holds a note* off while you record.

This route follows Image-Line's documentation and user reports: the port routing and the recording
haven't been tested in FL. The controller presets follow each maker's documentation, and none has
been tried on the hardware. If you record with a controller, please report how it went. The guide's
[Record from an MPE controller](FL_STUDIO.md#4-record-from-an-mpe-controller) has more.

## What has been tested, and what hasn't?

Tried by hand in FL Studio 26.1.7; by the maintainer, on Windows 11, unless it says otherwise:

- FL's own piano roll with third-party synths: the answer at the top of this page. Which synth,
  and how FL's wrapper for it was set, isn't recorded.
- Omnisphere 3 with its MPE on, played from MPE Roll over MPE: per-note glides, pressure and timbre.
- Surge XT's CLAP, played with CLAP note expressions, with nothing set up in it.
- One copy per note with Analog Lab (VST3), FabFilter Twin 3, Serum 1, Kontakt 8 and Arcade: each
  note's own glides, by a user's report in FL Studio 26.1.7. In all five the glides stopped at 2
  semitones, the synth's own pitch-bend range, until that's raised in the synth and *Zone bend
  range* set to match.
- *Send to FL piano roll*: the glides landed in tune.

Checked automatically on every change: unit tests of the MPE engine, and a test harness that plays
clips through VST3, VST2 and CLAP test synths.

Not tested:

- Any other version of FL Studio.
- The macOS build. Nobody has run it yet;
  [issue 12](https://github.com/freener0x/MPE-Roll/issues/12) asks for testers.
- Recording from a controller and MIDI port routing in FL, and the controller presets on the
  hardware.
- VST3 note expressions with a commercial synth.
- Any host other than FL Studio.
- FL's piano roll with each option of FL's wrapper for a third-party synth switched on in turn.
  The check at the top doesn't record how they were set, so an option there that changes the
  answer isn't ruled out.
- Polyphonic aftertouch: one key's own pressure in ordinary MIDI, which isn't MPE. Whether FL's
  piano roll can send it to a third-party synth isn't something this page has checked.
- A controller played live into a synth loaded in FL itself, without MPE Roll. This page is about
  notes written in a piano roll, and says nothing about that.

[Known issues](KNOWN_ISSUES.md) has the full list, with what to do about each.

## Where do I get MPE Roll?

- [Download the latest release](https://github.com/freener0x/MPE-Roll/releases/latest): free, for
  Windows 10 or 11 (64-bit) and macOS 11 or newer, with an experimental Linux build. The downloads
  aren't code-signed; [Unsigned builds](UNSIGNED_BUILDS.md) says what Windows and macOS show about
  them, and how to get past it.
- [Using MPE Roll in FL Studio](FL_STUDIO.md): installing it, loading your synth, routing, and what
  to do when something doesn't work.
- [Synths and their MPE](MPE_SYNTHS.md): instruments by maker, whether each has MPE, and where to
  turn it on.
- [The code](https://github.com/freener0x/MPE-Roll), under the AGPL-3.0.
