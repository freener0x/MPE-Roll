// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "UpdateChecker.h"
#include "BuildInfo.h"
#include "Links.h"

namespace mperoll
{

namespace
{
    // What the update check keeps, in "Update check.settings" beside the computer's MPE Roll settings
    // (UpdateChecker::getSettingsFile says why not in them):
    //   updateCheck             1 when the automatic check is on; off (0, or not there) unless the user turns it on
    //   updateCheckLast         when the last check started, in milliseconds since 1970: the day is counted from it
    //   updateCheckLastOutcome  how it ended: latest, newer, ahead, or "failed: <why>" (no answer, or none that
    //                           named a release; or that it was switched off, or MPE Roll closed, first); and
    //                           unfinished while it's under way (or if its MPE Roll stopped without a word)
    //   updateCheckLastTag      the latest release's tag, as the last check to get an answer found it (v0.2.0)
    //   updateCheckSkippedTag   the version the user chose to skip
    //   updateCheckUrl          never written by MPE Roll: an address to ask in place of GitHub's API, for
    //                           testing by hand (the environment variable MPEROLL_UPDATE_URL comes first)
    constexpr const char* settingsFileName = "Update check.settings";

    constexpr const char* enabledKey = "updateCheck";
    constexpr const char* lastKey = "updateCheckLast";
    constexpr const char* outcomeKey = "updateCheckLastOutcome";
    constexpr const char* lastTagKey = "updateCheckLastTag";
    constexpr const char* skippedKey = "updateCheckSkippedTag";
    constexpr const char* urlKey = "updateCheckUrl";

    constexpr const char* unfinishedOutcome = "unfinished";
    constexpr const char* failedPrefix = "failed: ";
    constexpr const char* switchedOffReason = "the check was switched off before GitHub answered";
    constexpr const char* closedReason = "MPE Roll closed before GitHub answered";

    constexpr int timeoutMs = 8000;                 // for connecting, and for each wait for the answer
    constexpr int stopWaitMs = timeoutMs + 2000;    // the longest stopping waits, should calling off fail
    constexpr size_t maxBodyBytes = 1 << 20;        // the API's answer is a few kilobytes
    constexpr int followMs = 1000;                  // how often the file is looked at for other MPE Rolls' changes
    constexpr juce::int64 longestCheckMs = 60000;   // longer than any check takes, its two requests' waits and all

    std::function<bool (const juce::URL&)>& browserForTesting()
    {
        static std::function<bool (const juce::URL&)> browser;
        return browser;
    }

    /** The update check's settings as their file holds them now. Made afresh for each decision and
        each change, which is saved at once (saveIfNeeded()), so no MPE Roll acts on what another has
        changed since it last looked, or writes it back. */
    std::unique_ptr<juce::PropertiesFile> readKeys (const juce::File& file)
    {
        juce::PropertiesFile::Options options;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = -1;      // saved when changed, by saveIfNeeded(), not later on a timer
        return std::make_unique<juce::PropertiesFile> (file, options);
    }

    updates::Endpoints endpointsFor (const juce::PropertiesFile& keys)
    {
        auto endpoints = updates::Endpoints::defaults();

        if (const auto url = keys.getValue (urlKey).trim(); url.isNotEmpty())
            endpoints.api = url;

        if (! UpdateChecker::asksReleasePage())
            endpoints.page.clear();

        return updates::Endpoints::fromEnvironment (endpoints);
    }

    /** A release as its tag names it, if that's a release's version. */
    std::optional<updates::Release> releaseTagged (const juce::String& tag, const juce::PropertiesFile& keys)
    {
        const auto version = updates::Version::parse (tag);

        if (! version.has_value() || version->isPreRelease())
            return {};

        updates::Release release;
        release.tag = tag;
        release.pageUrl = updates::tagPageUrl (endpointsFor (keys), tag);
        return release;
    }

    /** The release the last check to get an answer found, if it's newer than the version running,
        and whether it's skipped. */
    std::optional<UpdateChecker::KnownRelease> newerIn (const juce::PropertiesFile& keys)
    {
        const auto running = updates::Version::parse (getVersionString());
        const auto latest = releaseTagged (keys.getValue (lastTagKey), keys);

        if (! running.has_value() || ! latest.has_value())
            return {};

        switch (updates::judge (*running, *latest, keys.getValue (skippedKey)))
        {
            case updates::Outcome::newer:       return UpdateChecker::KnownRelease { *latest, false };
            case updates::Outcome::skipped:     return UpdateChecker::KnownRelease { *latest, true };
            case updates::Outcome::upToDate:
            case updates::Outcome::ahead:       break;
        }

        return {};
    }

    /** ...and the one to offer: that release, unless it's skipped. */
    std::optional<updates::Release> offeredIn (const juce::PropertiesFile& keys)
    {
        if (const auto known = newerIn (keys); known.has_value() && ! known->skipped)
            return known->release;

        return {};
    }

    bool sameRelease (const std::optional<updates::Release>& a, const std::optional<updates::Release>& b)
    {
        return a.has_value() == b.has_value() && (! a.has_value() || (a->tag == b->tag && a->pageUrl == b->pageUrl));
    }
}

//==============================================================================
/** Asks GitHub on a thread of its own, and hands the answer on. Its request can be called off from
    the message thread at any moment, which ends the thread at once rather than when the request
    times out. Before it asks the release page (once the API has given no release, which can take
    the API's 8 s), it asks stillWanted, on this thread, whether to go on: a daily check switched off
    meanwhile, in this MPE Roll or another, sends nothing more. */
class UpdateChecker::Worker : public juce::Thread
{
public:
    Worker (updates::Endpoints endpointsToAsk, juce::String runningVersion, std::function<bool()> stillWanted,
            std::function<void (const updates::Answer&)> answered)
        : juce::Thread ("MPE Roll update check"),
          endpoints (std::move (endpointsToAsk)),
          version (std::move (runningVersion)),
          wanted (std::move (stillWanted)),
          onAnswer (std::move (answered))
    {
    }

    ~Worker() override
    {
        // The request is called off wherever it is (connecting, waiting for the answer, reading
        // it), so the thread ends at once rather than when the request times out.
        signalThreadShouldExit();

        {
            const juce::ScopedLock sl (lock);

            if (stream != nullptr)
                stream->cancel();
        }

        stopThread (stopWaitMs);
    }

    void run() override
    {
        const auto answer = updates::findLatestRelease (endpoints, version, [this] (const updates::Request& r) { return get (r); });

        if (! threadShouldExit())
            onAnswer (switchedOff ? updates::Answer { std::nullopt, switchedOffReason } : answer);
    }

private:
    const updates::Endpoints endpoints;
    const juce::String version;
    const std::function<bool()> wanted;
    const std::function<void (const updates::Answer&)> onAnswer;
    juce::CriticalSection lock;
    juce::WebInputStream* stream = nullptr;     // the request under way, for the destructor to call off
    bool switchedOff = false;                   // stillWanted said no before the release page: this thread's only

    /** Gives up while the request is sent, if the thread is asked to stop. (A request without a body,
        as these are, may not be asked: the destructor's cancel() is what stops the rest.) */
    struct StopWhenAsked final : public juce::WebInputStream::Listener
    {
        explicit StopWhenAsked (const juce::Thread& t) : thread (t) {}

        bool postDataSendProgress (juce::WebInputStream&, int, int) override    { return ! thread.threadShouldExit(); }

        const juce::Thread& thread;
    };

    /** On a stream of our own, not juce::URL::createInputStream's, which keeps its stream to itself
        until the server answers, where the destructor couldn't call it off. */
    updates::Response get (const updates::Request& request)
    {
        if (request.url == endpoints.page && ! wanted())
        {
            switchedOff = true;
            return {};
        }

        juce::WebInputStream in (juce::URL (request.url), false);
        in.withExtraHeaders (request.headers)
          .withConnectionTimeout (timeoutMs)
          .withNumRedirectsToFollow (request.redirectsToFollow);

        {
            const juce::ScopedLock sl (lock);

            if (threadShouldExit())
                return {};

            stream = &in;
        }

        auto response = read (in);

        const juce::ScopedLock sl (lock);
        stream = nullptr;
        return response;
    }

    updates::Response read (juce::WebInputStream& in)
    {
        StopWhenAsked stopWhenAsked (*this);
        updates::Response response;

        if (! in.connect (&stopWhenAsked) || threadShouldExit())
            return response;

        response.status = in.getStatusCode();
        response.headers = in.getResponseHeaders();

        // To the end of the answer, which each system's networking finds for itself, rather than to
        // the length the server gave: macOS's unpacks a compressed answer, which is then longer.
        juce::MemoryOutputStream body;

        while (! in.isExhausted() && ! threadShouldExit() && body.getDataSize() < maxBodyBytes)
        {
            char buffer[8192];
            const auto got = in.read (buffer, (int) sizeof (buffer));

            if (got <= 0)
                break;

            body.write (buffer, (size_t) got);
        }

        response.body = body.toUTF8();
        return response;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Worker)
};

//==============================================================================
UpdateChecker::UpdateChecker() = default;

UpdateChecker::~UpdateChecker()
{
    stopTimer();

    // The host may unload the plugin as soon as its last editor has gone: the request is called
    // off and the thread ended here, not left running in code that's about to go. The file says
    // why that check ended, for other MPE Rolls' MPE settings.
    if (state.phase == Phase::checking)
        callOff (closedReason);

    worker.reset();
}

juce::File UpdateChecker::getSettingsFile (juce::PropertiesFile* settings)
{
    return settings != nullptr ? settings->getFile().getSiblingFile (settingsFileName) : juce::File();
}

bool UpdateChecker::isAutomatic (juce::PropertiesFile* settings)
{
    return readKeys (getSettingsFile (settings))->getBoolValue (enabledKey, false);
}

void UpdateChecker::setAutomatic (juce::PropertiesFile* settings, bool shouldCheck)
{
    follow (settings);

    {
        const auto keys = readKeys (getSettingsFile (settings));
        keys->setValue (enabledKey, shouldCheck);
        keys->saveIfNeeded();
    }

    // Switched off, nothing more is sent unless the user asks: a daily check waiting for GitHub is
    // called off where it is (one the user asked for goes on).
    if (! shouldCheck && state.phase == Phase::checking && ! state.askedByUser)
        callOff (switchedOffReason);

    // MPE settings' switch shows the change, whatever comes of it.
    sendChangeMessage();

    if (shouldCheck)
        checkIfDue (settings);
}

void UpdateChecker::follow (juce::PropertiesFile* settings)
{
    const auto file = getSettingsFile (settings);

    if (file == juce::File() || file == followed)
        return;

    followed = file;
    followedText = file.loadFileAsString();
    startTimer (followMs);
}

void UpdateChecker::timerCallback()
{
    followFile();
}

void UpdateChecker::followFile()
{
    if (followed == juce::File())
        return;

    // The whole file, a few lines long, rather than its time: two changes within one tick of a
    // file system's clock look alike by time.
    auto text = followed.loadFileAsString();

    if (text == followedText)
        return;

    followedText = std::move (text);
    const auto keys = readKeys (followed);

    // Switched off in another MPE Roll while a daily check here waits for GitHub: called off, as it
    // would be had it been switched off here.
    if (state.phase == Phase::checking && ! state.askedByUser && ! keys->getBoolValue (enabledKey, false))
        callOff (switchedOffReason);

    // What another MPE Roll found or skipped, offered here as there.
    state.newer = offeredIn (*keys);
    sendChangeMessage();
}

void UpdateChecker::callOff (const juce::String& why)
{
    worker.reset();     // the request is called off where it is, and the thread has ended

    const auto keys = readKeys (settingsFile);

    if (keys->getValue (lastKey) == startedAt && keys->getValue (outcomeKey) == unfinishedOutcome)
    {
        keys->setValue (outcomeKey, failedPrefix + why);
        keys->saveIfNeeded();
    }

    state.phase = Phase::failed;
    state.error = why;
}

void UpdateChecker::checkIfDue (juce::PropertiesFile* settings)
{
    follow (settings);

    if (state.phase == Phase::checking)
        return;

    const auto keys = readKeys (getSettingsFile (settings));

    // What the last answered check found, here or in another MPE Roll, is offered until it's
    // installed or skipped, not only on the day it was found, and whether the daily check is on or
    // not: it's what MPE settings' Last check line says is out.
    const auto offered = offeredIn (*keys);
    const auto changed = ! sameRelease (offered, state.newer);
    state.newer = offered;

    if (keys->getBoolValue (enabledKey, false)
        && updates::isDue (keys->getValue (lastKey).getLargeIntValue(), juce::Time::currentTimeMillis()))
        start (settings, false);
    else if (changed)
        sendChangeMessage();
}

void UpdateChecker::checkNow (juce::PropertiesFile* settings)
{
    follow (settings);

    if (state.phase == Phase::checking)
    {
        // The check under way is the user's now: its answer is shown, and it goes on to the release
        // page if need be, switch or no switch.
        state.askedByUser = true;

        if (askedFor != nullptr)
            askedFor->store (true);

        sendChangeMessage();
        return;
    }

    start (settings, true);
}

void UpdateChecker::start (juce::PropertiesFile* settings, bool askedByUser)
{
    worker.reset();     // the last check's, whose thread has finished

    // The newer release known stays offered meanwhile.
    const auto number = ++state.checkNumber;
    state.phase = Phase::checking;
    state.askedByUser = askedByUser;
    state.error.clear();
    settingsFile = getSettingsFile (settings);
    startedAt = juce::String (juce::Time::currentTimeMillis());
    askedFor = std::make_shared<std::atomic<bool>> (askedByUser);

    // The day is counted from this attempt, answered or not, so a computer that's offline isn't
    // asked about again every time MPE Roll opens that day.
    const auto keys = readKeys (settingsFile);
    keys->setValue (lastKey, startedAt);
    keys->setValue (outcomeKey, unfinishedOutcome);
    keys->saveIfNeeded();

    const auto endpoints = endpointsFor (*keys);

    // JUCE's own networking, which the Linux build uses, speaks plain HTTP only: it would turn
    // GitHub's addresses down before sending anything, and "no answer" would be the wrong reason.
    if (! reachesGitHub()
        && endpoints.api.startsWithIgnoreCase ("https:") && (endpoints.page.isEmpty() || endpoints.page.startsWithIgnoreCase ("https:")))
    {
        finish ({ std::nullopt, "the Linux build can't make HTTPS requests, so it can't ask GitHub" });
        return;
    }

    // Before the release page, on the worker's thread: a daily check goes on only while the switch
    // is still on, as the file says then, whoever switched it.
    auto stillWanted = [file = settingsFile, asked = askedFor]
    {
        return asked->load() || readKeys (file)->getBoolValue (enabledKey, false);
    };

    worker = std::make_unique<Worker> (endpoints, getVersionString(), std::move (stillWanted),
                                       [this, number, alive = std::weak_ptr<int> (lifetime)] (const updates::Answer& answer)
    {
        // On the worker's thread: the answer is kept on the message thread, unless this has gone.
        juce::MessageManager::callAsync ([this, number, answer, alive]
        {
            if (! alive.expired() && state.checkNumber == number && state.phase == Phase::checking)
                finish (answer);
        });
    });

    if (! worker->startThread())
        finish ({ std::nullopt, "the check couldn't start" });
    else
        sendChangeMessage();
}

void UpdateChecker::finish (const updates::Answer& answer)
{
    worker.reset();     // its thread has done its work, and ends in a moment

    // As the file is now: another MPE Roll may have skipped a version meanwhile.
    const auto keys = readKeys (settingsFile);
    const auto running = updates::Version::parse (getVersionString());
    juce::String outcome;

    if (answer.release.has_value() && running.has_value())
    {
        state.latest = *answer.release;

        switch (updates::judge (*running, state.latest, keys->getValue (skippedKey)))
        {
            case updates::Outcome::upToDate:    state.phase = Phase::upToDate;  outcome = "latest"; break;
            case updates::Outcome::ahead:       state.phase = Phase::ahead;     outcome = "ahead";  break;
            case updates::Outcome::newer:       state.phase = Phase::newer;     outcome = "newer";  break;

            // The user chose not to hear of it: said only in answer to a check they asked for, and
            // offered in MPE settings alone, where they go to look.
            case updates::Outcome::skipped:     state.phase = Phase::skipped;   outcome = "newer";  break;
        }

        keys->setValue (lastTagKey, answer.release->tag);
    }
    else
    {
        state.phase = Phase::failed;
        state.error = answer.release.has_value() ? juce::String ("this build's version can't be read") : answer.error;
        outcome = failedPrefix + state.error;
    }

    keys->setValue (outcomeKey, outcome);
    keys->saveIfNeeded();

    // The release to offer, as kept: this answer's, or an earlier one's when nothing answered.
    state.newer = offeredIn (*keys);
    sendChangeMessage();
}

void UpdateChecker::skip (juce::PropertiesFile* settings, const updates::Release& release)
{
    follow (settings);

    const auto keys = readKeys (getSettingsFile (settings));
    const auto skipping = updates::Version::parse (release.tag);
    const auto skippedBefore = updates::Version::parse (keys->getValue (skippedKey));

    if (skipping.has_value() && ! (skippedBefore.has_value() && skippedBefore->compare (*skipping) > 0))
    {
        keys->setValue (skippedKey, release.tag);
        keys->saveIfNeeded();
    }

    state.newer = offeredIn (*keys);
    sendChangeMessage();
}

std::optional<UpdateChecker::KnownRelease> UpdateChecker::findNewerRelease (juce::PropertiesFile* settings)
{
    return newerIn (*readKeys (getSettingsFile (settings)));
}

bool UpdateChecker::openReleasePage (const updates::Release& release)
{
    if (release.pageUrl.isEmpty())
        return false;

    const juce::URL page (release.pageUrl);

    if (const auto& browser = browserForTesting())
        return browser (page);

    return page.launchInDefaultBrowser();
}

bool UpdateChecker::asksReleasePage()
{
   #if JUCE_MAC
    return false;
   #else
    return true;
   #endif
}

bool UpdateChecker::reachesGitHub()
{
   #if JUCE_LINUX && ! JUCE_USE_CURL
    return false;
   #else
    return true;
   #endif
}

void UpdateChecker::setBrowserForTesting (std::function<bool (const juce::URL&)> browser)
{
    browserForTesting() = std::move (browser);
}

//==============================================================================
juce::String UpdateChecker::describeAnswer() const
{
    const auto running = getVersionString();
    const auto latest = state.latest.version();

    switch (state.phase)
    {
        case Phase::checking:
            return "Checking for updates...";

        case Phase::upToDate:
            return "MPE Roll " + running + " is the latest release.";

        case Phase::ahead:
            return "This MPE Roll, " + running + ", is newer than the latest release, " + latest + ".";

        case Phase::newer:
            return "MPE Roll " + latest + " is out (you have " + running + "): click Update to " + latest + " to get it.";

        case Phase::skipped:
            return "MPE Roll " + latest + " is out (you have " + running + "), but you chose to skip it. MPE settings > "
                   "Updates has its page.";

        case Phase::failed:
            return "Couldn't check for updates: " + state.error + ". The releases are at "
                 + juce::String (links::repository).fromFirstOccurrenceOf ("://", false, false) + "/releases";

        case Phase::idle:
            break;
    }

    return {};
}

juce::String UpdateChecker::describeLastCheck (juce::PropertiesFile* settings) const
{
    if (state.phase == Phase::checking)
        return "Asking GitHub for the latest release...";

    const auto keys = readKeys (getSettingsFile (settings));
    const auto last = keys->getValue (lastKey).getLargeIntValue();

    if (last <= 0)
        return "Never. Nothing is sent until you turn the check on or click Check for updates now.";

    const auto when = juce::Time (last).formatted ("%Y-%m-%d %H:%M");
    const auto outcome = keys->getValue (outcomeKey);

    // Started and not ended: another MPE Roll's check, waiting for GitHub (one ended by switching it
    // off or closing MPE Roll says so), unless that MPE Roll stopped without a word, a crash say,
    // which no check takes long enough to leave in doubt for more than a minute. That MPE Roll's
    // answer, when it comes, is drawn here as soon as this one looks at the file again.
    if (outcome == unfinishedOutcome)
    {
        const auto age = juce::Time::currentTimeMillis() - last;

        return age >= 0 && age < longestCheckMs ? "Asked GitHub at " + when + "; no answer yet."
                                                : "Tried " + when + ", but MPE Roll stopped before GitHub answered.";
    }

    if (outcome.startsWith (failedPrefix))
        return "Tried " + when + ": " + outcome.fromFirstOccurrenceOf (failedPrefix, false, false) + ".";

    const auto running = updates::Version::parse (getVersionString());
    const auto latest = releaseTagged (keys->getValue (lastTagKey), *keys);

    if (! running.has_value() || ! latest.has_value())
        return "Checked " + when + ".";

    const auto version = latest->version(), yours = getVersionString();

    switch (updates::judge (*running, *latest, keys->getValue (skippedKey)))
    {
        case updates::Outcome::upToDate:
            return "Checked " + when + ": " + version + " is the latest release, the one you have.";

        case updates::Outcome::newer:
            return "Checked " + when + ": MPE Roll " + version + " is out (you have " + yours + ").";

        case updates::Outcome::skipped:
            return "Checked " + when + ": MPE Roll " + version + " is out, and you chose to skip it (you have " + yours + ").";

        case updates::Outcome::ahead:
            return "Checked " + when + ": the latest release is " + version + ", older than yours (" + yours + ").";
    }

    return {};
}

} // namespace mperoll
