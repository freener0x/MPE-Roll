#include "PluginScanner.h"

namespace mperoll::PluginScanner
{

namespace
{
    juce::File executableForTesting;
    bool useExecutableForTesting = false;

    // A scanner that hasn't got as far as loading the plugin after this long is stuck in something
    // that isn't the plugin's doing.
    constexpr int startTimeoutMs = 20000;

    // The job and what the scanner writes back, in a folder of their own that goes when this does.
    // Scans can run on several threads at once, hence the unique name.
    struct JobFolder
    {
        JobFolder() : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("MPE Roll scan " + juce::Uuid().toDashedString()))
        {
            folder.createDirectory();
        }

        ~JobFolder()    { folder.deleteRecursively(); }

        const juce::File folder;
        const juce::File job { folder.getChildFile ("job.txt") };
    };
}

juce::File findExecutable()
{
    if (useExecutableForTesting)
        return executableForTesting;

   #if JUCE_WINDOWS
    const juce::String name ("MPE Roll Scanner.exe");
   #else
    const juce::String name ("MPE Roll Scanner");
   #endif

    // Next to MPE Roll's own binary: inside the VST3 bundle, or beside the .dll or the app.
    juce::Array<juce::File> candidates { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (name) };

    // A VST2 .dll or the app installed somewhere else can use the one in the installed VST3.
   #if JUCE_WINDOWS
    candidates.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                        .getChildFile ("Common Files\\VST3\\MPE Roll.vst3\\Contents\\x86_64-win")
                        .getChildFile (name));
   #elif JUCE_LINUX
    candidates.add (juce::File ("~/.vst3/MPE Roll.vst3/Contents/x86_64-linux").getChildFile (name));
   #endif

    for (const auto& candidate : candidates)
        if (candidate.existsAsFile())
            return candidate;

    return {};
}

void setExecutableForTesting (const juce::File& executable)
{
    executableForTesting = executable;
    useExecutableForTesting = executable != juce::File();
}

//==============================================================================
Outcome findTypes (const juce::String& formatName, const juce::String& fileOrIdentifier,
                   juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs,
                   const std::function<bool()>& shouldStop)
{
    const auto executable = findExecutable();

    if (executable == juce::File())
        return Outcome::unavailable;

    const JobFolder files;

    if (! files.job.replaceWithText (formatName + "\n" + fileOrIdentifier + "\n"))
        return Outcome::unavailable;

    juce::ChildProcess process;

    // No output streams: nothing reads them, and a full pipe would stall the scanner.
    if (! process.start (juce::StringArray { executable.getFullPathName(), scanArgument, files.job.getFullPathName() }, 0))
        return Outcome::unavailable;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    const auto stop = [&process] { process.kill(); process.waitForProcessToFinish (1000); };

    while (! process.waitForProcessToFinish (50))
    {
        if (shouldStop != nullptr && shouldStop())
        {
            stop();
            return Outcome::cancelled;
        }

        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startTime;
        const auto started = startedMarkerFor (files.job).exists();

        if (elapsed > timeoutMs || (! started && elapsed > startTimeoutMs))
        {
            stop();
            return started ? Outcome::timedOut : Outcome::unavailable;
        }
    }

    // Written in one go once the plugin has been looked at, so a scanner that went down after that
    // (in the plugin's clean-up code, say) still counts.
    if (auto list = juce::parseXML (resultFileFor (files.job)))
    {
        for (const auto* item : list->getChildIterator())
        {
            auto description = std::make_unique<juce::PluginDescription>();

            if (description->loadFromXml (*item))
                results.add (std::move (description));
        }

        return Outcome::found;
    }

    return startedMarkerFor (files.job).exists() ? Outcome::crashed : Outcome::unavailable;
}

juce::String describeFailure (Outcome outcome, const juce::String& fileName)
{
    switch (outcome)
    {
        case Outcome::crashed:
            return fileName + " crashed while MPE Roll was checking it, so it wasn't loaded: loading it "
                              "here would most likely have crashed MPE Roll and FL Studio with it.";
        case Outcome::timedOut:
            return fileName + " took too long to load while MPE Roll was checking it, so it wasn't loaded.";
        case Outcome::cancelled:
            return "Checking " + fileName + " was cancelled.";
        case Outcome::unavailable:
            return "MPE Roll Scanner isn't installed, so " + fileName + " couldn't be checked first.";
        case Outcome::found:
            break;
    }

    return {};
}

//==============================================================================
OutOfProcessScanner::OutOfProcessScanner (int timeout) : timeoutMs (timeout) {}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    const auto outcome = findTypes (format.getName(), fileOrIdentifier, result, timeoutMs, [this] { return shouldExit(); });

    if (outcome == Outcome::unavailable)
    {
        // No scanner program, or one that didn't get as far as the file: look in this process, as before.
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    // A file that crashed or hung is listed as failed.
    return outcome == Outcome::found || outcome == Outcome::cancelled;
}

} // namespace mperoll::PluginScanner
