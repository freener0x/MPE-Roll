#!/bin/bash
#
# install-macos.sh: installs MPE Roll's macOS build for the person who runs it.
#
#   curl -fsSL https://freener0x.github.io/MPE-Roll/install-macos.sh | bash
#
# It downloads a release's macOS zip and its SHA256SUMS.txt from MPE Roll's releases on GitHub,
# checks the zip against the checksum, unpacks it and copies the three bundles into your own
# Library folders:
#
#   ~/Library/Audio/Plug-Ins/VST3/MPE Roll.vst3              the VST3 plug-in
#   ~/Library/Audio/Plug-Ins/Components/MPE Roll.component   the Audio Unit
#   ~/Applications/MPE Roll.app                              the standalone app
#
# It asks for no password and uses no sudo. It changes nothing but those three bundles (a copy
# already there is replaced) and the folders they go in, which it makes if they're missing; what it
# downloads and unpacks goes into a temporary folder that it removes again. It takes the download
# (quarantine) mark off the three copies it installed, if they carry one: MPE Roll isn't notarized,
# and a DAW won't load a plug-in that has the mark. At the end it prints what it did and the
# command that undoes it.
#
# Which release, and other choices (an argument, or the environment variable in brackets):
#
#   <version>      install that release, 0.2.0 say, instead of the latest  [MPEROLL_VERSION]
#   --zip <file>   install from a macOS zip you already have, downloading nothing: a release's
#                  MPE-Roll-<version>-macOS.zip, or a build between releases (MPE-Roll-bundles.zip,
#                  or the zip from the Actions tab that holds it)  [MPEROLL_ZIP]
#   --sums <file>  with --zip: a SHA256SUMS.txt to check that zip against  [MPEROLL_SUMS]
#   --help         print how to use it, and do nothing
#
# With the one-line form, arguments go after "bash -s --":
#
#   curl -fsSL https://freener0x.github.io/MPE-Roll/install-macos.sh | bash -s -- 0.2.0
#
# Releases 0.2.0 and later can be installed this way (0.1.0 has no SHA256SUMS.txt). MPE Roll needs
# macOS 11 or newer.
#
# Checked on GitHub's macOS runner only, where every build of MPE Roll runs it
# (.github/workflows/build.yml). Nobody has run MPE Roll's macOS build on a Mac of their own yet:
# if you do, please say how it went at https://github.com/freener0x/MPE-Roll/issues/new/choose
#
# It runs in the bash that macOS ships (3.2). MPEROLL_INSTALL_TEST=1 lets it run on a system that
# isn't macOS, for testing the script itself with stand-ins for ditto and xattr.
#
# Part of MPE Roll, https://github.com/freener0x/MPE-Roll, free software under the GNU Affero
# General Public License, version 3 (the LICENSE file there).

set -euo pipefail

REPO_URL=https://github.com/freener0x/MPE-Roll
SITE_URL=https://freener0x.github.io/MPE-Roll

VST3_NAME="MPE Roll.vst3"
AU_NAME="MPE Roll.component"
APP_NAME="MPE Roll.app"

tmp=
vst3_dir=
au_dir=
app_dir=

say() {
  printf '%s\n' "$*"
}

fail() {
  printf '\ninstall-macos.sh: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<EOF
install-macos.sh installs MPE Roll's macOS build (VST3, Audio Unit and standalone app) into your
own Library folders, without sudo.

  curl -fsSL $SITE_URL/install-macos.sh | bash
  curl -fsSL $SITE_URL/install-macos.sh | bash -s -- 0.2.0
  bash install-macos.sh [<version>] [--zip <file> [--sums <file>]]

  <version>      the release to install (0.2.0 or later); the latest if left out
  --zip <file>   install from a macOS zip you already have, downloading nothing
  --sums <file>  with --zip: a SHA256SUMS.txt to check that zip against
  --help         this text

MPEROLL_VERSION, MPEROLL_ZIP and MPEROLL_SUMS in the environment do the same.
What it does, step by step: $SITE_URL/UNSIGNED_BUILDS.html
EOF
}

# Whether the text matches the extended regular expression.
matches() {
  printf '%s\n' "$1" | grep -Eq -- "$2"
}

have() {
  command -v "$1" > /dev/null 2>&1
}

# Removes the temporary folder, and any copy still waiting to be moved into place.
cleanup() {
  if [ -n "$tmp" ]; then
    rm -rf "$tmp"
  fi
  if [ -n "$vst3_dir" ]; then
    rm -rf "$vst3_dir/.$VST3_NAME.installing"
  fi
  if [ -n "$au_dir" ]; then
    rm -rf "$au_dir/.$AU_NAME.installing"
  fi
  if [ -n "$app_dir" ]; then
    rm -rf "$app_dir/.$APP_NAME.installing"
  fi
}

# fetch <url> <file> [bar]: downloads over HTTPS only; with "bar", shows progress on a terminal.
fetch() {
  if [ "${3:-}" = bar ] && [ -t 2 ]; then
    curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 --progress-bar \
      --output "$2" "$1"
  else
    curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 --silent --show-error \
      --output "$2" "$1"
  fi
}

# The file's SHA-256 checksum, in lower-case hexadecimal.
sha256_of() {
  if have shasum; then
    shasum -a 256 "$1" | awk '{ print tolower($1) }'
  else
    sha256sum "$1" | awk '{ print tolower($1) }'
  fi
}

# sum_for <SHA256SUMS file> <file name>: the checksum the list gives for that name, or nothing.
sum_for() {
  awk -v name="$2" '{ f = $2; sub(/^\*/, "", f); if (f == name) { print tolower($1); exit } }' "$1"
}

# How many of the files in a bundle carry the download (quarantine) mark.
quarantined() {
  local count
  count=$(xattr -r "$1" 2> /dev/null | grep -c 'com\.apple\.quarantine' || true)
  printf '%s\n' "${count:-0}"
}

# stage <bundle> <folder>: copies the bundle from the unpacked zip into the folder under a
# temporary name, making the folder if it's missing, and takes the download mark off the copy.
stage() {
  local name=$1 folder=$2 staging marked
  staging="$folder/.$name.installing"
  if [ ! -d "$folder" ]; then
    mkdir -p "$folder" || fail "Couldn't make the folder $folder."
    made="$made
  $folder"
  fi
  rm -rf "$staging"
  ditto "$src/$name" "$staging" || fail "Couldn't copy $name into $folder. Nothing was installed."
  if have xattr; then
    marked=$(quarantined "$staging")
    xattr -dr com.apple.quarantine "$staging" 2> /dev/null || true
    if [ "$(quarantined "$staging")" != 0 ]; then
      still_marked="$still_marked \"$folder/$name\""
    elif [ "$marked" != 0 ]; then
      unmarked="$unmarked
  $folder/$name"
    fi
  fi
}

# place <bundle> <folder>: puts the staged copy where it belongs, in place of a copy already there.
place() {
  local name=$1 folder=$2
  if [ -e "$folder/$name" ] || [ -L "$folder/$name" ]; then
    rm -rf "${folder:?}/${name:?}" \
      || fail "Couldn't remove the old $folder/$name. Is it in use? Close your DAW and MPE Roll, then run this again."
    replaced=yes
  fi
  mv "$folder/.$name.installing" "$folder/$name" || fail "Couldn't put $name into $folder."
}

main() {
  local version zip sums os macos major what sums_url name expected actual tag installing
  local semver zip_name hex other others

  version=${MPEROLL_VERSION:-}
  zip=${MPEROLL_ZIP:-}
  sums=${MPEROLL_SUMS:-}
  while [ $# -gt 0 ]; do
    case "$1" in
      -h|--help)
        usage
        exit 0
        ;;
      --zip|--sums|--version)
        if [ $# -lt 2 ]; then
          fail "$1 needs a value after it (see --help)."
        fi
        case "$1" in
          --zip) zip=$2 ;;
          --sums) sums=$2 ;;
          --version) version=$2 ;;
        esac
        shift 2
        ;;
      --zip=*) zip=${1#--zip=}; shift ;;
      --sums=*) sums=${1#--sums=}; shift ;;
      --version=*) version=${1#--version=}; shift ;;
      -*)
        fail "Unknown option $1 (see --help)."
        ;;
      *)
        version=$1
        shift
        ;;
    esac
  done

  version=${version#v}
  semver='^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?$'
  zip_name='^MPE-Roll-[0-9]+\.[0-9]+\.[0-9]+-macOS\.zip$'
  hex='^[0-9a-f]{64}$'
  if [ -n "$version" ] && ! matches "$version" "$semver"; then
    fail "'$version' isn't a version of MPE Roll: give three numbers such as 0.2.0 (a pre-release: 0.3.0-beta1)."
  fi
  if [ -n "$zip" ] && [ -n "$version" ]; then
    fail "Give a version to download or a zip to install from (--zip), not both."
  fi
  if [ -n "$sums" ] && [ -z "$zip" ]; then
    fail "--sums goes with --zip: a downloaded release is always checked against its own SHA256SUMS.txt."
  fi

  os=$(uname -s)
  if [ "$os" != Darwin ]; then
    if [ "${MPEROLL_INSTALL_TEST:-}" != 1 ]; then
      fail "This installs MPE Roll's macOS build, and this system is $os. The Windows installer and the Linux build are at $REPO_URL/releases/latest"
    fi
    say "This system is $os, not macOS: going on because MPEROLL_INSTALL_TEST=1 is set."
  else
    macos=$(sw_vers -productVersion 2> /dev/null || true)
    major=${macos%%.*}
    if matches "$major" '^[0-9]+$' && [ "$major" -lt 11 ]; then
      fail "MPE Roll needs macOS 11 or newer, and this Mac runs macOS $macos."
    fi
  fi
  if [ "$(id -u)" = 0 ]; then
    fail "Run this as yourself, without sudo: it installs into your own Library folder and needs no password."
  fi
  if [ -z "${HOME:-}" ] || [ ! -d "$HOME" ]; then
    fail "Your home folder (\$HOME) isn't set or isn't there, so there is nowhere to install to."
  fi
  for tool in ditto awk grep mktemp; do
    if ! have "$tool"; then
      fail "The command '$tool' is missing, which every Mac should have."
    fi
  done
  if ! have shasum && ! have sha256sum; then
    fail "The command 'shasum' is missing, which every Mac should have."
  fi

  vst3_dir="$HOME/Library/Audio/Plug-Ins/VST3"
  au_dir="$HOME/Library/Audio/Plug-Ins/Components"
  app_dir="$HOME/Applications"
  trap cleanup EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/mperoll-install.XXXXXX") || fail "Couldn't make a temporary folder."

  if [ -n "$zip" ]; then
    if [ ! -f "$zip" ]; then
      fail "There is no file $zip."
    fi
    installing="MPE Roll from $zip"
    say "Installing from $zip (nothing is downloaded)."
    if [ -n "$sums" ]; then
      if [ ! -f "$sums" ]; then
        fail "There is no file $sums."
      fi
      name=$(basename "$zip")
      expected=$(sum_for "$sums" "$name")
      if ! matches "$expected" "$hex"; then
        fail "$sums has no SHA-256 checksum for $name."
      fi
    else
      expected=
      say "No checksum to compare it with (give one with --sums)."
    fi
  else
    if ! have curl; then
      fail "The command 'curl' is missing, which every Mac should have."
    fi
    if [ -n "$version" ]; then
      what="release $version"
      tag="v$version"
      sums_url="$REPO_URL/releases/download/$tag/SHA256SUMS.txt"
    else
      what="the latest release"
      tag=
      sums_url="$REPO_URL/releases/latest/download/SHA256SUMS.txt"
    fi
    say "Getting the checksums of $what from $sums_url"
    sums="$tmp/SHA256SUMS.txt"
    if ! fetch "$sums_url" "$sums"; then
      fail "Couldn't get $sums_url. Are you online, and is there such a release? Releases 0.2.0 and later have a SHA256SUMS.txt; they are listed at $REPO_URL/releases"
    fi
    name=$(awk '{ f = $2; sub(/^\*/, "", f); if (f ~ /-macOS\.zip$/) { print f; exit } }' "$sums")
    if ! matches "$name" "$zip_name"; then
      fail "The SHA256SUMS.txt of $what names no MPE-Roll-<version>-macOS.zip, so there is nothing to check a download against. Its files are at $REPO_URL/releases"
    fi
    expected=$(sum_for "$sums" "$name")
    if ! matches "$expected" "$hex"; then
      fail "The SHA256SUMS.txt of $what has no SHA-256 checksum for $name."
    fi
    if [ -z "$tag" ]; then
      tag=${name#MPE-Roll-}
      tag="v${tag%-macOS.zip}"
    fi
    installing="MPE Roll ${tag#v}"
    zip="$tmp/$name"
    say "Downloading $REPO_URL/releases/download/$tag/$name"
    if ! fetch "$REPO_URL/releases/download/$tag/$name" "$zip" bar; then
      fail "Couldn't download $name. Nothing was installed."
    fi
  fi

  if [ -n "$expected" ]; then
    actual=$(sha256_of "$zip")
    if [ "$actual" != "$expected" ]; then
      fail "The checksum of $(basename "$zip") is wrong, so it was changed or damaged on the way. Nothing was installed.
  $(basename "$sums") says: $expected
  the file has: $actual"
    fi
    say "Its SHA-256 checksum matches the one in $(basename "$sums"): $actual"
  fi

  ditto -x -k "$zip" "$tmp/unpacked" || fail "Couldn't unpack $(basename "$zip"). Nothing was installed."
  # A build between releases comes from the Actions tab as a zip that holds MPE-Roll-bundles.zip.
  if [ ! -d "$tmp/unpacked/MPE Roll" ] && [ -f "$tmp/unpacked/MPE-Roll-bundles.zip" ]; then
    ditto -x -k "$tmp/unpacked/MPE-Roll-bundles.zip" "$tmp/unpacked-bundles" \
      || fail "Couldn't unpack the MPE-Roll-bundles.zip inside $(basename "$zip"). Nothing was installed."
    src="$tmp/unpacked-bundles/MPE Roll"
  else
    src="$tmp/unpacked/MPE Roll"
  fi
  for bundle in "$VST3_NAME" "$AU_NAME" "$APP_NAME"; do
    if [ ! -d "$src/$bundle" ]; then
      fail "$(basename "$zip") doesn't hold a folder 'MPE Roll' with $bundle in it, so it isn't MPE Roll's macOS zip. Nothing was installed."
    fi
  done
  if [ ! -x "$src/$APP_NAME/Contents/MacOS/MPE Roll" ]; then
    fail "The app in $(basename "$zip") has lost its permission to run, which happens when a zip is repacked by another tool. Nothing was installed."
  fi

  made=
  replaced=
  unmarked=
  still_marked=
  stage "$VST3_NAME" "$vst3_dir"
  stage "$AU_NAME" "$au_dir"
  stage "$APP_NAME" "$app_dir"
  place "$VST3_NAME" "$vst3_dir"
  place "$AU_NAME" "$au_dir"
  place "$APP_NAME" "$app_dir"

  say ""
  say "Installed $installing for $(id -un), without sudo:"
  say "  $vst3_dir/$VST3_NAME  (VST3)"
  say "  $au_dir/$AU_NAME  (Audio Unit)"
  say "  $app_dir/$APP_NAME  (standalone app)"
  if [ -n "$replaced" ]; then
    say "A copy that was already there was replaced."
  fi
  if [ -n "$made" ]; then
    say "These folders weren't there and were made:$made"
  fi
  if [ -n "$unmarked" ]; then
    say "macOS's download (quarantine) mark was taken off these copies, which carried it:$unmarked"
  fi
  if [ -n "$still_marked" ]; then
    say "The download (quarantine) mark couldn't be taken off every copy. Until it is, macOS won't"
    say "let a DAW load a plug-in that has it. Try:  xattr -dr com.apple.quarantine$still_marked"
  fi
  if ! have xattr; then
    say "(No xattr command here, so the download mark wasn't looked for.)"
  fi
  others=
  for other in "/Library/Audio/Plug-Ins/VST3/$VST3_NAME" "/Library/Audio/Plug-Ins/Components/$AU_NAME" \
               "/Applications/$APP_NAME"; do
    if [ -e "$other" ]; then
      others="$others
  $other"
    fi
  done
  if [ -n "$others" ]; then
    say ""
    say "There is another copy of MPE Roll in the folders for all users, which this script leaves alone."
    say "A DAW may load that one instead; remove it (it asks for your password) if it's older:$others"
  fi
  say ""
  say "Nothing else was changed. Now rescan plug-ins in your DAW (Logic Pro: Settings, Plug-in"
  say "Manager, select MPE Roll, Reset & Rescan Selection), or open MPE Roll from $app_dir."
  say "The bottom of MPE Roll's File menu names the build that's running."
  say ""
  say "To undo this, remove the three bundles:"
  say "  rm -rf \"$vst3_dir/$VST3_NAME\" \"$au_dir/$AU_NAME\" \"$app_dir/$APP_NAME\""
  say "Your settings (~/Library/Application Support/MPE Roll) are left alone either way."
  say ""
  say "Nobody has run MPE Roll's macOS build on a Mac of their own yet. Please say how it went,"
  say "working or not: $REPO_URL/issues/new/choose"
}

main "$@"
