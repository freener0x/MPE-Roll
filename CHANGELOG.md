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

- A [Known issues](https://freener0x.github.io/MPE-Roll/KNOWN_ISSUES.html) page, a
  [Privacy](https://freener0x.github.io/MPE-Roll/PRIVACY.html) page and an
  [Unsigned builds](https://freener0x.github.io/MPE-Roll/UNSIGNED_BUILDS.html) page (what Windows
  and macOS say about the downloads, and how to get past it) on the website.
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
- GitHub Actions updated (checkout v7, upload-artifact v7, download-artifact v8).
- The macOS bundles carry an ad-hoc code signature. It names no developer and isn't notarized, so
  macOS still warns about a download and the quarantine step stays.
- The release notes give the quarantine command for the whole unzipped `MPE Roll` folder, and the
  *Unblock* alternative for the Windows installer, and link the Unsigned builds page.
- The README, the FL Studio guide and the synth list record what release 0.1.0 was tested with in FL
  Studio 26.1.7 on Windows 11 (Analog Lab as VST2 and VST3, Omnisphere 3, Surge XT as CLAP, the
  installer over an earlier build, *Send to FL piano roll*).

### Fixed

- FL's undo history no longer fills with *Knob tweak (channelN pitch bend)* entries when a hosted
  synth's parameter follows the pitch bend, whatever the synth's format (VST2 or VST3).

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
- The MPE of 673 instruments from 181 makers, known to the plugin and listed on the website,
  with where each one's MPE switch is.
- A Windows installer that finds and removes older copies, tested in CI; a plugin log and a *Copy
  details* button for loads that fail; crash reports from the standalone app; a Support window; the
  website with the FL Studio guide; a release workflow and issue templates.

[Unreleased]: https://github.com/freener0x/MPE-Roll/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/freener0x/MPE-Roll/releases/tag/v0.1.0
