#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <mutex>

namespace mperoll::PluginScanner
{

/** Lists the plugins in a file by loading it in a separate process, the scanner program shipped with
    MPE Roll ("MPE Roll Scanner"). A plugin that crashes or hangs while it's being looked at then
    takes down only that process, not FL Studio.
*/

/** The ID on the scanner's command line, shared with the scanner program. */
inline constexpr const char* processId = "mperollscanner";

/** What the scanner says as soon as it's connected, so one that can't start is noticed quickly. */
inline constexpr const char* readyMessage = "mperoll-scanner-ready";

/** What the scanner says when it starts on a file, before it loads anything. A scanner that went down
    before saying it, or got stuck before it could, never reached the file, so the file isn't blamed.
*/
inline constexpr const char* startedMessage = "mperoll-scanner-started";

/** How long one file may take, including any dialog the plugin shows (authorisation, say). */
inline constexpr int defaultTimeoutMs = 180000;

/** The scanner program: next to MPE Roll's own binary, or in the standard VST3 install. Returns
    File() if it isn't there, in which case plugins are looked at inside this process as before.
*/
juce::File findExecutable();

/** Tests: use this scanner program instead (File() to go back to looking for it). */
void setExecutableForTesting (const juce::File&);

enum class Outcome
{
    found,          // the file loaded; results lists its plugins (possibly none)
    crashed,        // the scanner process died while loading it
    timedOut,       // it took too long, so the scanner process was stopped
    cancelled,      // the caller asked to stop
    unavailable     // there's no scanner program, or it wouldn't start
};

/** Looks at one file in a fresh scanner process. Blocks; any thread. */
Outcome findTypes (const juce::String& formatName, const juce::String& fileOrIdentifier,
                   juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs = defaultTimeoutMs);

/** Describes an outcome other than found, for showing to the user. */
juce::String describeFailure (Outcome, const juce::String& fileName);

/** For KnownPluginList: scans each file in a scanner process, keeping the process for the whole scan
    and starting a new one after a crash. A file that crashes or hangs is reported as failed, so the
    list blacklists it. Without a scanner program, files are scanned in this process as before.

    A scanner that had looked at other files may have been brought down by one of them (a thread or
    timer it left running), and one that never reached the file can't have been brought down by it:
    either way the file is looked at once more, in a fresh scanner, before it's blamed.
*/
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (int timeoutMs = defaultTimeoutMs);
    ~OutOfProcessScanner() override;

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;
    void scanFinished() override;

    class Process;

private:
    const int timeoutMs;
    std::mutex mutex;
    std::unique_ptr<Process> process;
};

} // namespace mperoll::PluginScanner
