#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace mperoll::LoadDiagnosis
{

/** Finds out why a VST2 or VST3 plugin didn't load. JUCE gives every such failure the same words
    ("Unable to load VST-3 plug-in file"), whether Windows refused the file, the plugin has no class
    matching what a scan listed, or its component wouldn't start, so this retraces JUCE's steps one
    at a time and says which one failed.

    It works in three parts, by how much of the plugin's own code each runs:
    - describe() and inspectFiles() run none: what the description and the files on disk say.
    - PreFlight loads the plugin's binaries in this process just before JUCE does, as JUCE is about
      to (which runs only their DllMain), so a failure comes with the system's own reason.
    - diagnose() runs everything JUCE runs, step by step, and is meant for the scanner process,
      where a plugin that crashes or hangs takes nothing else down.
*/

/** The binaries JUCE loads for a description, in the order it tries them: the file itself, or for
    a VST3 bundle folder the .vst3 files inside it. Empty for other formats. */
juce::Array<juce::File> findBinaries (const juce::PluginDescription&);

/** What the description says that bears on loading it: for a VST3, whether it came from the
    bundle's moduleinfo.json (a scan that ran none of the plugin's code) or from the plugin itself. */
juce::String describe (const juce::PluginDescription&);

/** What the files say, running none of the plugin's code: each binary's machine type, whether it's
    loaded in this process already, and for a VST3 bundle the classes its moduleinfo.json lists
    against the description. One line each, for the plugin log. */
juce::StringArray inspectFiles (const juce::PluginDescription&);

/** The machine a Windows binary is built for, from its header ("x64", "x86 (32-bit)", "ARM64"...),
    or an empty string when the file isn't a Windows binary. */
juce::String describeMachine (const juce::File&);

/** A Windows error code in plain words, with what usually causes it when loading a plugin. The
    file's name stands in for the "%1" some of Windows' own messages have. */
juce::String describeWindowsError (juce::uint32 code, const juce::String& fileName = {});

/** Loads the description's binaries the way JUCE is about to, keeping them loaded until it's
    destroyed, so that if JUCE's load fails the system's reason is known. Loading a binary runs its
    DllMain, which JUCE's load runs anyway right after (it then only adds a reference); no other code
    of the plugin's runs here. Does nothing on macOS and for other formats.
*/
class PreFlight
{
public:
    explicit PreFlight (const juce::PluginDescription&);
    ~PreFlight();

    /** What happened, one line per binary, for the plugin log. */
    const juce::StringArray& getLines() const noexcept   { return lines; }

    /** A sentence for the user when a binary couldn't be loaded, else empty. */
    const juce::String& getProblem() const noexcept     { return problem; }

private:
    juce::StringArray lines;
    juce::String problem;
    std::vector<void*> handles;

    JUCE_DECLARE_NON_COPYABLE (PreFlight)
};

/** Prefix of the line diagnose() ends with when it finds the answer. */
inline constexpr const char* verdictPrefix = "Verdict: ";

/** In every verdict that finds the plugin working on its own (it loads, or starts when called
    directly), so worksElsewhere() can tell. */
inline constexpr const char* separateProcessPhrase = " in a separate process";

/** Retraces JUCE's load of a VST2 or VST3 step by step, running the plugin's code as JUCE does, so
    only for a separate process (the scanner). It first loads the plugin with JUCE as MPE Roll would;
    if that works, the plugin itself is fine and the failure belongs to the process it failed in.
    Otherwise it takes JUCE's steps one by one. Each line goes to writeLine as soon as it's known, so
    a plugin that crashes or hangs leaves the steps it got through; the last line starts with
    verdictPrefix once the cause is found.
*/
void diagnose (const juce::PluginDescription&, juce::AudioPluginFormatManager&,
               const std::function<void (const juce::String&)>& writeLine);

/** The sentence for the user from diagnose()'s lines, which a crash or hang may have cut short
    (crashed / timedOut), or empty when they say nothing useful. */
juce::String summarise (const juce::StringArray& lines, bool crashed, bool timedOut, const juce::String& pluginName);

/** Whether diagnose()'s verdict found the plugin working in the scanner's process (it loaded, or
    started when called directly): the failure then belongs to the process it failed in, or to its
    first try there, so it's worth loading once more. */
bool worksElsewhere (const juce::StringArray& lines);

} // namespace mperoll::LoadDiagnosis
