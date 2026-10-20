// MPE Roll Scanner: started by MPE Roll to look inside plugin files, so that a plugin which crashes or
// hangs while being loaded takes down only this process, not MPE Roll and the DAW around it.
// Talks to MPE Roll through JUCE's ChildProcessCoordinator/Worker, as JUCE's AudioPluginHost does.

#include "plugin/PluginScanner.h"

#if MPEROLL_HOST_CLAP
 #include "plugin/clap/ClapHosting.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <queue>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace
{

class ScannerWorker final : private juce::ChildProcessWorker,
                            private juce::AsyncUpdater
{
public:
    ScannerWorker()
    {
        juce::addDefaultFormatsToManager (formatManager);

       #if MPEROLL_HOST_CLAP
        formatManager.addFormat (std::make_unique<mperoll::ClapPluginFormat>());
       #endif
    }

    ~ScannerWorker() override
    {
        cancelPendingUpdate();
    }

    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    juce::AudioPluginFormatManager formatManager;
    std::mutex mutex;
    std::queue<juce::MemoryBlock> pending;

    void handleMessageFromCoordinator (const juce::MemoryBlock& block) override
    {
        if (block.isEmpty())
            return;

        {
            const std::lock_guard<std::mutex> lock (mutex);
            pending.push (block);
        }

        // Plugins are loaded on the message thread, as they expect.
        triggerAsyncUpdate();
    }

    void say (const juce::String& text)
    {
        sendMessageToCoordinator ({ text.toRawUTF8(), text.getNumBytesAsUTF8() });
    }

    void handleConnectionMade() override
    {
        say (mperoll::PluginScanner::readyMessage);
    }

    void handleConnectionLost() override
    {
        // MPE Roll has gone, or wants this process gone (after a plugin took too long, say). Leave
        // at once: the message thread may be stuck inside that plugin, so don't wait for it. On
        // Windows, _Exit would still run every DLL's detach code, the stuck plugin's included.
       #if JUCE_WINDOWS
        TerminateProcess (GetCurrentProcess(), 0);
       #endif
        std::_Exit (0);
    }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            juce::MemoryBlock block;

            {
                const std::lock_guard<std::mutex> lock (mutex);

                if (pending.empty())
                    return;

                block = pending.front();
                pending.pop();
            }

            say (mperoll::PluginScanner::startedMessage);
            sendMessageToCoordinator (scan (block));
        }
    }

    juce::MemoryBlock scan (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream stream (block, false);
        const auto formatName = stream.readString();
        const auto identifier = stream.readString();

        juce::XmlElement list ("LIST");

        for (auto* format : formatManager.getFormats())
        {
            if (format->getName() != formatName)
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, identifier);

            for (auto* description : found)
                list.addChildElement (description->createXml().release());
        }

        const auto text = list.toString();
        return { text.toRawUTF8(), text.getNumBytesAsUTF8() };
    }
};

#if JUCE_WINDOWS
LONG WINAPI exitQuietly (EXCEPTION_POINTERS*)
{
    TerminateProcess (GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_WINDOWS
    // A plugin that crashes here just ends this process: no Windows error dialog for the user.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter (exitQuietly);
   #endif

    juce::ScopedJuceInitialiser_GUI juceInit;
    ScannerWorker worker;

    if (! worker.initialiseFromCommandLine (juce::StringArray (argv + 1, argc - 1).joinIntoString (" "),
                                            mperoll::PluginScanner::processId))
    {
        std::printf ("MPE Roll Scanner is started by MPE Roll to look at plugins safely.\n"
                     "It isn't meant to be run on its own.\n");
        return 1;
    }

    juce::MessageManager::getInstance()->runDispatchLoop();
    return 0;
}
