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
    debugger breakpoint. Elsewhere it simply calls fn and returns true.

    What crashed must not be called again: whatever locks or state it held are left as they were.
    fn must hold nothing that needs destroying when it's abandoned part way: keep the work in a
    plain function that calls into the synth.
*/
bool call (void (*fn) (void*), void* context, unsigned int* exceptionCode = nullptr) noexcept;

/** "an access violation", "an illegal instruction"... for an exception code from call(). */
juce::String describeException (unsigned int exceptionCode);

/** Physical memory the system could give programs right now, in megabytes, or -1 where unknown
    (Windows: GlobalMemoryStatusEx's available physical memory; Linux: MemAvailable; macOS: -1).
*/
juce::int64 getAvailableMemoryMB();

/** Memory this process has committed, in megabytes, or -1 where unknown (Windows: private
    bytes; Linux: VmRSS; macOS: -1). For telling how much one copy of a synth takes.
*/
juce::int64 getProcessMemoryMB();

/** For tests: makes getAvailableMemoryMB() return this instead (-2 to stop pretending). */
void pretendAvailableMemoryMB (juce::int64 megabytes);

} // namespace mperoll::CopySafety
