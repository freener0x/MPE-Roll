#pragma once

#include <juce_core/juce_core.h>

namespace mperoll::CopySafety
{

/** True where call() can survive a crash in what it calls: Windows, built with MSVC. */
bool canCatchCrashes() noexcept;

/** Calls fn (context). On Windows (MSVC) a crash inside it - an access violation, an illegal
    instruction, an integer division by zero, a C++ exception that escaped the synth, and the
    like - is caught there: call() returns false and sets exceptionCode, and the program carries
    on. A stack overflow is not caught (the thread's stack would be left unusable), nor is a
    debugger breakpoint. Elsewhere only a C++ exception that escaped the synth is caught.

    What crashed must not be called again: whatever locks or state it held are left as they were.
    fn must hold nothing that needs destroying when it's abandoned part way: keep the work in a
    plain function that calls into the synth.
*/
bool call (void (*fn) (void*), void* context, unsigned int* exceptionCode = nullptr) noexcept;

/** "an access violation", "an illegal instruction"... for an exception code from call(). */
juce::String describeException (unsigned int exceptionCode);

/** Memory the system could give programs right now, in megabytes, or -1 where unknown (Windows:
    the smaller of the available physical memory and what can still be committed; Linux:
    MemAvailable; macOS: free and inactive pages).
*/
juce::int64 getAvailableMemoryMB();

/** Memory this process has committed, in megabytes, or -1 where unknown (Windows: private
    bytes; Linux: VmRSS; macOS: -1). For telling how much one copy of a synth takes.
*/
juce::int64 getProcessMemoryMB();

/** For tests: makes getAvailableMemoryMB() return this instead (-2 to stop pretending). */
void pretendAvailableMemoryMB (juce::int64 megabytes);

/** This process's ID. */
juce::int64 getProcessId();

/** True if a process with this ID is running (or can't be told apart from one that is). */
bool isProcessRunning (juce::int64 processId);

} // namespace mperoll::CopySafety
