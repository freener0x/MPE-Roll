# Security policy

## Supported versions

The [latest release](https://github.com/freener0x/MPE-Roll/releases/latest) is the one that gets
fixes; a security fix comes as a new release, not as a patch to an old one. Builds between releases
(from the Actions tab) carry everything merged so far, so they have a fix before the release does.

| Version | Supported |
| --- | --- |
| The latest release (0.2.0 as this is written) | Yes |
| Builds between releases, from the Actions tab | Yes, as what the next release will be |
| Anything older | No: update |

## Reporting a vulnerability

Please report privately, not in a public issue:

1. Use GitHub's private vulnerability reporting on the repository's *Security* tab (*Report a
   vulnerability*, which opens <https://github.com/freener0x/MPE-Roll/security/advisories/new>).
   If there's no such button, or the form doesn't open, it isn't turned on yet: go to step 2.
2. Until then, open an issue titled "Security" with no details in it. The maintainer will reply
   with a channel to send them through.

Say which build (the line at the bottom of MPE Roll's File menu), the host and the system, what
happens and how to make it happen. MPE Roll is one person's project, so please allow some days for
an answer. You'll be credited in the fix's notes if you want to be.

## Scope

- **Plugins you load are yours.** MPE Roll loads third-party plugin binaries that you choose (VST3,
  VST2, CLAP) into FL Studio's process, or its own as the standalone app, as every plugin host does;
  a malicious or broken synth can then do whatever the host can. That is out of scope: MPE Roll
  can't protect you from a synth you install. (The scanner looks at each plugin file in a separate
  process so a crash doesn't take FL down; that is for stability, not a security boundary.)
- **In scope** is what MPE Roll itself parses or produces from data you give it: MIDI files it
  imports, what the scanner reads from plugin files and passes back to the plugin, the piano roll
  script that *Send to FL piano roll* writes (Python that FL Studio runs: a clip must never produce
  a script that does anything but rebuild the clip), and the settings and project state it reads
  back. A crafted MIDI file that crashes MPE Roll, or a clip that makes the script do something
  else, is a report we want.
- **Network: the update check only.** Release 0.1.0 makes no network requests. 0.2.0 and later
  have an update check, off until the user turns it on or asks: an HTTPS request to GitHub for the
  latest release (the [Privacy page](https://freener0x.github.io/MPE-Roll/PRIVACY.html) says
  exactly what it sends). What MPE Roll reads from the answer, the release's tag from GitHub's JSON
  or from the release page's redirect, is in scope; the page it opens is built from that tag, never
  taken from the answer. Otherwise the only web addresses it opens are links you click (donation,
  share, issue forms), in your browser.
- **Supply chain.** The downloads are built by GitHub Actions from the public source and aren't code
  signed (why, and how to check a download, is on the
  [Unsigned builds page](https://freener0x.github.io/MPE-Roll/UNSIGNED_BUILDS.html)). JUCE, CLAP
  and LAME are fetched at build time, pinned to a version in `CMakeLists.txt` (LAME's archive to a
  SHA-256 hash as well); the GitHub Actions the workflows use are kept current by Dependabot.
