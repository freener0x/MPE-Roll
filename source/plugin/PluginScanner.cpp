#include "PluginScanner.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <unistd.h>
#endif

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

juce::int64 currentProcessId()
{
   #if JUCE_WINDOWS
    return (juce::int64) GetCurrentProcessId();
   #else
    return (juce::int64) getpid();
   #endif
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
    Job job;
    job.formatName = formatName;
    job.fileOrIdentifier = fileOrIdentifier;
    job.parentProcessId = currentProcessId();
    job.timeoutMs = timeoutMs;

    if (! files.job.replaceWithText (job.toText()))
        return Outcome::unavailable;

    juce::ChildProcess process;

    // No output streams: nothing reads them, and a full pipe would stall the scanner.
    if (! process.start (juce::StringArray { executable.getFullPathName(), scanArgument, files.job.getFullPathName() }, 0))
        return Outcome::unavailable;

    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    const auto stop = [&process] { process.kill(); process.waitForProcessToFinish (1000); };
    const auto ready = [&files] { return readyMarkerFor (files.job).exists(); };
    const auto started = [&files] { return startedMarkerFor (files.job).exists(); };

    while (! process.waitForProcessToFinish (50))
    {
        if (shouldStop != nullptr && shouldStop())
        {
            stop();
            return Outcome::cancelled;
        }

        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startTime;

        if (elapsed > timeoutMs || (! started() && elapsed > startTimeoutMs))
        {
            stop();
            return started() ? Outcome::timedOut : ready() ? Outcome::unavailable : Outcome::outdated;
        }
    }

    // A scanner that left without reading the job is from another version of MPE Roll (it turns
    // the job down) or is broken: either way, nothing can be said about the file.
    if (! ready())
        return Outcome::outdated;

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

    return started() ? Outcome::crashed : Outcome::unavailable;
}

namespace
{
    juce::String describeOutdatedScanner (const juce::String& consequence)
    {
        return "The MPE Roll Scanner installed here is older than this MPE Roll (or isn't working), so "
               + consequence + " Install the MPE Roll Scanner that came with this version of MPE Roll: it's inside "
                 "the .vst3 folder, and goes next to MPE Roll.dll.";
    }
}

bool isFlStudioNativePluginFile (const juce::String& path)
{
    // FL's own plugins live under its install folder, in Plugins\Fruity\Generators and Effects.
    const auto p = path.replaceCharacter ('\\', '/');
    return p.containsIgnoreCase ("/Plugins/Fruity/") || p.containsIgnoreCase ("/Image-Line/FL Studio");
}

juce::String describeFlStudioNativeFiles (const juce::StringArray& files)
{
    return juce::String (files.size() == 1 ? "This file is one of FL Studio's own plugins: "
                                           : "These files are FL Studio's own plugins: ")
         + files.joinIntoString (", ") + ".\n\n"
           "Those come in Image-Line's own format, which only FL Studio can load, so MPE Roll can't host them "
           "and has left them out of the scan. To play them with what you write in MPE Roll, use "
           "File > Send to FL piano roll, which rebuilds the clip in FL's own piano roll.";
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
        case Outcome::outdated:
            return describeOutdatedScanner (fileName + " couldn't be checked, and so wasn't loaded.");
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
    // FL's own plugins hold no VST or CLAP, so there's nothing to start a scanner for. Listed as
    // failed they'd look broken, left out silently they'd look lost: so they're left out and named
    // once the scan is over, with where to find them instead.
    if (isFlStudioNativePluginFile (fileOrIdentifier))
    {
        const juce::ScopedLock lock (skippedLock);
        flStudioNativeFiles.addIfNotAlreadyThere (juce::File::isAbsolutePath (fileOrIdentifier)
                                                      ? juce::File (fileOrIdentifier).getFileName() : fileOrIdentifier);
        return true;
    }

    const auto outcome = findTypes (format.getName(), fileOrIdentifier, result, timeoutMs, [this] { return shouldExit(); });

    if (outcome == Outcome::unavailable)
    {
        // No scanner program, or one that didn't get as far as the file: look in this process, as before.
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    if (outcome == Outcome::outdated)
    {
        // The scanner turned the job down, so the file wasn't looked at anywhere. Not here either:
        // that's where a crashing plugin takes FL down. It isn't the file's fault, so it isn't set
        // aside, and the user is told what's wrong once per scan. The box is held by this scanner, so
        // it closes with it; if the scanner has gone by the time the message thread gets here (the
        // synth was deleted mid-scan), the box isn't shown at all.
        if (! toldScannerOutdated.exchange (true))
        {
            juce::MessageManager::callAsync ([box = std::weak_ptr<juce::ScopedMessageBox> (outdatedBox)]
            {
                if (auto held = box.lock())
                {
                    const auto options = juce::MessageBoxOptions()
                                             .withIconType (juce::MessageBoxIconType::WarningIcon)
                                             .withTitle ("MPE Roll Scanner is out of date")
                                             .withMessage (describeOutdatedScanner ("plugins can't be checked, and this scan lists none."))
                                             .withButton ("OK");
                    *held = juce::AlertWindow::showScopedAsync (options, nullptr);
                }
            });
        }

        return true;
    }

    // A file that crashed or hung is listed as failed.
    return outcome == Outcome::found || outcome == Outcome::cancelled;
}

void OutOfProcessScanner::scanFinished()
{
    toldScannerOutdated = false;

    juce::StringArray skipped;
    {
        const juce::ScopedLock lock (skippedLock);
        skipped.swapWith (flStudioNativeFiles);
    }

    if (skipped.isEmpty())
        return;

    // Held by this scanner, like the out-of-date box, so it closes with it.
    juce::MessageManager::callAsync ([box = std::weak_ptr<juce::ScopedMessageBox> (nativeBox), skipped]
    {
        if (auto held = box.lock())
        {
            const auto options = juce::MessageBoxOptions()
                                     .withIconType (juce::MessageBoxIconType::InfoIcon)
                                     .withTitle ("FL Studio's own plugins can't be hosted")
                                     .withMessage (describeFlStudioNativeFiles (skipped))
                                     .withButton ("OK");
            *held = juce::AlertWindow::showScopedAsync (options, nullptr);
        }
    });
}

} // namespace mperoll::PluginScanner
