#!/usr/bin/env python3
"""Makes MPE Roll's list of instruments and their MPE from docs/synths.json.

It writes, both committed so nobody needs this script to read or build them:

  docs/MPE_SYNTHS.md          the list as a page: each instrument's MPE, where to turn it on, sources
  source/plugin/KnownSynths.h the same list for the plugin, which matches a loaded synth against it to
                              say where its MPE switch is, or that it has none (SynthProfiles.cpp)

docs/synths.json holds "checked" (when the list was last checked, e.g. "October 2026") and
"synths", one entry per instrument:

  name, maker       as the user knows them
  category          synth, sampler-rompler, keys-piano, orchestral-acoustic, drums, physical-model,
                    modular or other
  formats           what there is for Windows: VST2, VST3, CLAP, AAX, Standalone
  mpe               yes, limited, no, undocumented (the maker never mentions MPE for it) or unclear
  followsSetup      true when it turns MPE on by itself from MPE Roll's setup messages
  enable            where to turn MPE on, as the instrument shows it, with " > " between steps
  bendRange         where its per-note bend range is set, and its default or fixed value
  noteExpressions   what it does with VST3 or CLAP note expressions, if anything is known
  notes             anything else worth knowing (limits, versions, discontinued)
  instead           for one without MPE: what to do instead, if there's more to say than Plain MIDI
  advice            what MPE Roll says when it's loaded (needed for yes and limited)
  clapAdvice        added to the advice in builds that host CLAP plugins
  sources           [{url, says}]: where each claim comes from
  checked           false when no second, independent check has confirmed it yet (default true)
  match             how the plugin recognises it; all optional:
                      names     the names it goes by in a host (default: its name), matched at the start;
                                [""] for any instrument of its makers, which then goes by its own name
                      excludes  names that start the same but are something else ("Serum FX")
                      makers    the maker as plugins give it (default: any maker); "=waves" matches
                                exactly, anything else anywhere in the maker's name
                      versions  major versions it covers ([1, 2]); default: any
                      ownName   true when one entry covers several instruments, which then go by
                                their own names
                    Names and makers are compared in lower case with only letters and digits kept,
                    so "Serum 2", "Serum2" and "serum-2" are the same.
  plugin            false to leave it out of the plugin's list (the page still has it)

Text the plugin shows must be plain ASCII; "›", dashes and curly quotes are turned into ASCII.

Run it from anywhere; --check only says whether the files it writes are up to date:

    python3 tools/make_synth_list.py [--check]
"""

from __future__ import annotations

import json
import re
import sys
from collections import OrderedDict
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "docs" / "synths.json"
PAGE_OUT = ROOT / "docs" / "MPE_SYNTHS.md"
PLUGIN_OUT = ROOT / "source" / "plugin" / "KnownSynths.h"
FL_GUIDE = "FL_STUDIO.md"

CATEGORIES = ["synth", "sampler-rompler", "keys-piano", "orchestral-acoustic", "drums", "physical-model", "modular", "other"]
FORMATS = ["VST2", "VST3", "CLAP", "AAX", "Standalone"]
MPE = OrderedDict([
    ("yes", "Yes"),
    ("limited", "Yes, limited"),
    ("no", "**No**"),
    ("undocumented", "Not mentioned"),
    ("unclear", "Unclear"),
])


def fail(message: str) -> None:
    sys.exit(f"make_synth_list.py: {message}")


def key(text: str) -> str:
    """How names and makers are compared, here and in the plugin: lower case letters and digits."""
    return re.sub(r"[^a-z0-9]", "", text.lower())


ASCII = {"›": ">", "→": ">", "–": "-", "—": " - ", "‘": "'", "’": "'", "“": '"', "”": '"', "…": "...",
         "×": "x", "±": "+/-", " ": " ", "é": "e", "ö": "o", "ü": "u", "ä": "a"}


def ascii(text: str) -> str:
    for a, b in ASCII.items():
        text = text.replace(a, b)
    if not text.isascii():
        fail(f"the plugin can only show plain ASCII text, which this isn't: {text!r}")
    return text


def load() -> tuple[str, list[dict]]:
    data = json.loads(DATA.read_text(encoding="utf-8"))
    checked = data.get("checked", "")
    synths = data.get("synths", [])
    if not checked or not synths:
        fail("docs/synths.json needs \"checked\" and a list of \"synths\"")

    seen = {}
    for s in synths:
        where = f"{s.get('maker', '?')} {s.get('name', '?')}"
        for field in ("name", "maker", "category", "formats", "mpe", "sources"):
            if field not in s:
                fail(f"{where} has no {field}")
        if s["category"] not in CATEGORIES:
            fail(f"{where}: category {s['category']!r} isn't one of {CATEGORIES}")
        if s["mpe"] not in MPE:
            fail(f"{where}: mpe {s['mpe']!r} isn't one of {list(MPE)}")
        for f in s["formats"]:
            if f not in FORMATS:
                fail(f"{where}: format {f!r} isn't one of {FORMATS}")
        if not s["sources"]:
            fail(f"{where} has no sources")
        if s["mpe"] in ("yes", "limited") and s.get("plugin", True) and not s.get("advice"):
            fail(f"{where} has MPE but no advice for the plugin to give")
        k = (key(s["maker"]), key(s["name"]))
        if k in seen:
            fail(f"{where} is in the list twice")
        seen[k] = True
    return checked, synths


# ---------------------------------------------------------------- docs/MPE_SYNTHS.md

def anchor(heading: str) -> str:
    """The anchor GitHub gives a heading."""
    return re.sub(r"[^a-z0-9 _-]", "", heading.lower()).replace(" ", "-")


def cell(text: str) -> str:
    return " ".join(str(text).split()).replace("|", "\\|").replace(" > ", " › ")


def source_links(sources: list[dict]) -> str:
    links = []
    for i, s in enumerate(sources):
        host = urlparse(s["url"]).netloc.removeprefix("www.") or f"source {i + 1}"
        links.append(f"[{host}]({s['url']})")
    return ", ".join(OrderedDict.fromkeys(links))


def how_to(s: dict) -> str:
    parts = []
    if s["mpe"] in ("yes", "limited"):
        parts.append(s.get("enable") or "On without a switch")
        if s.get("bendRange"):
            parts.append(f"Bend range: {s['bendRange']}")
        if s.get("noteExpressions"):
            parts.append(s["noteExpressions"])
    else:
        parts.append("*Plain MIDI*")
        if s.get("instead"):
            parts.append(s["instead"])
    return ". ".join(cell(p).rstrip(".") for p in parts if p) + "."


def render_page(checked: str, synths: list[dict]) -> str:
    by_maker: dict[str, list[dict]] = OrderedDict()
    for s in sorted(synths, key=lambda s: (s["maker"].lower(), s["name"].lower())):
        by_maker.setdefault(s["maker"], []).append(s)

    counts = OrderedDict((m, sum(1 for s in synths if s["mpe"] == m)) for m in MPE)

    out = [
        "# MPE in software instruments",
        "",
        "<!-- Made by tools/make_synth_list.py from docs/synths.json. Change those, not this page. -->",
        "",
        f"Which instruments for Windows have MPE, and where to turn it on: {len(synths)} instruments from "
        f"{len(by_maker)} makers, sold by the major stores (Plugin Boutique, Sweetwater, Thomann, Best Service, "
        f"KVR Marketplace, Native Instruments, Splice, ADSR, Audio Plugin Deals) or given away. Checked {checked} "
        "against the makers' manuals, support pages and release notes, and the MPE lists of controller makers. "
        "The sources are in the last column. Found a mistake, or an instrument that's missing? Please open an issue.",
        "",
        f"{counts['yes']} have MPE, {counts['limited']} have it with limits, {counts['no']} don't, "
        f"{counts['undocumented']} never mention it and {counts['unclear']} are unclear.",
        "",
        "## Reading the list",
        "",
        "- **Yes**: it has MPE. Turn it on where the list says, and set its per-note bend range to MPE Roll's "
        "*Per-note bend range* (48 unless you change it), or MPE Roll's to match it.",
        "- **Yes, limited**: it has MPE with a limit the notes spell out (one note at a time, some presets only...).",
        "- **No**: its maker says it has no MPE. In MPE Roll's *MPE settings*, set *Synth expects* to "
        "*Plain MIDI (one channel)*: the newest note's glides, pressure and timbre then drive it, and every "
        "held note follows them.",
        "- **Not mentioned**: its maker never mentions MPE for it, so it almost certainly has none. Use "
        "*Plain MIDI* as for **No**.",
        "- **Unclear**: the sources disagree. Try MPE first; if held notes bend together, use *Plain MIDI*.",
        "- **\\***: not yet confirmed by a second, independent check.",
        "",
        "MPE Roll knows these instruments: when you load one, its status line and *MPE settings* say where "
        "its MPE switch is, and for one without MPE it offers to switch to *Plain MIDI*. A VST3 that takes "
        "VST3 note expressions, or a CLAP that takes CLAP note expressions, can get each note's expression "
        "that way, with nothing to set up in the instrument; *MPE settings › Synth is played with* says "
        f"which way MPE Roll plays it. [{FL_GUIDE}]({FL_GUIDE}#7-notes-on-particular-synths) has more on "
        "some of them.",
        "",
        "Only instruments are listed: effects don't play notes, so MPE doesn't apply to them.",
        "",
        "## Makers",
        "",
        " · ".join(f"[{m}](#{anchor(m)})" for m in by_maker),
        "",
    ]

    for maker, items in by_maker.items():
        out += [
            f"## {maker}",
            "",
            "| Instrument | Windows | MPE | How to play it with MPE Roll | Notes | Sources |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
        for s in items:
            out.append("| " + " | ".join([
                cell(s["name"]),
                cell(", ".join(s["formats"])),
                MPE[s["mpe"]] + ("" if s.get("checked", True) else " \\*"),
                how_to(s),
                cell(s.get("notes", "")),
                source_links(s["sources"]),
            ]) + " |")
        out.append("")

    return "\n".join(out)


# ---------------------------------------------------------------- source/plugin/KnownSynths.h

def cpp_string(text: str) -> str:
    return json.dumps(ascii(text))


def plugin_entry(s: dict) -> dict:
    match = s.get("match", {})
    names = [key(n) for n in match.get("names", [s["name"]])]
    if "" in names:
        names = []      # any instrument of its makers, which then goes by its own name
    makers = [("=" + key(m[1:])) if m.startswith("=") else key(m) for m in match.get("makers", [])]
    excludes = [key(n) for n in match.get("excludes", [])]
    versions = [str(int(v)) for v in match.get("versions", [])]

    if s["mpe"] in ("yes", "limited"):
        kind = "followsSetup" if s.get("followsSetup") else "yes"
        advice = s["advice"]
    elif s["mpe"] == "no" and s.get("checked", True):
        kind = "no"
        advice = s.get("advice") or f"{s['name']} has no MPE."
        if s.get("instead"):
            advice += " " + s["instead"].rstrip(".") + "."
        advice += " Set Synth expects to Plain MIDI."
    else:
        kind = "undocumented"
        advice = (s.get("advice") or f"{s['name']}'s maker doesn't mention MPE for it, so it most likely has none.") \
            + " Set Synth expects to Plain MIDI."

    return {
        "name": "" if match.get("ownName") or not names else s["name"],
        "names": "|".join(names),
        "excludes": "|".join(excludes),
        "makers": "|".join(makers),
        "versions": "|".join(versions),
        "kind": kind,
        "advice": advice,
        "clapAdvice": s.get("clapAdvice", ""),
        # More particular entries first: those for some versions only, then longer names, and the
        # ones for any instrument of a maker (no name) last.
        "order": (0 if versions else 1, -max((len(n) for n in names), default=0)),
    }


def render_plugin(checked: str, synths: list[dict]) -> str:
    entries = [plugin_entry(s) for s in synths if s["mpe"] != "unclear" and s.get("plugin", True)]
    entries.sort(key=lambda e: e["order"])

    out = [
        "#pragma once",
        "",
        "// Made by tools/make_synth_list.py from docs/synths.json. Change those, not this file.",
        f"// The instruments MPE Roll knows the MPE of, checked {checked}: the same list as docs/MPE_SYNTHS.md.",
        "",
        "#include <array>",
        "",
        "namespace mperoll::knownsynths",
        "{",
        "    enum class Mpe { yes, followsSetup, no, undocumented };",
        "",
        "    // Names and makers in lower case letters and digits, several separated by '|'. A synth is this one",
        "    // when its name starts with one of the names (any name, if there are none) and not with one of the",
        "    // excludes, its maker contains one of the makers (or is one, for those starting with '='), and its",
        "    // major version is one of the versions. The first that fits is the one.",
        "    struct Synth",
        "    {",
        "        const char* name;",
        "        const char* names;",
        "        const char* excludes;",
        "        const char* makers;",
        "        const char* versions;",
        "        Mpe mpe;",
        "        const char* advice;",
        "        const char* clapAdvice;     // added in builds that host CLAP plugins",
        "    };",
        "",
        f"    inline constexpr std::array<Synth, {len(entries)}> synths {{ {{",
    ]
    for e in entries:
        out.append(f"        {{ {cpp_string(e['name'])}, {cpp_string(e['names'])}, {cpp_string(e['excludes'])}, "
                   f"{cpp_string(e['makers'])}, {cpp_string(e['versions'])}, Mpe::{e['kind']},")
        out.append(f"          {cpp_string(e['advice'])},")
        out.append(f"          {cpp_string(e['clapAdvice'])} }},")
    out += ["    } };", "}", ""]
    return "\n".join(out)


# ---------------------------------------------------------------- main

def main() -> None:
    check = "--check" in sys.argv[1:]
    checked, synths = load()
    outputs = {PAGE_OUT: render_page(checked, synths), PLUGIN_OUT: render_plugin(checked, synths)}

    stale = []
    for path, text in outputs.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current == text:
            continue
        if check:
            stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(ROOT).as_posix()}")

    if stale:
        fail(f"{', '.join(stale)} don't match docs/synths.json: run python3 tools/make_synth_list.py")
    print(f"{len(synths)} instruments" + (", files up to date" if check else ""))


if __name__ == "__main__":
    main()
