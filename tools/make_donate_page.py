#!/usr/bin/env python3
"""Makes MPE Roll's donation page from docs/donate.json.

It writes, all of them committed so nobody needs this script to read them:

  DONATE.md            the page GitHub shows: links, QR codes as images, addresses in code blocks
  docs/donate.html     the same page as one self-contained file, with copy buttons; it works from
                       disk and on GitHub Pages, and loads nothing from anywhere else
  docs/index.html      the front page of the GitHub Pages site (docs/ on the default branch)
  docs/donate/*.svg    the QR codes (a wallet URI such as bitcoin:<address>)
  .github/FUNDING.yml  the links behind the Sponsor button on the GitHub page

Only plain links and QR codes: no payment widget or third-party script is ever involved, so the
page is as private as the plugin. Empty values in donate.json are left out. "pageUrl" is where
GitHub Pages serves docs/donate.html; the plugin opens the same address from source/plugin/Links.h,
and this script stops if the two differ.

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
REPO_URL = "https://github.com/freener0x/glowing-pancake"
ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "docs" / "donate.json"
QR_DIR = ROOT / "docs" / "donate"
HTML_OUT = ROOT / "docs" / "donate.html"
INDEX_OUT = ROOT / "docs" / "index.html"
MD_OUT = ROOT / "DONATE.md"
FUNDING_OUT = ROOT / ".github" / "FUNDING.yml"
LINKS_HEADER = ROOT / "source" / "plugin" / "Links.h"
DONATE_MD_URL = f"{REPO_URL}/blob/main/DONATE.md"
FL_GUIDE_URL = f"{REPO_URL}/blob/main/docs/FL_STUDIO.md"

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
    """Where the donation page is served, which the plugin must agree on; DONATE.md on GitHub if unset."""
    url = clean(config.get("pageUrl"))
    if not url:
        return DONATE_MD_URL
    if not url.startswith("https://"):
        fail('"pageUrl" must be an https:// link')
    if LINKS_HEADER.exists() and f'"{url}"' not in LINKS_HEADER.read_text(encoding="utf-8"):
        fail(f'"pageUrl" is {url}, but {LINKS_HEADER.relative_to(ROOT).as_posix()} opens another address; '
             "change both")
    return url


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


# ---------------------------------------------------------------- docs/donate.html

CSS = """\
:root {
  color-scheme: dark;
  --bg: #0f1114; --panel-2: #1f232a; --panel-3: #2a2f38; --line: #2a2f37; --line-strong: #3a414c;
  --text: #e8eaed; --text-2: #b4bac4; --muted: #858d9a;
  --accent: #ffab2e; --accent-ink: #1b1204; --focus: #79c0ff;
  --radius-sm: 4px;
  --font-ui: system-ui, -apple-system, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
  --font-mono: ui-monospace, 'SF Mono', 'Cascadia Mono', 'Segoe UI Mono', Menlo, Consolas, 'Liberation Mono', monospace;
}
@media (prefers-color-scheme: light) {
  :root {
    color-scheme: light;
    --bg: #fff4e0; --panel-2: #ffefd4; --panel-3: #ffe3b8; --line: #f1d6ad; --line-strong: #e2b97c;
    --text: #3b2210; --text-2: #694529; --muted: #8d6a4b;
    --accent: #e67700; --accent-ink: #ffffff; --focus: #1c7ed6;
  }
}
*, *::before, *::after { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; background: var(--bg); color: var(--text); font: 16px/1.55 var(--font-ui); }
main { max-width: 720px; margin: 0 auto; padding: 40px 16px 56px; }
h1 { font-size: 1.9rem; line-height: 1.2; margin: 0 0 10px; }
section { margin-top: 24px; }
h2 { font-size: 1.15rem; margin: 34px 0 12px; }
h3 { font-size: 1rem; margin: 0 0 6px; }
p { margin: 10px 0 0; }
.lead { font-size: 1.08rem; color: var(--text-2); }
.muted { color: var(--muted); }
.small { font-size: 0.82rem; }
a { color: var(--accent); }
:focus-visible { outline: 2px solid var(--focus); outline-offset: 2px; }
.donate-links { list-style: none; padding: 0; margin: 0; display: flex; flex-direction: column; gap: 10px; }
.donate-links li { display: flex; flex-wrap: wrap; align-items: center; gap: 6px 12px; }
.button-link {
  display: inline-block; padding: 9px 16px; border-radius: var(--radius-sm);
  background: var(--accent); color: var(--accent-ink); font-weight: 600; text-decoration: none;
}
.button-link:hover { filter: brightness(1.08); }
.coins { display: flex; flex-direction: column; gap: 16px; }
.coin { display: flex; flex-wrap: wrap; gap: 14px; align-items: flex-start; }
.coin img { width: 160px; height: 160px; background: #fff; border-radius: var(--radius-sm); }
.coin > div { flex: 1 1 240px; min-width: 0; }
.address {
  display: block; max-width: 420px; overflow-wrap: anywhere; margin: 0 0 8px;
  font-family: var(--font-mono); font-size: 0.88em; background: var(--panel-2); padding: 4px 8px; border-radius: 4px;
}
button {
  font: inherit; color: var(--text); padding: 7px 12px; border: 1px solid var(--line-strong);
  border-radius: var(--radius-sm); background: var(--panel-2); cursor: pointer; white-space: nowrap; line-height: 1.3;
}
button:hover { background: var(--panel-3); }
footer { margin-top: 44px; padding-top: 16px; border-top: 1px solid var(--line); }
footer a + a { margin-left: 14px; }
"""

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


def render_html(links: list[dict], coins: list[dict], svgs: dict[str, bytes]) -> str:
    e = html.escape
    out = [
        "<!doctype html>",
        '<html lang="en">',
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        '<meta name="color-scheme" content="dark light">',
        f"<title>Support {e(PROJECT)}</title>",
        "<!-- Made by tools/make_donate_page.py from docs/donate.json. Change those, not this file. -->",
        "<style>",
        CSS.rstrip("\n"),
        "</style>",
        "</head>",
        "<body>",
        "<main>",
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
    out += [
        '<footer class="muted small">',
        f'<a href="{e(REPO_URL)}">{e(PROJECT)} on GitHub</a>',
        f'<a href="{e(REPO_URL)}/blob/main/LICENSE">Source code (AGPL-3.0)</a>',
        "</footer>",
        "</main>",
        "<script>",
        SCRIPT.rstrip("\n"),
        "</script>",
        "</body>",
        "</html>",
    ]
    return "\n".join(out) + "\n"


def render_index(url: str) -> str:
    """The front page of the Pages site: where to read, download and donate."""
    e = html.escape
    out = [
        "<!doctype html>",
        '<html lang="en">',
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        '<meta name="color-scheme" content="dark light">',
        f"<title>{e(PROJECT)}</title>",
        "<!-- Made by tools/make_donate_page.py. Change the script, not this file. -->",
        "<style>",
        CSS.rstrip("\n"),
        "</style>",
        "</head>",
        "<body>",
        "<main>",
        f"<h1>{e(PROJECT)}</h1>",
        '<p class="lead">An MPE piano roll for FL Studio. Load your synth inside it, and every note gets its own '
        "pitch glide, pressure and timbre. Free and open source.</p>",
        "<section>",
        '<ul class="donate-links">',
        f'<li><a class="button-link" href="{e(REPO_URL)}">{e(PROJECT)} on GitHub</a>'
        ' <span class="muted small">What it does, downloads, and the source code.</span></li>',
        f'<li><a class="button-link" href="{e(FL_GUIDE_URL)}">Using it in FL Studio</a>'
        ' <span class="muted small">Setup, routing, the native-synth bridge and troubleshooting.</span></li>',
        f'<li><a class="button-link" href="{e(url.rsplit("/", 1)[-1])}">Support {e(PROJECT)}</a>'
        ' <span class="muted small">Cash App, or Bitcoin, Ethereum, Solana and Monero by QR code.</span></li>',
        "</ul>",
        "</section>",
        '<footer class="muted small">',
        f'<a href="{e(REPO_URL)}/blob/main/LICENSE">Source code (AGPL-3.0)</a>',
        "</footer>",
        "</main>",
        "</body>",
        "</html>",
    ]
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
    if url != DONATE_MD_URL:
        out += [f"This page is also online, with copy buttons, at <{url}> (the plugin's *File > Support MPE "
                "Roll* opens it).", ""]
    out += ["The pages, the QR codes and `.github/FUNDING.yml` are made by `tools/make_donate_page.py` from "
            "[docs/donate.json](docs/donate.json): change the JSON and run the script rather than editing them.",
            ""]
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


def main() -> None:
    config = load_config()
    url = page_url(config)
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
    write(INDEX_OUT, render_index(url))
    write(MD_OUT, render_markdown(links, coins, url))
    write(FUNDING_OUT, render_funding(links, url))
    print(f"{len(links)} payment link(s), {len(coins)} crypto address(es)")


if __name__ == "__main__":
    main()
