#pragma once

#include "UpdateCheck.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace mperoll
{

/** The update check: asks GitHub, on a thread of its own, whether a newer MPE Roll has been
    released, once a day as MPE Roll opens if the user has turned that on (MPE settings > Updates),
    or when the user asks (File > Check for updates now...). It's off until then, and nothing is
    sent.

    One per process, shared by every editor: hold it with juce::SharedResourcePointer. The MPE
    settings panel borrows its editor's rather than holding one, so the checker goes with the last
    editor, and its destructor calls off a request under way and waits for the thread: the host may
    unload the plugin right after. Message thread only; listeners hear of every change.

    What it knows (whether it's on, when it last asked, what it found, what's skipped) is kept in a
    file of its own beside the computer's MPE Roll settings, which the functions below are given to
    find it by (getSettingsFile() says why it's not in them). Every MPE Roll running on the computer
    shares that file, each with a checker of its own (the VST2 and the VST3 in one FL project, the
    standalone app, another host), so it's read afresh for every decision and written at once, and
    looked at again every second for what another has changed (followFile()).
*/
class UpdateChecker : public juce::ChangeBroadcaster,
                      private juce::Timer
{
public:
    UpdateChecker();
    ~UpdateChecker() override;

    /** How the last check went, or that one is under way. */
    enum class Phase
    {
        idle,       // no check yet in this process
        checking,
        upToDate,   // the latest release is the version running
        newer,      // a newer release is out
        skipped,    // a newer release is out, but it's the one the user chose to skip (or an older one)
        ahead,      // the version running is newer than the latest release (a build between releases)
        failed      // no answer, or none that named a release
    };

    struct State
    {
        Phase phase = Phase::idle;
        updates::Release latest;            // the latest release the last check found
        juce::String error;                 // why the last check failed
        bool askedByUser = false;           // the user asked for it, so its answer is shown whatever it is
        int checkNumber = 0;                // counts the checks started, so an answer is shown once

        // The newer release the Update button and the File menu offer: the one the last check to get
        // an answer found, here or in another MPE Roll, whether the daily check is on or not, until
        // it's installed or skipped. It stands while a check is under way, and when one gets no
        // answer. Never one the user chose to skip.
        std::optional<updates::Release> newer;
    };

    const State& getState() const noexcept      { return state; }

    /** The file the update check keeps what it knows in: "Update check.settings", beside the
        computer's MPE Roll settings (no file without them). Not in those settings themselves: every
        MPE Roll running keeps a copy of them in memory, read as it started, and writes all of it
        back whenever it saves anything, so a check switched off in one MPE Roll would be switched on
        again by the next save of another. */
    static juce::File getSettingsFile (juce::PropertiesFile* settings);

    /** Whether the automatic check is on (MPE settings > Updates); off unless the user turned it on. */
    static bool isAutomatic (juce::PropertiesFile* settings);

    /** Turns the automatic check on or off. Turned on, it checks at once if it's due. Turned off, an
        automatic check waiting for GitHub is called off, so nothing more is sent (one the user asked
        for goes on). */
    void setAutomatic (juce::PropertiesFile* settings, bool shouldCheck);

    /** As MPE Roll opens: the newer release the last answered check found is offered, unless it's
        been installed or skipped since, and if the automatic check is on and a day has passed since
        the last attempt (by any MPE Roll on the computer), a check starts. */
    void checkIfDue (juce::PropertiesFile* settings);

    /** File > Check for updates now... and MPE settings' Check now: checks whether the automatic
        check is on or not, whatever the day, and the answer is shown whatever it is. A check already
        under way isn't started again: its answer is shown instead. */
    void checkNow (juce::PropertiesFile* settings);

    /** Skip version ...: this release is no longer offered, nor is an older one; a newer one is. A
        version skipped before that's newer still stays skipped. */
    void skip (juce::PropertiesFile* settings, const updates::Release&);

    /** Looks at the update check's file again, which another MPE Roll may have changed: the release
        offered follows it, listeners hear of the change (MPE settings' switch and lines are drawn
        from the file), and a daily check under way here that's been switched off is called off.
        Done every second once the checker has been given the settings, and by the File menu before
        it opens. Nothing happens if the file is as it was last time. */
    void followFile();

    /** A release newer than the version running, as the last check to get an answer found it, and
        whether it's the one the user chose to skip. */
    struct KnownRelease
    {
        updates::Release release;
        bool skipped = false;
    };

    /** For MPE settings, which offers the newer release's page even if it's skipped: nothing if no
        check has found one, or it's been installed since. */
    static std::optional<KnownRelease> findNewerRelease (juce::PropertiesFile* settings);

    /** Opens a release's page in the browser. False if no browser would open. */
    static bool openReleasePage (const updates::Release&);

    /** Whether a check asks the latest release's page when GitHub's API gives no release. Not on
        macOS: the system's networking there keeps the cookies an answer sets, in the host's store,
        for later requests to send, and a github.com page can set GitHub's own, which would then go
        with every check. */
    static bool asksReleasePage();

    /** Whether this build can ask GitHub at all: not the Linux build, whose networking (JUCE's own,
        without libcurl) has no HTTPS. It sends nothing to GitHub's addresses, and a check only says
        why. */
    static bool reachesGitHub();

    /** For the status line, after a check the user asked for: what it found. */
    juce::String describeAnswer() const;

    /** For MPE settings: the last check's result as the settings keep it, or that one is under way. */
    juce::String describeLastCheck (juce::PropertiesFile* settings) const;

    /** For tests: release pages go to this instead of the browser (none to stop doing so). */
    static void setBrowserForTesting (std::function<bool (const juce::URL&)>);

private:
    class Worker;

    State state;
    std::unique_ptr<Worker> worker;
    juce::File settingsFile;        // where the check under way keeps what it finds
    juce::String startedAt;         // ...and when it started, as it wrote it there

    // Whether the user asked for the check under way, which its thread reads before it asks the
    // release page: a daily check switched off by then asks nothing more. Set when Check now
    // takes over a daily check under way.
    std::shared_ptr<std::atomic<bool>> askedFor;

    juce::File followed;            // the file followFile() looks at...
    juce::String followedText;      // ...and what it held last time

    std::shared_ptr<int> lifetime = std::make_shared<int> (0);  // lets an answer on its way see that we're gone

    void follow (juce::PropertiesFile* settings);
    void timerCallback() override;
    void start (juce::PropertiesFile* settings, bool askedByUser);
    void finish (const updates::Answer&);

    /** Ends the check under way at once, sending nothing more, and says why in the file, unless
        another MPE Roll has started a check of its own since. */
    void callOff (const juce::String& why);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateChecker)
};

} // namespace mperoll
