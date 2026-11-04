<!--
How this file is kept. It follows Keep a Changelog (https://keepachangelog.com/en/1.1.0/): newest
first, an [Unreleased] section at the top, then one section per release, "## [<version>] - <date>",
with the changes grouped as Added, Changed, Fixed or Removed, in plain sentences for the people who
use MPE Roll. Every pull request adds its lines under [Unreleased]. To release, rename [Unreleased]
to the version and the day, add its link at the bottom, and open an empty [Unreleased] above it:
the release workflow refuses to release a version that has no "## [<version>]" section here, and
pastes that section into the release notes, above the list of pull requests GitHub generates
(grouped by label as .github/release.yml says). A pre-release needs the section too, under the
version without its suffix ("## [0.2.0]" for v0.2.0-beta1). Links point at the website rather than
at files in the repository, so they still work from a release page.
-->

# Changelog

All notable changes to MPE Roll, newest first. The version numbers are the ones in `CMakeLists.txt`
and the release tags; a build between releases names itself by its number and commit instead
(bottom of the File menu).

## [Unreleased]

### Added

- An update check, off until you turn it on. *MPE settings › Updates › Check for updates when MPE
  Roll opens (once a day)* asks GitHub for the latest release at most once a day, as MPE Roll opens;
  *File › Check for updates now…* (or *Check now* in the same section) asks once, whether the daily
  check is on or not. A newer release shows as an *Update to 0.2.0* button (with the version found)
  beside the status line, and in the File menu as *Get MPE Roll 0.2.0 (release page)…* and *Skip
  version 0.2.0*, until you install or skip it. The button and *Get* open the release's page in
  your browser: MPE Roll downloads and installs nothing. Pre-releases are never offered, a skipped
  version stays quiet until a newer one is out, and a check that gets no answer says nothing unless
  you asked. A pre-release build checks as its whole version (0.2.0-beta1), so it's told when its
  release is out. A check sends GitHub one request (on Windows a second, to the release page, if
  the first fails) with MPE Roll's name and version, and nothing about you beyond what every web
  request carries; the
  [Privacy](https://freener0x.github.io/MPE-Roll/PRIVACY.html#the-update-check-off-by-default) page
  says exactly what it sends and keeps. What it keeps is in a settings file of its own, which every
  MPE Roll you run goes by, so the check switched off in one is off in those already open too (a
  check waiting for GitHub's answer is called off), and what one finds or skips shows in the
  others' windows a moment later. The experimental Linux build can't reach GitHub (it has no HTTPS)
  and says so.
- An icon: a piano-roll note whose end glides up. It shows on the Windows app, plugin and scanner
  files, on the VST3 folder in Explorer, in the macOS bundles, in the installer and on the website,
  and the README opens with a banner.
- *Report a bug…* in the File menu opens the issue form on GitHub in your browser with the build and
  the system filled in, and puts the plugin log on the clipboard to paste into it. The box that says
  a synth couldn't load has a *Report it* button that does the same with the *A synth won't load*
  form, the synth filled in too, and the *Load synth* menu has *Report a problem with this synth…*
  (*Report a synth that won't load…* while none is loaded). Your browser opens only when you click
  one, and nothing is sent until you submit the form on GitHub.
- *Follow FL only while FL holds a note* (*MPE settings › Playback*): in *Follow FL* mode the clip
  sounds only while FL holds a note on MPE Roll's channel, at the song position, so FL's patterns
  and playlist start and stop it, and its Channel Rack switch should too, if FL ends a switched-off
  channel's notes (not confirmed yet). Notes that touch keep it playing without a break. Those
  notes aren't played, but with *Rec* armed they're recorded and become notes of the clip, so
  switch the option off to record.
- A [Known issues](https://freener0x.github.io/MPE-Roll/KNOWN_ISSUES.html) page, a
  [Privacy](https://freener0x.github.io/MPE-Roll/PRIVACY.html) page and an
  [Unsigned builds](https://freener0x.github.io/MPE-Roll/UNSIGNED_BUILDS.html) page (what Windows
  and macOS say about the downloads, and how to get past it) on the website.
- A [share page](https://freener0x.github.io/MPE-Roll/share.html) on the website, with posts about
  MPE Roll ready for X, Facebook and Instagram, each with a link to the website. The X and Facebook
  buttons open a post for you to edit; Instagram has no link that starts a post, so its caption has
  a *Copy caption* button instead, beside a picture of the editor to post with it. Nothing is
  posted until you post it.
- `SHA256SUMS.txt` attached to every release, with the SHA-256 checksum of each download; the
  release notes list the checksums too.
- An experimental Linux build (the VST3, the standalone app and the scanner) attached to every
  release as `MPE-Roll-<version>-Linux.tar.gz`, and to every build between releases. No Linux host
  has run it yet.
- Apple's AU validator (`auval`) checks the macOS AU on every build and before a release.
- This changelog, a security policy (`SECURITY.md`), release notes that start with this changelog's
  section for the version and group the pull requests by label (`.github/release.yml`), and
  Dependabot for the GitHub Actions the workflows use.

### Changed

- The website's Download button opens the latest release.
- The website has a new front page: what MPE Roll does, in twelve parts with pictures of the editor
  and two from FL Studio, a short film of the editor at work, the formats, systems and requirements
  (and the other hosts that can load it as a VST3 or AU, untested there), what's in each download,
  and where to report a problem. Its Download button says which file of the latest release is the
  one for your computer (Windows, macOS or Linux). The tagline is one line: *The MPE piano roll for
  FL Studio*.
- GitHub Actions updated (checkout v7, upload-artifact v7, download-artifact v8).
- The release workflow takes only a version of three numbers (`0.2.0`) and a pre-release suffix of
  letters and digits with dots between (`beta1`, `rc.1`), the shapes the update check reads.
- The macOS bundles carry an ad-hoc code signature. It names no developer and isn't notarized, so
  macOS still warns about a download and the quarantine step stays.
- The release notes give the quarantine command for the whole unzipped `MPE Roll` folder, and the
  *Unblock* alternative for the Windows installer, and link the Unsigned builds page.
- The README, the FL Studio guide and the synth list record what release 0.1.0 was tested with in FL
  Studio 26.1.7 on Windows 11 (Analog Lab as VST2 and VST3, Omnisphere 3, Surge XT as CLAP, the
  installer over an earlier build, *Send to FL piano roll*).
- *Send to FL piano roll* saves the script straight into FL's scripts folder,
  `<User data folder>\FL Studio\Settings\Piano roll scripts`, with no dialog, and makes the
  `Piano roll scripts` folder when FL hasn't yet. On Windows it looks for the user data folder where
  FL's installer records it; whether that follows a folder moved in FL's settings hasn't been tried,
  so if the box names a folder outside the user data folder *Options › File settings* shows, move
  the file into that one's `FL Studio\Settings\Piano roll scripts`. It asks where to save only when
  FL's folder isn't on the computer, and then warns, with a *Show file* button, if the file lands
  anywhere else. The box, the status line and the toolbar button's tooltip name the folder and
  where FL runs the script: the piano roll menu › *Tools* › *MPE Roll Import*.
- MPE Roll opens in the *FL Studio* skin. If you never changed MPE Roll's look (its skin, colours or
  background picture), you'll see it after updating; *MPE settings › Look › Skin › MPE Roll* brings
  the old look back. A look you chose stays as it is.
- Donations are in *MPE settings* only: the File menu no longer has *Support MPE Roll (donate)*, and
  the *Support MPE Roll* link at the foot of the settings opens the same window. Beside it, a quiet
  *Share MPE Roll* link opens the website's share page, with posts ready for X, Instagram and
  Facebook.

### Fixed

- FL's undo history no longer fills with *Knob tweak (channelN pitch bend)* entries when a hosted
  synth's parameter follows the pitch bend, whatever the synth's format (VST2 or VST3).
- *Send to FL piano roll* no longer asks where to save, starting at the Documents folder, when FL's
  `Piano roll scripts` folder doesn't exist yet, and no longer reports success for a script saved
  outside the scripts folder it found for FL.
- *Follow FL*: an All Notes Off or All Sound Off from FL (CC 123, CC 120) while the song plays now
  stops the clip, until the transport restarts or jumps (each loop of a pattern or song counts as
  a jump) or FL sends a note. FL may send one when MPE Roll's channel is switched off in the
  Channel Rack, which until now left the clip playing; the plugin log notes the first one, so it
  shows whether yours does.
- *Note trigger*: an All Notes Off or All Sound Off from FL now stops a clip a note started, as the
  note's end does; until now the clip played on until that note's note-off.
- In *MPE settings*, a long name no longer runs under its setting's value (*Note expressions (CLAP,
  VST3)* was cut short).

## [0.1.0] - 2026-11-01

The first release, for Windows and macOS. Tested in FL Studio 26.1.7 on Windows 11; the macOS build
hasn't been run by a person yet. Its known issues are on the
[Known issues](https://freener0x.github.io/MPE-Roll/KNOWN_ISSUES.html) page.

### Added

- An MPE piano roll as an instrument plugin for FL Studio: VST3, VST2 and a standalone app on
  Windows, VST3, AU and the app on macOS. You load your synth inside it (VST3, VST2 through the
  clean-room FST headers, or CLAP), with a plugin scanner that looks at each plugin file in a
  separate process and a crash-safe blacklist; the synth's parameters show up as MPE Roll's, for
  FL's automation.
- Every MPE expression on every note: strike, lift, glide, vibrato, press, slide and any
  controller, drawn in expression lanes under the notes (draw, line, erase), plus zone-wide lanes
  and the MPE details (zones, channel rotation, the configuration message, bend ranges).
- Three ways to reach a synth: MPE over MIDI; CLAP note expressions, and VST3 note expressions for
  VST3 synths that map pitch bend on channel 1 only; and *One copy per note*, which plays each note
  on its own copy of a synth without MPE (up to 32 copies). *Plain MIDI* for synths without MPE,
  where the newest note's expression drives the synth.
- FL's piano roll tools and keys: Draw, Paint, Delete, Slice, Select and Glide on an icon toolbar,
  the Tools menu (quantize, chop, glue, arpeggiate, strum, flam, legato, articulate, limit, flip,
  randomize, scale levels) with live preview, FL's editing keys, and 16 clip slots played in sync
  with FL or triggered by notes. Glides snap to semitones and the grid; a vibrato lane played by
  MPE Roll.
- Recording from an MPE controller, with presets for ROLI, LinnStrument, Haken, Osmose, Push 3,
  K-Board Pro 4, QuNexus, Erae, Sensel Morph, Joué and poly-aftertouch keyboards.
- *Send to FL piano roll*: for FL's native synths, a piano roll script that rebuilds the clip in
  FL's own piano roll with slide notes, fine pitch and Mod X / Mod Y.
- MIDI file import and export (or drag the *File* button into FL), and audio export as 24-bit WAV
  or 320 kbps MP3 through a copy of the synth, release tail included.
- Ten skins, any colour of them changeable, and a background picture of your own.
- The MPE of 673 instruments from 181 makers, listed on the website with where each one's MPE
  switch is; the plugin knows 593 of them (most of the others play inside another plugin, such as
  Kontakt, which it knows).
- A Windows installer that finds and removes older copies, tested in CI; a plugin log and a *Copy
  details* button for loads that fail; crash reports from the standalone app; a Support window; the
  website with the FL Studio guide; a release workflow and issue templates.

[Unreleased]: https://github.com/freener0x/MPE-Roll/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/freener0x/MPE-Roll/releases/tag/v0.1.0
