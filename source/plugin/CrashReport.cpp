#include "CrashReport.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <dbghelp.h>

 #include <cstdarg>
 #include <cstdio>
 #include <cwchar>
 #include <exception>
 #include <iterator>
 #include <stdlib.h>

 #ifdef _MSC_VER
  #pragma comment (lib, "dbghelp.lib")
 #endif
#endif

namespace mperoll::CrashReport
{

#if JUCE_WINDOWS
namespace
{
    // Everything the crash handlers use is set up beforehand: by the time they run, the heap may
    // be what broke.
    wchar_t reportPath[1024] {};
    char activity[1024] {};
    bool showDialog = true;
    bool installed = false;

    struct Report
    {
        char text[32768] {};
        size_t length = 0;

        void add (const char* format, ...)
        {
            if (length + 1 >= sizeof (text))
                return;

            va_list args;
            va_start (args, format);
            const auto written = std::vsnprintf (text + length, sizeof (text) - length, format, args);
            va_end (args);

            if (written > 0)
                length = juce::jmin (sizeof (text) - 1, length + (size_t) written);
        }
    };

    Report report;

    const char* describeCode (DWORD code)
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:        return "an access violation";
            case EXCEPTION_STACK_OVERFLOW:          return "a stack overflow";
            case EXCEPTION_ILLEGAL_INSTRUCTION:     return "an illegal instruction";
            case EXCEPTION_PRIV_INSTRUCTION:        return "a privileged instruction";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:      return "an integer division by zero";
            case EXCEPTION_IN_PAGE_ERROR:           return "an in-page error";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   return "an array bounds error";
            case 0xe06d7363:                        return "an uncaught C++ exception";
            default:                                return "an exception";
        }
    }

    void startReport (const char* what)
    {
        report.length = 0;
        report.text[0] = 0;

       #ifdef JucePlugin_VersionString
        report.add ("MPE Roll %s crashed with %s.\r\n", JucePlugin_VersionString, what);
       #else
        report.add ("MPE Roll crashed with %s.\r\n", what);
       #endif

        SYSTEMTIME time {};
        GetLocalTime (&time);
        report.add ("Time: %04u-%02u-%02u %02u:%02u:%02u\r\n", (unsigned) time.wYear, (unsigned) time.wMonth,
                    (unsigned) time.wDay, (unsigned) time.wHour, (unsigned) time.wMinute, (unsigned) time.wSecond);

        if (activity[0] != 0)
            report.add ("MPE Roll was: %s\r\n", activity);
    }

    void initialiseSymbols (HANDLE process)
    {
        // Symbols come from .pdb files next to the modules, when there are any.
        SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize (process, nullptr, TRUE);
    }

    void addLocation (HANDLE process, DWORD64 address)
    {
        // The module and the offset in it always; the function, file and line when there are symbols.
        char moduleName[MAX_PATH] = "?";
        auto offset = address;
        HMODULE module = nullptr;

        if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR> (address), &module))
        {
            wchar_t path[MAX_PATH] {};

            if (GetModuleFileNameW (module, path, MAX_PATH) > 0)
            {
                const auto* name = std::wcsrchr (path, L'\\');
                WideCharToMultiByte (CP_UTF8, 0, name != nullptr ? name + 1 : path, -1,
                                     moduleName, (int) sizeof (moduleName), nullptr, nullptr);
            }

            offset = address - (DWORD64) reinterpret_cast<uintptr_t> (module);
        }

        report.add ("%s+0x%llx", moduleName, (unsigned long long) offset);

        alignas (SYMBOL_INFO) char symbolStorage[sizeof (SYMBOL_INFO) + 512] {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*> (symbolStorage);
        symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;

        if (SymFromAddr (process, address, &displacement, symbol))
            report.add ("  %s+0x%llx", symbol->Name, (unsigned long long) displacement);

        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof (line);
        DWORD lineDisplacement = 0;

        if (SymGetLineFromAddr64 (process, address, &lineDisplacement, &line))
            report.add ("  (%s:%lu)", line.FileName, line.LineNumber);

        report.add ("\r\n");
    }

    void addStack (HANDLE process, CONTEXT context, HANDLE thread)
    {
        report.add ("\r\nCall stack, innermost first:\r\n");

       #if defined (_M_X64)
        STACKFRAME64 frame {};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        for (int i = 0; i < 64; ++i)
        {
            if (! StackWalk64 (IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                               SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
                || frame.AddrPC.Offset == 0)
                break;

            report.add ("%3d  ", i);
            addLocation (process, frame.AddrPC.Offset);
        }
       #else
        juce::ignoreUnused (process, context, thread);
        report.add ("  (not recorded on this processor)\r\n");
       #endif
    }

    void finishReport()
    {
        const auto file = CreateFileW (reportPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool saved = file != INVALID_HANDLE_VALUE;

        if (saved)
        {
            DWORD written = 0;
            WriteFile (file, report.text, (DWORD) report.length, &written, nullptr);
            CloseHandle (file);
        }

        if (! showDialog)
            return;

        wchar_t message[1600] {};

        if (saved)
            std::swprintf (message, std::size (message),
                           L"MPE Roll crashed. A report was saved to:\n\n%ls\n\n"
                           L"Please send it, together with what you were doing.", reportPath);
        else
            std::swprintf (message, std::size (message), L"MPE Roll crashed, and the report couldn't be saved to %ls.", reportPath);

        MessageBoxW (nullptr, message, L"MPE Roll", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    }

    // The thread writing the report. Only the first crash is reported: a second one while it's being
    // written (on another thread, say) is most likely a consequence of the first, and would overwrite it.
    volatile LONG reportingThread = 0;

    enum class Claim { first, again, other };

    Claim claimReport()
    {
        const auto me = (LONG) GetCurrentThreadId();
        const auto owner = InterlockedCompareExchange (&reportingThread, me, 0);
        return owner == 0 ? Claim::first : owner == me ? Claim::again : Claim::other;
    }

    // Returns only to the first crash. A crash while writing the report gives up on it; any other
    // waits for the first to end the process.
    bool startReporting()
    {
        switch (claimReport())
        {
            case Claim::first:  return true;
            case Claim::again:  return false;
            case Claim::other:  break;
        }

        Sleep (INFINITE);
        return false;
    }

    void reportCurrentStack (const char* what)
    {
        if (! startReporting())
            return;

        const auto process = GetCurrentProcess();
        initialiseSymbols (process);
        startReport (what);

        CONTEXT context {};
        RtlCaptureContext (&context);
        addStack (process, context, GetCurrentThread());
        finishReport();
    }

    void reportException (const EXCEPTION_POINTERS& pointers, HANDLE thread)
    {
        const auto& record = *pointers.ExceptionRecord;
        const auto process = GetCurrentProcess();
        initialiseSymbols (process);
        startReport (describeCode (record.ExceptionCode));

        report.add ("Exception 0x%08lx at ", record.ExceptionCode);
        addLocation (process, (DWORD64) reinterpret_cast<uintptr_t> (record.ExceptionAddress));

        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            const auto kind = record.ExceptionInformation[0];
            report.add ("It tried to %s address 0x%llx.\r\n", kind == 0 ? "read" : kind == 1 ? "write to" : "run code at",
                        (unsigned long long) record.ExceptionInformation[1]);
        }

        addStack (process, *pointers.ContextRecord, thread);
        finishReport();
    }

    // A thread that has run out of stack has only a few KB left to handle that in: not enough to walk
    // the stack and read symbols, or to start a thread safely. The report is written on this one
    // instead, started beforehand and waiting until then.
    struct OverflowHelper
    {
        HANDLE start = nullptr, done = nullptr, thread = nullptr;
        EXCEPTION_POINTERS* pointers = nullptr;
        HANDLE crashedThread = nullptr;
    };

    OverflowHelper overflow;

    DWORD WINAPI reportOverflow (LPVOID)
    {
        WaitForSingleObject (overflow.start, INFINITE);
        InterlockedExchange (&reportingThread, (LONG) GetCurrentThreadId());
        reportException (*overflow.pointers, overflow.crashedThread);
        SetEvent (overflow.done);
        return 0;
    }

    void startOverflowHelper()
    {
        if (overflow.thread != nullptr)
            return;

        overflow.start = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        overflow.done = CreateEventW (nullptr, FALSE, FALSE, nullptr);

        if (overflow.start != nullptr && overflow.done != nullptr)
            overflow.thread = CreateThread (nullptr, 1 << 20, reportOverflow, nullptr, 0, nullptr);
    }

    LONG WINAPI handleException (EXCEPTION_POINTERS* pointers)
    {
        if (! startReporting())
            return EXCEPTION_EXECUTE_HANDLER;

        if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW && overflow.thread != nullptr)
        {
            overflow.pointers = pointers;
            DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &overflow.crashedThread,
                             0, FALSE, DUPLICATE_SAME_ACCESS);
            SetEvent (overflow.start);
            WaitForSingleObject (overflow.done, INFINITE);
            return EXCEPTION_EXECUTE_HANDLER;
        }

        reportException (*pointers, GetCurrentThread());
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // These two cover MPE Roll's own code: each module has its own C++ runtime, and a synth's runtime
    // ends the process itself (with no report) when the synth makes one of these mistakes.
    [[noreturn]] void handleTerminate()
    {
        reportCurrentStack ("an uncaught C++ exception");
        TerminateProcess (GetCurrentProcess(), 3);
        std::abort();
    }

    void __cdecl handlePureCall()
    {
        reportCurrentStack ("a call to a pure virtual function (an object used after it was deleted?)");
        TerminateProcess (GetCurrentProcess(), 3);
    }

    void setHandlers()
    {
        SetUnhandledExceptionFilter (handleException);
        std::set_terminate (handleTerminate);
        _set_purecall_handler (handlePureCall);
    }
}
#endif

juce::File getDefaultReportFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("MPE Roll")
               .getChildFile ("Crash report.txt");
}

void install (const juce::File& reportFile, bool tellUser)
{
   #if JUCE_WINDOWS
    reportFile.getParentDirectory().createDirectory();
    wcsncpy_s (reportPath, reportFile.getFullPathName().toWideCharPointer(), _TRUNCATE);
    showDialog = tellUser;
    installed = true;
    startOverflowHelper();
    setHandlers();
   #else
    juce::ignoreUnused (reportFile, tellUser);
   #endif
}

void renew()
{
   #if JUCE_WINDOWS
    if (installed)
        setHandlers();
   #endif
}

void setActivity (const juce::String& text)
{
   #if JUCE_WINDOWS
    text.copyToUTF8 (activity, sizeof (activity));
   #else
    juce::ignoreUnused (text);
   #endif
}

} // namespace mperoll::CrashReport
