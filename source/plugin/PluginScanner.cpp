#include "PluginScanner.h"

#include <chrono>
#include <condition_variable>

namespace mperoll::PluginScanner
{

namespace
{
    juce::File executableForTesting;
    bool useExecutableForTesting = false;

    // A scanner whose message thread is free says it has started on a file at once; one that hasn't
    // after this long is stuck in something left over from before.
    constexpr int startTimeoutMs = 20000;
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
class OutOfProcessScanner::Process final : private juce::ChildProcessCoordinator
{
public:
    ~Process() override
    {
        // Before the members the connection's callbacks use are destroyed. The scanner exits when the
        // connection closes, even if it's stuck inside a plugin.
        killWorkerProcess();
    }

    bool launch (const juce::File& executable)
    {
        // No output streams: nothing reads them, and a full pipe would stall the scanner.
        if (! launchWorkerProcess (executable, processId, 0, 0))
            return false;

        // Until the scanner says it's there, nothing would notice it failing to start.
        std::unique_lock<std::mutex> lock (mutex);
        return condition.wait_for (lock, std::chrono::seconds (20), [this] { return ready || lost; }) && ready && ! lost;
    }

    Outcome scan (const juce::String& formatName, const juce::String& fileOrIdentifier,
                  juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs,
                  const std::function<bool()>& shouldStop)
    {
        {
            const std::lock_guard<std::mutex> lock (mutex);
            reply.reset();
            started = replied = false;

            if (lost)
                return Outcome::crashed;
        }

        juce::MemoryBlock request;

        {
            juce::MemoryOutputStream stream (request, false);
            stream.writeString (formatName);
            stream.writeString (fileOrIdentifier);
        }

        if (! sendMessageToWorker (request))
            return Outcome::crashed;

        const auto sent = juce::Time::getMillisecondCounterHiRes();
        const auto deadline = sent + timeoutMs;
        const auto startDeadline = sent + juce::jmin (timeoutMs, startTimeoutMs);
        auto nextProbe = sent;
        std::unique_lock<std::mutex> lock (mutex);

        while (! replied && ! lost)
        {
            if (condition.wait_for (lock, std::chrono::milliseconds (50), [this] { return replied || lost; }))
                break;

            if (shouldStop != nullptr && shouldStop())
                return Outcome::cancelled;

            const auto now = juce::Time::getMillisecondCounterHiRes();

            if (now > deadline || (! started && now > startDeadline))
                return Outcome::timedOut;

            // JUCE reports a scanner gone only through the message thread, which may be this one, or
            // (on Linux) after a long silence. Writing to a scanner that's gone fails at once. The
            // scanner ignores empty messages.
            if (now >= nextProbe)
            {
                nextProbe = now + 250.0;
                lock.unlock();
                const auto alive = sendMessageToWorker ({});
                lock.lock();

                // Its answer may still be on its way through the pipe.
                if (! alive)
                {
                    condition.wait_for (lock, std::chrono::milliseconds (250), [this] { return replied; });
                    break;
                }
            }
        }

        if (! replied)
            return Outcome::crashed;

        ++filesLookedAt;

        if (reply != nullptr)
        {
            for (const auto* item : reply->getChildIterator())
            {
                auto description = std::make_unique<juce::PluginDescription>();

                if (description->loadFromXml (*item))
                    results.add (std::move (description));
            }
        }

        return Outcome::found;
    }

    /** Whether the last scan() got as far as the file. If not, it failed through no fault of the file's. */
    bool reachedFile()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return started;
    }

    /** Whether this scanner has loaded other files, any of which could have left something running in it. */
    bool hasLookedAtOtherFiles() const noexcept     { return filesLookedAt > 0; }

private:
    std::mutex mutex;
    std::condition_variable condition;
    std::unique_ptr<juce::XmlElement> reply;
    bool ready = false, started = false, replied = false, lost = false;
    int filesLookedAt = 0;

    // These arrive on the connection's own thread, so they get through while the caller waits.
    void handleMessageFromWorker (const juce::MemoryBlock& block) override
    {
        const auto text = block.toString();

        if (text == readyMessage || text == startedMessage)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            (text == readyMessage ? ready : started) = true;
            condition.notify_all();
            return;
        }

        auto xml = juce::parseXML (text);

        const std::lock_guard<std::mutex> lock (mutex);
        reply = std::move (xml);
        replied = true;
        condition.notify_all();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        lost = true;
        condition.notify_all();
    }
};

//==============================================================================
Outcome findTypes (const juce::String& formatName, const juce::String& fileOrIdentifier,
                   juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs)
{
    const auto executable = findExecutable();

    if (executable == juce::File())
        return Outcome::unavailable;

    OutOfProcessScanner::Process process;

    if (! process.launch (executable))
        return Outcome::unavailable;

    const auto outcome = process.scan (formatName, fileOrIdentifier, results, timeoutMs, {});

    // A fresh scanner that failed before it reached the file says nothing about the file.
    if (outcome != Outcome::found && ! process.reachedFile())
        return Outcome::unavailable;

    return outcome;
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

OutOfProcessScanner::~OutOfProcessScanner() = default;

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    const std::lock_guard<std::mutex> lock (mutex);

    for (int attempt = 0;; ++attempt)
    {
        if (process == nullptr)
        {
            const auto executable = findExecutable();
            auto fresh = std::make_unique<Process>();

            if (executable == juce::File() || ! fresh->launch (executable))
            {
                // A scanner that just worked won't start again: skip the file rather than risk it here.
                if (attempt > 0)
                    return true;

                // No scanner program: look in this process, as before.
                format.findAllTypesForFile (result, fileOrIdentifier);
                return true;
            }

            process = std::move (fresh);
        }

        const auto afterOtherFiles = process->hasLookedAtOtherFiles();
        const auto outcome = process->scan (format.getName(), fileOrIdentifier, result, timeoutMs,
                                            [this] { return shouldExit(); });

        if (outcome == Outcome::found || outcome == Outcome::cancelled)
            return true;

        // It crashed or hung: whatever comes next gets a new scanner.
        const auto reachedFile = process->reachedFile();
        process.reset();

        // Not this file's doing if the scanner never reached it, and perhaps not if an earlier file left
        // something behind that crashed it: look again in a fresh scanner. A file that hung once it was
        // reached is blamed straight away, rather than given the whole time allowed again.
        if (attempt == 0 && (! reachedFile || (afterOtherFiles && outcome == Outcome::crashed)))
            continue;

        // Listed as failed, unless the scanner never got to it at all: then the next scan tries it again.
        return ! reachedFile;
    }
}

void OutOfProcessScanner::scanFinished()
{
    const std::lock_guard<std::mutex> lock (mutex);
    process.reset();
}

} // namespace mperoll::PluginScanner
