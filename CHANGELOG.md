<!--
How this file is kept. It follows Keep a Changelog (https://keepachangelog.com/en/1.1.0/): newest
first, an [Unreleased] section at the top, then one section per release, "## [<version>] - <date>",
with the changes grouped as Added, Changed, Fixed or Removed, in plain sentences for the people who
use MPE Roll. Every pull request adds its lines under [Unreleased]. To release, rename [Unreleased]
to the version and the day, add its link at the bottom, and open an empty [Unreleased] above it:
the release workflow refuses to release a version that has no "## [<version>]" section here, and
pastes that section into the release notes, above the list of pull requests GitHub generates
(grouped by label as .github/release.yml says). A pre-release needs the section too, under the
version without its suffix ("## [0.3.0]" for v0.3.0-beta1). Links point at the website rather than
at files in the repository, so they still work from a release page.
-->

# Changelog

All notable changes to MPE Roll, newest first. The version numbers are the ones in `CMakeLists.txt`
and the release tags; a build between releases names itself by its number and commit instead
(bottom of the File menu).

## [Unreleased]

### Added

- A page that answers
  [Does FL Studio support MPE?](https://freener0x.github.io/MPE-Roll/MPE_IN_FL_STUDIO.html): what FL
  Studio's own piano roll does for one note and what it doesn't (checked in FL Studio 26.1.7 on
  Windows 11; the page says what that check doesn't record), what an MPE synth needs, how MPE Roll
  gets MPE into FL, what to do with a synth without MPE or with FL's own synths, recording from a
  controller, and what has and hasn't been tested. The website's header and front page link to it.
- A script that installs the macOS build with one line in Terminal:
  `curl -fsSL https://freener0x.github.io/MPE-Roll/install-macos.sh | bash`. It downloads the
  latest release's macOS zip (0.2.0 or later; a version can be given), checks it against the
  release's `SHA256SUMS.txt`, and copies the VST3, the Audio Unit and the standalone app into your
  own Library folders, with no password and nothing else changed, then prints what it did and the
  command that undoes it. A file fetched this way should get no quarantine flag, and the script
  takes the flag off its copies if they have one.
  [Unsigned builds](https://freener0x.github.io/MPE-Roll/UNSIGNED_BUILDS.html#macos-the-install-script)
  says what it does step by step, how to read it before running it, and how to install from a zip
  you already have. It hasn't run on a Mac yet: GitHub's macOS runner is set up to run it whenever
  the code changes, which hadn't had its first run when this was written, and nobody has run it,
  or the macOS build, on a Mac of their own.
- From the next release on, the release workflow signs a record (an artifact attestation, signed
  through Sigstore and kept by GitHub) that each download was built by it, and
  `gh attestation verify <file> --repo freener0x/MPE-Roll` (GitHub's command-line tool, 2.49.0 or
  newer, signed in) checks a download against it:
  [how, and what it shows](https://freener0x.github.io/MPE-Roll/UNSIGNED_BUILDS.html#verify-what-you-downloaded).
  It isn't code signing, so Windows and macOS warn about the downloads as before. Releases 0.1.0
  and 0.2.0 have no attestation, and the check hasn't been tried on a real download yet.
- The README says how MPE Roll is made
  ([How MPE Roll is made](https://github.com/freener0x/MPE-Roll#how-mpe-roll-is-made)): it is
  designed and tested in FL Studio by its maintainer and written with an AI coding assistant, and
  the section says what the automated tests cover. The website's front page links to it.

### Changed

- The README, the website and the share page's posts say more exactly what FL Studio lacks: FL's
  own piano roll can't give one note of a chord its own glide, pressure or timbre on a third-party
  synth (VST, VST3 or CLAP), and its slide notes, per-note pitch and Mod X / Mod Y work with FL's
  own synths only. They used to say only that FL's piano roll can't do MPE.
- The README and every page of the website say that MPE Roll is an independent project, not made
  by or affiliated with Image-Line, and that FL Studio is Image-Line's trademark.
- The website tells search engines more about itself: it has a `sitemap.xml`, each guide has a
  description of its own (five had only the site's tagline), the front page describes MPE Roll as a
  piece of software (free, its systems, where to download it, its licence), and the page of
  ready-made posts is left out of the sitemap. The front page's description is shorter (157
  characters, was 194). CI fails if a page is left with only the tagline as its description, or
  with one longer than 160 characters. The site still loads nothing from other sites.
- The website's header keeps its seven buttons in two even rows on a laptop and a tablet, and on a
  phone the last one takes the full width.
- A release that isn't a pre-release can be announced by the release workflow itself: in a Discord
  channel, and as a discussion in the repository's Announcements category, where the maintainer
  has set those up (the README's *Downloading a build* says how). An announcement that fails
  doesn't hold the release back.
- The release notes give the one-line macOS install and the attestation check beside the
  checksums, and link the "Does FL Studio support MPE?" page.
- The [Privacy](https://freener0x.github.io/MPE-Roll/PRIVACY.html#the-website) page says what the
  macOS install script contacts when someone runs it: GitHub, for the release's checksums and zip.
- The README, the FL Studio guide and
  [Known issues](https://freener0x.github.io/MPE-Roll/KNOWN_ISSUES.html) record what release 0.2.0
  was tested with in FL Studio 26.1.7 on Windows 11, as a VST3: the installer over 0.1.0, *Send to
  FL piano roll* saving into FL's scripts folder, the update check, *Report a bug…*, and Analog Lab
  (VST2 and VST3) no longer filling FL's undo history. They also say what that test found: FL sends
  no All Notes Off when MPE Roll's channel is switched off in the Channel Rack, so in *Follow FL*
  mode the switch stops MPE Roll only with *Follow FL only while FL holds a note* on and a note on
  its channel; and FL's plugin list can go on naming *Glowing Pancake* as MPE Roll's maker after an
  upgrade from 0.1.0, though MPE Roll gives *MPE Roll*, because FL's plugin wrapper keeps an empty
  registry entry under the old name. The FL Studio guide says how to remove it
  ([Removing the old maker name from FL](https://freener0x.github.io/MPE-Roll/FL_STUDIO.html#removing-the-old-maker-name-from-fl)).

## [0.2.0] - 2026-11-04

### Added

- An update check, off until you turn it on. *MPE settings › Updates › Check for updates when MPE
  Roll opens (once a day)* asks GitHub for the latest release at most once a day, as MPE Roll opens;
  *File › Check for updates now…* (or *Check now* in the same section) asks once, whether the daily
  check is on or not. A newer release shows as an *Update to 0.3.0* button (with the version found)
  beside the status line, and in the File menu as *Get MPE Roll 0.3.0 (release page)…* and *Skip
  version 0.3.0*, until you install or skip it. The button and *Get* open the release's page in
  your browser: MPE Roll downloads and installs nothing. Pre-releases are never offered, a skipped
  version stays quiet until a newer one is out, and a check that gets no answer says nothing unless
  you asked. A pre-release build checks as its whole version (0.3.0-beta1), so it's told when its
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
- A changelog (`CHANGELOG.md`), a security policy (`SECURITY.md`), release notes that start with the
  changelog's section for the version and group the pull requests by label (`.github/release.yml`),
  and Dependabot for the GitHub Actions the workflows use.

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
- The files of the plugin's code, tests, tools, build, installer and CI workflows start with an
  SPDX header: licence AGPL-3.0-only, copyright 2026 The MPE Roll contributors. CI checks them
  (`tools/check_spdx.py`).
- The maker name is *MPE Roll*, where it was *Glowing Pancake* (the repository's old name): hosts
  show it as the plugin's maker (the vendor of the VST3 and the VST2, the maker in the AU's name),
  and Windows' *Installed apps* as the publisher. The plugin's IDs, which hosts save in projects,
  and its settings folder stay as they were. VST3 hosts that use the standard preset folders keep
  each plugin's presets in a folder named after its maker, so the ones saved before are in
  `Documents\VST3 Presets\Glowing Pancake\MPE Roll` on Windows and
  `~/Library/Audio/Presets/Glowing Pancake/MPE Roll` on macOS: rename that *Glowing Pancake* folder
  to *MPE Roll* to see them again. If FL Studio still shows the old name, scan again with
  *Verify plugins* and *Rescan previously verified plugins* ticked (*Options › Manage plugins*).

### Fixed

- FL's undo history should no longer fill with *Knob tweak (channelN pitch bend)* entries when a
  hosted VST2 or VST3 synth's parameter follows the pitch bend (checked in MPE Roll's own tests, not
  yet in FL).
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

[Unreleased]: https://github.com/freener0x/MPE-Roll/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/freener0x/MPE-Roll/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/freener0x/MPE-Roll/releases/tag/v0.1.0
