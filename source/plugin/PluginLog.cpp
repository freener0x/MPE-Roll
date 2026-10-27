#include "PluginLog.h"
#include "BuildInfo.h"

namespace mperoll::PluginLog
{

namespace
{
    constexpr juce::int64 maxBytes = 512 * 1024;

    juce::CriticalSection& lock()
    {
        static juce::CriticalSection l;
        return l;
    }

    juce::File& testFile()
    {
        static juce::File f;
        return f;
    }
}

juce::File getFile()
{
    if (testFile() != juce::File())
        return testFile();

    // Beside MPE Roll's settings file (see HostedSynth's SharedSettings).
    juce::PropertiesFile::Options options;
    options.applicationName = "MPE Roll";
    options.folderName = "MPE Roll";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    return options.getDefaultFile().getSiblingFile ("Plugin log.txt");
}

void setFileForTesting (const juce::File& file)
{
    const juce::ScopedLock sl (lock());
    testFile() = file;
}

void add (const juce::String& line)
{
    const juce::ScopedLock sl (lock());
    const auto file = getFile();
    file.getParentDirectory().createDirectory();

    // Keep it small: past the limit, only the newer half stays.
    if (file.getSize() > maxBytes)
    {
        const auto text = file.loadFileAsString();
        file.replaceWithText (text.substring (text.length() / 2).fromFirstOccurrenceOf ("\n", false, false));
    }

    file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + line.trimEnd() + "\n");
}

juce::String describe (const juce::PluginDescription& d)
{
    juce::String s;
    s << d.name << " (" << d.pluginFormatName;

    if (d.version.isNotEmpty())
        s << " " << d.version;

    if (d.manufacturerName.isNotEmpty())
        s << ", " << d.manufacturerName;

    s << ") from " << d.fileOrIdentifier;
    return s;
}

juce::String tail (int maxLines)
{
    const juce::ScopedLock sl (lock());
    juce::StringArray lines;
    lines.addLines (getFile().loadFileAsString());
    lines.removeEmptyStrings();

    if (lines.size() > maxLines)
        lines.removeRange (0, lines.size() - maxLines);

    return lines.joinIntoString ("\n");
}

juce::String report (const juce::PluginDescription* plugin, const juce::String& error)
{
    juce::String s;
    s << getBuildName() << " on " << juce::SystemStats::getOperatingSystemName()
      << ", in " << juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName() << "\n";

    if (plugin != nullptr)
        s << "Plugin: " << describe (*plugin) << "\n";

    if (error.isNotEmpty())
        s << "What happened: " << error << "\n";

    s << "\nThe end of the plugin log (" << getFile().getFullPathName() << "):\n" << tail();
    return s;
}

void reveal()
{
    const auto file = getFile();

    if (! file.existsAsFile())
        add ("(the log starts here)");

    file.revealToUser();
}

} // namespace mperoll::PluginLog
