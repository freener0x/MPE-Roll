#!/usr/bin/env bash
# Makes the website's demo film from frames the developer harness draws: three notes drawn, a glide
# dragged up from one, pressure painted under them in the lane, the skins gone through, and the clip
# cleared again so it loops (MpeRollHarness demo, tools/Harness.cpp).
#
#   tools/make_demo.sh <path to MpeRollHarness> [scale]
#
# Writes into docs/images:
#   demo.webm         VP9, for <video autoplay muted loop playsinline>; must stay under 3 MB
#   demo.gif          the same, smaller, for where WebM video doesn't play; must stay under 4 MB
#   demo-poster.png   the editor with everything drawn, shown before the film plays
#
# Needs ffmpeg (with libvpx-vp9); on Linux without a display, xvfb-run. Python 3 with Pillow, if
# there, recompresses the poster. Build the harness with -DMPEROLL_BUILD_DEV_TOOLS=ON:
#   cmake --build build --target MpeRollHarness
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <path to MpeRollHarness> [scale]" >&2
    exit 2
fi

harness=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
scale=${2:-2}
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/docs/images
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

command -v ffmpeg >/dev/null || { echo "ffmpeg is needed" >&2; exit 1; }

# The harness opens windows: on Linux without a display, on a virtual one.
if [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}" ]; then
    xvfb-run -a -s "-screen 0 1920x1200x24" "$harness" demo "$work" "$scale"
else
    "$harness" demo "$work" "$scale"
fi

frames=("$work"/frame-*.png)
[ -e "${frames[0]}" ] && [ -f "$work/poster.png" ] || { echo "The harness wrote no frames" >&2; exit 1; }

# 15 frames a second, as the harness draws them. yuv420p plays everywhere VP9 does; -crf with -b:v 0
# is VP9's constant quality. bitexact leaves out the random IDs and version strings WebM files carry,
# so the same frames give the same file.
ffmpeg -loglevel error -y -framerate 15 -i "$work/frame-%04d.png" -an \
    -c:v libvpx-vp9 -b:v 0 -crf 32 -pix_fmt yuv420p -row-mt 1 -deadline good -cpu-used 2 \
    -fflags +bitexact -flags:v +bitexact "$out/demo.webm"

# Half the size, in one palette made for the whole film; no dithering, so the window's flat colours
# stay flat, and only what changes from frame to frame is stored.
ffmpeg -loglevel error -y -framerate 15 -i "$work/frame-%04d.png" \
    -vf "scale=iw/2:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    -loop 0 "$out/demo.gif"

cp "$work/poster.png" "$out/demo-poster.png"

if python3 -c "import PIL" 2>/dev/null; then
    python3 - "$out/demo-poster.png" <<'EOF'
import sys
from PIL import Image

path = sys.argv[1]
image = Image.open(path)
if image.mode == "RGBA" and image.getextrema()[3][0] == 255:
    image = image.convert("RGB")
image.save(path, optimize=True)
EOF
fi

status=0

for file in demo.webm:3000000 demo.gif:4000000 demo-poster.png:0; do
    name=${file%%:*}
    limit=${file##*:}
    size=$(wc -c < "$out/$name")
    echo "$name: $size bytes"

    if [ "$limit" -gt 0 ] && [ "$size" -ge "$limit" ]; then
        echo "  over $limit bytes: shorten the film (tools/Harness.cpp, demoFrames) or lower the scale" >&2
        status=1
    fi
done

exit $status
