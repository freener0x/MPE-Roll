#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The MPE Roll contributors
# SPDX-License-Identifier: AGPL-3.0-only

"""Checks that the files of MPE Roll's code, tests, tools, build, installer and CI workflows start
with its SPDX header, and with --fix gives the header to those that have no SPDX lines at all.

The header is the file's first comment, after its #! line and a Python file's coding line if it has
them: two lines in the file's own comment syntax,

    // SPDX-FileCopyrightText: 2026 The MPE Roll contributors
    // SPDX-License-Identifier: AGPL-3.0-only

"// " in C, C++ and Objective-C++ (BuildInfo.h.in, the code cmake/PatchJuce.cmake puts into JUCE and
the scanner's resource script too), "# " in Python, shell, PowerShell, CMake and the GitHub
workflows, "; " in the Inno Setup script. Lines naming another author of a file's code
(SPDX-FileCopyrightText or SPDX-FileContributor) may follow the two, and every
SPDX-License-Identifier in the file must name AGPL-3.0-only. --fix puts the header, and a blank line
after it, into each file that has no SPDX lines; it leaves a file that has some to be fixed by hand,
since they may be another author's.

The files are the ones git tracks in source/, tests/, tools/, installer/, cmake/ and
third_party/lame/ (LAME's build configuration, written for MPE Roll), MPE Roll's two include shims
in third_party/fst/pluginterfaces/vst2.x/, CMakeLists.txt, assets/icon/MpeRollScanner.rc and
.github/workflows/*.yml, leaving out Markdown, JSON and images. The two files in source/ that
scripts make get the header from those scripts: source/plugin/KnownSynths.h from
tools/make_synth_list.py and source/plugin/ui/DonateData.h from tools/make_donate_page.py.

No other file may have MPE Roll's SPDX lines (Markdown, JSON and images aren't read): FST's own
headers in third_party/fst/ are under FST's licence, GPL-3.0-or-later, and a file of MPE Roll's own
that should have the header goes into SCOPE, so that it is checked. Also without the header: docs/
(the website), the rest of .github (the issue templates, FUNDING.yml, dependabot.yml, release.yml),
the other files at the top (LICENSE, .gitignore, _config.yml), and Markdown, JSON and images
wherever they are.

Needs only Python 3 and git. Run it from anywhere; CI runs it without --fix:

    python3 tools/check_spdx.py [--fix]
"""

from __future__ import annotations

import codecs
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LICENSE_ID = "AGPL-3.0-only"
HEADER = ["SPDX-FileCopyrightText: 2026 The MPE Roll contributors",
          "SPDX-License-Identifier: AGPL-3.0-only"]

# What git lists the files of: folders, files and patterns, from the top of the repository.
SCOPE = ["source", "tests", "tools", "installer", "cmake", "third_party/lame",
         "third_party/fst/pluginterfaces/vst2.x", "CMakeLists.txt", "assets/icon/MpeRollScanner.rc",
         ".github/workflows/*.yml", ".github/workflows/*.yaml"]

# What starts a comment line, by the end of the file's name (the longest end that fits).
COMMENT = {
    ".c": "// ", ".cpp": "// ", ".h": "// ", ".mm": "// ", ".h.in": "// ", ".inc": "// ", ".rc": "// ",
    ".py": "# ", ".sh": "# ", ".ps1": "# ", ".cmake": "# ", "CMakeLists.txt": "# ", ".yml": "# ", ".yaml": "# ",
    ".iss": "; ",
}

# Not code: no header, wherever they are.
NOT_CODE = (".md", ".json", ".png", ".svg", ".ico", ".jpg", ".gif", ".webp")

# An SPDX tag ("SPDX-", a name of letters and hyphens, and a colon), in a comment or anywhere else.
SPDX_TAG = re.compile(r"SPDX-[A-Za-z][A-Za-z-]*:")
# The licence expression after an SPDX-License-Identifier tag: up to the end of the line, a quote
# or a backslash (the tag can be in a string the code writes), less the end of a block comment. The
# pattern is written in two pieces so that this line isn't such a tag itself.
LICENSE = re.compile("SPDX-License" + r"-Identifier:[ \t]*([A-Za-z0-9(][^\"'`\\\r\n]*)")
COMMENT_END = re.compile(r"[ \t]*(?:\*/|-->)?[ \t]*$")
# A Python coding declaration (PEP 263), which only counts on line 1 or 2.
CODING = re.compile(r"^[ \t\f]*#.*?coding[:=]")

NO_HEADER = "no SPDX header"


def fail(message: str) -> None:
    sys.exit(f"check_spdx.py: {message}")


def tracked(*pathspecs: str) -> list[str]:
    """The files git tracks (those the pathspecs name, or all), as paths from the top of the repository."""
    try:
        listing = subprocess.run(["git", "ls-files", "-z", "--", *pathspecs], cwd=ROOT, capture_output=True,
                                 check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"git couldn't list the files ({error})")
    return sorted(name for name in listing.decode("utf-8").split("\0") if name)


def comment(name: str) -> str | None:
    """What starts a comment line in the file, or None for a kind of file COMMENT doesn't have."""
    base = name.rsplit("/", 1)[-1]
    ends = [end for end in COMMENT if base.endswith(end)]
    return COMMENT[max(ends, key=len)] if ends else None


def header_line(lines: list[str], name: str) -> int:
    """The line the header starts on: after a #! line, and after a Python file's coding line."""
    top = 1 if lines and lines[0].startswith("#!") else 0
    if name.endswith(".py"):
        for number in (0, 1):
            if number < len(lines) and CODING.match(lines[number]):
                top = number + 1
    return top


def problem(text: str, name: str, prefix: str) -> str | None:
    """What is wrong with the file's SPDX lines, or None if nothing is."""
    for match in LICENSE.finditer(text):
        found = COMMENT_END.sub("", match.group(1))
        if found != LICENSE_ID:
            return f"line {text.count(chr(10), 0, match.start()) + 1} names {found}, not {LICENSE_ID}"
    lines = text.splitlines()
    top = header_line(lines, name)
    if lines[top:top + len(HEADER)] == [prefix + line for line in HEADER]:
        return None
    if SPDX_TAG.search(text):
        return "it has SPDX lines, but MPE Roll's header isn't first: fix it by hand"
    return NO_HEADER


def with_header(text: str, name: str, prefix: str) -> str:
    """The text with the header where it goes, and a blank line between it and what follows."""
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)
    top = header_line(lines, name)
    before, rest = lines[:top], lines[top:]
    if before and not before[-1].endswith(("\r", "\n")):
        before[-1] += newline
    if rest and rest[0].strip():
        rest = [newline, *rest]
    return "".join(before + [prefix + line + newline for line in HEADER] + rest)


def main() -> None:
    args = sys.argv[1:]
    for arg in args:
        if arg != "--fix":
            fail(f"unknown argument {arg}; see the top of the script")
    fix = "--fix" in args

    in_scope = tracked(*SCOPE)
    checked, wrong, unknown = 0, [], []
    for name in in_scope:
        path = ROOT / name
        if name.lower().endswith(NOT_CODE) or path.is_symlink() or not path.is_file():
            continue
        prefix = comment(name)
        if prefix is None:
            unknown.append(name)
            continue
        checked += 1
        data = path.read_bytes()
        bom = codecs.BOM_UTF8 if data.startswith(codecs.BOM_UTF8) else b""
        try:
            text = data[len(bom):].decode("utf-8")
        except UnicodeDecodeError:
            wrong.append(f"{name} (not UTF-8 text, so it wasn't read)")
            continue
        found = problem(text, name, prefix)
        if found is None:
            continue
        if fix and found == NO_HEADER:
            path.write_bytes(bom + with_header(text, name, prefix).encode("utf-8"))
            print(f"gave the SPDX header to {name}")
        else:
            wrong.append(f"{name} ({found})")

    # The header belongs in those files only: anywhere else it goes unchecked, or isn't MPE Roll's to give.
    scoped, others, stray = set(in_scope), 0, []
    for name in tracked():
        path = ROOT / name
        if name in scoped or name.lower().endswith(NOT_CODE) or path.is_symlink() or not path.is_file():
            continue
        others += 1
        data = path.read_bytes()
        if any(line.encode("utf-8") in data for line in HEADER):
            stray.append(name)

    problems = []
    if unknown:
        problems.append("no comment syntax known for these files: add their kind to COMMENT in tools/check_spdx.py "
                        "(or to NOT_CODE, if they aren't code)\n    " + "\n    ".join(unknown))
    if wrong:
        header = "\n".join("    // " + line for line in HEADER)
        problems.append(f"{len(wrong)} file(s) don't have MPE Roll's SPDX header as they should\n    "
                        + "\n    ".join(wrong)
                        + "\n  The header, first in each file (after a #! line, and a Python file's coding line), "
                        + f"in the file's own comment syntax:\n{header}\n"
                        + "  Lines naming another author of the file's code (SPDX-FileCopyrightText or "
                        + "SPDX-FileContributor) may follow it.\n"
                        + f"  python3 tools/check_spdx.py --fix adds it to the files marked ({NO_HEADER}).")
    if stray:
        problems.append(f"{len(stray)} file(s) that this check doesn't cover have MPE Roll's SPDX lines\n    "
                        + "\n    ".join(stray)
                        + "\n  FST's files keep FST's licence. If a file is MPE Roll's own and should have the "
                        + "header, add it to SCOPE in tools/check_spdx.py.")
    if problems:
        fail("\n  ".join(problems))
    print(f"{checked} files start with MPE Roll's SPDX header, and {others} others (not Markdown, JSON or images) "
          "don't have it")


if __name__ == "__main__":
    main()
