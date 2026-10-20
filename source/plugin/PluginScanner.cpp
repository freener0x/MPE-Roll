#include "PluginScanner.h"

#include <chrono>
#include <condition_variable>

namespace mperoll::PluginScanner
{

namespace
{
    juce::File executableForTesting;
    bool useExecutableForTesting = false;
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

            if (lost)
                return Outcome::crashed;

            reply.reset();
            replied = false;
        }

        juce::MemoryBlock request;

        {
            juce::MemoryOutputStream stream (request, false);
            stream.writeString (formatName);
            stream.writeString (fileOrIdentifier);
        }

        if (! sendMessageToWorker (request))
            return Outcome::crashed;

        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        std::unique_lock<std::mutex> lock (mutex);

        while (! replied && ! lost)
        {
            if (condition.wait_for (lock, std::chrono::milliseconds (50), [this] { return replied || lost; }))
                break;

            if (shouldStop != nullptr && shouldStop())
                return Outcome::cancelled;

            if (juce::Time::getMillisecondCounterHiRes() > deadline)
                return Outcome::timedOut;
        }

        if (! replied)
            return Outcome::crashed;

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

private:
    std::mutex mutex;
    std::condition_variable condition;
    std::unique_ptr<juce::XmlElement> reply;
    bool ready = false, replied = false, lost = false;

    // Both arrive on the connection's own thread, so they get through while the caller waits.
    void handleMessageFromWorker (const juce::MemoryBlock& block) override
    {
        const auto text = block.toString();

        if (text == readyMessage)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            ready = true;
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

    return process.scan (formatName, fileOrIdentifier, results, timeoutMs, {});
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

    if (process == nullptr)
    {
        const auto executable = findExecutable();
        auto fresh = std::make_unique<Process>();

        if (executable == juce::File() || ! fresh->launch (executable))
        {
            // No scanner program: look in this process, as before.
            format.findAllTypesForFile (result, fileOrIdentifier);
            return true;
        }

        process = std::move (fresh);
    }

    const auto outcome = process->scan (format.getName(), fileOrIdentifier, result, timeoutMs,
                                        [this] { return shouldExit(); });

    if (outcome == Outcome::found || outcome == Outcome::cancelled)
        return true;

    // It crashed or hung: the next file gets a new scanner, and this one is listed as failed.
    process.reset();
    return false;
}

void OutOfProcessScanner::scanFinished()
{
    const std::lock_guard<std::mutex> lock (mutex);
    process.reset();
}

} // namespace mperoll::PluginScanner
