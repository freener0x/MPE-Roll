#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The MPE Roll contributors
# SPDX-License-Identifier: AGPL-3.0-only

"""Makes MPE Roll's icon files from the hand-written SVGs in assets/icon.

The artwork:

  assets/icon/mpe-roll.svg          the icon, "a note that bends": the toolbar's glide glyph in the plugin's own
                                    skin, in a 64-unit box
  assets/icon/mpe-roll-small.svg    the same mark for 16 to 32 px: no roll lines, thicker strokes
  assets/icon/social.svg            1280x640 link card: the icon, the wordmark and the tagline on ink
  assets/icon/banner.svg            1280x320 README banner for dark mode, on a transparent background so that it
                                    sits on any of GitHub's dark themes; banner-light.svg is its light variant
  assets/icon/wizard-large.svg      the installer's side image (Inno Setup, a 164:314 area)
  assets/icon/wizard-small.svg      the installer's header image (square)
  assets/icon/apple-touch-icon.svg  the website's touch icon: the icon as a full-bleed square, ink to the edges with
                                    no rim and no transparency, since iOS rounds its corners itself

The templates (social, banner, banner-light, wizard-large, wizard-small, apple-touch-icon) carry a copy of the
icon's shapes in a group, since a browser opened on a file won't reliably load an SVG that another file refers
to. The script checks that every copy matches mpe-roll.svg (the touch icon's copy leaves out the tile, and its
roll lines cross its whole width): after changing the icon, copy its shapes into them.

It writes, all committed so nobody needs this script or its tools to build the plugin, the installer or
the website:

  assets/icon/png/icon-<size>.png            16 20 24 32 from the small variant; 40 48 64 128 256 512 1024 from the icon
  assets/icon/icon-big-1024.png              JUCE's ICON_BIG and ICON_SMALL on Windows (CMakeLists.txt): JUCE turns them
  assets/icon/icon-small-64.png              into the .ico inside the binaries and the Explorer icon of MPE Roll.vst3
                                             (Plugin.ico and desktop.ini), drawing its sizes up to 48 px from the small one
  assets/icon/icon-big-mac-1024.png          the same pair for macOS, where JUCE makes the bundles' .icns of them: the
  assets/icon/icon-small-mac-64.png          tile 824 px wide in the middle of a transparent 1024-px square (52 of 64),
                                             the margin of macOS's icon grid, so the app is the size of the others in the
                                             Dock
  assets/icon/mpe-roll.ico                   16 24 32 48 64 as BMP, 256 as PNG: the installer's SetupIconFile and the
                                             scanner's icon (assets/icon/MpeRollScanner.rc)
  assets/icon/wizard-large-<w>x<h>.png       the installer's side image at Inno Setup's seven DPI sizes
  assets/icon/wizard-small-<size>.png        its header image at seven sizes
  docs/favicon.svg, docs/favicon.ico         the website's icons (favicon.svg a copy of the small variant,
                                             favicon.ico 16 32 48)
  docs/apple-touch-icon.png                  180 px, opaque (apple-touch-icon.svg)
  docs/images/logo.svg                       the website's header logo (a copy of the icon)
  docs/images/icon-192.png, icon-512.png     jekyll-seo-tag's logo, and a plain PNG of the icon
  docs/images/social.png                     1280x640 for link cards (og:image), and the repository's social preview
  docs/images/banner-dark.png                1280x320, README's banner in dark mode (banner.svg), transparent around
                                             the icon and the words
  docs/images/banner-light.png               the same in light mode (banner-light.svg)
  assets/icon/stamp.json                     the SHA-256 of every input (the SVGs and this script), the renderer, and
                                             every output's path, pixel size and SHA-256

Rendering needs one of these, looked for in this order: rsvg-convert, Inkscape, a Chromium-family browser
(Chromium, Chrome, Edge), Node with Playwright. --renderer picks one by kind (rsvg-convert, inkscape,
chromium, playwright) or by the name or path of its program. Making the files also needs Pillow
(python3 -m pip install pillow). Renderers differ in anti-aliasing, and machines in fonts (the wordmark
asks for Inter first), so the bytes differ from one machine to another; the stamp says which renderer
made them.

Run it from anywhere. --check needs nothing but Python: it recomputes the hashes of the SVGs and of this
script, checks the templates' copies of the icon, reads every output's header (PNG IHDR, ICO directory)
and hash, and fails if anything is missing, differs or has the wrong size, so CI runs it without a
renderer or Pillow:

    python3 tools/make_icons.py [--renderer KIND|PROGRAM] [--check]
"""

from __future__ import annotations

import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = Path(__file__).resolve()
ICON_DIR = ROOT / "assets" / "icon"
DOCS = ROOT / "docs"
STAMP = ICON_DIR / "stamp.json"

ICON = ICON_DIR / "mpe-roll.svg"
SMALL = ICON_DIR / "mpe-roll-small.svg"
SOCIAL = ICON_DIR / "social.svg"
BANNER = ICON_DIR / "banner.svg"
BANNER_LIGHT = ICON_DIR / "banner-light.svg"
WIZARD_LARGE = ICON_DIR / "wizard-large.svg"
WIZARD_SMALL = ICON_DIR / "wizard-small.svg"
TOUCH = ICON_DIR / "apple-touch-icon.svg"
TEMPLATES = [SOCIAL, BANNER, BANNER_LIGHT, WIZARD_LARGE, WIZARD_SMALL, TOUCH]
INPUTS = [ICON, SMALL, *TEMPLATES, SCRIPT]

# What each SVG's render must look like, which finished_png() checks: the tile alone on a transparent
# background; the tile and the words on a transparent background; any other SVG is opaque throughout.
TILES = {ICON, SMALL, WIZARD_SMALL}
BANNERS = {BANNER, BANNER_LIGHT}
BANNER_BOX = (1280, 320)               # the banners' viewBox
BANNER_TILE = [(344, 160), (344, 270)]  # in it, the middle of the icon's tile and of the tile's bottom edge

SMALL_UP_TO = 32                   # the small variant serves the sizes up to this
ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024]
ICO_SIZES = [16, 24, 32, 48, 64, 256]
FAVICON_SIZES = [16, 32, 48]
# macOS's icon grid (since Big Sur): the tile is 824 px of a 1024-px icon, in its middle; 52 of 64 for the
# small image, the whole-pixel size nearest that share.
MAC_TILE_BIG = 824
MAC_TILE_SMALL = 52
# Inno Setup 6.6's wizard image areas, 100 % to 250 %: 164:314 for the side image, square for the header.
WIZARD_LARGE_SIZES = [(202, 386), (269, 515), (336, 643), (403, 772), (430, 824), (498, 953), (534, 1022)]
WIZARD_SMALL_SIZES = [58, 77, 97, 116, 124, 143, 159]
SOCIAL_LIMIT = 1_000_000           # GitHub's social preview must be under 1 MB
RENDER_TIMEOUT = 300               # seconds for one run of a renderer

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def fail(message: str) -> None:
    sys.exit(f"make_icons.py: {message}")


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def variant(size: int) -> Path:
    return SMALL if size <= SMALL_UP_TO else ICON


# ---------------------------------------------------------------- what is made

@dataclass(frozen=True)
class Png:
    path: Path
    svg: Path
    width: int
    height: int
    limit: int = 0          # the file must stay under this many bytes, if set
    tile: int = 0           # if set, the SVG is drawn this many pixels square in the middle of a transparent image

    def job(self) -> Job:
        """What the renderer draws for this file."""
        return Job(self.svg, self.tile or self.width, self.tile or self.height)


@dataclass(frozen=True)
class Ico:
    path: Path
    sizes: tuple[int, ...]  # each from variant(size); below 256 as BMP, 256 as PNG

    def images(self) -> list[str]:
        return [f"{size}x{size} {'png' if size >= 256 else 'bmp'}" for size in self.sizes]


@dataclass(frozen=True)
class Copy:
    path: Path
    svg: Path


def outputs() -> list[Png | Ico | Copy]:
    out: list[Png | Ico | Copy] = []
    for size in ICON_SIZES:
        out.append(Png(ICON_DIR / "png" / f"icon-{size}.png", variant(size), size, size))
    out.append(Png(ICON_DIR / "icon-big-1024.png", ICON, 1024, 1024))
    out.append(Png(ICON_DIR / "icon-small-64.png", SMALL, 64, 64))
    out.append(Png(ICON_DIR / "icon-big-mac-1024.png", ICON, 1024, 1024, tile=MAC_TILE_BIG))
    out.append(Png(ICON_DIR / "icon-small-mac-64.png", SMALL, 64, 64, tile=MAC_TILE_SMALL))
    out.append(Ico(ICON_DIR / "mpe-roll.ico", tuple(ICO_SIZES)))
    for w, h in WIZARD_LARGE_SIZES:
        out.append(Png(ICON_DIR / f"wizard-large-{w}x{h}.png", WIZARD_LARGE, w, h))
    for size in WIZARD_SMALL_SIZES:
        out.append(Png(ICON_DIR / f"wizard-small-{size}.png", WIZARD_SMALL, size, size))
    out.append(Copy(DOCS / "favicon.svg", SMALL))
    out.append(Ico(DOCS / "favicon.ico", tuple(FAVICON_SIZES)))
    out.append(Png(DOCS / "apple-touch-icon.png", TOUCH, 180, 180))
    out.append(Copy(DOCS / "images" / "logo.svg", ICON))
    out.append(Png(DOCS / "images" / "icon-192.png", ICON, 192, 192))
    out.append(Png(DOCS / "images" / "icon-512.png", ICON, 512, 512))
    out.append(Png(DOCS / "images" / "social.png", SOCIAL, 1280, 640, SOCIAL_LIMIT))
    out.append(Png(DOCS / "images" / "banner-dark.png", BANNER, 1280, 320))
    out.append(Png(DOCS / "images" / "banner-light.png", BANNER_LIGHT, 1280, 320))
    return out


# ---------------------------------------------------------------- hashes, headers and the SVGs (standard library only)

def text_bytes(path: Path) -> bytes:
    """An SVG or this script, with Windows line endings made Unix ones, so a checkout with CRLF hashes the same."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def png_size(data: bytes) -> tuple[int, int]:
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        raise ValueError("not a PNG")
    return struct.unpack(">II", data[16:24])


def ico_images(data: bytes) -> list[str]:
    """Each image in an .ico as "<width>x<height> bmp|png", checked against the directory's own record of it."""
    reserved, kind, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or kind != 1 or count == 0:
        raise ValueError("not an ICO")
    images = []
    for i in range(count):
        width, height, _, _, _, bits, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        image = data[offset:offset + length]
        if len(image) != length:
            raise ValueError("an image runs past the end of the file")
        if image[:8] == PNG_SIGNATURE:
            w, h = png_size(image)
            stored = "png"
        else:
            header_size, w, h2, _, bmp_bits = struct.unpack_from("<IiiHH", image, 0)
            if header_size != 40 or h2 != 2 * w or bmp_bits != 32 or bits != 32:
                raise ValueError("an image is a BMP of a kind this script doesn't write")
            h = h2 // 2            # the colours and the AND mask, one above the other
            mask_row = ((w + 31) // 32) * 4
            if length != 40 + w * h * 4 + mask_row * h:
                raise ValueError(f"the {w}x{h} image's length doesn't match its size")
            stored = "bmp"
        if (width or 256) != w or (height or 256) != h:
            raise ValueError(f"the directory says {width or 256}x{height or 256} for a {w}x{h} image")
        images.append(f"{w}x{h} {stored}")
    return images


def svg_root(path: Path) -> ET.Element:
    return ET.fromstring(text_bytes(path))


def shapes(element: ET.Element) -> list[tuple[str, dict[str, str]]]:
    """The elements directly inside an SVG element, as tag and attributes; comments aren't kept."""
    return [(child.tag, dict(child.attrib)) for child in element]


def view_box(path: Path) -> list[int | float]:
    """An SVG's width and height in its own units, from its viewBox."""
    numbers = [float(n) for n in svg_root(path).get("viewBox", "").replace(",", " ").split()]
    return [int(n) if n.is_integer() else n for n in numbers[2:]]


def across(shape: tuple[str, dict[str, str]]) -> tuple[str, dict[str, str]]:
    """A shape with each horizontal line of its path ("M x y H x") reduced to its height: the touch icon's roll
    lines cross its whole width, where the icon's stop at its rim."""
    tag, attributes = shape
    if "d" in attributes:
        lines = re.sub(r"M\s*[-\d.]+[\s,]+([-\d.]+)\s*H\s*[-\d.]+", r"y=\1 ", attributes["d"])
        attributes = {**attributes, "d": lines}
    return tag, attributes


def template_problems() -> list[str]:
    """Each template must hold the icon's shapes as they are in mpe-roll.svg, alone in its root or in a group.
    The touch icon, a full-bleed square, holds them without the tile, its roll lines compared by their heights."""
    try:
        icon = shapes(svg_root(ICON))
    except FileNotFoundError:
        return []                  # reported as a missing input
    except ET.ParseError as error:
        return [f"{relative(ICON)} isn't well-formed SVG: {error}"]
    found = []
    for template in TEMPLATES:
        try:
            root = svg_root(template)
        except FileNotFoundError:
            continue
        except ET.ParseError as error:
            found.append(f"{relative(template)} isn't well-formed SVG: {error}")
            continue
        groups = [shapes(group) for group in [root, *root.iter("{http://www.w3.org/2000/svg}g")]]
        if template == TOUCH:
            mark = [across(shape) for shape in icon[1:]]
            if not any([across(shape) for shape in group] == mark for group in groups):
                found.append(f"{relative(template)} doesn't hold a copy of {relative(ICON)}'s shapes after the "
                             "tile, its roll lines at the same heights: copy them into its group")
        elif icon not in groups:
            found.append(f"{relative(template)} doesn't hold a copy of {relative(ICON)}'s shapes: copy them into "
                         "its group")
    return found


def describe_outputs(made: list[Png | Ico | Copy]) -> dict[str, dict]:
    """Every output's record for the stamp, read from the file on disk (its size from its header, its hash)."""
    records = {}
    for item in made:
        data = item.path.read_bytes()
        if isinstance(item, Png):
            record: dict = {"size": list(png_size(data)), "sha256": sha256(data)}
            if item.limit:
                record["bytes"] = len(data)
        elif isinstance(item, Ico):
            record = {"images": ico_images(data), "sha256": sha256(data)}
        else:
            record = {"copy_of": relative(item.svg), "viewBox": view_box(item.path),
                      "sha256": sha256(text_bytes(item.path))}
        records[relative(item.path)] = record
    return records


def problems(made: list[Png | Ico | Copy], stamp: dict) -> list[str]:
    """What's wrong with the files on disk against the stamp, and with the stamp against this script."""
    found = []
    for path in INPUTS:
        if not path.exists():
            found.append(f"{relative(path)} is missing")
        elif stamp.get("inputs", {}).get(relative(path)) != sha256(text_bytes(path)):
            found.append(f"{relative(path)} has changed since the icon files were made")
    found += template_problems()

    stamped = stamp.get("outputs", {})
    for item in made:
        name = relative(item.path)
        if not item.path.exists():
            found.append(f"{name} is missing")
            continue
        record = stamped.get(name)
        if record is None:
            found.append(f"{name} isn't in the stamp")
            continue
        data = item.path.read_bytes()
        try:
            if isinstance(item, Png):
                size = list(png_size(data))
                if size != [item.width, item.height]:
                    found.append(f"{name} is {size[0]}x{size[1]}, not {item.width}x{item.height}")
                if item.limit and len(data) >= item.limit:
                    found.append(f"{name} is {len(data)} bytes, not under {item.limit}")
                if record.get("sha256") != sha256(data) or record.get("size") != size:
                    found.append(f"{name} differs from the stamp")
            elif isinstance(item, Ico):
                images = ico_images(data)
                if images != item.images():
                    found.append(f"{name} holds {', '.join(images)}, not {', '.join(item.images())}")
                if record.get("sha256") != sha256(data) or record.get("images") != images:
                    found.append(f"{name} differs from the stamp")
            else:
                if item.svg.exists() and text_bytes(item.path) != text_bytes(item.svg):
                    found.append(f"{name} isn't a copy of {relative(item.svg)}")
                if record.get("sha256") != sha256(text_bytes(item.path)):
                    found.append(f"{name} differs from the stamp")
        except (ValueError, struct.error, ET.ParseError) as error:
            found.append(f"{name} can't be read: {error}")

    extra = set(stamped) - {relative(item.path) for item in made}
    if extra:
        found.append(f"the stamp lists files this script doesn't make: {', '.join(sorted(extra))}")
    return found


# ---------------------------------------------------------------- renderers

@dataclass(frozen=True)
class Job:
    svg: Path
    width: int
    height: int


class Renderer:
    """Turns an SVG into a PNG of an exact size with a transparent background."""

    kind = ""

    def __init__(self, program: str):
        self.program = program

    def version(self) -> str:
        """What the stamp records: the program's own name and version, if it says."""
        try:
            out = subprocess.run([self.program, "--version"], capture_output=True, text=True, timeout=60)
            first = (out.stdout or out.stderr).strip().splitlines()
            return first[0].strip() if out.returncode == 0 and first else self.kind
        except (OSError, subprocess.TimeoutExpired):
            return self.kind

    def render_all(self, jobs: list[Job]) -> dict[Job, bytes]:
        return {job: self.render(job) for job in jobs}

    def render(self, job: Job) -> bytes:
        raise NotImplementedError

    @staticmethod
    def run(command: list[str], what: str) -> None:
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=RENDER_TIMEOUT)
        except subprocess.TimeoutExpired:
            fail(f"{what} took more than {RENDER_TIMEOUT} s")
        if result.returncode != 0:
            fail(f"{what} failed ({result.returncode}):\n{result.stdout}{result.stderr}")


class RsvgConvert(Renderer):
    kind = "rsvg-convert"

    def render(self, job: Job) -> bytes:
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / "out.png"
            self.run([self.program, "-w", str(job.width), "-h", str(job.height), "-o", str(out), str(job.svg)],
                     f"rsvg-convert on {relative(job.svg)}")
            return out.read_bytes()


class Inkscape(Renderer):
    kind = "inkscape"

    def render(self, job: Job) -> bytes:
        if not hasattr(self, "old"):
            self.old = self.version().startswith("Inkscape 0.")     # 0.92 and older take other options
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / "out.png"
            if self.old:
                command = [self.program, "-z", "-e", str(out), "-w", str(job.width), "-h", str(job.height), str(job.svg)]
            else:
                command = [self.program, "--export-type=png", f"--export-filename={out}", "--export-background-opacity=0",
                           "-w", str(job.width), "-h", str(job.height), str(job.svg)]
            self.run(command, f"Inkscape on {relative(job.svg)}")
            return out.read_bytes()


def scratch_folder() -> tempfile.TemporaryDirectory:
    """A temporary folder for a browser's page and profile. On Windows a browser that has just quit can
    hold files in its profile for a moment longer, so what can't be removed yet is left to the system."""
    try:
        return tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
    except TypeError:              # Python before 3.10
        return tempfile.TemporaryDirectory()


def wrapper_html(job: Job) -> str:
    """A page that shows the SVG inline at exactly the wanted size, for the browsers. Inline, because a browser
    opened on a file won't reliably load an SVG that another file refers to."""
    text = job.svg.read_text(encoding="utf-8")
    svg = text[text.index("<svg"):]
    return ("<!doctype html><html><head><meta charset=\"utf-8\"><style>"
            "html,body{margin:0;padding:0;background:transparent;overflow:hidden}"
            f"svg{{display:block;width:{job.width}px;height:{job.height}px}}"
            f"</style></head><body>{svg}</body></html>")


class Chromium(Renderer):
    """Chromium, Chrome or Edge: a headless screenshot of the wrapper page.

    The browser's new headless mode keeps room for a toolbar it never shows, so the page is laid out in a
    viewport shorter than --window-size (by 87 px in Chromium 141) and a window the image's own size cuts
    the bottom off, or shows nothing at all below 87 px. The window is therefore made larger than the
    image, which sits at the page's top left corner, and the screenshot is cropped to the image."""

    kind = "chromium"
    MARGIN = 300

    def render(self, job: Job) -> bytes:
        with scratch_folder() as folder:
            page = Path(folder) / "page.html"
            out = Path(folder) / "out.png"
            page.write_text(wrapper_html(job), encoding="utf-8")
            self.run([self.program, "--headless=new", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                      "--no-first-run", "--force-device-scale-factor=1", "--default-background-color=00000000",
                      f"--user-data-dir={Path(folder) / 'profile'}",
                      f"--window-size={job.width + self.MARGIN},{job.height + self.MARGIN}",
                      f"--screenshot={out}", page.resolve().as_uri()],
                     f"{self.program} on {relative(job.svg)}")
            if not out.exists():
                fail(f"{self.program} wrote no screenshot of {relative(job.svg)}")
            return top_left(out.read_bytes(), job)


def top_left(png: bytes, job: Job) -> bytes:
    """The job's image cut from the top left corner of a larger screenshot."""
    Image = pillow()
    image = Image.open(io.BytesIO(png))
    if image.size[0] < job.width or image.size[1] < job.height:
        fail(f"the screenshot of {relative(job.svg)} is {image.size[0]}x{image.size[1]}, smaller than the "
             f"{job.width}x{job.height} wanted")
    out = io.BytesIO()
    image.crop((0, 0, job.width, job.height)).save(out, "PNG")
    return out.getvalue()


PLAYWRIGHT_SCRIPT = r"""
const fs = require('fs');
let playwright;
try { playwright = require('playwright'); }
catch (e) { playwright = require(require('child_process').execSync('npm root -g').toString().trim() + '/playwright'); }
const jobs = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
(async () => {
  const browser = await playwright.chromium.launch();
  for (const job of jobs) {
    const page = await browser.newPage({ viewport: { width: job.width, height: job.height }, deviceScaleFactor: 1 });
    await page.goto(job.url);
    await page.screenshot({ path: job.png, omitBackground: true });
    await page.close();
  }
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
"""


class Playwright(Renderer):
    """Node's Playwright, driving its own Chromium: every page in one browser."""

    kind = "playwright"

    def version(self) -> str:
        try:
            out = subprocess.run([self.program, "-e", "console.log(require('playwright/package.json').version)"],
                                 capture_output=True, text=True, timeout=60)
            return f"Playwright {out.stdout.strip()}" if out.returncode == 0 and out.stdout.strip() else "Playwright"
        except (OSError, subprocess.TimeoutExpired):
            return "Playwright"

    def render_all(self, jobs: list[Job]) -> dict[Job, bytes]:
        with scratch_folder() as folder:
            work = Path(folder)
            manifest = []
            for i, job in enumerate(jobs):
                page = work / f"page-{i}.html"
                page.write_text(wrapper_html(job), encoding="utf-8")
                manifest.append({"url": page.resolve().as_uri(), "png": str(work / f"out-{i}.png"),
                                 "width": job.width, "height": job.height})
            (work / "render.js").write_text(PLAYWRIGHT_SCRIPT, encoding="utf-8")
            (work / "jobs.json").write_text(json.dumps(manifest), encoding="utf-8")
            self.run([self.program, str(work / "render.js"), str(work / "jobs.json")], "Playwright")
            return {job: (work / f"out-{i}.png").read_bytes() for i, job in enumerate(jobs)}

    def render(self, job: Job) -> bytes:
        return self.render_all([job])[job]


def browsers() -> list[str]:
    """Where a Chromium-family browser may be, in the order tried."""
    names = ["/opt/pw-browsers/chromium", "chromium", "chromium-browser", "google-chrome", "google-chrome-stable",
             "chrome", "msedge", "microsoft-edge"]
    if sys.platform == "win32":
        for root in filter(None, [os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)"),
                                  os.environ.get("LocalAppData")]):
            names += [str(Path(root) / "Google" / "Chrome" / "Application" / "chrome.exe"),
                      str(Path(root) / "Microsoft" / "Edge" / "Application" / "msedge.exe"),
                      str(Path(root) / "Chromium" / "Application" / "chrome.exe")]
    elif sys.platform == "darwin":
        names += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                  "/Applications/Chromium.app/Contents/MacOS/Chromium",
                  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"]
    return names


def program(name: str) -> str | None:
    """The path of a program, given its name or path, if it can be run."""
    if os.path.sep in name or (os.path.altsep and os.path.altsep in name):
        return name if os.path.isfile(name) and os.access(name, os.X_OK) else None
    return shutil.which(name)


def playwright_works(node: str) -> bool:
    try:
        return subprocess.run([node, "-e", "require('playwright')"], capture_output=True, timeout=60).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def locate(kind: str) -> Renderer | None:
    """The first program of a kind of renderer that this machine has."""
    if kind == "rsvg-convert":
        found = program("rsvg-convert")
        return RsvgConvert(found) if found else None
    if kind == "inkscape":
        found = program("inkscape")
        return Inkscape(found) if found else None
    if kind == "chromium":
        for name in browsers():
            found = program(name)
            if found:
                return Chromium(found)
        return None
    node = program("node")
    return Playwright(node) if node and playwright_works(node) else None


KINDS = ["rsvg-convert", "inkscape", "chromium", "playwright"]


def find_renderer(choice: str | None) -> Renderer:
    if choice in KINDS:
        found = locate(choice)
        if not found:
            fail(f"no {choice} on this machine; name its program instead (--renderer PATH)")
        return found
    if choice:
        found = program(choice)
        if not found:
            fail(f"can't run {choice}")
        name = Path(found).name.lower()
        if "rsvg" in name:
            return RsvgConvert(found)
        if "inkscape" in name:
            return Inkscape(found)
        if name in ("node", "node.exe"):
            if not playwright_works(found):
                fail(f"{found} can't load Playwright (npm install -g playwright)")
            return Playwright(found)
        return Chromium(found)
    for kind in KINDS:
        found = locate(kind)
        if found:
            return found
    fail("no renderer: install rsvg-convert (librsvg), Inkscape, Chromium/Chrome/Edge, or Node with Playwright, "
         "or name one with --renderer")


# ---------------------------------------------------------------- making the files

def pillow():
    try:
        from PIL import Image
    except ImportError:
        fail("Pillow is needed to make the files: python3 -m pip install pillow")
    return Image


def finished_png(raw: bytes, job: Job) -> bytes:
    """The rendered PNG checked and made small: RGB when it has no transparency, optimised either way.

    The tile alone (TILES: the box with its rounded corners on the page) must come out transparent at its
    corners, opaque in the middle and mostly so at the bottom edge's middle (its edge, anti-aliased at the
    small sizes). A banner must come out transparent at its corners, with its icon's tile opaque in the
    middle and at the bottom edge. Any other image must be opaque throughout. A renderer that painted a
    background, scaled the drawing or cut it short fails here rather than leaving a wrong file that --check
    would then guard."""
    Image = pillow()
    image = Image.open(io.BytesIO(raw))
    if image.size != (job.width, job.height):
        fail(f"the renderer made {relative(job.svg)} {image.size[0]}x{image.size[1]}, not {job.width}x{job.height}")
    image = image.convert("RGBA")
    w, h = image.size
    alpha = image.getchannel("A")
    if job.svg in TILES or job.svg in BANNERS:
        if alpha.getpixel((0, 0)) != 0 or alpha.getpixel((w - 1, h - 1)) != 0:
            fail(f"the renderer painted a background behind {relative(job.svg)}: its corners should be transparent")
        if job.svg in TILES:
            solid = [(w // 2, h // 2, 255), (w // 2, h - 1, 128)]
        else:
            solid = [(x * w // BANNER_BOX[0], y * h // BANNER_BOX[1], 255) for x, y in BANNER_TILE]
        if any(alpha.getpixel((x, y)) < least for x, y, least in solid):
            fail(f"the renderer left the middle or the bottom of the icon in {relative(job.svg)} blank at {w}x{h}")
    elif alpha.getextrema() != (255, 255):
        fail(f"the renderer left part of {relative(job.svg)} blank at {w}x{h}: it should be opaque throughout")
    if alpha.getextrema() == (255, 255):
        image = image.convert("RGB")
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def centred(png: bytes, width: int, height: int) -> bytes:
    """A rendered tile in the middle of a larger transparent image."""
    Image = pillow()
    tile = Image.open(io.BytesIO(png)).convert("RGBA")
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    image.paste(tile, ((width - tile.width) // 2, (height - tile.height) // 2))
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def bmp_entry(png: bytes) -> bytes:
    """An icon image as Windows stores one below 256 px: a 32-bit BMP DIB, bottom-up, with a 1-bit AND mask
    (set where a pixel is fully transparent) after the pixels, and a header whose height counts both."""
    Image = pillow()
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    w, h = image.size
    rows = image.tobytes("raw", "BGRA")
    stride = w * 4
    xor = b"".join(rows[y * stride:(y + 1) * stride] for y in range(h - 1, -1, -1))
    alpha = image.getchannel("A").tobytes()
    mask_stride = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray(mask_stride)
        for x in range(w):
            if alpha[y * w + x] == 0:
                line[x // 8] |= 0x80 >> (x % 8)
        mask += line
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + xor + bytes(mask)


def ico_file(images: list[tuple[int, bytes]]) -> bytes:
    """An .ico of the given PNGs (size, bytes), the ones below 256 px turned into BMP entries."""
    entries = [(size, png if size >= 256 else bmp_entry(png)) for size, png in images]
    directory = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    body = bytearray()
    for size, data in entries:
        directory += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset + len(body))
        body += data
    return bytes(directory + body)


def make(renderer: Renderer) -> None:
    made = outputs()
    missing = [relative(path) for path in INPUTS if not path.exists()]
    if missing:
        fail(f"{', '.join(missing)} missing")
    wrong = template_problems()
    if wrong:
        fail("\n  ".join(["the templates don't match the icon:", *wrong]))

    jobs: list[Job] = []
    for item in made:
        if isinstance(item, Png):
            jobs.append(item.job())
        elif isinstance(item, Ico):
            jobs += [Job(variant(size), size, size) for size in item.sizes]
    jobs = list(dict.fromkeys(jobs))      # each distinct render once

    description = renderer.version()
    print(f"rendering {len(jobs)} images with {description} ({renderer.program})")
    raw = renderer.render_all(jobs)
    pngs = {job: finished_png(raw[job], job) for job in jobs}

    for item in made:
        item.path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(item, Png):
            data = pngs[item.job()]
            if item.tile:
                data = centred(data, item.width, item.height)
            if item.limit and len(data) >= item.limit:
                fail(f"{relative(item.path)} is {len(data)} bytes; it must stay under {item.limit}")
            item.path.write_bytes(data)
            tile = f", the tile {item.tile} px" if item.tile else ""
            print(f"wrote {relative(item.path)} ({item.width}x{item.height}{tile}, {len(data)} bytes)")
        elif isinstance(item, Ico):
            data = ico_file([(size, pngs[Job(variant(size), size, size)]) for size in item.sizes])
            item.path.write_bytes(data)
            print(f"wrote {relative(item.path)} ({', '.join(map(str, item.sizes))}; {len(data)} bytes)")
        else:
            item.path.write_bytes(text_bytes(item.svg))
            print(f"wrote {relative(item.path)} (a copy of {relative(item.svg)})")

    stamp = {
        "made_by": "tools/make_icons.py",
        "renderer": description,
        "inputs": {relative(path): sha256(text_bytes(path)) for path in INPUTS},
        "outputs": describe_outputs(made),
    }
    text = json.dumps(stamp, indent=2)
    text = re.sub(r"\[\s*([^\[\]{}]*?)\s*\]",                # sizes and image lists on one line each
                  lambda m: "[" + ", ".join(part.strip() for part in m.group(1).split(",")) + "]", text)
    STAMP.write_bytes((text + "\n").encode("utf-8"))           # Unix line endings on Windows too
    print(f"wrote {relative(STAMP)}")

    wrong = problems(made, stamp)
    if wrong:
        fail("the files just written don't pass --check:\n  " + "\n  ".join(wrong))
    print(f"{len(made)} icon files made")


def check() -> None:
    if not STAMP.exists():
        fail(f"{relative(STAMP)} is missing: run python3 tools/make_icons.py")
    try:
        stamp = json.loads(STAMP.read_text(encoding="utf-8"))
    except ValueError as error:
        fail(f"{relative(STAMP)} isn't valid JSON ({error}): run python3 tools/make_icons.py")
    made = outputs()
    wrong = problems(made, stamp)
    if wrong:
        fail("the icon files don't match their SVGs:\n  " + "\n  ".join(wrong) + "\nRun python3 tools/make_icons.py")
    print(f"{len(made)} icon files up to date (made with {stamp.get('renderer', 'an unknown renderer')})")


# ---------------------------------------------------------------- main

def main() -> None:
    args = sys.argv[1:]
    renderer = None
    if "--renderer" in args:
        at = args.index("--renderer")
        if at + 1 >= len(args):
            fail("--renderer needs a kind (" + ", ".join(KINDS) + ") or a program")
        renderer = args[at + 1]
        del args[at:at + 2]
    for arg in args:
        if arg.startswith("--renderer="):
            renderer = arg.split("=", 1)[1]
        elif arg != "--check":
            fail(f"unknown argument {arg}; see the top of the script")

    if "--check" in args:
        check()
    else:
        make(find_renderer(renderer))


if __name__ == "__main__":
    main()
