// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "BugReport.h"
#include "BuildInfo.h"
#include "Links.h"

namespace mperoll::BugReport
{

juce::URL issueUrl (const juce::String& templateFile, const juce::String& build, const juce::String& system,
                    const juce::String& synth)
{
    // withParameter escapes each value; GitHub fills the form's inputs whose ids match the names.
    const auto url = juce::URL (juce::String (links::issues) + "/new")
                         .withParameter ("template", templateFile)
                         .withParameter ("build", build)
                         .withParameter ("system", system);

    return synth.isNotEmpty() ? url.withParameter ("synth", synth) : url;
}

juce::String systemLine()
{
    // The facts the plugin log's report opens with, plus the plugin format. The host's version comes
    // from its file's version resource on Windows (FL's carries one); elsewhere there's none to read.
    const auto host = juce::File::getSpecialLocation (juce::File::hostApplicationPath);
    const auto hostVersion = host.getVersion();

    juce::String s;
    s << juce::SystemStats::getOperatingSystemName() << ", in " << host.getFileName();

    if (hostVersion.isNotEmpty())
        s << " " << hostVersion;

    // Which of MPE Roll's formats the host loaded. The wrapper says; the harness and the tests have none.
    // JUCE calls the VST2 one "VST", where the forms, like the rest of MPE Roll, say VST2.
    if (const auto format = juce::PluginHostType::getPluginLoadedAs(); format == juce::AudioProcessor::wrapperType_VST)
        s << ", VST2";
    else if (format != juce::AudioProcessor::wrapperType_Undefined)
        s << ", " << juce::AudioProcessor::getWrapperTypeDescription (format);

    return s;
}

juce::String synthLine (const juce::PluginDescription& d)
{
    juce::String s (d.name);

    if (d.version.isNotEmpty())
        s << " " << d.version;

    // JUCE calls VST2 plugins' format "VST"; the forms, like the rest of MPE Roll, say VST2.
    if (d.pluginFormatName.isNotEmpty())
        s << ", " << (d.pluginFormatName == "VST" ? juce::String ("VST2") : d.pluginFormatName);

    return s;
}

bool open (const juce::String& templateFile, const juce::String& report, const juce::String& synth)
{
    juce::SystemClipboard::copyTextToClipboard (report);
    return issueUrl (templateFile, getBuildName(), systemLine(), synth).launchInDefaultBrowser();
}

} // namespace mperoll::BugReport
