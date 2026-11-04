#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The MPE Roll contributors
# SPDX-License-Identifier: AGPL-3.0-only

"""Checks MPE Roll's website as Jekyll built it (the _site folder): that it loads nothing from other
sites, and that nothing in it points at a file or a place on a page that isn't there.

    python3 tools/check_site.py <built site> [--baseurl /MPE-Roll]

It reads every .html and .css file in the folder, and fails, with a line for each, on:

  - anything a page loads from another site: a picture, a clip and its poster, a script, a frame, a
    stylesheet, an icon or a font (src, srcset, poster, an <object>'s data, a <link> that loads, a
    url() or @import in a stylesheet, a <style> or a style attribute). The website loads nothing from
    anywhere else (docs/_config.yml). Links to other sites (<a href>, <link rel="canonical">) are
    fine: they load nothing until they're clicked.
  - anything that points into the site, as a link or as something loaded (a relative path, a path
    from the site's root, or the site's own address), at a file the site doesn't have, at a
    #fragment no element of that page has as its id, or at a path from the root outside the
    folder the site is served from (its baseurl), which on GitHub Pages is another site's.

GitHub Pages serves the site from https://freener0x.github.io/MPE-Roll/, so its pages' paths from
the root start with /MPE-Roll when Jekyll knew that (GitHub's build does; one at the root has no
baseurl). The baseurl is read from the front page: its <link rel="canonical"> (jekyll-seo-tag,
when the site's url is known), or else where its stylesheet is; --baseurl says it instead.

Needs only Python 3. CI runs it on the site it builds (.github/workflows/build.yml, the Website job).
"""

from __future__ import annotations

import argparse
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

        if "style" in a:
            for url in css_urls(a["style"]):
                self.add(f"<{tag} style>", url, True)
        if tag == "style":
            self.in_style = True
            self.style_line = self.getpos()[0]

    def handle_endtag(self, tag: str) -> None:
        if tag == "style":
            self.in_style = False

    def handle_data(self, data: str) -> None:
        if self.in_style:
            for url in css_urls(data):
                self.page.refs.append(Ref(self.style_line, "<style>", url, True))


def read_page(path: Path) -> Page:
    reader = PageReader()
    reader.feed(path.read_text(encoding="utf-8", errors="replace"))
    reader.close()
    return reader.page


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
        if "index.html" not in self.pages:
            sys.exit(f"tools/check_site.py: {root} has no index.html: is it a site Jekyll built?")
        self.host: str | None = None     # the site's own host name, when the front page says it
        if baseurl is not None:
            self.baseurl, self.found_by = baseurl.rstrip("/"), "--baseurl"
        else:
            self.baseurl, self.found_by = self.find_baseurl()

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

    def problems(self) -> tuple[list[str], int]:
        found = []
        count = 0
        everything = [(source, page.refs) for source, page in self.pages.items()] + list(self.css.items())
        for source, refs in everything:
            for ref in refs:
                count += 1
                where = f"{source}:{ref.line}: {ref.what} {ref.url}"
                t = self.target(source, ref.url)
                if t.kind == "elsewhere" and ref.loads:
                    found.append(f"{where}: loaded from another site")
                elif t.kind == "wrong":
                    found.append(f"{where}: {t.why}")
                elif t.kind != "here":
                    continue
                elif not self.is_file(t.path):
                    found.append(f"{where}: the site has no {t.path}")
                elif (t.fragment is not None and t.fragment not in ALWAYS_THERE and t.path in self.pages
                      and t.fragment not in self.pages[t.path].ids):
                    found.append(f"{where}: {t.path} has no element with the id {t.fragment!r}")
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
          f"{site.baseurl or '/'} ({site.found_by}); {count} links and loads checked")
    for problem in problems:
        print(problem)
    if problems:
        sys.exit(f"tools/check_site.py: {len(problems)} problem(s)")
    print("Nothing is loaded from another site, and every link into the site leads somewhere.")


if __name__ == "__main__":
    main()
