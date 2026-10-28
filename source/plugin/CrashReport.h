#pragma once

#include <juce_core/juce_core.h>

namespace mperoll::CrashReport
{

/** %APPDATA%\MPE Roll\Crash report.txt */
juce::File getDefaultReportFile();

/** Windows, standalone app only (a plugin leaves crash handling to its host): if MPE Roll crashes,
    writes what went wrong - the exception, the call stack and what MPE Roll was doing - to a text
    file, and tells the user where it is. Does nothing elsewhere.
*/
void install (const juce::File& reportFile = getDefaultReportFile(), bool tellUser = true);

/** If install() was called, takes the crash handlers back, in case a synth just loaded replaced
    them with its own.
*/
void renew();

/** Notes what MPE Roll is doing, such as loading a synth from a file, for the report. Message thread. */
void setActivity (const juce::String& activity);

} // namespace mperoll::CrashReport
