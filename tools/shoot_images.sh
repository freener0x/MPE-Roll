#!/usr/bin/env bash
# Draws the website's pictures of the editor with the developer harness (MpeRollHarness shoot,
# tools/Harness.cpp), in the FL Studio skin at twice the size (for screens with twice the pixels),
# into docs/images. docs/_data/screenshots.yml says which page picture is which file, and its size.
#
#   tools/shoot_images.sh <path to MpeRollHarness> [synth for the settings pictures]
#
# The settings pictures show how a loaded synth is played. Given no synth, the CLAP test synth built
# next to the harness is used (cmake --build build --target MpeRollClapTestSynth); its name shows in
# neither picture. Without one, they say no synth is loaded.
#
# The demo film is tools/make_demo.sh. The two pictures from FL Studio (fl-studio-*.png) are the
# maintainer's own screenshots, cropped: they can't be drawn here.
#
# Needs, on Linux without a display, xvfb-run; Python 3 with Pillow, if there, recompresses the files.
# Build the harness with -DMPEROLL_BUILD_DEV_TOOLS=ON: cmake --build build --target MpeRollHarness
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <path to MpeRollHarness> [synth for the settings pictures]" >&2
    exit 2
fi

harness=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/docs/images

if [ $# -ge 2 ]; then
    synth=$2
else
    # build/MpeRollHarness_artefacts/<config>/MpeRollHarness -> build/MpeRollClapTestSynth_artefacts/<config>/
    config_dir=$(dirname "$harness")
    synth="$(dirname "$(dirname "$config_dir")")/MpeRollClapTestSynth_artefacts/$(basename "$config_dir")/MPE Roll Test Synth.clap"
fi

if [ ! -e "$synth" ]; then
    echo "No synth at $synth: the settings pictures will say no synth is loaded" >&2
    synth=
fi

run() {
    if [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}" ]; then
        xvfb-run -a -s "-screen 0 1920x1200x24" "$harness" "$@"
    else
        "$harness" "$@"
    fi
}

# state in the harness : file in docs/images. The -narrow ones are for a phone's width: the page
# shows them instead of the wide ones there (screenshots.yml, narrow).
pictures="hero:editor-fl-studio-skin.png lanes:lane-menu.png vibrato:vibrato-lane.png
          glide:glide-drawing.png tools:tool-arpeggiate.png settings:settings-output.png
          copies:settings-copies.png skins:skins.png vibrato-narrow:vibrato-lane-narrow.png
          glide-narrow:glide-drawing-narrow.png skins-narrow:skins-narrow.png"

for picture in $pictures; do
    state=${picture%%:*}
    file=$out/${picture##*:}

    case $state in
        settings|copies) run shoot "$state" "$file" 2 ${synth:+"$synth"} ;;
        *)               run shoot "$state" "$file" 2 ;;
    esac
done

# Smaller files, the same pixels: no alpha channel where nothing is see-through, and zlib's best.
if python3 -c "import PIL" 2>/dev/null; then
    for picture in $pictures; do
        python3 - "$out/${picture##*:}" <<'EOF'
import sys
from PIL import Image

path = sys.argv[1]
image = Image.open(path)
if image.mode == "RGBA" and image.getextrema()[3][0] == 255:
    image = image.convert("RGB")
image.save(path, optimize=True)
EOF
    done
fi

for picture in $pictures; do
    file=$out/${picture##*:}
    echo "${picture##*:}: $(wc -c < "$file") bytes"
done
