#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace mperoll::PluginScanner
{

/** Lists the plugins in a file by loading it in a separate process, the scanner program shipped with
    MPE Roll ("MPE Roll Scanner"), started afresh for each file. A plugin that crashes or hangs while
    it's being looked at then takes down only that process, not FL Studio, and nothing it leaves
    behind can affect the next file.

    The two talk through files: MPE Roll writes a job (see Job) and starts the scanner with
    scanArgument and the job's path. The scanner creates the ready marker once it has read the job,
    the started marker just before it loads anything, and writes the result (a LIST of plugin
    descriptions) once it has looked. A scanner that leaves without the ready marker is one from
    another version of MPE Roll (which turns the job down) or is broken; the file isn't looked at in
    this process instead, since that's where a crashing plugin takes FL down.
*/

/** The scanner's first argument; the second is the job file. */
inline constexpr const char* scanArgument = "--mperoll-scan";

inline juce::File readyMarkerFor (const juce::File& job)    { return job.getSiblingFile (job.getFileName() + ".ready"); }
inline juce::File startedMarkerFor (const juce::File& job)  { return job.getSiblingFile (job.getFileName() + ".started"); }
inline juce::File resultFileFor (const juce::File& job)     { return job.getSiblingFile (job.getFileName() + ".result"); }

/** How long one file may take, including any dialog the plugin shows (authorisation, say). */
inline constexpr int defaultTimeoutMs = 180000;

/** How much longer than the job's timeout the scanner gives itself before ending on its own. MPE Roll
    stops a scanner that takes too long, but can't once it has been closed (or has crashed), and a
    scanner stuck inside a plugin would otherwise stay around for good.
*/
inline constexpr int watchdogMarginMs = 5000;

/** What MPE Roll writes to the job file: the format to try, the file, and what the scanner needs to
    end itself if MPE Roll never gets round to it. The first line names this layout, so a scanner from
    another version of MPE Roll turns the job down rather than misreading it.
*/
struct Job
{
    juce::String formatName, fileOrIdentifier;
    juce::int64 parentProcessId = 0;    // MPE Roll's process: the scanner ends once it's gone...
    int timeoutMs = defaultTimeoutMs;   // ...or this long plus watchdogMarginMs after it started

    static constexpr const char* header = "MPE Roll scan job 2";

    juce::String toText() const
    {
        return juce::String (header) + "\n" + formatName + "\n" + fileOrIdentifier + "\n"
             + juce::String (parentProcessId) + "\n" + juce::String (timeoutMs) + "\n";
    }

    /** False for text that isn't a job in this layout. */
    static bool parse (const juce::String& text, Job& job)
    {
        const auto lines = juce::StringArray::fromLines (text);

        if (lines.size() < 5 || lines[0] != header)
            return false;

        job.formatName = lines[1];
        job.fileOrIdentifier = lines[2];
        job.parentProcessId = lines[3].getLargeIntValue();
        job.timeoutMs = lines[4].getIntValue();
        return true;
    }
};

/** This process's ID, as the job tells the scanner. */
juce::int64 currentProcessId();

/** The scanner program: next to MPE Roll's own binary, or in the standard VST3 install. Returns
    File() if it isn't there, in which case plugins are looked at inside this process as before.
*/
juce::File findExecutable();

/** Tests: use this scanner program instead (File() to go back to looking for it). */
void setExecutableForTesting (const juce::File&);

enum class Outcome
{
    found,          // the file loaded; results lists its plugins (possibly none)
    crashed,        // the scanner went down while loading it
    timedOut,       // it took too long, so the scanner was stopped
    cancelled,      // the caller asked to stop
    unavailable,    // there's no scanner program, or it went down before it got to the file
    outdated        // the scanner program is there but turned the job down: one from another
                    // version of MPE Roll, most likely, so the file wasn't looked at anywhere
};

/** Looks at one file in a scanner process of its own. Blocks; any thread. */
Outcome findTypes (const juce::String& formatName, const juce::String& fileOrIdentifier,
                   juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs = defaultTimeoutMs,
                   const std::function<bool()>& shouldStop = {});

/** Describes an outcome other than found, for showing to the user. */
juce::String describeFailure (Outcome, const juce::String& fileName);

/** For KnownPluginList: scans each file with findTypes. A file that crashes or hangs is reported as
    failed, so the list blacklists it. Without a scanner program, files are scanned in this process
    as before. With one that turns jobs down (Outcome::outdated), no file is looked at anywhere, and
    the user is told so once per scan.
*/
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (int timeoutMs = defaultTimeoutMs);

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

private:
    void scanFinished() override;

    const int timeoutMs;
    std::atomic<bool> toldScannerOutdated { false };
    std::shared_ptr<juce::ScopedMessageBox> outdatedBox = std::make_shared<juce::ScopedMessageBox>();  // closes with this scanner
};

} // namespace mperoll::PluginScanner
