#include "PluginScanner.h"
#include "LoadDiagnosis.h"
#include "PluginLog.h"

#include <algorithm>

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
namespace
{
    // Runs the scanner on the job in files and waits for it. Returns found once the scanner has
    // ended after reading the job (what it wrote is for the caller to read), or why it didn't get that far.
    Outcome runScanner (const juce::File& executable, const JobFolder& files, int timeoutMs, const std::function<bool()>& shouldStop)
    {
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
        return ready() ? Outcome::found : Outcome::outdated;
    }
}

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

    if (const auto outcome = runScanner (executable, files, timeoutMs, shouldStop); outcome != Outcome::found)
        return outcome;

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

Diagnosis diagnose (const juce::PluginDescription& description, int timeoutMs, const std::function<bool()>& shouldStop)
{
    Diagnosis diagnosis;
    const auto executable = findExecutable();

    if (executable == juce::File())
        return diagnosis;

    const JobFolder files;
    DiagnoseJob job;
    job.parentProcessId = currentProcessId();
    job.timeoutMs = timeoutMs;

    if (! files.job.replaceWithText (job.toText())
        || ! descriptionFileFor (files.job).replaceWithText (description.createXml()->toString()))
        return diagnosis;

    diagnosis.outcome = runScanner (executable, files, timeoutMs, shouldStop);
    diagnosis.lines.addLines (resultFileFor (files.job).loadFileAsString());
    diagnosis.lines.removeEmptyStrings();

    // It ends with a verdict, unless the plugin took the scanner down first.
    const auto finished = std::any_of (diagnosis.lines.begin(), diagnosis.lines.end(),
                                       [] (const juce::String& line) { return line.startsWith (LoadDiagnosis::verdictPrefix); });

    if (diagnosis.outcome == Outcome::found && ! finished)
        diagnosis.outcome = startedMarkerFor (files.job).exists() ? Outcome::crashed : Outcome::unavailable;

    return diagnosis;
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

namespace
{
    const char* const activationAdvice =
        "A plugin usually does this when it isn't activated or authorised on this computer yet. Open it once "
        "directly in FL Studio, or sign in to its maker's app (Roland Cloud Manager for ZENOLOGY and the other "
        "Roland Cloud instruments, Arturia Software Center, Native Access, iLok License Manager...) and check "
        "it's authorised there, then try again. Load synth > Show the plugin log has what MPE Roll saw.";
}

juce::String describeRefusedToStart (const juce::StringArray& fileNames, bool fromScan)
{
    // JUCE leaves out a VST3 whose component won't initialise, so it looks like a file with nothing in it.
    if (fileNames.size() == 1 && ! fromScan)
        return fileNames[0] + " didn't start when MPE Roll looked inside it, so there was nothing to load.\n\n"
               + activationAdvice;

    return juce::String (fileNames.size() == 1 ? "This plugin didn't start when MPE Roll looked inside it, so it isn't listed: "
                                               : "These plugins didn't start when MPE Roll looked inside them, so they aren't listed: ")
         + fileNames.joinIntoString (", ") + ".\n\n" + activationAdvice;
}

bool mightRefuseToStart (const juce::String& fileOrIdentifier)
{
    // A .vst3 or .clap is certainly a plugin, so finding nothing in one means it wouldn't start.
    // A .dll may be any library that happens to sit in a VST folder.
    return fileOrIdentifier.endsWithIgnoreCase (".vst3") || fileOrIdentifier.endsWithIgnoreCase (".clap");
}

juce::String describeFailure (Outcome outcome, const juce::String& fileName)
{
    switch (outcome)
    {
        case Outcome::crashed:
            return fileName + " crashed while MPE Roll was checking it, so it wasn't loaded: loading it "
                              "here would most likely have crashed MPE Roll and FL Studio with it.";
        case Outcome::timedOut:
            return fileName + " took too long to load while MPE Roll was checking it, so it wasn't loaded. "
                              "Some plugins wait for a licence or sign-in window the first time: open it once "
                              "directly in FL Studio, then try again.";
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

    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto before = result.size();
    const auto outcome = findTypes (format.getName(), fileOrIdentifier, result, timeoutMs, [this] { return shouldExit(); });
    const auto took = juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);

    if (outcome == Outcome::found)
    {
        PluginLog::add ("Scan: " + fileOrIdentifier + ": " + juce::String (result.size() - before) + " plugin(s), "
                        + juce::String (took) + " s");
        noteIfRefused (fileOrIdentifier, result.size() == before);
    }
    else if (outcome != Outcome::cancelled)
        PluginLog::add ("Scan: " + fileOrIdentifier + ": " + describeFailure (outcome, format.getName() + " file")
                        + " (" + juce::String (took) + " s)");

    if (outcome == Outcome::unavailable)
    {
        // No scanner program, or one that didn't get as far as the file: look in this process, as before.
        format.findAllTypesForFile (result, fileOrIdentifier);
        noteIfRefused (fileOrIdentifier, result.size() == before);
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

void OutOfProcessScanner::noteIfRefused (const juce::String& fileOrIdentifier, bool foundNothing)
{
    if (! foundNothing || ! mightRefuseToStart (fileOrIdentifier))
        return;

    // Scans run on threads of their own.
    const juce::ScopedLock lock (skippedLock);
    refusedFiles.addIfNotAlreadyThere (juce::File::isAbsolutePath (fileOrIdentifier)
                                           ? juce::File (fileOrIdentifier).getFileName() : fileOrIdentifier);
}

void OutOfProcessScanner::scanFinished()
{
    toldScannerOutdated = false;

    juce::StringArray skipped, refused;
    {
        const juce::ScopedLock lock (skippedLock);
        skipped.swapWith (flStudioNativeFiles);
        refused.swapWith (refusedFiles);
    }

    if (skipped.isEmpty() && refused.isEmpty())
        return;

    juce::StringArray paragraphs;

    if (! refused.isEmpty())
        paragraphs.add (describeRefusedToStart (refused, true));

    if (! skipped.isEmpty())
        paragraphs.add (describeFlStudioNativeFiles (skipped));

    const auto title = refused.isEmpty() ? "FL Studio's own plugins can't be hosted"
                                         : (skipped.isEmpty() ? "Some plugins didn't start"
                                                              : "Some plugins weren't added");

    // Held by this scanner, like the out-of-date box, so it closes with it.
    juce::MessageManager::callAsync ([box = std::weak_ptr<juce::ScopedMessageBox> (nativeBox),
                                      message = paragraphs.joinIntoString ("\n\n"), title = juce::String (title)]
    {
        if (auto held = box.lock())
        {
            const auto options = juce::MessageBoxOptions()
                                     .withIconType (juce::MessageBoxIconType::InfoIcon)
                                     .withTitle (title)
                                     .withMessage (message)
                                     .withButton ("OK");
            *held = juce::AlertWindow::showScopedAsync (options, nullptr);
        }
    });
}

} // namespace mperoll::PluginScanner
