#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The MPE Roll contributors
# SPDX-License-Identifier: AGPL-3.0-only

"""Checks MPE Roll's website as Jekyll built it (the _site folder): that it loads nothing from other
sites, that nothing in it points at a file or a place on a page that isn't there, and that what it
tells search engines holds.

    python3 tools/check_site.py <built site> [--baseurl /MPE-Roll]

It reads every .html and .css file in the folder, and sitemap.xml, and fails, with a line for each,
on:

  - anything a page loads from another site: a picture, a clip and its poster, a script, a frame, a
    stylesheet, an icon or a font (src, srcset, poster, an <object>'s data, a <link> that loads, a
    url() or @import in a stylesheet, a <style> or a style attribute). The website loads nothing from
    anywhere else (docs/_config.yml). Links to other sites (<a href>, <link rel="canonical">) are
    fine: they load nothing until they're clicked.
  - anything that points into the site, as a link or as something loaded (a relative path, a path
    from the site's root, or the site's own address), at a file the site doesn't have, at a
    #fragment no element of that page has as its id, or at a path from the root outside the
    folder the site is served from (its baseurl), which on GitHub Pages is another site's.
  - a block of data for search engines (<script type="application/ld+json">: data in the page,
    which loads nothing) that isn't JSON, or that names an address in the site that isn't there.
  - an address in sitemap.xml that isn't a file of the site: one it doesn't have, or another
    site's.
  - a page of the site (one with the site's header) whose description (<meta name="description">) is
    missing, or is only the site's tagline, the words under the name in that header: what a page
    without a description of its own gets. docs/_config.yml has each guide's.
  - two pages with the same description, or a description longer than 160 characters, the length
    the site keeps to so that a search result can show it whole.

GitHub Pages serves the site from https://freener0x.github.io/MPE-Roll/, so its pages' paths from
the root start with /MPE-Roll when Jekyll knew that (GitHub's build does; one at the root has no
baseurl). The baseurl is read from the front page: its <link rel="canonical"> (jekyll-seo-tag,
when the site's url is known), or else where its stylesheet is; --baseurl says it instead.

Needs only Python 3. CI runs it on the site it builds (.github/workflows/build.yml, the Website job).
"""

from __future__ import annotations

import argparse
import html
import json
import posixpath
import re
import sys
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

# The attributes that load something into a page, by element. (<input src> only for type="image".)
LOADING = {
    "img": ("src", "srcset"),
    "source": ("src", "srcset"),
    "video": ("src", "poster"),
    "audio": ("src",),
    "track": ("src",),
    "script": ("src",),
    "iframe": ("src",),
    "frame": ("src",),
    "embed": ("src",),
    "object": ("data",),
    "input": ("src",),
}
LINKING = {"a": ("href",), "area": ("href",)}

# A <link> with one of these in its rel loads something (or opens a connection) as the page loads;
# any other (canonical, alternate, license...) is a link.
LOADING_RELS = {"stylesheet", "icon", "apple-touch-icon", "apple-touch-icon-precomposed", "mask-icon",
                "manifest", "preload", "prefetch", "modulepreload", "preconnect", "dns-prefetch",
                "prerender"}

# The picture a link to the site shows on social networks: the networks fetch it, not the visitor,
# so it may be anywhere, but one in the site must be there.
CARD_METAS = {"og:image", "og:image:url", "og:image:secure_url", "twitter:image"}

CSS_URL = re.compile(r"""url\(\s*(?:"([^"]*)"|'([^']*)'|([^)\s]*))\s*\)""", re.IGNORECASE)
CSS_IMPORT = re.compile(r"""@import\s+(?:"([^"]*)"|'([^']*)')""", re.IGNORECASE)

# In a block of data for search engines, a string that is an address: a whole one, or a path from
# the root (what Jekyll writes for a site whose url it doesn't know).
DATA_ADDRESS = re.compile(r"(?:https?://|/)\S+")
DATA_TYPE = "application/ld+json"

SITEMAP_LOC = re.compile(r"<loc>\s*([^<\s]+)\s*</loc>")

# The element of the site's header that holds the tagline (docs/_layouts/default.html): a page that
# has it is one of the site's own, laid out like the rest.
TAGLINE_CLASS = "project-tagline"

# What to do about a page whose description isn't its own.
OWN_DESCRIPTION = ("give the page its own, in a sentence or two (a guide's goes under defaults in "
                   "docs/_config.yml, another page's in its front matter)")

# The longest description a page may have. The site keeps to 160 characters, a common rule of thumb
# for what a search engine shows under a result before cutting it short (no search engine's own
# figure was looked up for it).
DESCRIPTION_LIMIT = 160

# Fragments every page has: the top of the page.
ALWAYS_THERE = {"", "top"}


@dataclass
class Ref:
    line: int
    what: str     # where it was found, as "<img src>"
    url: str
    loads: bool   # loaded as the page loads, rather than followed when clicked


@dataclass
class Page:
    ids: set[str] = field(default_factory=set)
    refs: list[Ref] = field(default_factory=list)
    canonical: str | None = None
    stylesheets: list[str] = field(default_factory=list)
    description: str | None = None                              # its <meta name="description">
    tagline: str | None = None                                  # the tagline in its header
    data: list[tuple[int, str]] = field(default_factory=list)   # its JSON-LD blocks: line, text


@dataclass
class Target:
    kind: str                     # "here" (a file in the site), "elsewhere" (another site), "not a
                                  # file" (mailto:, data: and the like) or "wrong"
    path: str = ""                # here: the file, from the site's root
    fragment: str | None = None   # here: after the #, if there's one
    why: str = ""                 # wrong: what's wrong


def css_urls(text: str) -> list[str]:
    """The addresses a stylesheet (or a style attribute) loads: url(...) and @import "...". """
    urls = [next(g for g in m.groups() if g is not None) for m in CSS_URL.finditer(text)]
    urls += [next(g for g in m.groups() if g is not None) for m in CSS_IMPORT.finditer(text)]
    return urls


def data_addresses(value: object) -> list[str]:
    """The addresses a block of JSON-LD names, wherever they are in it."""
    if isinstance(value, dict):
        return [address for inner in value.values() for address in data_addresses(inner)]
    if isinstance(value, list):
        return [address for inner in value for address in data_addresses(inner)]
    if isinstance(value, str) and DATA_ADDRESS.fullmatch(value):
        return [value]
    return []


def srcset_urls(value: str) -> list[str]:
    """The addresses in a srcset: comma-separated, each one followed by its size or density."""
    return [candidate.split()[0] for candidate in value.split(",") if candidate.strip()]


class PageReader(HTMLParser):
    """Collects a page's ids and what it points at, with the line each is on."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.page = Page()
        self.style_line = 0
        self.in_style = False
        self.data_line = 0
        self.in_data = False
        self.data_text: list[str] = []
        self.tagline_tag: str | None = None     # the element holding the tagline, while inside it
        self.tagline_text: list[str] = []

    def add(self, what: str, url: str, loads: bool) -> None:
        self.page.refs.append(Ref(self.getpos()[0], what, url, loads))

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        a: dict[str, str] = {}
        for name, value in attrs:
            a.setdefault(name, value or "")

        if a.get("id"):
            self.page.ids.add(a["id"])
        if tag == "a" and a.get("name"):
            self.page.ids.add(a["name"])

        for name in LOADING.get(tag, ()):
            if name not in a or (tag == "input" and a.get("type", "").lower() != "image"):
                continue
            for url in srcset_urls(a[name]) if name == "srcset" else [a[name]]:
                self.add(f"<{tag} {name}>", url, True)
        for name in LINKING.get(tag, ()):
            if name in a:
                self.add(f"<{tag} {name}>", a[name], False)

        if tag == "link" and "href" in a:
            rels = set(a.get("rel", "").lower().split())
            self.add(f'<link rel="{a.get("rel", "")}">', a["href"], bool(rels & LOADING_RELS))
            if "canonical" in rels:
                self.page.canonical = a["href"]
            if "stylesheet" in rels:
                self.page.stylesheets.append(a["href"])

        if tag == "meta":
            attr = "property" if a.get("property") else "name"
            name = a.get(attr, "").lower()
            if name in CARD_METAS and a.get("content"):
                self.add(f'<meta {attr}="{name}">', a["content"], False)
            if a.get("name", "").lower() == "description" and self.page.description is None:
                self.page.description = " ".join(a.get("content", "").split())

        if tag == "script" and a.get("type", "").strip().lower() == DATA_TYPE:
            self.in_data = True
            self.data_line = self.getpos()[0]
            self.data_text = []

        if (TAGLINE_CLASS in a.get("class", "").split() and self.page.tagline is None
                and self.tagline_tag is None):
            self.tagline_tag = tag
            self.tagline_text = []

        if "style" in a:
            for url in css_urls(a["style"]):
                self.add(f"<{tag} style>", url, True)
        if tag == "style":
            self.in_style = True
            self.style_line = self.getpos()[0]

    def handle_endtag(self, tag: str) -> None:
        if tag == "style":
            self.in_style = False
        if tag == "script" and self.in_data:
            self.in_data = False
            self.page.data.append((self.data_line, "".join(self.data_text)))
        if tag == self.tagline_tag:
            self.tagline_tag = None
            self.page.tagline = " ".join("".join(self.tagline_text).split())

    def handle_data(self, data: str) -> None:
        if self.in_data:
            self.data_text.append(data)
        if self.tagline_tag:
            self.tagline_text.append(data)
        if self.in_style:
            for url in css_urls(data):
                self.page.refs.append(Ref(self.style_line, "<style>", url, True))


def read_page(path: Path) -> Page:
    reader = PageReader()
    reader.feed(path.read_text(encoding="utf-8", errors="replace"))
    reader.close()
    return reader.page


def read_sitemap(path: Path) -> list[Ref]:
    """The addresses sitemap.xml lists, if the site has one."""
    if not path.is_file():
        return []
    refs = []
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        refs += [Ref(number, "<loc>", html.unescape(m.group(1)), False)
                 for m in SITEMAP_LOC.finditer(line)]
    return refs


def read_css(path: Path) -> list[Ref]:
    refs = []
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        refs += [Ref(number, "url() or @import", url, True) for url in css_urls(line)]
    return refs


class Site:
    def __init__(self, root: Path, baseurl: str | None) -> None:
        self.root = root
        self.pages = {p.relative_to(root).as_posix(): read_page(p) for p in sorted(root.rglob("*.html"))}
        self.css = {p.relative_to(root).as_posix(): read_css(p) for p in sorted(root.rglob("*.css"))}
        self.sitemap = read_sitemap(root / "sitemap.xml")
        if "index.html" not in self.pages:
            sys.exit(f"tools/check_site.py: {root} has no index.html: is it a site Jekyll built?")
        self.host: str | None = None     # the site's own host name, when the front page says it
        self.baseurl, self.found_by = self.find_baseurl()
        if baseurl is not None:
            self.baseurl, self.found_by = baseurl.rstrip("/"), "--baseurl"

    def find_baseurl(self) -> tuple[str, str]:
        """Where the site is served from, as the front page says."""
        index = self.pages["index.html"]
        if index.canonical:
            parts = urlsplit(index.canonical)
            self.host = parts.netloc.lower() or None
            return parts.path.rstrip("/"), "the front page's canonical link"
        for href in index.stylesheets:
            path = urlsplit(href).path
            if not path.startswith("/"):
                continue
            # The stylesheet is in the site whatever its baseurl, so what's before its place in
            # the site is the baseurl.
            segments = path.split("/")
            for i in range(1, len(segments)):
                if (self.root / "/".join(segments[i:])).is_file():
                    return "/".join(segments[:i]), "where the front page's stylesheet is"
        return "", "nothing on the front page, so none"

    def is_file(self, path: str) -> bool:
        return (self.root / path).is_file()

    def target(self, source: str, url: str) -> Target:
        """Where url, found in the site's file source, leads."""
        url = url.strip()
        parts = urlsplit(url)
        fragment = unquote(parts.fragment) if "#" in url else None
        if parts.scheme and parts.scheme.lower() not in ("http", "https"):
            return Target("not a file")
        path = parts.path
        if parts.netloc:
            ours = (self.host is not None and parts.netloc.lower() == self.host
                    and (path == self.baseurl or path.startswith(self.baseurl + "/")))
            if not ours:
                return Target("elsewhere")

        if path.startswith("/"):
            if self.baseurl and not (path == self.baseurl or path.startswith(self.baseurl + "/")):
                return Target("wrong", why=f"is outside {self.baseurl}/, where the site is served from")
            path = path[len(self.baseurl):].lstrip("/")
            if not path or path.endswith("/"):
                path += "index.html"
        elif not path:
            path = source
        else:
            folder_index = path.endswith("/")
            path = posixpath.normpath(posixpath.join(posixpath.dirname(source), path))
            if path == ".." or path.startswith("../"):
                return Target("wrong", why="leads out of the site")
            if folder_index or path == ".":
                path = posixpath.join(path, "index.html").removeprefix("./")
        path = unquote(path)

        if (self.root / path).is_dir():
            path = posixpath.join(path, "index.html")
        elif not self.is_file(path) and self.is_file(path + ".html"):
            path += ".html"     # GitHub Pages serves page.html as page too
        return Target("here", path, fragment)

    def wrong(self, source: str, url: str, loads: bool, listed: bool = False) -> str | None:
        """What's wrong with url, found in the site's file source: loaded as the page loads, or
        listed (in sitemap.xml) as one of the site's own. None when nothing is."""
        t = self.target(source, url)
        if t.kind == "elsewhere" and loads:
            return "loaded from another site"
        if t.kind == "wrong":
            return t.why
        if t.kind != "here":
            return "isn't an address of this site" if listed else None
        if not self.is_file(t.path):
            return f"the site has no {t.path}"
        if (t.fragment is not None and t.fragment not in ALWAYS_THERE and t.path in self.pages
                and t.fragment not in self.pages[t.path].ids):
            return f"{t.path} has no element with the id {t.fragment!r}"
        return None

    def problems(self) -> tuple[list[str], int]:
        found = []
        count = 0

        def check(source: str, ref: Ref, listed: bool = False) -> None:
            nonlocal count
            count += 1
            why = self.wrong(source, ref.url, ref.loads, listed)
            if why:
                found.append(f"{source}:{ref.line}: {ref.what} {ref.url}: {why}")

        everything = [(source, page.refs) for source, page in self.pages.items()] + list(self.css.items())
        for source, refs in everything:
            for ref in refs:
                check(source, ref)

        # The data for search engines: JSON, and its addresses in the site lead somewhere.
        for source, page in self.pages.items():
            for line, text in page.data:
                what = f'<script type="{DATA_TYPE}">'
                try:
                    data = json.loads(text)
                except ValueError as error:
                    found.append(f"{source}:{line}: {what}: isn't JSON ({error})")
                    continue
                for address in data_addresses(data):
                    check(source, Ref(line, what, address, False))

        # sitemap.xml lists the site's own files, and only those.
        for ref in self.sitemap:
            check("sitemap.xml", ref, listed=True)

        # A description is one page's own. A page left without one gets the site's tagline from
        # jekyll-seo-tag, the words its header already shows, which tells a search engine nothing
        # about that page. A file without the site's header isn't one of its pages, and is let be.
        by_description: dict[str, list[str]] = {}
        for source, page in self.pages.items():
            if page.tagline is None:
                continue
            if not page.description:
                found.append(f'{source}: no description (<meta name="description">): {OWN_DESCRIPTION}')
            elif page.description == page.tagline:
                found.append(f"{source}: its description is only the site's tagline "
                             f"({page.description!r}): {OWN_DESCRIPTION}")
            else:
                by_description.setdefault(page.description, []).append(source)
                if len(page.description) > DESCRIPTION_LIMIT:
                    found.append(f"{source}: its description is {len(page.description)} characters, over "
                                 f"the {DESCRIPTION_LIMIT} the site keeps to: shorten it (a guide's is in "
                                 "docs/_config.yml, the front page's in tools/make_donate_page.py)")
        for description, sources in by_description.items():
            if len(sources) > 1:
                found.append(f"{', '.join(sources)}: the same description ({description!r}): "
                             f"{OWN_DESCRIPTION}")
        return found, count


def main() -> None:
    parser = argparse.ArgumentParser(description="Checks a built copy of MPE Roll's website.")
    parser.add_argument("site", type=Path, help="the folder Jekyll built the site into (_site)")
    parser.add_argument("--baseurl", help="the path the site is served from, such as /MPE-Roll (default: "
                                          "as the front page says)")
    args = parser.parse_args()
    if not args.site.is_dir():
        sys.exit(f"tools/check_site.py: no folder {args.site}")

    site = Site(args.site, args.baseurl)
    problems, count = site.problems()
    print(f"{args.site}: {len(site.pages)} pages and {len(site.css)} stylesheets, served from "
          f"{site.baseurl or '/'} ({site.found_by}); {count} links and loads checked, "
          f"{len(site.sitemap)} of them sitemap.xml's")
    for problem in problems:
        print(problem)
    if problems:
        sys.exit(f"tools/check_site.py: {len(problems)} problem(s)")
    print("Nothing is loaded from another site, and every link into the site leads somewhere.")
    listed = ("sitemap.xml lists only the site's own files" if site.sitemap
              else "there's no sitemap.xml to check")
    described = sum(1 for page in site.pages.values() if page.tagline is not None)
    print(f"The data for search engines is JSON, {listed}, and each of the {described} pages with the "
          "site's header has a description of its own.")


if __name__ == "__main__":
    main()
