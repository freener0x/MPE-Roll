# Unsigned builds: what Windows and macOS will say

MPE Roll's downloads aren't code-signed, so Windows and macOS warn before they run them. This page
says what each system shows for release 0.1.0 and later, what to do about it, why the
warnings appear, how to check that a download is the one GitHub Actions made, and what signing
would take later, and it describes the script that installs the macOS build with one line in
Terminal. The short form is in the [FL Studio guide](FL_STUDIO.md)'s install step.

Where a sentence rests on other people's reports and on documentation rather than on a check of
this build, it says "reported"; the last section lists those points.

## Windows 11: the installer

1. Download `MPE-Roll-Setup-<version>.exe` from the
   [releases page](https://github.com/freener0x/MPE-Roll/releases). If Microsoft Edge says the file
   "isn't commonly downloaded", open the "..." menu on that download, choose *Keep*, then *Show
   more* and *Keep anyway*. Other browsers have a similar *Keep* choice.
2. Close FL Studio and double-click the installer. Windows shows a blue *Windows protected your PC*
   screen: click *More info*, then *Run anyway*.
3. A User Account Control prompt follows, with a yellow banner and *Publisher: Unknown*, because
   the installer writes to Program Files. Click *Yes*.
4. Instead of step 2 you can clear the download mark once: right-click the `.exe`, choose
   *Properties*, tick *Unblock* on the *General* tab and click *OK*. Windows then starts it without
   the SmartScreen screen.
5. No *Run anyway* button? Smart App Control is on (it comes on with new Windows 11 PCs), or
   SmartScreen is set to block rather than warn. Smart App Control blocks every program without a
   trusted signature and can't make an exception for one app, so there is no button to look for.
   It can only be turned off for the whole PC, and Windows may not let you turn it back on without
   reinstalling, so this page gives no steps for that: wait for a signed build, or use a PC without
   it. Whether it also stops FL Studio loading the unsigned plugin files isn't known; please report
   what you see.

## Windows 11: the zip

1. Before extracting `MPE-Roll-<version>-Windows.zip`, right-click it, choose *Properties*, tick
   *Unblock* and click *OK*. Windows marks a downloaded zip as from the internet, and File Explorer
   is reported to pass that mark on to every file it extracts (7-Zip doesn't, unless its *Propagate
   Zone.Id stream* option is on); unblocking the zip first means nothing extracted carries it.
2. Extract, then copy the whole `MPE Roll.vst3` folder to `C:\Program Files\Common Files\VST3`,
   and/or `MPE Roll.dll` with `MPE Roll Scanner.exe` beside it to a VST2 folder FL Studio scans
   (the [FL Studio guide](FL_STUDIO.md) has the details). FL Studio loads the plugin without a
   SmartScreen check: that check runs when you start a program from Explorer, not when a DAW loads
   a plugin file.
3. If you skipped step 1, the standalone `MPE Roll.exe` shows *Windows protected your PC* the first
   time you start it: *More info*, then *Run anyway*, or *Properties* and *Unblock* as above.

## macOS: the install script

One line in Terminal installs the latest release (0.2.0 or later) for you, with no password:

```
curl -fsSL https://freener0x.github.io/MPE-Roll/install-macos.sh | bash
```

[install-macos.sh](install-macos.sh) downloads the release's `MPE-Roll-<version>-macOS.zip` and
its `SHA256SUMS.txt` from GitHub, stops if the zip's checksum isn't the one listed, unpacks the zip
with `ditto`, and copies the three bundles into your own Library folders, in place of copies
already there:

- `MPE Roll.vst3` into `~/Library/Audio/Plug-Ins/VST3`
- `MPE Roll.component` (the Audio Unit) into `~/Library/Audio/Plug-Ins/Components`
- `MPE Roll.app` into `~/Applications`

It uses no `sudo`, makes those folders if they're missing, and changes nothing else; what it
downloads goes into a temporary folder that it removes. At the end it prints where the bundles
went and the command that undoes the install, which removes the three of them:

```
rm -rf ~/Library/Audio/Plug-Ins/VST3/"MPE Roll.vst3" \
    ~/Library/Audio/Plug-Ins/Components/"MPE Roll.component" ~/Applications/"MPE Roll.app"
```

Then rescan plug-ins in your DAW (step 4 below). There should be no download mark to remove
first: macOS's quarantine mark is put on a file by the program that downloads it, which browsers
do and `curl` is reported not to. The script doesn't count on that: it looks for the mark on the
three copies it installed, takes it off if it's there, and says so.

To read the script before it runs, download it, look at it, then run it:

```
curl -fsSLO https://freener0x.github.io/MPE-Roll/install-macos.sh
less install-macos.sh
bash install-macos.sh
```

It can also be read in a browser, in the
[repository on GitHub](https://github.com/freener0x/MPE-Roll/blob/main/docs/install-macos.sh);
the website serves that same file.

Other things it can do:

- Another release: `bash install-macos.sh 0.2.0`, or with the one-line form
  `curl -fsSL https://freener0x.github.io/MPE-Roll/install-macos.sh | bash -s -- 0.2.0`. Release
  0.1.0 has no `SHA256SUMS.txt`, so the script can't install it.
- A zip you already have, downloading nothing: `bash install-macos.sh --zip <file>`, with a
  release's `MPE-Roll-<version>-macOS.zip` or a build between releases (`MPE-Roll-bundles.zip`, or
  the zip from the Actions tab that holds it). Add `--sums SHA256SUMS.txt` to have it checked
  against that list.

What has been checked, and where: so far the script has run on Linux only, with stand-ins for the
macOS commands it uses (`ditto`, `xattr`). There it downloaded release 0.2.0, matched its
checksum and put the three bundles in place. It hasn't run on a Mac. GitHub Actions is set up to
run it on its macOS runner whenever the code changes, with the bash that macOS ships: on that
build's own zip, on the same zip carrying the quarantine mark, and, fed to bash as the one line
above does it, for the latest release, download and checksum included. Each time it then checks
that the three bundles are in place, complete and without the mark, and Apple's AU validator
(`auval`) is run on the installed Audio Unit. The same job looks for the quarantine mark on a file
fetched with `curl`. A failure fails that build's macOS job, except in the run for the latest
release, which only warns: a release that's already out can't be mended by a later change. Those
steps hadn't had their first run when this was written; the macOS job of the latest
[Build run](https://github.com/freener0x/MPE-Roll/actions/workflows/build.yml) shows how they
went. No person has run the script on a Mac of their own, and nobody has yet loaded the installed
plug-in in a DAW, so whether macOS then shows a dialog isn't known. If you try it, please
[open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose) and say what happened.

## macOS 15 and 26: the zip

To install by hand instead of with the script, follow the steps below. Nobody has run the macOS
build yet: GitHub Actions builds `MPE Roll.vst3`, `MPE Roll.component` and `MPE Roll.app` on every
code change, but no person has opened them on a Mac, so the dialogs below are what macOS is
reported to show for unsigned software, not what was seen with this build. If you try it, please
[open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose) with what happened, the
exact dialog text included, whether it worked or not.

1. Double-click `MPE-Roll-<version>-macOS.zip` in Finder, or run
   `ditto -x -k MPE-Roll-<version>-macOS.zip .` in Terminal; other unzip tools can drop the app's
   executable permission. You get a folder `MPE Roll` holding the three bundles. A build between
   releases (**MPE-Roll-macOS-build-N** from the Actions tab) unzips to one more zip,
   `MPE-Roll-bundles.zip`: open that one the same way to get the folder.
2. Remove the download (quarantine) mark while the folder is still in Downloads, so no admin
   password is needed. In Terminal:

   ```
   xattr -dr com.apple.quarantine ~/Downloads/"MPE Roll"
   ```

   Or type `xattr -dr com.apple.quarantine ` and drag the folder into the Terminal window, then
   press Return. Without this the DAW is reported to say "Apple could not verify 'MPE Roll.vst3' is
   free of malware..." (older wording: "...cannot be opened because the developer cannot be
   verified") and the plugin doesn't load. If that dialog appears, click *Done*, not *Move to
   Trash*.
3. Move `MPE Roll.vst3` to `/Library/Audio/Plug-Ins/VST3` (or `~/Library/Audio/Plug-Ins/VST3` for
   your user only) and `MPE Roll.component` to `/Library/Audio/Plug-Ins/Components` (or
   `~/Library/Audio/Plug-Ins/Components`). Finder asks for your password for the `/Library`
   folders. `~/Library` is hidden: hold Option and open Finder's *Go* menu.
4. Rescan in your DAW. Logic Pro: *Logic Pro › Settings › Plug-in Manager*, select MPE Roll,
   *Reset & Rescan Selection*. Other DAWs: rescan plug-ins in their preferences.
5. The standalone app: drag `MPE Roll.app` to Applications and double-click it. If you skipped
   step 2, macOS says it could not verify the app: click *Done*, open *System Settings › Privacy &
   Security*, scroll to *Security*, click *Open Anyway* beside the MPE Roll line (it's there for
   about an hour after the attempt), enter your password, then click *Open*. macOS remembers the
   exception. Control-click › *Open* no longer gets past this since macOS 15. For the plug-in
   bundles use step 2: *Open Anyway* is reported not to appear, or not to work, for plug-ins,
   because the DAW loads them rather than you opening them.
6. Already copied the bundles into `/Library` before removing the mark? Run

   ```
   sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/MPE Roll.vst3" \
       "/Library/Audio/Plug-Ins/Components/MPE Roll.component"
   ```

   then rescan.

## Why the warnings

Windows and macOS both check a downloaded program for a signature from a registered developer.
Windows (Microsoft Defender SmartScreen) looks up the reputation of the signing certificate and of
the file itself; a file with no signature and no history gets the *Windows protected your PC*
screen until enough people have run it. macOS (Gatekeeper) expects software opened from a download
to be signed with an Apple Developer ID and notarized by Apple, and blocks anything else the first
time it's opened.

MPE Roll's builds are made by GitHub Actions straight from the public source, and they aren't
signed because a signature costs money every year (about US$99 for Apple's Developer Program, and
about US$129 to 540 plus a hardware key for a Windows certificate), which the project doesn't spend
yet. From 0.2.0 on, the macOS bundles carry an ad-hoc signature, one that names no developer and
isn't notarized, so macOS still warns about a download and the steps above stay the same. The
warnings say "unknown publisher", not "malware found". Download only from the
[releases page](https://github.com/freener0x/MPE-Roll/releases), and check that the build stamp at
the bottom of MPE Roll's File menu names the release you downloaded, e.g. "MPE Roll 0.1.0, release
v0.1.0, fd8e433, 2026-11-01".

## Verify what you downloaded

Every release from 0.2.0 on carries a `SHA256SUMS.txt` beside the downloads, with the SHA-256
checksum of each file as GitHub Actions made it (0.1.0 was published before the release workflow
wrote one, so its page has none). Compare it with the checksum of the file you have:

- Windows, in a Command Prompt or PowerShell window: `certutil -hashfile <file> SHA256`
- macOS and Linux, in a terminal: `shasum -a 256 <file>`; or, in the folder you downloaded into,
  `shasum -a 256 -c SHA256SUMS.txt`, which prints *OK* for each listed file you have (and complains
  about the ones you didn't download).

If the checksum differs, the file was changed or damaged on the way: delete it and download it
again. A matching checksum says the file is the one the release carries; it isn't a signature, since
GitHub serves both the file and the checksum.

Releases after 0.2.0 also get an *artifact attestation*: when the release workflow has built the
files, it signs a record of where each one came from (this repository, its Release workflow, the
commit), naming the file by its SHA-256. It signs through Sigstore, the public signing service
GitHub's attestations use for a public repository, with the identity GitHub gives that run of the
workflow, and GitHub keeps the record with the repository. Releases 0.1.0 and 0.2.0 were made
before the workflow did this and have none, and no release has been made with it yet, so the steps
below haven't been tried on a real download. To check a file against its attestation you need the
[GitHub CLI](https://cli.github.com) (`gh`), version 2.49.0 or newer, signed in to a GitHub
account (`gh auth login`), on any system:

```
gh attestation verify MPE-Roll-<version>-macOS.zip --repo freener0x/MPE-Roll
```

It works for each of a release's files, `SHA256SUMS.txt` included. `gh` works out the file's
SHA-256, asks GitHub for this repository's attestations of it and checks their signatures.
*Verification succeeded!*, with the repository and the workflow that built the file listed under
it, means your file is byte for byte one that MPE Roll's Release workflow built on GitHub's
runners. *No attestations found* means it isn't one of those files (or it's from 0.1.0 or 0.2.0).
To have `gh` also insist on the workflow, add
`--signer-workflow freener0x/MPE-Roll/.github/workflows/release.yml`.

An attestation says where a file was built, not that the code in it is safe, and it isn't code
signing: Windows and macOS don't look at it, and warn about the downloads as before.

## Signing later: the options

| Option | Cost | Covers | Notes |
| --- | --- | --- | --- |
| [Apple Developer Program](https://developer.apple.com/programs/) | about US$99 a year | macOS: app, VST3, AU | No fee waiver for individuals or open-source projects. The developer's legal name becomes the signing identity. Notarization is included. |
| [Azure Artifact Signing](https://azure.microsoft.com/en-us/products/artifact-signing) (was Trusted Signing) | about US$9.99 a month | Windows | Individuals only in the USA and Canada; organisations in the US, Canada, the EU and the UK (three years' history reported). Signs from GitHub Actions. No instant SmartScreen reputation. |
| OV or IV code-signing certificate (Sectigo, SSL.com, DigiCert and their resellers) | about US$129 to 540 a year, plus a hardware key | Windows | The private key must live on hardware (since June 2023), and certificates are valid at most 460 days (since March 2026), so they're reissued yearly. The hardware key can't be used on GitHub's runners: signing happens on the developer's PC or through a paid cloud signing service. |
| [SignPath Foundation](https://signpath.org/) | free | Windows | For open-source projects built on GitHub-hosted Actions, which MPE Roll is. Windows then shows "SignPath Foundation" as the publisher, the website must carry a "Code signing policy" line, every release is approved by hand, and acceptance is at the Foundation's discretion. |
| [Certum Open Source Code Signing](https://shop.certum.eu/open-source-code-signing.html) | about EUR 49 to 69 a year | Windows | Individuals only. Windows shows "Open Source Developer, \<name\>" as the publisher. |

Prices are approximate, were read in November 2026 and change; the vendors' pages have the current
ones. A signature doesn't end the *Windows protected your PC* screen by itself: SmartScreen's
reputation still builds with clean downloads over time, signed or not. MPE Roll is published by one
person in the USA today, so the routes open to an individual there (Apple's program, Azure Artifact
Signing, an IV certificate, SignPath Foundation, Certum) are all open to it; none has been chosen
yet.

## Reported, not checked

Three things on this page come from other people's reports and from documentation, not from a check
of this build: the exact dialog macOS shows for these bundles (they have no Developer ID signature,
so macOS could also say the file "is damaged"; the `xattr` command clears either), that *Open
Anyway* doesn't work for plug-in bundles, and that File Explorer passes the download mark on to the
files it extracts. Two more are for GitHub's macOS runner to check whenever the code changes, and
hadn't been checked there or on any other Mac when this was written: that a file fetched with
`curl` carries no quarantine mark, and that the install script leaves the three bundles in place
without it. If you can confirm or correct any of them, please
[open an issue](https://github.com/freener0x/MPE-Roll/issues/new/choose).
