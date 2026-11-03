# Privacy

What MPE Roll does with your data, as of release 0.1.0 and the builds since (3 November 2026): it
sends nothing anywhere, writes a few files on your computer, and its website is served by GitHub
Pages. Each statement below was checked against the source code; the file names in brackets say
where to look.

## What MPE Roll sends: nothing

MPE Roll makes no network requests. It is built without JUCE's networking and web-browser parts
(`NEEDS_CURL FALSE` and `NEEDS_WEB_BROWSER FALSE` in `CMakeLists.txt`), and nothing in its source
opens a connection: no update check, no telemetry, no crash reports sent anywhere, no analytics, no
ads. The only web addresses it opens are the donation links behind the buttons in *File › Support
MPE Roll* (and the link at the foot of *MPE settings*), which open the payment site in your browser
when you click them (`source/plugin/ui/DonatePanel.cpp`); nothing from MPE Roll goes with them, and
the Support window's QR codes are part of the plugin, not loaded from the web. The repository's
address is in the plugin too (`source/plugin/Links.h`), but nothing opens it.

## What it writes on your computer

In `%APPDATA%\MPE Roll` on Windows and `~/Library/Application Support/MPE Roll` on macOS:

- `MPE Roll.settings`: the settings every MPE Roll on this computer shares, with the list of synths
  a scan found (their names, formats, versions and file paths) (`source/plugin/HostedSynth.cpp`).
- `Look.settings`: the skin and colours you chose (`source/plugin/ui/Skins.cpp`).
- `Plugin log.txt`: every scan and load of a synth, with how long it took and what went wrong
  (`source/plugin/PluginLog.cpp`). *Load synth… › Show the plugin log* opens it.
- `Crash report.txt`: written by the standalone app on Windows only, if it crashes: the exception,
  the call stack and what MPE Roll was doing (`source/plugin/CrashReport.h`). In FL Studio, crash
  handling is FL's.
- `RecentlyCrashedPluginsList`: while a scan runs, the plugin file being looked at, so one that took
  a scan down is remembered next time (`source/plugin/HostedSynth.cpp`).

Elsewhere:

- *Send to FL piano roll* writes `MPE Roll Import.pyscript` into
  `Documents\Image-Line\FL Studio\Settings\Piano roll scripts`, or where you choose in the save
  dialog (`source/core/FlScriptExporter.cpp`). The script holds your clip's notes.
- Dragging the *File* button into FL writes the clip as a MIDI file in your temporary folder, under
  `MPE Roll` (`source/plugin/PluginEditor.cpp`). A scan keeps its working files in
  `MPE Roll scan …` folders there, deleted when it's done (`source/plugin/PluginScanner.cpp`).
- Exported MIDI, WAV and MP3 files go where you save them.
- Your FL Studio project holds MPE Roll's state (clips, settings, which synth), as it does for every
  plugin.

None of these leave your computer unless you send them.

## "Copy details" and the plugin log

When a synth won't load, MPE Roll's box has a *Copy details* button. It puts on the clipboard: the
build name ("MPE Roll 0.1.0, release v0.1.0, …"), your operating system's name ("Windows 11",
say), the name of the host program (`FL64.exe`, say), the plugin's name, format, version and file
path, the error, and the last 60 lines of the plugin log, headed by the log's own path (`report` in
`source/plugin/PluginLog.cpp`). That text goes nowhere until you paste it, into an issue for
instance. Read it first if you mind what's in it: the log's path, and any plugin path under your
profile, include your Windows user name.

## The website

This site (freener0x.github.io/MPE-Roll) is static pages served by GitHub Pages from the
repository's `docs/` folder. It loads nothing from other sites: no fonts, scripts, images, analytics
or ads from third parties (the project's CI fails the build if a page pulls fonts from Google), and
the donation page's QR codes are images in the site itself. GitHub logs visitors' IP addresses for
security when a Pages site is visited (see
[About GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages#data-collection)
and
[GitHub's privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement));
the project sees no visitor data. The *Download* and *View on GitHub* buttons, and the donation
links, lead to other sites with their own policies.

Issues and pull requests on GitHub are public.

## Planned

An opt-in update check is planned for release 0.2.0: off until you turn it on, at most one request a
day to GitHub for the latest release's version, with a *Check now* you can run yourself. This page
will describe exactly what it sends when it ships. Nothing else that talks to the network is
planned.
