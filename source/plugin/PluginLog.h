#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** A plain-text log of what happened to plugins: each synth loaded (with how long it took, its buses
    and whether it takes MIDI), each load that failed and why, and what the scanner made of each file.
    It lives next to MPE Roll's settings ("Plugin log.txt"), keeps the last few hundred kilobytes, and
    is what a user can send along when a synth won't load or won't play. Any thread.
*/
namespace mperoll::PluginLog
{

/** Appends a line, with the date and time in front. */
void add (const juce::String& line);

/** One line describing a plugin: name, format, version, maker, file. */
juce::String describe (const juce::PluginDescription&);

/** The last `maxLines` lines of the log. */
juce::String tail (int maxLines = 60);

/** A report to paste into a message: MPE Roll's version, the system, the plugin, the error, and the
    end of the log. */
juce::String report (const juce::PluginDescription* plugin, const juce::String& error);

/** Where the log is. */
juce::File getFile();

/** For tests: write the log elsewhere (an empty file puts it back). */
void setFileForTesting (const juce::File&);

/** Shows the log file in the system's file browser (Explorer, Finder). */
void reveal();

} // namespace mperoll::PluginLog
