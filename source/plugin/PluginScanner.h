#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace mperoll::PluginScanner
{

/** Lists the plugins in a file by loading it in a separate process, the scanner program shipped with
    MPE Roll ("MPE Roll Scanner"), started afresh for each file. A plugin that crashes or hangs while
    it's being looked at then takes down only that process, not FL Studio, and nothing it leaves
    behind can affect the next file.

    The two talk through files: MPE Roll writes a job (the format's name, then the file or
    identifier, one per line) and starts the scanner with scanArgument and the job's path. The
    scanner creates the started marker just before it loads anything, and writes the result (a LIST
    of plugin descriptions) once it has looked.
*/

/** The scanner's first argument; the second is the job file. */
inline constexpr const char* scanArgument = "--mperoll-scan";

inline juce::File startedMarkerFor (const juce::File& job)  { return job.getSiblingFile (job.getFileName() + ".started"); }
inline juce::File resultFileFor (const juce::File& job)     { return job.getSiblingFile (job.getFileName() + ".result"); }

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
    crashed,        // the scanner went down while loading it
    timedOut,       // it took too long, so the scanner was stopped
    cancelled,      // the caller asked to stop
    unavailable     // there's no scanner program, or it went down before it got to the file
};

/** Looks at one file in a scanner process of its own. Blocks; any thread. */
Outcome findTypes (const juce::String& formatName, const juce::String& fileOrIdentifier,
                   juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs = defaultTimeoutMs,
                   const std::function<bool()>& shouldStop = {});

/** Describes an outcome other than found, for showing to the user. */
juce::String describeFailure (Outcome, const juce::String& fileName);

/** For KnownPluginList: scans each file with findTypes. A file that crashes or hangs is reported as
    failed, so the list blacklists it. Without a working scanner program, files are scanned in this
    process as before.
*/
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (int timeoutMs = defaultTimeoutMs);

    bool findPluginTypesFor (juce::AudioPluginFormat&, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

private:
    const int timeoutMs;
};

} // namespace mperoll::PluginScanner
