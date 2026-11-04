# Privacy

What MPE Roll does with your data, as of release 0.2.0 (4 November 2026): it sends nothing anywhere
unless you turn on its update check or ask it to check (release 0.1.0 has no update check), writes a
few files on your computer, and its website is served by GitHub Pages. Each statement below was
checked against the source code; the file names in brackets say where to look.

## What MPE Roll sends

Nothing, unless you turn on the update check or ask it to check
([below](#the-update-check-off-by-default)): it's the only part of MPE Roll that opens a
connection. There is no telemetry, no crash report sent anywhere, no analytics and no ads. MPE Roll
is built without JUCE's web-browser part (`NEEDS_WEB_BROWSER FALSE` in `CMakeLists.txt`) and
without libcurl (`NEEDS_CURL FALSE`). The update check uses JUCE's networking, which goes through
the system's own on Windows and macOS; on Linux it has no HTTPS, so the experimental Linux build's
check never reaches GitHub and sends nothing. Its links open web pages in your browser only when
you click them, and nothing from MPE Roll goes with them except what the address itself carries:

- The donation buttons in the Support window (*MPE settings › Support MPE Roll*, at the foot of the
  settings) open the payment site (`source/plugin/ui/DonatePanel.cpp`). The window's QR codes are
  part of the plugin, not loaded from the web.
- *Share MPE Roll*, beside it, opens the website's share page.
- *Report a bug…* (File menu), *Report it* on the box for a synth that won't load, and *Report a
  problem with this synth…* (*Load synth* menu) open the matching issue form on GitHub with the
  build, your system (the OS, the host's file name and MPE Roll's format) and the synth's name
  filled in from the address, and put the plugin log on your clipboard. Nothing is sent until you
  submit the form on GitHub yourself, and the log reaches it only if you paste it
  (`source/plugin/BugReport.cpp`).

The addresses are in `source/plugin/Links.h`.

### The update check (off by default)

MPE Roll can tell you when a new release is out. Turn it on under *MPE settings › Updates*, or ask
once with *File › Check for updates now…*. When it checks, MPE Roll sends one plain HTTPS request to
GitHub for the latest release (api.github.com, or on Windows
github.com/freener0x/MPE-Roll/releases/latest if that fails). Nothing about you, your computer or
your project goes with it beyond what every web request carries: your IP address, the plugin's name
and version in the request's User-Agent header (`MPE Roll/0.2.0`, say), and the headers your system
adds to any request (on a Mac, the languages it prefers, and cookies for github.com if the host
program keeps any). MPE Roll keeps only whether the check is on, the time of the last check and how
it went, the version it found and any version you chose to skip, in a settings file of its own.

In detail (`source/plugin/UpdateCheck.cpp` and `source/plugin/UpdateChecker.cpp`):

- **When it checks.** Never until you act. With *Check for updates when MPE Roll opens (once a day)*
  switched on in *MPE settings › Updates*, it checks when an MPE Roll window opens and a day has
  passed since the last attempt, answered or not; switching it on checks at once on the same
  terms. *File › Check for updates now…* and *Check now* in *MPE settings › Updates* check when
  you click them, whether the daily check is on or not.
- **What it sends.** An HTTPS GET of
  `https://api.github.com/repos/freener0x/MPE-Roll/releases/latest`. On Windows, if that gives no
  release (no answer within 8 seconds, an error such as GitHub's limit of 60 requests an hour from
  one address, or an answer that isn't a release), one more GET, of
  `https://github.com/freener0x/MPE-Roll/releases/latest`, whose redirect names the latest release;
  the redirect isn't followed. MPE Roll sets `User-Agent: MPE Roll/<version>` on both (a
  pre-release gives its whole version, `MPE Roll/0.3.0-beta1`), and on the first also
  `Accept: application/vnd.github+json` and `X-GitHub-Api-Version: 2022-11-28`, which GitHub's API
  documentation recommends. Nothing else of MPE Roll's goes with them: no account, no ID, nothing
  from your projects. The system's networking adds its standard headers, as it does for any
  program, and uses the proxy the system is set to use, if any. On Windows (WinINet) the requests
  are made with cookies turned off (`INTERNET_FLAG_NO_COOKIES` in JUCE's Windows networking). On
  macOS the request goes through the system's default web session, which also sends the languages
  the Mac prefers, and would send cookies for github.com if the host program kept any. That
  session also keeps the cookies an answer sets, in the host program, for its later requests to
  send; the github.com page is an ordinary web page, which can set GitHub's own, so on a Mac the
  API is the only address asked.
- **What it keeps**, in `Update check.settings` (below): whether the daily check is on, when the
  last check started, how it ended (latest, newer, ahead, or why it found nothing: the reason it
  failed, or that it was switched off or MPE Roll closed before GitHub answered; unfinished while
  it waits for the answer), the latest release's tag it found, and the version you chose to skip.
  Every MPE Roll reads the file again before it decides anything, writes a change to it at once,
  and looks at it again every second while one of its windows is open, so they all go by the same
  settings, including those already running, and show the same a moment later: the switch, *Last
  check*, the *Update* button and the File menu.
- **What you see.** A newer release, say 0.3.0, shows as an *Update to 0.3.0* button beside the
  status line, and in the File menu as *Get MPE Roll 0.3.0 (release page)…* and *Skip version
  0.3.0*, until you install it or skip it, whether the daily check is on or not. The button and
  *Get* open the release's page on GitHub in your browser; MPE Roll downloads and installs nothing.
  Pre-releases are never offered, and a build newer than the latest release (one made between
  releases) is told of nothing. A version you skipped is no news until a newer one is out: a check
  you ask for names it in the status line, and *MPE settings › Updates* still has its page, but no
  button shows. A daily check that finds nothing new, or gets no answer, says nothing:
  *MPE settings › Updates › Last check* shows how it went. A check you ask for answers in the status
  line.
- **Turning it off.** Switch *Check for updates when MPE Roll opens (once a day)* off in
  *MPE settings › Updates*. Nothing is sent from then on unless you ask, by any MPE Roll you run,
  including one already open (the other plugin format, the standalone app, another host): a daily
  check waiting for GitHub's answer at that moment is called off, and doesn't go on to the release
  page.
- **Once a day per user.** Every MPE Roll you run reads the same file before it checks, so one
  check a day covers them all, MPE Roll's VST2 and VST3 in one FL project included. The file is
  your user account's (below): another account on the same computer has its own, and its own
  check.
- **What GitHub sees** is a request from your IP address, as for any visit to GitHub (see
  [GitHub's privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement)).
- **For testing**, the environment variables `MPEROLL_UPDATE_URL` and `MPEROLL_UPDATE_PAGE_URL`, and
  an `updateCheckUrl` entry in `Update check.settings`, point the check at other addresses. MPE
  Roll never sets them itself.

Nothing else in MPE Roll talks to the network, and nothing else that would is planned.

## What it writes on your computer

In `%APPDATA%\MPE Roll` on Windows, `~/Library/Application Support/MPE Roll` on macOS and
`~/MPE Roll` in the experimental Linux build, which are your user account's own:

- `MPE Roll.settings`: the settings every MPE Roll on this computer shares, with the list of synths
  a scan found (their names, formats, versions and file paths) (`source/plugin/HostedSynth.cpp`).
- `Update check.settings`: what the update check keeps
  ([above](#the-update-check-off-by-default)), written once you use its switch or a check runs
  (`source/plugin/UpdateChecker.cpp`).
- `Look.settings`: the skin and colours you chose (`source/plugin/ui/Skins.cpp`).
- `Plugin log.txt`: every scan and load of a synth, with how long it took and what went wrong
  (`source/plugin/PluginLog.cpp`). *Load synth… › Show the plugin log* opens it.
- `Crash report.txt`: written by the standalone app on Windows only, if it crashes: the exception,
  the call stack and what MPE Roll was doing (`source/plugin/CrashReport.h`). In FL Studio, crash
  handling is FL's.
- `RecentlyCrashedPluginsList`: while a scan runs, the plugin file being looked at, so one that took
  a scan down is remembered next time (`source/plugin/HostedSynth.cpp`).

Elsewhere:

- *Send to FL piano roll* writes `MPE Roll Import.pyscript` into FL's scripts folder,
  `<User data folder>\FL Studio\Settings\Piano roll scripts` (normally under
  `Documents\Image-Line`), making the `Piano roll scripts` folder if FL hasn't, or, when FL's folder
  isn't on the computer, where you choose in the save dialog (`source/core/FlScriptExporter.cpp`).
  On Windows it reads where FL keeps its user data from the registry, as MPE Roll's installer
  does. The script holds your clip's notes.
- Dragging the *File* button into FL writes the clip as a MIDI file in your temporary folder, under
  `MPE Roll` (`source/plugin/PluginEditor.cpp`). A scan keeps its working files in
  `MPE Roll scan …` folders there, deleted when it's done (`source/plugin/PluginScanner.cpp`).
- Exported MIDI, WAV and MP3 files go where you save them.
- Your FL Studio project holds MPE Roll's state (clips, settings, which synth), as it does for every
  plugin.

None of these leave your computer unless you send them.

## "Copy details" and the plugin log

When a synth won't load, MPE Roll's box has a *Copy details* button, and the *Report…* entries put
the same kind of report on the clipboard. It holds: the
build name ("MPE Roll 0.1.0, release v0.1.0, …"), your operating system's name ("Windows 11",
say), the name of the host program (`FL64.exe`, say), the plugin's name, format, version and file
path, the error, and the last 60 lines of the plugin log, headed by the log's own path (`report` in
`source/plugin/PluginLog.cpp`). That text goes nowhere until you paste it, into an issue for
instance. Read it first if you mind what's in it: the log's path, and any plugin path under your
profile, include your Windows user name.

## The website

This site (freener0x.github.io/MPE-Roll) is static pages served by GitHub Pages from the
repository's `docs/` folder. It loads nothing from other sites: no fonts, scripts, images, analytics
or ads from third parties (the project's CI fails the build if a page loads anything from another
site, `tools/check_site.py`), and the donation page's QR codes are images in the site itself.
GitHub logs visitors' IP addresses for security when a Pages site is visited (see
[About GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages#data-collection)
and
[GitHub's privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement));
the project sees no visitor data. The *Download* and *View on GitHub* buttons, the issue forms on
the front page, the donation links, and the share page's X, Facebook and Instagram buttons lead to
other sites with their own policies; the share buttons' links carry nothing but the post's words
and the site's address.

Issues and pull requests on GitHub are public.
