#!/usr/bin/env python3
"""Makes MPE Roll's donation page from docs/donate.json, and the website pages that go with it.

It writes, all of them committed so nobody needs this script to read them:

  DONATE.md            the page GitHub shows: links, QR codes as images, addresses in code blocks
  docs/donate.html     the same page on the website (GitHub Pages builds docs/ with Jekyll, in the
                       Cayman theme), with copy buttons and the QR codes inside it
  docs/index.html      the website's front page
  docs/_data/project.yml
                       the links into the repository that the website's header and footer show
  docs/donate/*.svg    the QR codes (a wallet URI such as bitcoin:<address>)
  .github/FUNDING.yml  the links behind the Sponsor button on the GitHub page
  source/plugin/ui/DonateData.h
                       the same details and QR codes for the plugin, which shows them in a window
                       of its own (File > Support MPE Roll), so it needs no website at all

Only plain links and QR codes: no payment widget or third-party script is ever involved, and the
website loads nothing from other sites, so the page is as private as the plugin. Empty values in
donate.json are left out. "pageUrl" is where the website serves docs/donate.html. "repoPublic":
false while the repository is private, so the website links to nothing in it (GitHub would show
visitors a 404); true once it's public.

Needs the segno package (pip install segno). Run it from anywhere:

    python3 tools/make_donate_page.py
"""

from __future__ import annotations

import base64
import html
import io
import json
import re
import sys
from pathlib import Path
from urllib.parse import quote

try:
    import segno
except ImportError:  # pragma: no cover - depends on the machine
    sys.exit("This script needs the segno package: pip install segno")

PROJECT = "MPE Roll"
REPO_URL = "https://github.com/freener0x/mpe-roll"
ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "docs" / "donate.json"
QR_DIR = ROOT / "docs" / "donate"
HTML_OUT = ROOT / "docs" / "donate.html"
INDEX_OUT = ROOT / "docs" / "index.html"
PROJECT_DATA_OUT = ROOT / "docs" / "_data" / "project.yml"
MD_OUT = ROOT / "DONATE.md"
FUNDING_OUT = ROOT / ".github" / "FUNDING.yml"
PLUGIN_DATA_OUT = ROOT / "source" / "plugin" / "ui" / "DonateData.h"
DONATE_MD_URL = f"{REPO_URL}/blob/main/DONATE.md"
DOWNLOAD_URL = f"{REPO_URL}#downloading-a-build"
LICENSE_URL = f"{REPO_URL}/blob/main/LICENSE"

ADDRESS = re.compile(r"^[A-Za-z0-9:]{20,120}$")

LEAD = (f"{PROJECT} is free and open source, with no ads and no tracking. "
        "If it helps you, a donation keeps it going.")
COMING_SOON = "Donation links are coming soon."

# Coin id -> (name, the URI scheme wallets understand in a QR code)
COINS = {
    "BTC": ("Bitcoin (BTC)", "bitcoin"),
    "ETH": ("Ethereum (ETH)", "ethereum"),
    "SOL": ("Solana (SOL)", "solana"),
    "XMR": ("Monero (XMR)", "monero"),
}


def fail(message: str) -> None:
    sys.exit(f"{CONFIG.relative_to(ROOT)}: {message}")


def clean(value) -> str:
    return value.strip() if isinstance(value, str) else ""


def load_config() -> dict:
    try:
        config = json.loads(CONFIG.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(f"can't be read: {error}")
    if not isinstance(config, dict):
        fail("must hold a JSON object")
    if not isinstance(config.get("crypto", {}), dict):
        fail('"crypto" must be an object of coin: address')
    return config


def page_url(config: dict) -> str:
    """Where the donation page is served; DONATE.md on GitHub if unset."""
    url = clean(config.get("pageUrl"))
    if not url:
        return DONATE_MD_URL
    if not url.startswith("https://"):
        fail('"pageUrl" must be an https:// link')
    return url


def repo_is_public(config: dict) -> bool:
    """Whether visitors can open the repository. GitHub answers 404 to anyone who can't, so while
    it's private the web pages link to nothing in it."""
    public = config.get("repoPublic", True)
    if not isinstance(public, bool):
        fail('"repoPublic" must be true or false')
    return public


def build_links(config: dict) -> list[dict]:
    """The payment links, in the order they're shown."""
    links = []
    cash = clean(config.get("cashApp")).lstrip("$")
    if cash:
        links.append(dict(id="cashapp", name="Cash App", url="https://cash.app/$" + quote(cash, safe="")))
    paypal = clean(config.get("paypal"))
    if paypal:
        links.append(dict(id="paypal", name="PayPal", url="https://paypal.me/" + quote(paypal, safe="")))
    checkout = clean(config.get("checkout"))
    if checkout:
        if not checkout.startswith("https://"):
            fail('"checkout" must be an https:// link')
        links.append(dict(id="checkout", name="Card or Apple Pay", url=checkout,
                          note="A secure checkout page (cards, Apple Pay, Google Pay)."))
    sponsors = clean(config.get("githubSponsors"))
    if sponsors:
        links.append(dict(id="github", name="GitHub Sponsors",
                          url="https://github.com/sponsors/" + quote(sponsors, safe="")))
    libera = clean(config.get("liberapay"))
    if libera:
        links.append(dict(id="liberapay", name="Liberapay", url="https://liberapay.com/" + quote(libera, safe="")))
    return links


def build_coins(config: dict) -> list[dict]:
    """The crypto addresses, each with the text its QR code carries."""
    crypto = config.get("crypto") or {}
    coins = []
    for coin_id, (name, scheme) in COINS.items():
        address = clean(crypto.get(coin_id))
        if not address:
            continue
        if not ADDRESS.match(address):
            fail(f"the {coin_id} address doesn't look right")
        note = None
        if coin_id == "ETH":
            network = clean(config.get("ethNetwork")) or "Ethereum mainnet"
            note = f"Network: {network} only. Sending on another network can lose the money."
        elif coin_id == "XMR":
            note = "Private by design: your donation stays private too."
        coins.append(dict(id=coin_id, name=name, address=address, uri=f"{scheme}:{address}", note=note,
                          alt=f"QR code for the {name} address", file=QR_DIR / f"{coin_id.lower()}.svg"))
    return coins


def qr_svg(coin: dict) -> bytes:
    """A QR code as a scalable SVG with a quiet zone, black on white (readable on a dark page too)."""
    qr = segno.make(coin["uri"], error="m", micro=False)
    out = io.BytesIO()
    qr.save(out, kind="svg", border=2, dark="#000000", light="#ffffff", omitsize=True,
            svgclass=None, lineclass=None, xmldecl=True, nl=True, title=coin["alt"])
    return out.getvalue()


# ---------------------------------------------------------------- the website: docs/donate.html, docs/index.html

# Pages of the website. Jekyll puts each in docs/_layouts/default.html (Cayman's layout, with the
# header and footer), and docs/assets/css/style.scss styles the classes used here.

SCRIPT = """\
(function () {
  var buttons = document.querySelectorAll('button[data-copy]');
  Array.prototype.forEach.call(buttons, function (button) {
    button.addEventListener('click', function () {
      var text = button.getAttribute('data-copy');
      var done = function () {
        button.textContent = 'Copied \\u2713';
        setTimeout(function () { button.textContent = 'Copy address'; }, 2000);
      };
      var selectInstead = function () {
        var code = document.getElementById(button.getAttribute('data-target'));
        if (!code || !window.getSelection) return;
        var range = document.createRange();
        range.selectNodeContents(code);
        var selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
      };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(done, selectInstead);
      } else {
        selectInstead();
      }
    });
  });
})();
"""


def front_matter(**fields: str) -> list[str]:
    """A website page's front matter (a JSON string is a YAML string too), and where it comes from."""
    return (["---", "layout: default"] + [f"{key}: {json.dumps(value)}" for key, value in fields.items()]
            + ["---", "<!-- Made by tools/make_donate_page.py from docs/donate.json. Change those, not this file. -->"])


def ways_to_give(links: list[dict], coins: list[dict]) -> str:
    """"Cash App, or Bitcoin, Ethereum, Solana and Monero by QR code": what donate.json has."""
    def listed(names: list[str]) -> str:
        return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]

    ways = []
    if links:
        ways.append(listed([link["name"] for link in links]))
    if coins:
        ways.append(listed([coin["name"].split(" (")[0] for coin in coins]) + " by QR code")
    return ", or ".join(ways)


def render_html(links: list[dict], coins: list[dict], svgs: dict[str, bytes]) -> str:
    e = html.escape
    out = front_matter(title=f"Support {PROJECT}", description=LEAD) + [
        f"<h1>Support {e(PROJECT)}</h1>",
        f'<p class="lead">{e(LEAD)}</p>',
    ]
    if not links and not coins:
        out.append(f"<section><p>{e(COMING_SOON)}</p></section>")
    if links:
        out += ["<section>", "<h2>Send a donation</h2>", '<ul class="donate-links">']
        for link in links:
            item = (f'<li><a class="button-link" href="{e(link["url"])}" rel="noopener noreferrer" '
                    f'target="_blank">{e(link["name"])}</a>')
            if link.get("note"):
                item += f' <span class="muted small">{e(link["note"])}</span>'
            out.append(item + "</li>")
        out += ["</ul>",
                '<p class="muted small">These open the payment site in a new tab; nothing from this page is '
                "sent with them.</p>",
                "</section>"]
    if coins:
        out += ["<section>", "<h2>Crypto</h2>", '<div class="coins">']
        for coin in coins:
            data = base64.b64encode(svgs[coin["id"]]).decode("ascii")
            code_id = f"address-{coin['id'].lower()}"
            out += [
                '<div class="coin">',
                f'<img src="data:image/svg+xml;base64,{data}" alt="{e(coin["alt"])}" width="160" height="160">',
                "<div>",
                f"<h3>{e(coin['name'])}</h3>",
                f'<code class="address" id="{code_id}">{e(coin["address"])}</code>',
                f'<button type="button" data-copy="{e(coin["address"])}" data-target="{code_id}">Copy address</button>',
            ]
            if coin["note"]:
                out.append(f'<p class="muted small">{e(coin["note"])}</p>')
            out += ["</div>", "</div>"]
        out += ["</div>", "</section>"]
    out += ["<script>", SCRIPT.rstrip("\n"), "</script>"]
    return "\n".join(out) + "\n"


def render_index(links: list[dict], coins: list[dict], public: bool) -> str:
    """The website's front page: what MPE Roll is, where to get it (not while the repository is
    private), the guides, and donating. The header above it has the name and the tagline."""
    e = html.escape
    out = front_matter() + [
        f'<p class="lead">{e(PROJECT)} is an instrument plugin for FL Studio with its own piano roll: VST3, plus '
        "VST2 on Windows, AU on macOS and a standalone app. You load your synth inside it (VST3, VST2 or CLAP), "
        "so every note gets its own pitch glide, pressure, timbre and controllers. FL Studio's own piano roll "
        "can't send per-note expression to plugins.</p>",
        f"<p>For FL's own synths (FLEX, Sytrus, Harmor, 3xOsc...), which can't receive MPE at all, {e(PROJECT)} "
        "writes a piano roll script that rebuilds the clip in FL's own piano roll with slide notes, fine pitch "
        "and Mod X / Mod Y.</p>",
        f'<p><img src="images/editor.png" alt="{e(PROJECT)}&#39;s editor: notes with their glides drawn over '
        'them, and a pressure lane under the piano roll"></p>',
    ]
    if public:
        out += ["<h2>Get it</h2>",
                "<p>Every change is built for Windows (an installer, or the files to copy by hand) and for macOS: "
                f'<a href="{e(DOWNLOAD_URL)}">Downloading a build</a> says where to find them. {e(PROJECT)} is an '
                "early version: automated tests cover it on Windows, macOS and Linux, and it has been lightly "
                "tested inside FL Studio so far.</p>"]
    out += ["<h2>Guides</h2>",
            "<ul>",
            f'<li><a href="FL_STUDIO.html">Using {e(PROJECT)} in FL Studio</a>: installing it, loading your synth, '
            "routing, the bridge for FL's own synths, and what to do when something doesn't work.</li>",
            '<li><a href="MPE_SYNTHS.html">Synths and their MPE</a>: instruments by maker, whether each has MPE, '
            "and where to turn it on.</li>",
            "</ul>",
            f"<h2>Support {e(PROJECT)}</h2>"]
    ways = ways_to_give(links, coins)
    out.append(f'<p>{e(LEAD)} <a href="donate.html">Donate</a>: {e(ways)}.</p>' if ways
               else f"<p>{e(COMING_SOON)}</p>")
    return "\n".join(out) + "\n"


def render_project_data(public: bool) -> str:
    """docs/_data/project.yml: the repository's links in the website's header and footer."""
    out = ["# Made by tools/make_donate_page.py from docs/donate.json. Change those, not this file.",
           "# The links into the repository that the website's header and footer show, none while it's private.",
           f"repo_public: {json.dumps(public)}"]
    if public:
        out += [f"repo_url: {json.dumps(REPO_URL)}",
                f"download_url: {json.dumps(DOWNLOAD_URL)}",
                f"license_url: {json.dumps(LICENSE_URL)}"]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- DONATE.md

def render_markdown(links: list[dict], coins: list[dict], url: str) -> str:
    out = [f"# Support {PROJECT}", "", LEAD, ""]
    if not links and not coins:
        out += [COMING_SOON, ""]
    if links:
        out += ["## Send a donation", ""]
        for link in links:
            line = f"- **[{link['name']}]({link['url']})**"
            if link.get("note"):
                line += f" {link['note']}"
            out.append(line)
        out += ["", f"These open the payment site; nothing from {PROJECT} is sent with them.", ""]
    if coins:
        out += ["## Crypto", "",
                "Scan a code with your wallet, or copy the address from the box under it.", ""]
        for coin in coins:
            rel = coin["file"].relative_to(ROOT).as_posix()
            out += [f"### {coin['name']}", "",
                    f'<img src="{rel}" alt="{html.escape(coin["alt"])}" width="160" height="160">', "",
                    "```text", coin["address"], "```", ""]
            if coin["note"]:
                out += [coin["note"], ""]
    out += ["---", ""]
    out += [f"In the plugin, *File > Support MPE Roll* shows the same details, QR codes and copy buttons included, "
            "in a window of its own.", ""]
    if url != DONATE_MD_URL:
        out += [f"This page is also on {PROJECT}'s website: <{url}>.", ""]
    out += ["The pages, the plugin's copy, the QR codes and `.github/FUNDING.yml` are made by "
            "`tools/make_donate_page.py` from [docs/donate.json](docs/donate.json): change the JSON and run the "
            "script rather than editing them.", ""]
    return "\n".join(out)


# ---------------------------------------------------------------- .github/FUNDING.yml

def render_funding(links: list[dict], url: str) -> str:
    """GitHub's Sponsor button: it only takes links, so the crypto addresses stay on the donation page."""
    out = ["# The Sponsor button on the GitHub page. Made by tools/make_donate_page.py from docs/donate.json;",
           "# the button only takes links, so the crypto addresses are on the donation page."]
    sponsors = [link for link in links if link["id"] == "github"]
    if sponsors:
        out.append("github: " + json.dumps([sponsors[0]["url"].rsplit("/", 1)[1]]))
    libera = [link for link in links if link["id"] == "liberapay"]
    if libera:
        out.append("liberapay: " + libera[0]["url"].rsplit("/", 1)[1])
    custom = [link["url"] for link in links if link["id"] not in ("github", "liberapay")]
    custom = (custom + [url])[:4]  # GitHub allows four custom links
    out.append("custom: " + json.dumps(custom))
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- main

def write(path: Path, data: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode("utf-8")
    path.write_bytes(data)
    print(f"wrote {path.relative_to(ROOT).as_posix()}")


# ---------------------------------------------------------------- source/plugin/ui/DonateData.h

def cpp_string(text: str) -> str:
    """A C++ string literal; the text must be plain ASCII, so it reads the same in every compiler."""
    if not text.isascii():
        fail(f"the plugin can only show plain ASCII text, which this isn't: {text!r}")
    return json.dumps(text)


def render_plugin_data(links: list[dict], coins: list[dict], svgs: dict[str, bytes]) -> str:
    out = [
        "#pragma once",
        "",
        "// Made by tools/make_donate_page.py from docs/donate.json. Change those, not this file.",
        "// The plugin's own copy of the donation details, shown by File > Support MPE Roll without the website.",
        "",
        "#include <array>",
        "",
        "namespace mperoll::donate",
        "{",
        "    struct Link { const char* name; const char* url; const char* note; };",
        "    struct Coin { const char* id; const char* name; const char* address; const char* note; const char* qrSvg; };",
        "",
        f"    inline constexpr const char* lead = {cpp_string(LEAD)};",
        "",
        f"    inline constexpr std::array<Link, {len(links)}> links {{ {{",
    ]
    for link in links:
        out.append(f"        {{ {cpp_string(link['name'])}, {cpp_string(link['url'])}, {cpp_string(link.get('note') or '')} }},")
    out += ["    } };", "", f"    inline constexpr std::array<Coin, {len(coins)}> coins {{ {{"]
    for coin in coins:
        svg = svgs[coin["id"]].decode("ascii")
        if ")svg\"" in svg:
            fail("a QR code's drawing can't be put in a raw string")
        out += [f"        {{ {cpp_string(coin['id'])}, {cpp_string(coin['name'])}, {cpp_string(coin['address'])}, "
                f"{cpp_string(coin['note'] or '')},",
                f"          R\"svg({svg.strip()})svg\" }},"]
    out += ["    } };", "}", ""]
    return "\n".join(out)


def main() -> None:
    config = load_config()
    url = page_url(config)
    public = repo_is_public(config)
    links = build_links(config)
    coins = build_coins(config)

    QR_DIR.mkdir(parents=True, exist_ok=True)
    for stale in QR_DIR.glob("*.svg"):
        stale.unlink()
    svgs = {}
    for coin in coins:
        svgs[coin["id"]] = qr_svg(coin)
        write(coin["file"], svgs[coin["id"]])

    write(HTML_OUT, render_html(links, coins, svgs))
    write(INDEX_OUT, render_index(links, coins, public))
    write(PROJECT_DATA_OUT, render_project_data(public))
    write(MD_OUT, render_markdown(links, coins, url))
    write(FUNDING_OUT, render_funding(links, url))
    write(PLUGIN_DATA_OUT, render_plugin_data(links, coins, svgs))
    print(f"{len(links)} payment link(s), {len(coins)} crypto address(es)")


if __name__ == "__main__":
    main()
