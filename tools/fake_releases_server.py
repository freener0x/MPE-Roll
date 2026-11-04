#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The MPE Roll contributors
# SPDX-License-Identifier: AGPL-3.0-only

"""A stand-in for GitHub's answers about MPE Roll's latest release, for the update check's test
(MpeRollHarness updatetest). Standard library only.

It listens on 127.0.0.1, over plain HTTP (MPE Roll's Linux build has no TLS), at a port the system
picks, and answers:

  GET /repos/freener0x/MPE-Roll/releases/latest   as GitHub's API does, or as --api says otherwise
  GET /freener0x/MPE-Roll/releases/latest         302 to .../releases/tag/<--page-tag>, as github.com does
  GET /freener0x/MPE-Roll/releases/tag/<tag>      a small page

Every request is added to --log as a line of JSON (method, path, the headers as [name, value]
pairs, in order and with any repeated, and "t", the seconds since the server started), before it's
answered, so a test can count the requests, see what came with them and when. The port is written
to --port-file once the server listens.

  --tag TAG          the latest release's tag in the API's answer (default v9.9.0)
  --page-tag TAG     the tag the release page redirects to (default: --tag)
  --api ANSWER       200 (default), 403 (GitHub's rate limit), 404, 500, prerelease, draft, html (a
                     web page in GitHub's place) or hang (no answer at all)
  --api-delay SECS   the API answers only after this long (default 0)
  --max-seconds N    how long it runs at most (default 300); it also stops once the process that
                     started it is gone (not on Windows, where the time limit does it)

Usage: python3 tools/fake_releases_server.py --port-file port.txt --log requests.jsonl [options]
"""

import argparse
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPOSITORY = "freener0x/MPE-Roll"
API_PATH = "/repos/" + REPOSITORY + "/releases/latest"
PAGE_PATH = "/" + REPOSITORY + "/releases/latest"
TAG_PATH = "/" + REPOSITORY + "/releases/tag/"


def release_answer(tag, port, prerelease=False, draft=False):
    """The API's answer for a release, with the fields MPE Roll reads and a few around them."""
    return {
        "url": "http://127.0.0.1:%d/repos/%s/releases/1" % (port, REPOSITORY),
        "html_url": "http://127.0.0.1:%d%s%s" % (port, TAG_PATH, tag),
        "id": 1,
        "tag_name": tag,
        "target_commitish": "main",
        "name": "MPE Roll " + tag.lstrip("v"),
        "draft": draft,
        "prerelease": prerelease,
        "created_at": "2026-11-01T11:00:00Z",
        "published_at": "2026-11-01T12:00:00Z",
        "assets": [],
        "body": "A release served by tools/fake_releases_server.py.",
    }


def main():
    parser = argparse.ArgumentParser(description="A stand-in for GitHub's latest-release answers.")
    parser.add_argument("--port-file", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--tag", default="v9.9.0")
    parser.add_argument("--page-tag")
    parser.add_argument("--api", default="200",
                        choices=["200", "403", "404", "500", "prerelease", "draft", "html", "hang"])
    parser.add_argument("--api-delay", type=float, default=0.0)
    parser.add_argument("--max-seconds", type=float, default=300.0)
    args = parser.parse_args()
    page_tag = args.page_tag or args.tag

    stopping = threading.Event()
    log_lock = threading.Lock()
    started = time.monotonic()

    class Handler(BaseHTTPRequestHandler):
        # HTTP/1.0: the connection closes after each answer, as JUCE's Linux networking expects.
        protocol_version = "HTTP/1.0"

        def log_message(self, format, *values):
            pass    # the requests go to --log

        def answer(self, status, body, content_type="application/json; charset=utf-8", headers=()):
            data = body.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            for name, value in headers:
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            # One write of the whole line, so a reader never meets half of one but at the end.
            line = json.dumps({"method": "GET", "path": self.path,
                               "headers": [[k, v] for k, v in self.headers.items()],
                               "t": round(time.monotonic() - started, 2)}) + "\n"
            with log_lock:
                with open(args.log, "a", encoding="utf-8") as log:
                    log.write(line)

            port = self.server.server_address[1]

            if self.path == API_PATH:
                self.answer_api(port)
            elif self.path == PAGE_PATH:
                location = "http://127.0.0.1:%d%s%s" % (port, TAG_PATH, page_tag)
                self.answer(302, '<html><body>You are being <a href="%s">redirected</a>.</body></html>' % location,
                            "text/html; charset=utf-8", [("Location", location)])
            elif self.path.startswith(TAG_PATH):
                self.answer(200, "<html><body>MPE Roll %s</body></html>" % self.path[len(TAG_PATH):],
                            "text/html; charset=utf-8")
            else:
                self.answer(404, json.dumps({"message": "Not Found"}))

        def answer_api(self, port):
            if args.api == "hang":
                stopping.wait(args.max_seconds)     # and then close without a word
                return

            if args.api_delay > 0:
                stopping.wait(args.api_delay)

            if args.api == "200":
                self.answer(200, json.dumps(release_answer(args.tag, port)))
            elif args.api == "prerelease":
                self.answer(200, json.dumps(release_answer(args.tag, port, prerelease=True)))
            elif args.api == "draft":
                self.answer(200, json.dumps(release_answer(args.tag, port, draft=True)))
            elif args.api == "403":
                message = ("API rate limit exceeded for 127.0.0.1. (But here's the good news: Authenticated "
                           "requests get a higher rate limit. Check out the documentation for more details.)")
                self.answer(403, json.dumps({"message": message, "documentation_url":
                                             "https://docs.github.com/rest/overview/rate-limits-for-the-rest-api"}),
                            headers=[("x-ratelimit-limit", "60"), ("x-ratelimit-remaining", "0"),
                                     ("x-ratelimit-reset", str(int(time.time()) + 3600))])
            elif args.api == "404":
                self.answer(404, json.dumps({"message": "Not Found", "status": "404"}))
            elif args.api == "500":
                self.answer(500, json.dumps({"message": "Server Error"}))
            else:
                self.answer(200, "<!DOCTYPE html><html><body>Sign in to the network to continue.</body></html>",
                            "text/html; charset=utf-8")

        def handle(self):
            # A client that gives up (MPE Roll calling a request off) leaves a closed connection behind.
            try:
                super().handle()
            except (ConnectionError, OSError):
                pass

    class Server(ThreadingHTTPServer):
        daemon_threads = True

    server = Server(("127.0.0.1", 0), Handler)
    port = server.server_address[1]

    # Written whole or not at all, for whoever waits for it.
    with open(args.port_file + ".tmp", "w", encoding="utf-8") as f:
        f.write("%d\n" % port)
    os.replace(args.port_file + ".tmp", args.port_file)
    print("Serving the latest release %s on http://127.0.0.1:%d" % (args.tag, port), flush=True)

    threading.Thread(target=server.serve_forever, daemon=True).start()

    parent = os.getppid()
    end = time.monotonic() + args.max_seconds

    while time.monotonic() < end and (os.name == "nt" or os.getppid() == parent):
        time.sleep(0.2)

    stopping.set()
    server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
