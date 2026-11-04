#include "plugin/Links.h"
#include "plugin/UpdateCheck.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <vector>

namespace mperoll
{

class UpdateCheckTests : public juce::UnitTest
{
public:
    UpdateCheckTests() : juce::UnitTest ("UpdateCheck", "MPE Roll") {}

    void runTest() override
    {
        using namespace updates;

        const auto parse = [] (const char* text) { return Version::parse (text); };
        const auto version = [] (const char* text) { return Version::parse (text).value_or (Version { -1, -1, -1, {} }); };
        const auto text = [] (const std::optional<Version>& v) { return v.has_value() ? v->toString() : juce::String ("(none)"); };
        const auto release = [] (const char* tag)
        {
            Release r;
            r.tag = tag;
            return r;
        };

        beginTest ("Version tags parse");
        {
            const auto v = parse ("v0.1.0");
            expect (v.has_value() && v->major == 0 && v->minor == 1 && v->patch == 0 && ! v->isPreRelease());
            expectEquals (text (v), juce::String ("0.1.0"));

            expectEquals (text (parse ("0.1.0")), juce::String ("0.1.0"), "without the v");
            expect (parse ("V1.2.3").has_value() && parse ("V1.2.3")->major == 1 && parse ("V1.2.3")->patch == 3);
            expectEquals (text (parse (" v0.1.0\n")), juce::String ("0.1.0"), "space around it is no matter");
            expect (parse ("10.20.30").has_value() && parse ("10.20.30")->minor == 20);

            const auto beta = parse ("v0.2.0-beta1");
            expect (beta.has_value() && beta->isPreRelease() && beta->preRelease == juce::StringArray ("beta1"));
            expectEquals (text (beta), juce::String ("0.2.0-beta1"));

            const auto rc = parse ("v1.0.0-rc.1");
            expect (rc.has_value() && rc->preRelease == juce::StringArray ("rc", "1"));
            expectEquals (text (rc), juce::String ("1.0.0-rc.1"));

            const auto built = parse ("v1.0.0+build.5");
            expect (built.has_value() && ! built->isPreRelease(), "build metadata is read");
            expectEquals (text (built), juce::String ("1.0.0"), "and dropped");
            expectEquals (text (parse ("1.0.0-beta+exp.sha.5114f85")), juce::String ("1.0.0-beta"));
            expect (parse ("1.0.0-x-y-z.--").has_value() && parse ("1.0.0-x-y-z.--")->preRelease == juce::StringArray ("x-y-z", "--"),
                    "hyphens inside a pre-release's identifiers");

            for (const auto* bad : { "v1.2", "latest", "", "v", "v1.2.3.4", "1.2.3-", "1.2.3-beta..1", "1.2.3-.beta", "1.2.3+",
                                     "1.2.x", "1.2.-3", "v 1.2.3", "1.2.3-be ta", "1.2.3-beta_1", "-1.2.3", "1..2.3",
                                     "1234567890.0.0", "release-2026", "v1.2.3/", "vv1.2.3" })
                expect (! parse (bad).has_value(), "isn't a version: \"" + juce::String (bad) + "\"");
        }

        beginTest ("Versions compare as Semantic Versioning says");
        {
            // Semantic Versioning 2.0.0, item 11's own example, in order.
            const std::vector<const char*> ordered { "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                                     "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1",
                                                     "1.1.0", "2.0.0" };

            for (size_t i = 0; i < ordered.size(); ++i)
            {
                expectEquals (version (ordered[i]).compare (version (ordered[i])), 0);

                for (size_t j = i + 1; j < ordered.size(); ++j)
                {
                    expect (version (ordered[i]).compare (version (ordered[j])) < 0,
                            juce::String (ordered[i]) + " before " + ordered[j]);
                    expect (version (ordered[j]).compare (version (ordered[i])) > 0,
                            juce::String (ordered[j]) + " after " + ordered[i]);
                }
            }

            expect (version ("0.1.0").compare (version ("0.2.0")) < 0 && version ("0.2.0").compare (version ("1.0.0")) < 0);
            expect (version ("0.9.0").compare (version ("0.10.0")) < 0, "numbers by value, not as text");
            expect (version ("0.2.0-beta1").compare (version ("0.2.0")) < 0, "a pre-release comes before its release");
            expect (version ("0.2.0-beta1").compare (version ("0.2.0-beta2")) < 0);
            expect (version ("0.2.0-alpha.11").compare (version ("0.2.0-beta.2")) < 0);
            expect (version ("0.2.0-beta.2").compare (version ("0.2.0-beta.11")) < 0, "numeric identifiers by value");
            expect (version ("1.0.0-rc.2").compare (version ("1.0.0-rc.99999999999999999999")) < 0, "however long");
            expect (version ("1.0.0-rc.1").compare (version ("1.0.0-rc.a")) < 0, "numbers before words");
            expectEquals (version ("v0.1.0").compare (version ("0.1.0")), 0);
            expectEquals (version ("1.0.0+a").compare (version ("1.0.0+b")), 0, "build metadata plays no part");
        }

        beginTest ("The latest-release JSON is read");
        {
            // As GitHub's API answers, trimmed: the fields read and a few around them.
            const juce::String answer (R"({
                "url": "https://api.github.com/repos/freener0x/MPE-Roll/releases/1",
                "html_url": "https://github.com/freener0x/MPE-Roll/releases/tag/v0.1.0",
                "id": 1,
                "tag_name": "v0.1.0",
                "target_commitish": "main",
                "name": "MPE Roll 0.1.0",
                "draft": false,
                "prerelease": false,
                "created_at": "2026-11-01T11:00:00Z",
                "published_at": "2026-11-01T12:00:00Z",
                "assets": [ { "name": "MPE-Roll-Setup-0.1.0.exe", "size": 12345678 } ],
                "body": "## What changed\n- Everything"
            })");

            const auto latest = parseLatestReleaseJson (answer).value_or (release ("(none)"));
            expectEquals (latest.tag, juce::String ("v0.1.0"));
            expectEquals (latest.version(), juce::String ("0.1.0"));
            expect (latest.pageUrl.isEmpty(), "the page is MPE Roll's to name, not the answer's");

            const auto with = [&answer] (const char* from, const char* to) { return parseLatestReleaseJson (answer.replace (from, to)); };
            expect (! with ("\"prerelease\": false", "\"prerelease\": true").has_value(), "a pre-release is never offered");
            expect (! with ("\"draft\": false", "\"draft\": true").has_value(), "nor a draft");
            expect (! with ("\"tag_name\": \"v0.1.0\"", "\"tag_name\": \"v0.2.0-beta1\"").has_value(), "nor a pre-release's tag");
            expect (! with ("\"tag_name\": \"v0.1.0\",", "").has_value(), "nor a release without a tag");
            expect (! with ("\"tag_name\": \"v0.1.0\"", "\"tag_name\": 10").has_value(), "nor a tag that isn't text");
            expect (! with ("\"tag_name\": \"v0.1.0\"", "\"tag_name\": \"nightly\"").has_value(), "nor a tag that isn't a version");
            expect (with ("\"name\": \"MPE Roll 0.1.0\"", "\"name\": null").has_value(), "a release may be untitled");
            expect (with ("\"published_at\": \"2026-11-01T12:00:00Z\"", "\"published_at\": \"yesterday\"").has_value(),
                    "and its date, unused, may be anything");

            const juce::String rateLimited (R"json({"message": "API rate limit exceeded for 192.0.2.1. (But here's the good news: )json"
                                            R"json(Authenticated requests get a higher rate limit.)", "documentation_url": )json"
                                            R"json("https://docs.github.com/rest/overview/rate-limits-for-the-rest-api"})json");
            expect (! parseLatestReleaseJson (rateLimited).has_value(), "nor GitHub's rate limit answer");
            expect (! parseLatestReleaseJson (R"({"message": "Not Found", "status": "404"})").has_value());
            expect (! parseLatestReleaseJson ("<!DOCTYPE html><html><body>Sign in to the network</body></html>").has_value(),
                    "nor a web page in GitHub's place");
            expect (! parseLatestReleaseJson ("[ { \"tag_name\": \"v0.1.0\" } ]").has_value());
            expect (! parseLatestReleaseJson ({}).has_value());
        }

        beginTest ("The release page's redirect names the tag");
        {
            const auto tagOf = [] (const char* location)
            {
                const auto r = releaseFromRedirect (location);
                return r.has_value() ? r->tag : juce::String ("(none)");
            };

            expectEquals (tagOf ("https://github.com/freener0x/MPE-Roll/releases/tag/v0.2.0"), juce::String ("v0.2.0"));
            expectEquals (tagOf ("https://github.com/freener0x/MPE-Roll/releases/tag/v0.2.0/"), juce::String ("v0.2.0"));
            expectEquals (tagOf ("https://github.com/freener0x/MPE-Roll/releases/tag/v0.2.0?ref=x#y"), juce::String ("v0.2.0"));
            expectEquals (tagOf ("/freener0x/MPE-Roll/releases/tag/v0.2.0"), juce::String ("v0.2.0"));
            expectEquals (tagOf ("https://github.com/freener0x/MPE-Roll/releases"), juce::String ("(none)"));      // no release yet
            expectEquals (tagOf ("https://github.com/freener0x/MPE-Roll/releases/tag/"), juce::String ("(none)"));
            expectEquals (tagOf ("https://github.com/freener0x/MPE-Roll/releases/tag/v0.3.0-beta1"), juce::String ("(none)"));
            expectEquals (tagOf ("https://github.com/login?return_to=%2Ffreener0x%2FMPE-Roll"), juce::String ("(none)"));
            expectEquals (tagOf (""), juce::String ("(none)"));
        }

        beginTest ("Judging a release against the version running");
        {
            expect (judge (version ("0.1.0"), release ("v0.2.0"), {}) == Outcome::newer);
            expect (judge (version ("0.1.0"), release ("v0.1.0"), {}) == Outcome::upToDate);
            expect (judge (version ("0.1.0"), release ("0.1.0"), {}) == Outcome::upToDate, "with or without the v");
            expect (judge (version ("0.2.0"), release ("v0.1.0"), {}) == Outcome::ahead, "a build ahead of the release");
            expect (judge (version ("0.2.0-beta1"), release ("v0.2.0"), {}) == Outcome::newer, "a pre-release is told of its release");
            expect (judge (version ("0.1.0"), release ("v0.2.0"), "v0.2.0") == Outcome::skipped);
            expect (judge (version ("0.1.0"), release ("v0.2.0"), "0.2.0") == Outcome::skipped, "the skipped tag with or without the v");
            expect (judge (version ("0.1.0"), release ("v0.2.1"), "v0.2.0") == Outcome::newer, "until a newer one is out");
            expect (judge (version ("0.1.0"), release ("v0.2.0"), "v0.3.0") == Outcome::skipped, "an older one than the skipped is no news");
            expect (judge (version ("0.1.0"), release ("v0.2.0"), "nonsense") == Outcome::newer);
            expect (judge (version ("0.1.0"), release ("v0.1.0"), "v0.1.0") == Outcome::upToDate);
            expect (judge (version ("0.1.0"), release ("latest"), {}) == Outcome::upToDate, "a tag that isn't a version is nothing to tell");
        }

        beginTest ("The version a build runs as");
        {
            // As BuildInfo.cpp asks, with the project's version (CMakeLists.txt) and the build's name,
            // which the release workflow gives as "release <tag>, <commit>, <day>" and the Build
            // workflow as "build <run>, <commit>, <day>".
            const juce::String betaName ("release v0.2.0-beta1, f2ea3c4, 2026-11-04");
            expectEquals (buildVersion ("0.2.0", betaName), juce::String ("0.2.0-beta1"), "a pre-release, by its tag");
            expectEquals (buildVersion ("1.0.0", "release v1.0.0-rc.1, f2ea3c4, 2026-11-04"), juce::String ("1.0.0-rc.1"));
            expectEquals (buildVersion ("0.2.0", "release v0.2.0, f2ea3c4, 2026-11-04"), juce::String ("0.2.0"), "a release");
            expectEquals (buildVersion ("0.2.0", "build 63, f2ea3c4, 2026-10-26"), juce::String ("0.2.0"), "a build between releases");
            expectEquals (buildVersion ("0.2.0", "local build"), juce::String ("0.2.0"));
            expectEquals (buildVersion ("0.2.0", {}), juce::String ("0.2.0"));
            expectEquals (buildVersion ("0.2.0", "release v0.3.0-beta1, f2ea3c4, 2026-11-04"), juce::String ("0.2.0"),
                          "a tag of another version is no part of this one");
            expectEquals (buildVersion ("0.2.0", "release v0.2.0-beta_1, f2ea3c4, 2026-11-04"), juce::String ("0.2.0"),
                          "nor one that isn't a version");

            // A beta tester is told of the release once it's out, and isn't told of the beta itself.
            const auto beta = Version::parse (buildVersion ("0.2.0", betaName));
            expect (beta.has_value() && judge (*beta, release ("v0.2.0"), {}) == Outcome::newer, "a beta build is told of its release");
            expect (beta.has_value() && judge (*beta, release ("v0.2.0-beta1"), {}) == Outcome::upToDate);
            expect (beta.has_value() && judge (*beta, release ("v0.1.0"), {}) == Outcome::ahead);
        }

        beginTest ("Once a day, counted from the last attempt");
        {
            const juce::int64 now = juce::Time (2026, 10, 4, 12, 0).toMilliseconds();
            const juce::int64 minute = 60 * 1000, hour = 60 * minute;

            expect (isDue (0, now), "never tried");
            expect (! isDue (now, now));
            expect (! isDue (now - 23 * hour - 59 * minute, now), "23 h 59 min after");
            expect (isDue (now - 24 * hour, now), "a day after");
            expect (isDue (now - 25 * hour, now));
            expect (isDue (now + hour, now), "the clock went back");
            expectEquals (checkIntervalMs, (juce::int64) 86400000);
        }

        beginTest ("Where it asks, and what it sends");
        {
            const auto defaults = Endpoints::defaults();
            expectEquals (defaults.api, juce::String (links::latestReleaseApi));
            expectEquals (defaults.page, juce::String (links::latestRelease));
            expect (defaults.api.startsWith ("https://api.github.com/repos/freener0x/MPE-Roll/")
                        && defaults.page.startsWith ("https://github.com/freener0x/MPE-Roll/"),
                    "GitHub's, over HTTPS");
            expectEquals (tagPageUrl (defaults, "v0.2.0"), juce::String ("https://github.com/freener0x/MPE-Roll/releases/tag/v0.2.0"));

            const Endpoints local { "http://127.0.0.1:8000/repos/freener0x/MPE-Roll/releases/latest",
                                    "http://127.0.0.1:8000/freener0x/MPE-Roll/releases/latest" };
            expectEquals (tagPageUrl (local, "v9.9.0"), juce::String ("http://127.0.0.1:8000/freener0x/MPE-Roll/releases/tag/v9.9.0"));

            setEnvironment ("MPEROLL_UPDATE_URL", "");
            setEnvironment ("MPEROLL_UPDATE_PAGE_URL", "");
            expect (Endpoints::fromEnvironment (defaults).api == defaults.api && Endpoints::fromEnvironment (defaults).page == defaults.page,
                    "GitHub's, unless the environment says otherwise");

            setEnvironment ("MPEROLL_UPDATE_URL", local.api.toRawUTF8());
            setEnvironment ("MPEROLL_UPDATE_PAGE_URL", local.page.toRawUTF8());
            expect (Endpoints::fromEnvironment (defaults).api == local.api && Endpoints::fromEnvironment (defaults).page == local.page,
                    "MPEROLL_UPDATE_URL and MPEROLL_UPDATE_PAGE_URL, for tests");
            setEnvironment ("MPEROLL_UPDATE_URL", "");
            setEnvironment ("MPEROLL_UPDATE_PAGE_URL", "");

            expectEquals (userAgent ("0.1.0"), juce::String ("MPE Roll/0.1.0"));
        }

        beginTest ("Asking the API, then the release page");
        {
            const Endpoints at { "https://api.example/latest", "https://example/freener0x/MPE-Roll/releases/latest" };
            std::vector<Request> asked;
            Response apiAnswer, pageAnswer;

            const auto ask = [&]
            {
                asked.clear();
                return findLatestRelease (at, "0.1.0", [&] (const Request& r)
                {
                    asked.push_back (r);
                    return r.url == at.api ? apiAnswer : pageAnswer;
                });
            };

            const auto pageOf = [] (const Answer& a) { return a.release.has_value() ? a.release->pageUrl : juce::String ("(none)"); };

            apiAnswer.status = 200;
            apiAnswer.body = R"({"tag_name": "v0.2.0", "name": "MPE Roll 0.2.0", "draft": false, "prerelease": false,
                                 "html_url": "https://elsewhere.example/download"})";
            auto answer = ask();
            expect (answer.release.has_value() && answer.release->tag == "v0.2.0" && answer.error.isEmpty());
            expect (asked.size() == 1, "one request when the API answers");
            expectEquals (pageOf (answer), juce::String ("https://example/freener0x/MPE-Roll/releases/tag/v0.2.0"),
                          "the page beside the one MPE Roll knows, whatever the answer says");

            // Exactly what goes with the requests: the User-Agent, and the API's format.
            const auto headers = juce::StringArray::fromLines (asked[0].headers);
            expect (headers == juce::StringArray ("User-Agent: MPE Roll/0.1.0", "Accept: application/vnd.github+json",
                                                  "X-GitHub-Api-Version: 2022-11-28"),
                    "the API request carries the User-Agent and the format asked for, nothing else: " + asked[0].headers);
            expect (asked[0].url == at.api && asked[0].redirectsToFollow == 2);

            // GitHub's limit on requests from one address: the page's redirect names the tag instead.
            apiAnswer.status = 403;
            apiAnswer.body = R"({"message": "API rate limit exceeded for 192.0.2.1."})";
            pageAnswer.status = 302;
            pageAnswer.headers.set ("location", "https://github.com/freener0x/MPE-Roll/releases/tag/v0.2.1");
            answer = ask();
            expect (answer.release.has_value() && answer.release->tag == "v0.2.1", "the page's redirect, when the API refuses");
            expect (asked.size() == 2 && asked[1].url == at.page && asked[1].redirectsToFollow == 0,
                    "asked without following the redirect");
            expectEquals (asked[1].headers, juce::String ("User-Agent: MPE Roll/0.1.0"), "with the User-Agent alone");
            expectEquals (pageOf (answer), juce::String ("https://example/freener0x/MPE-Roll/releases/tag/v0.2.1"));

            // A pre-release from the API isn't offered; the page's latest release is.
            apiAnswer.status = 200;
            apiAnswer.body = R"({"tag_name": "v0.3.0-beta1", "prerelease": true})";
            answer = ask();
            expect (answer.release.has_value() && answer.release->tag == "v0.2.1" && asked.size() == 2);

            // No answer from either: offline, or something in the way.
            apiAnswer = {};
            pageAnswer = {};
            answer = ask();
            expect (! answer.release.has_value() && asked.size() == 2);
            expectEquals (answer.error, juce::String ("no answer from GitHub"));

            apiAnswer.status = 404;
            pageAnswer.status = 302;
            pageAnswer.headers.set ("Location", "https://github.com/freener0x/MPE-Roll/releases");
            answer = ask();
            expect (! answer.release.has_value());
            expectEquals (answer.error, juce::String ("GitHub answered 404"), "what the API said, when neither gives a release");

            apiAnswer = {};
            pageAnswer = {};
            pageAnswer.status = 503;
            expectEquals (ask().error, juce::String ("GitHub answered 503"), "or the page, when the API said nothing");

            apiAnswer.status = 200;
            apiAnswer.body = "<html>Sign in to the network</html>";
            pageAnswer.status = 200;
            pageAnswer.body = "<html>Sign in to the network</html>";
            expectEquals (ask().error, juce::String ("the answer named no release"), "a page in GitHub's place isn't called GitHub's");

            apiAnswer = {};
            apiAnswer.status = 407;
            pageAnswer = apiAnswer;
            expectEquals (ask().error, juce::String ("a proxy on the network asked for a sign-in"));
        }

        beginTest ("Asking the API alone, with no release page to ask");
        {
            // As on macOS, where the system's networking would keep the cookies a github.com page sets.
            const Endpoints apiOnly { "https://api.example/latest", {} };
            std::vector<Request> asked;
            Response apiAnswer;

            const auto ask = [&]
            {
                asked.clear();
                return findLatestRelease (apiOnly, "0.1.0", [&] (const Request& r)
                {
                    asked.push_back (r);
                    return r.url == apiOnly.api ? apiAnswer : Response();
                });
            };

            apiAnswer.status = 403;
            apiAnswer.body = R"({"message": "API rate limit exceeded for 192.0.2.1."})";
            auto answer = ask();
            expect (! answer.release.has_value() && asked.size() == 1 && asked[0].url == apiOnly.api,
                    "a refusal from the API is the answer: nothing else is asked");
            expectEquals (answer.error, juce::String ("GitHub answered 403"));

            // GitHub's limit of requests from one address, as its headers say it's used up: named, so
            // that the reader can tell it from a fault (with the API alone, it holds until the hour is out).
            apiAnswer.headers.set ("X-RateLimit-Limit", "60");
            apiAnswer.headers.set ("X-RateLimit-Remaining", "0");
            answer = ask();
            expectEquals (answer.error, juce::String ("GitHub's limit of 60 requests an hour from your network is used up "
                                                      "(it answered 403)"));

            apiAnswer.status = 429;
            apiAnswer.headers.remove ("X-RateLimit-Limit");
            answer = ask();
            expectEquals (answer.error, juce::String ("GitHub's limit of requests an hour from your network is used up "
                                                      "(it answered 429)"),
                          "the limit named whatever its number");

            apiAnswer.headers.set ("X-RateLimit-Remaining", "12");
            answer = ask();
            expectEquals (answer.error, juce::String ("GitHub answered 429"), "but not while the headers say some are left");
            apiAnswer.headers.clear();

            apiAnswer.status = 200;
            apiAnswer.body = R"({"tag_name": "v0.2.0", "draft": false, "prerelease": false})";
            answer = ask();
            expect (answer.release.has_value() && asked.size() == 1);

            if (answer.release.has_value())
                expectEquals (answer.release->pageUrl, juce::String ("https://github.com/freener0x/MPE-Roll/releases/tag/v0.2.0"),
                              "and its page is the release's on GitHub");
        }
    }

private:
    static void setEnvironment (const char* name, const char* value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value);
       #else
        if (*value == 0)
            unsetenv (name);
        else
            setenv (name, value, 1);
       #endif
    }
};

static UpdateCheckTests updateCheckTests;

} // namespace mperoll
