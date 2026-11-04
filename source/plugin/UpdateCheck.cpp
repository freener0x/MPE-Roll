// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "UpdateCheck.h"
#include "Links.h"

#include <algorithm>

namespace mperoll::updates
{

namespace
{
    bool isNumber (const juce::String& s)
    {
        return s.isNotEmpty() && s.containsOnly ("0123456789");
    }

    /** A pre-release or build identifier: ASCII letters, digits and hyphens. */
    bool isIdentifier (const juce::String& s)
    {
        return s.isNotEmpty() && s.containsOnly ("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-");
    }

    /** Dot-separated identifiers, or nothing if one of them is empty or holds anything else. */
    std::optional<juce::StringArray> identifiers (const juce::String& text)
    {
        const auto parts = juce::StringArray::fromTokens (text, ".", "");

        if (parts.isEmpty() || ! std::all_of (parts.begin(), parts.end(), isIdentifier))
            return {};

        return parts;
    }

    int compareIdentifiers (const juce::String& a, const juce::String& b)
    {
        const auto aIsNumber = isNumber (a), bIsNumber = isNumber (b);

        // Numbers by value, however long: without leading zeros, the longer one is the larger.
        if (aIsNumber && bIsNumber)
        {
            const auto x = a.trimCharactersAtStart ("0"), y = b.trimCharactersAtStart ("0");

            if (x.length() != y.length())
                return x.length() < y.length() ? -1 : 1;

            return juce::jlimit (-1, 1, x.compare (y));
        }

        if (aIsNumber != bIsNumber)
            return aIsNumber ? -1 : 1;      // numbers come before words

        return juce::jlimit (-1, 1, a.compare (b));     // code points, which for these is ASCII order
    }

    /** Why a check found no release, in a few words for the status line and MPE settings: what the
        API said, which is what the check mostly goes by, or the page's when the API gave no answer. */
    juce::String describeFailure (const Response& api, const Response& page)
    {
        const auto& said = api.status > 0 ? api : page;

        if (said.status <= 0)
            return "no answer from GitHub";

        // Something on the network can answer in GitHub's place: a proxy that wants a sign-in, or a
        // network's own sign-in page (a hotel's, say), which comes back as a page with no release in it.
        if (said.status == 407)
            return "a proxy on the network asked for a sign-in";

        if (said.status == 200)
            return "the answer named no release";

        // GitHub's API answers a set number of requests an hour from one address (60, without an
        // account), and says in its headers when they're used up: named, so it isn't taken for a
        // fault. It lifts within the hour.
        if ((said.status == 403 || said.status == 429) && said.headers["X-RateLimit-Remaining"].trim() == "0")
        {
            const auto limit = said.headers["X-RateLimit-Limit"].trim();

            return "GitHub's limit of " + (isNumber (limit) ? limit + " " : juce::String()) + "requests an hour from your "
                   "network is used up (it answered " + juce::String (said.status) + ")";
        }

        return "GitHub answered " + juce::String (said.status);
    }

    // What GitHub's documentation recommends API callers send, besides the User-Agent it requires: the
    // format of the answer and the version of the API it's written for.
    constexpr const char* apiHeaders = "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28";
}

//==============================================================================
std::optional<Version> Version::parse (const juce::String& text)
{
    auto rest = text.trim();

    if (rest.startsWithChar ('v') || rest.startsWithChar ('V'))
        rest = rest.substring (1);

    // Build metadata, after a +, says nothing of which version is newer, but must be well formed.
    if (rest.containsChar ('+'))
    {
        if (! identifiers (rest.fromFirstOccurrenceOf ("+", false, false)).has_value())
            return {};

        rest = rest.upToFirstOccurrenceOf ("+", false, false);
    }

    Version version;

    // A pre-release, after the first hyphen (its identifiers may hold hyphens of their own).
    if (rest.containsChar ('-'))
    {
        const auto preRelease = identifiers (rest.fromFirstOccurrenceOf ("-", false, false));

        if (! preRelease.has_value())
            return {};

        version.preRelease = *preRelease;
        rest = rest.upToFirstOccurrenceOf ("-", false, false);
    }

    const auto numbers = juce::StringArray::fromTokens (rest, ".", "");

    if (numbers.size() != 3
        || ! std::all_of (numbers.begin(), numbers.end(), [] (const juce::String& n) { return isNumber (n) && n.length() <= 9; }))
        return {};

    version.major = numbers[0].getIntValue();
    version.minor = numbers[1].getIntValue();
    version.patch = numbers[2].getIntValue();
    return version;
}

int Version::compare (const Version& other) const
{
    for (const auto& [mine, theirs] : { std::pair { major, other.major }, std::pair { minor, other.minor },
                                        std::pair { patch, other.patch } })
        if (mine != theirs)
            return mine < theirs ? -1 : 1;

    // A pre-release comes before its release.
    if (isPreRelease() != other.isPreRelease())
        return isPreRelease() ? -1 : 1;

    for (int i = 0; i < juce::jmin (preRelease.size(), other.preRelease.size()); ++i)
        if (const auto order = compareIdentifiers (preRelease[i], other.preRelease[i]); order != 0)
            return order;

    if (preRelease.size() != other.preRelease.size())
        return preRelease.size() < other.preRelease.size() ? -1 : 1;

    return 0;
}

juce::String Version::toString() const
{
    return juce::String (major) + "." + juce::String (minor) + "." + juce::String (patch)
         + (isPreRelease() ? "-" + preRelease.joinIntoString (".") : juce::String());
}

juce::String buildVersion (const juce::String& projectVersion, const juce::String& buildId)
{
    const juce::String prefix ("release ");

    if (! buildId.startsWith (prefix))
        return projectVersion;

    const auto project = Version::parse (projectVersion);
    const auto tagged = Version::parse (buildId.substring (prefix.length()).upToFirstOccurrenceOf (",", false, false));

    // The tag's own version, if it's this one's with a suffix: a tag of another version, or one that
    // isn't a version, is no part of this build's.
    if (project.has_value() && tagged.has_value() && tagged->isPreRelease() && ! project->isPreRelease()
        && tagged->major == project->major && tagged->minor == project->minor && tagged->patch == project->patch)
        return tagged->toString();

    return projectVersion;
}

juce::String Release::version() const
{
    if (const auto v = Version::parse (tag))
        return v->toString();

    return tag;
}

//==============================================================================
std::optional<Release> parseLatestReleaseJson (const juce::String& json)
{
    juce::var answer;

    if (juce::JSON::parse (json, answer).failed() || ! answer.isObject())
        return {};

    // The API's latest release is neither a draft nor a pre-release, but whatever answers in its
    // place could say otherwise: neither is ever offered.
    if ((bool) answer.getProperty ("draft", false) || (bool) answer.getProperty ("prerelease", false))
        return {};

    const auto tagName = answer.getProperty ("tag_name", {});
    const auto version = Version::parse (tagName.toString());

    if (! tagName.isString() || ! version.has_value() || version->isPreRelease())
        return {};

    Release release;
    release.tag = tagName.toString().trim();
    return release;
}

std::optional<Release> releaseFromRedirect (const juce::String& location)
{
    const juce::String marker ("/releases/tag/");
    const auto at = location.indexOf (marker);

    if (at < 0)
        return {};

    const auto tag = location.substring (at + marker.length())
                             .upToFirstOccurrenceOf ("/", false, false)
                             .upToFirstOccurrenceOf ("?", false, false)
                             .upToFirstOccurrenceOf ("#", false, false);
    const auto version = Version::parse (tag);

    // The page leads to the latest release, never a pre-release; a tag with anything around it is
    // no tag of MPE Roll's.
    if (! version.has_value() || version->isPreRelease() || tag != tag.trim())
        return {};

    Release release;
    release.tag = tag;
    return release;
}

//==============================================================================
Outcome judge (const Version& running, const Release& latest, const juce::String& skippedTag)
{
    const auto version = Version::parse (latest.tag);

    if (! version.has_value())
        return Outcome::upToDate;       // nothing to tell of

    const auto order = version->compare (running);

    if (order == 0)
        return Outcome::upToDate;

    if (order < 0)
        return Outcome::ahead;

    // The skipped version is no news, nor anything up to it: only a newer one is.
    if (const auto skipped = Version::parse (skippedTag); skipped.has_value() && version->compare (*skipped) <= 0)
        return Outcome::skipped;

    return Outcome::newer;
}

bool isDue (juce::int64 lastAttemptMs, juce::int64 nowMs)
{
    return lastAttemptMs <= 0 || nowMs < lastAttemptMs || nowMs - lastAttemptMs >= checkIntervalMs;
}

//==============================================================================
Endpoints Endpoints::defaults()
{
    return { links::latestReleaseApi, links::latestRelease };
}

Endpoints Endpoints::fromEnvironment (const Endpoints& fallback)
{
    // Set but empty is the same as not set: that's how a test takes them back.
    const auto read = [] (const char* name, const juce::String& otherwise)
    {
        const auto value = juce::SystemStats::getEnvironmentVariable (name, {}).trim();
        return value.isNotEmpty() ? value : otherwise;
    };

    return { read ("MPEROLL_UPDATE_URL", fallback.api), read ("MPEROLL_UPDATE_PAGE_URL", fallback.page) };
}

juce::String tagPageUrl (const Endpoints& endpoints, const juce::String& tag)
{
    const juce::String releases ("/releases/");
    const auto base = endpoints.page.contains (releases) ? endpoints.page.upToLastOccurrenceOf (releases, true, false)
                                                         : juce::String (links::repository) + releases;
    return base + "tag/" + tag;
}

juce::String userAgent (const juce::String& version)
{
    return "MPE Roll/" + version;
}

Answer findLatestRelease (const Endpoints& endpoints, const juce::String& version, const Get& get)
{
    const auto agent = "User-Agent: " + userAgent (version);

    // Two redirects are followed, as GitHub sends for a repository that was renamed.
    const auto api = get ({ endpoints.api, agent + "\r\n" + apiHeaders, 2 });
    std::optional<Release> release;

    if (api.status == 200)
        release = parseLatestReleaseJson (api.body);

    // The API answers 60 requests an hour from one address, which a shared network can use up, and
    // a proxy may answer in GitHub's place. The release page isn't limited so, and its redirect
    // names the latest release's tag. Only the User-Agent goes with it, so it answers as it would
    // a browser. (No page address: the API alone is asked.)
    Response page;

    if (! release.has_value() && endpoints.page.isNotEmpty())
    {
        page = get ({ endpoints.page, agent, 0 });

        if (page.status >= 300 && page.status < 400)
            release = releaseFromRedirect (page.headers["Location"]);
    }

    if (! release.has_value())
        return { std::nullopt, describeFailure (api, page) };

    release->pageUrl = tagPageUrl (endpoints, release->tag);
    return { release, {} };
}

} // namespace mperoll::updates
