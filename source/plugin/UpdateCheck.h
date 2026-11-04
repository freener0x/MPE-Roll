// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>

/** The parts of the update check that need no window and no thread: reading version tags and
    GitHub's answers, what to ask and in what order, what an answer means for the running version,
    and when to ask again. UpdateChecker runs them on a thread of its own. Only juce_core, so the
    unit tests build them too; the running version is passed in.
*/
namespace mperoll::updates
{

/** A version as the release tags have it: "v0.2.0", or "v0.2.0-beta1" for a pre-release, by
    Semantic Versioning 2.0.0 (a leading v allowed, build metadata after a + read and ignored). */
struct Version
{
    int major = 0, minor = 0, patch = 0;
    juce::StringArray preRelease;       // "beta1"; "rc" and "1" for "-rc.1"; empty for a release

    /** Nothing for text that isn't one: "v1.2", "latest", "v1.2.3.4". */
    static std::optional<Version> parse (const juce::String& text);

    /** Below zero, zero or above zero as this version comes before the other, is the same, or comes
        after it, by Semantic Versioning's precedence: a pre-release before its release, numeric
        identifiers by value and before words, words in ASCII order, and more identifiers after
        fewer when the rest are equal. Build metadata plays no part. */
    int compare (const Version& other) const;

    bool isPreRelease() const noexcept      { return ! preRelease.isEmpty(); }

    /** "0.2.0", "0.2.0-beta1": as MPE Roll's own version reads, without the v. */
    juce::String toString() const;
};

/** The version a build runs as, which the check compares and names in its User-Agent: the project's
    version (CMakeLists.txt, "0.2.0"), or a pre-release's, "0.2.0-beta1". The release workflow
    makes a pre-release from the project's version and a tag with a suffix, v0.2.0-beta1, and names
    the build after the tag ("release v0.2.0-beta1, f2ea3c4, 2026-11-04"), which is where the
    suffix is read from. Any other build's name (a build between releases, a local one) leaves the
    project's version as it is. */
juce::String buildVersion (const juce::String& projectVersion, const juce::String& buildId);

/** The latest release, as GitHub names it. */
struct Release
{
    juce::String tag;           // "v0.2.0"
    juce::String pageUrl;       // its page, where the downloads are

    /** The tag as a version, "0.2.0". */
    juce::String version() const;
};

/** Reads the answer of GitHub's API for the latest release (GET /repos/<owner>/<repo>/releases/latest)
    for its tag. Nothing for anything else: an error such as the rate limit's, a draft, a pre-release,
    a tag that isn't a release's version, text that isn't JSON. The page address is left for
    findLatestRelease to fill in. */
std::optional<Release> parseLatestReleaseJson (const juce::String& json);

/** Reads where github.com/<owner>/<repo>/releases/latest sends the browser,
    ".../releases/tag/v0.2.0", for the tag. Nothing for any other address, or a pre-release's tag. */
std::optional<Release> releaseFromRedirect (const juce::String& location);

/** What the latest release means for the version running. */
enum class Outcome
{
    upToDate,   // it's the version running
    newer,      // it's newer: worth telling
    skipped,    // it's newer, but the user chose to skip it (or a later one)
    ahead       // the version running is newer still: a build between releases
};

/** Judges a release against the version running and the tag the user chose to skip, if any: a
    skipped version, or an older one, is no news until a newer one is out. */
Outcome judge (const Version& running, const Release& latest, const juce::String& skippedTag);

/** One day in milliseconds: the automatic check asks at most once in this long. */
inline constexpr juce::int64 checkIntervalMs = 24 * 60 * 60 * 1000;

/** True if an automatic check may ask now: there was no attempt before (lastAttemptMs 0 or less), a
    day has passed since the last one, or the clock has gone back past it. */
bool isDue (juce::int64 lastAttemptMs, juce::int64 nowMs);

/** Where to ask: GitHub's API for the latest release and, failing that, the latest release's page
    (none: the API alone). */
struct Endpoints
{
    juce::String api, page;

    /** GitHub's (Links.h). */
    static Endpoints defaults();

    /** The addresses in the environment variables MPEROLL_UPDATE_URL (the API) and
        MPEROLL_UPDATE_PAGE_URL (the page), where set, else the given ones. For tests, which serve
        the answers themselves. */
    static Endpoints fromEnvironment (const Endpoints& fallback);
};

/** The page of a release with this tag, beside the page asked (or MPE Roll's own on GitHub, with no
    page to ask): github.com/<owner>/<repo>/releases/tag/v0.2.0. */
juce::String tagPageUrl (const Endpoints&, const juce::String& tag);

/** "MPE Roll/0.1.0": the User-Agent of every request, which GitHub asks every program for. */
juce::String userAgent (const juce::String& version);

/** One GET request: the address, the headers to send (CRLF-separated) and how many redirects to
    follow. */
struct Request
{
    juce::String url;
    juce::String headers;
    int redirectsToFollow = 0;
};

/** What came back: the status (0 or less for no answer at all: offline, refused, timed out,
    called off), the headers and the body. */
struct Response
{
    int status = 0;
    juce::StringPairArray headers;
    juce::String body;
};

/** Makes a request and waits for the answer. */
using Get = std::function<Response (const Request&)>;

/** The latest release, or why there's none to tell of ("no answer from GitHub", "GitHub answered
    404"). */
struct Answer
{
    std::optional<Release> release;
    juce::String error;
};

/** Asks GitHub for the latest release with get: the API, and if that gives no release (no answer,
    an error such as its limit of requests from one address, a draft or a pre-release, something
    that isn't GitHub's answer), the release page, if there's one to ask, whose redirect names the
    latest release's tag.
    Each request carries the User-Agent, and the API's also the format of answer it's asked for:
    nothing else. The page address in the answer is always tagPageUrl's, so an answer can't send the
    browser anywhere else. */
Answer findLatestRelease (const Endpoints&, const juce::String& version, const Get& get);

} // namespace mperoll::updates
