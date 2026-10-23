#include "CopySafety.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>

 #ifndef PSAPI_VERSION
  #define PSAPI_VERSION 2     // the K32 functions, which are in kernel32: no psapi.lib needed
 #endif
 #include <psapi.h>
 #include <float.h>

 #if JUCE_INTEL
  #include <xmmintrin.h>
 #endif
#else
 #include <cerrno>
 #include <csignal>
 #include <unistd.h>

 #if JUCE_LINUX
  #include <cstdio>
  #include <cstdlib>
  #include <cstring>
 #elif JUCE_MAC
  #include <mach/mach.h>
  #include <sys/sysctl.h>
 #endif
#endif

#include <atomic>

namespace mperoll::CopySafety
{

namespace
{
    // Windows' exception codes, spelled out so they can be described on every system.
    constexpr unsigned int accessViolation          = 0xc0000005;
    constexpr unsigned int inPageError              = 0xc0000006;
    constexpr unsigned int illegalInstruction       = 0xc000001d;
    constexpr unsigned int arrayBoundsExceeded      = 0xc000008c;
    constexpr unsigned int firstFloatException      = 0xc000008d;     // a denormal operand...
    constexpr unsigned int lastFloatException       = 0xc0000093;     // ...to an underflow
    constexpr unsigned int integerDivideByZero      = 0xc0000094;
    constexpr unsigned int integerOverflow          = 0xc0000095;
    constexpr unsigned int privilegedInstruction    = 0xc0000096;
    constexpr unsigned int stackOverflow            = 0xc00000fd;
    constexpr unsigned int datatypeMisalignment     = 0x80000002;
    constexpr unsigned int floatMultipleFaults      = 0xc00002b4;     // SSE/AVX floating-point faults...
    constexpr unsigned int floatMultipleTraps       = 0xc00002b5;     // ...and traps, on x64
    constexpr unsigned int cppException             = 0xe06d7363;     // what MSVC's throw raises ("msc")

   #if JUCE_WINDOWS
    static_assert (accessViolation == EXCEPTION_ACCESS_VIOLATION);
    static_assert (inPageError == EXCEPTION_IN_PAGE_ERROR);
    static_assert (illegalInstruction == EXCEPTION_ILLEGAL_INSTRUCTION);
    static_assert (arrayBoundsExceeded == EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
    static_assert (firstFloatException == EXCEPTION_FLT_DENORMAL_OPERAND);
    static_assert (lastFloatException == EXCEPTION_FLT_UNDERFLOW);
    static_assert (integerDivideByZero == EXCEPTION_INT_DIVIDE_BY_ZERO);
    static_assert (integerOverflow == EXCEPTION_INT_OVERFLOW);
    static_assert (privilegedInstruction == EXCEPTION_PRIV_INSTRUCTION);
    static_assert (stackOverflow == EXCEPTION_STACK_OVERFLOW);
    static_assert (datatypeMisalignment == EXCEPTION_DATATYPE_MISALIGNMENT);
   #endif

    std::atomic<juce::int64> pretendedMB { -2 };

   #if JUCE_WINDOWS && JUCE_MSVC
    /** Which crashes call() survives: those that leave the thread fit to carry on. */
    int filterCrash (DWORD code) noexcept
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_PRIV_INSTRUCTION:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_INT_OVERFLOW:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            case EXCEPTION_DATATYPE_MISALIGNMENT:
            case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_FLT_DENORMAL_OPERAND:
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            case EXCEPTION_FLT_INEXACT_RESULT:
            case EXCEPTION_FLT_INVALID_OPERATION:
            case EXCEPTION_FLT_OVERFLOW:
            case EXCEPTION_FLT_STACK_CHECK:
            case EXCEPTION_FLT_UNDERFLOW:
            case floatMultipleFaults:
            case floatMultipleTraps:
            case cppException:
                return EXCEPTION_EXECUTE_HANDLER;

            default:
                // A stack overflow leaves the thread no stack to carry on with, and breakpoints,
                // single steps and guard pages are for debuggers and Windows itself.
                return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    // No C++ objects with destructors in here: they can't share a function with __try.
    bool callGuarded (void (*fn) (void*), void* context, DWORD* code)
    {
        __try
        {
            fn (context);
        }
        __except (filterCrash (GetExceptionCode()))
        {
            *code = GetExceptionCode();
            return false;
        }

        return true;
    }
   #endif

   #if JUCE_LINUX
    /** The number from a "Name:   1234 kB" line of a file in /proc, or -1. */
    juce::int64 readProcKilobytes (const char* path, const char* name)
    {
        auto* file = std::fopen (path, "r");

        if (file == nullptr)
            return -1;

        const auto nameLength = std::strlen (name);
        juce::int64 result = -1;
        char line[256];

        while (std::fgets (line, (int) sizeof (line), file) != nullptr)
        {
            if (std::strncmp (line, name, nameLength) == 0 && line[nameLength] == ':')
            {
                result = std::strtoll (line + nameLength + 1, nullptr, 10);
                break;
            }
        }

        std::fclose (file);
        return result;
    }
   #endif
}

bool canCatchCrashes() noexcept
{
   #if JUCE_WINDOWS && JUCE_MSVC
    return true;
   #else
    return false;
   #endif
}

bool call (void (*fn) (void*), void* context, unsigned int* exceptionCode) noexcept
{
   #if JUCE_WINDOWS && JUCE_MSVC
    // A synth that unmasked floating-point exceptions and then faulted would leave the thread
    // that way: every later copy on it would fault too.
   #if JUCE_INTEL
    const auto floatControl = _mm_getcsr();
   #endif

    DWORD code = 0;

    if (callGuarded (fn, context, &code))
        return true;

   #if JUCE_INTEL
    _clearfp();
    _mm_setcsr (floatControl);
   #endif

    if (exceptionCode != nullptr)
        *exceptionCode = (unsigned int) code;

    return false;
   #else
    // Elsewhere only a C++ exception that escaped the synth is caught.
    try
    {
        fn (context);
        return true;
    }
    catch (...)
    {
        if (exceptionCode != nullptr)
            *exceptionCode = cppException;

        return false;
    }
   #endif
}

juce::String describeException (unsigned int exceptionCode)
{
    switch (exceptionCode)
    {
        case accessViolation:           return "an access violation";
        case stackOverflow:             return "a stack overflow";
        case illegalInstruction:        return "an illegal instruction";
        case privilegedInstruction:     return "a privileged instruction";
        case integerDivideByZero:       return "an integer division by zero";
        case integerOverflow:           return "an integer overflow";
        case inPageError:               return "an in-page error";
        case arrayBoundsExceeded:       return "an array bounds error";
        case datatypeMisalignment:      return "a misaligned memory access";
        case floatMultipleFaults:
        case floatMultipleTraps:        return "a floating-point error";
        case cppException:              return "a C++ exception the synth didn't catch";
        default:                        break;
    }

    if (exceptionCode >= firstFloatException && exceptionCode <= lastFloatException)
        return "a floating-point error";

    return "exception 0x" + juce::String::toHexString (exceptionCode).paddedLeft ('0', 8);
}

juce::int64 getAvailableMemoryMB()
{
    if (const auto pretended = pretendedMB.load(); pretended != -2)
        return pretended;

   #if JUCE_WINDOWS
    MEMORYSTATUSEX status {};
    status.dwLength = (DWORD) sizeof (status);

    // What can still be committed counts too: with the page file off (as many audio computers
    // have it) that runs out first, memory a synth has committed without touching it yet included.
    if (GlobalMemoryStatusEx (&status))
        return (juce::int64) (juce::jmin (status.ullAvailPhys, status.ullAvailPageFile) / (1024 * 1024));

    return -1;
   #elif JUCE_LINUX
    const auto kilobytes = readProcKilobytes ("/proc/meminfo", "MemAvailable");
    return kilobytes >= 0 ? kilobytes / 1024 : -1;
   #elif JUCE_MAC
    // Free pages, and inactive ones the system takes back first.
    vm_statistics64_data_t stats {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    if (host_statistics64 (mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t> (&stats), &count) != KERN_SUCCESS)
        return -1;

    return (juce::int64) (((juce::uint64) stats.free_count + (juce::uint64) stats.inactive_count) * (juce::uint64) vm_page_size
                          / (1024 * 1024));
   #else
    return -1;
   #endif
}

juce::int64 getProcessMemoryMB()
{
   #if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = (DWORD) sizeof (counters);

    if (K32GetProcessMemoryInfo (GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*> (&counters), counters.cb))
        return (juce::int64) (counters.PrivateUsage / (1024 * 1024));

    return -1;
   #elif JUCE_LINUX
    const auto kilobytes = readProcKilobytes ("/proc/self/status", "VmRSS");
    return kilobytes >= 0 ? kilobytes / 1024 : -1;
   #else
    return -1;
   #endif
}

void pretendAvailableMemoryMB (juce::int64 megabytes)
{
    pretendedMB = megabytes;
}

juce::int64 getProcessId()
{
   #if JUCE_WINDOWS
    return (juce::int64) GetCurrentProcessId();
   #else
    return (juce::int64) getpid();
   #endif
}

bool isProcessRunning (juce::int64 processId)
{
    if (processId <= 0)
        return false;

   #if JUCE_WINDOWS
    if (auto* process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) processId))
    {
        DWORD exitCode = 0;
        const auto running = GetExitCodeProcess (process, &exitCode) && exitCode == STILL_ACTIVE;
        CloseHandle (process);
        return running;
    }

    // There, but not ours to look at (another user's, say).
    return GetLastError() == ERROR_ACCESS_DENIED;
   #else
    return kill ((pid_t) processId, 0) == 0 || errno == EPERM;
   #endif
}

juce::int64 getProcessStartTime (juce::int64 processId)
{
    if (processId <= 0)
        return 0;

   #if JUCE_WINDOWS
    // In 100 ns steps since 1601.
    juce::int64 started = 0;

    if (auto* process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) processId))
    {
        FILETIME creation {}, exited {}, kernel {}, user {};

        if (GetProcessTimes (process, &creation, &exited, &kernel, &user))
            started = (juce::int64) (((uint64_t) creation.dwHighDateTime << 32) | creation.dwLowDateTime);

        CloseHandle (process);
    }

    return started;
   #elif JUCE_LINUX
    // In clock ticks since the system started: the 22nd field of /proc/<id>/stat, counted after the
    // program's name, which is in brackets and may hold spaces and brackets of its own.
    const auto stat = juce::File ("/proc/" + juce::String (processId) + "/stat").loadFileAsString();
    const auto fields = juce::StringArray::fromTokens (stat.fromLastOccurrenceOf (")", false, false), " ", "");

    // The state is the 3rd field, the 1st after the name: the start time is 19 after it.
    juce::StringArray afterName;

    for (const auto& field : fields)
        if (field.isNotEmpty())
            afterName.add (field);

    return afterName.size() > 19 ? afterName[19].getLargeIntValue() : 0;
   #elif JUCE_MAC
    // In microseconds since 1970.
    int request[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int) processId };
    kinfo_proc info {};
    size_t size = sizeof (info);

    if (sysctl (request, 4, &info, &size, nullptr, 0) != 0 || size == 0)
        return 0;

    return (juce::int64) info.kp_proc.p_starttime.tv_sec * 1000000 + info.kp_proc.p_starttime.tv_usec;
   #else
    return 0;
   #endif
}

bool isProcessRunning (juce::int64 processId, juce::int64 startTime)
{
    if (startTime == 0)
        return isProcessRunning (processId);

    // A process given the same ID later, or one not ours to look at, isn't the one that started then.
    return getProcessStartTime (processId) == startTime && isProcessRunning (processId);
}

} // namespace mperoll::CopySafety
