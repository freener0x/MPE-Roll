#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Opening a bug report on GitHub from inside the plugin. The repository's issue forms
    (.github/ISSUE_TEMPLATE) each ask for the build line and the system, and GitHub fills a form's
    text inputs from query parameters named after the inputs' ids, so the plugin can hand both over
    in the address it opens. Nothing is sent anywhere until the user presses Submit on GitHub; the
    plugin log, too long for an address, goes on the clipboard instead.
*/
namespace mperoll::BugReport
{

/** The issue forms, by file name. Each has the text inputs `build` and `system`; the two about a
    synth also have `synth`. */
inline constexpr const char* bugTemplate = "bug.yml";                               // "Something else is wrong"
inline constexpr const char* synthWontLoadTemplate = "synth-wont-load.yml";         // "A synth won't load"
inline constexpr const char* noSoundTemplate = "no-sound-or-expression.yml";        // "No sound, or expression that..."

/** The address of a new issue from the given form, with its "MPE Roll build" input (id `build`),
    its system input (id `system`) and, if one is given, its synth input (id `synth`) filled in.
    The values are escaped here: pass them as they are. */
juce::URL issueUrl (const juce::String& templateFile, const juce::String& build, const juce::String& system,
                    const juce::String& synth = {});

/** What the forms' system field asks for, as far as the plugin can tell: the operating system, the
    host it runs in (with the host's version where the system keeps one in the file) and the plugin
    format, e.g. "Windows 11, in FL64.exe 26.1.7.5291, VST3". */
juce::String systemLine();

/** What the synth forms' synth field asks for: the synth's name, version and format, e.g.
    "Analog Lab V 5.10.1, VST3". */
juce::String synthLine (const juce::PluginDescription&);

/** Puts the report on the clipboard, for the user to paste into the form, and opens the form in
    the browser with this build and system filled in (and the synth, if given). False if no browser
    would open; the report is on the clipboard either way. Message thread. */
bool open (const juce::String& templateFile, const juce::String& report, const juce::String& synth = {});

} // namespace mperoll::BugReport
