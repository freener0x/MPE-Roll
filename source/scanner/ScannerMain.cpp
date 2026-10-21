// MPE Roll Scanner: started by MPE Roll to look inside one plugin file, so that a plugin which crashes
// or hangs while being loaded takes down only this process, not MPE Roll and the DAW around it.
// See PluginScanner.h for how the two talk.

#include "plugin/PluginScanner.h"

#if MPEROLL_HOST_CLAP
 #include "plugin/clap/ClapHosting.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shellapi.h>

 #ifdef _MSC_VER
  #pragma comment (lib, "shell32.lib")
 #endif
#else
 #include <cerrno>
 #include <signal.h>
 #include <unistd.h>
#endif

namespace
{

#if JUCE_WINDOWS
LONG WINAPI exitQuietly (EXCEPTION_POINTERS*)
{
    TerminateProcess (GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

juce::StringArray arguments ([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    juce::StringArray result;

   #if JUCE_WINDOWS
    // argv is in the ANSI code page, which can't hold every path (a user name with accents, say).
    int count = 0;

    if (auto** wide = CommandLineToArgvW (GetCommandLineW(), &count))
    {
        for (int i = 1; i < count; ++i)
            result.add (juce::String (wide[i]));

        LocalFree (wide);
    }
   #else
    for (int i = 1; i < argc; ++i)
        result.add (juce::String::fromUTF8 (argv[i]));
   #endif

    return result;
}

[[noreturn]] void leave()
{
    // Without running the plugins' clean-up code: what was found is saved, and that code is
    // where badly behaved plugins tend to crash or hang.
    std::fflush (nullptr);

   #if JUCE_WINDOWS
    TerminateProcess (GetCurrentProcess(), 0);
   #endif
    std::_Exit (0);
}

// Ends this process as soon as MPE Roll's has gone, or once the job's time plus a margin is up.
// MPE Roll stops a scanner that takes too long, but can't once it has been closed (or has crashed),
// and a scanner stuck inside a plugin would otherwise stay around for good. The main thread may be
// stuck in the plugin, so the process is ended from here without waiting for it.
void startWatchdog (const mperoll::PluginScanner::Job& job)
{
    const auto deadlineMs = juce::jmax (0, job.timeoutMs) + mperoll::PluginScanner::watchdogMarginMs;

    std::thread ([parent = job.parentProcessId, deadlineMs]
    {
       #if JUCE_WINDOWS
        if (auto* parentProcess = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) parent))
            WaitForSingleObject (parentProcess, (DWORD) deadlineMs);   // returns when MPE Roll ends, or at the deadline
        else if (GetLastError() != ERROR_INVALID_PARAMETER)             // no such process means MPE Roll has gone already
            Sleep ((DWORD) deadlineMs);                                 // can't watch it, so only the deadline applies

        TerminateProcess (GetCurrentProcess(), 4);
       #else
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (deadlineMs);

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (parent > 0 && ::kill ((pid_t) parent, 0) != 0 && errno == ESRCH)
                break;

            std::this_thread::sleep_for (std::chrono::milliseconds (250));
        }

        std::_Exit (4);
       #endif
    }).detach();
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_WINDOWS
    // A plugin that crashes here just ends this process: no Windows error dialog for the user.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter (exitQuietly);
   #endif

    const auto args = arguments (argc, argv);

    if (args.size() < 2 || args[0] != mperoll::PluginScanner::scanArgument)
    {
        std::printf ("MPE Roll Scanner is started by MPE Roll to look at plugins safely.\n"
                     "It isn't meant to be run on its own.\n");
        return 1;
    }

    const juce::File jobFile (args[1]);
    mperoll::PluginScanner::Job job;

    // A job in another layout comes from another version of MPE Roll: leave without the ready
    // marker, and that MPE Roll tells the user the two don't match.
    if (! mperoll::PluginScanner::Job::parse (jobFile.loadFileAsString(), job))
        return 1;

    mperoll::PluginScanner::readyMarkerFor (jobFile).create();
    startWatchdog (job);

    // Plugins are loaded on the message thread, as they expect.
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

   #if MPEROLL_HOST_CLAP
    formats.addFormat (std::make_unique<mperoll::ClapPluginFormat>());
   #endif

    juce::XmlElement list ("LIST");

    for (auto* format : formats.getFormats())
    {
        if (format->getName() != job.formatName)
            continue;

        // From here on, a crash or hang is the plugin's.
        mperoll::PluginScanner::startedMarkerFor (jobFile).create();

        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, job.fileOrIdentifier);

        for (auto* description : found)
            list.addChildElement (description->createXml().release());
    }

    mperoll::PluginScanner::resultFileFor (jobFile).replaceWithText (list.toString());
    leave();
}
