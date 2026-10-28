#include "SynthProfiles.h"
#include "KnownSynths.h"

namespace mperoll
{

namespace
{
    // How the list compares names and makers: lower case letters and digits only.
    juce::String key (const juce::String& s)
    {
        return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    }

    juce::StringArray items (const char* list)
    {
        return juce::StringArray::fromTokens (list, "|", {});
    }

    // Which of a synth's names a plugin name starts with: "" for one that covers any name of its
    // maker, nothing when none fits.
    std::optional<juce::String> nameFitting (const knownsynths::Synth& s, const juce::String& name)
    {
        const auto names = items (s.names);
        std::optional<juce::String> fit;

        for (const auto& n : names)
            if (name.startsWith (n) && (! fit.has_value() || n.length() > fit->length()))
                fit = n;

        if (names.isEmpty())
            fit = juce::String();

        for (const auto& e : items (s.excludes))
            if (name.startsWith (e))
                return std::nullopt;

        return fit;
    }

    bool makerFits (const knownsynths::Synth& s, const juce::String& maker)
    {
        const auto makers = items (s.makers);

        if (makers.isEmpty())
            return true;

        for (const auto& m : makers)
            if (m.startsWith ("=") ? maker == m.substring (1) : maker.contains (m))
                return true;

        return false;
    }

    bool versionFits (const knownsynths::Synth& s, int major)
    {
        const auto versions = items (s.versions);

        if (versions.isEmpty())
            return true;

        for (const auto& v : versions)
            if (v.getIntValue() == major)
                return true;

        return false;
    }
}

std::optional<SynthProfile> SynthProfile::find (const juce::PluginDescription& d)
{
    const auto name = key (d.name);
    const auto maker = key (d.manufacturerName);
    const auto major = d.version.upToFirstOccurrenceOf (".", false, false).trim().getIntValue();

    for (const auto& s : knownsynths::synths)
    {
        const auto fit = nameFitting (s, name);

        if (! fit.has_value() || ! makerFits (s, maker) || ! versionFits (s, major))
            continue;

        using Known = knownsynths::Mpe;
        SynthProfile profile;

        // One that covers a maker's whole range, or several instruments, goes by the synth's own name.
        profile.name = fit->isEmpty() || juce::String (s.name).isEmpty() ? d.name.trim() : juce::String (s.name);
        profile.mpe = s.mpe == Known::yes ? Mpe::needsSwitch : s.mpe == Known::followsSetup ? Mpe::followsSetup : Mpe::none;
        profile.certain = s.mpe != Known::undocumented;
        profile.advice = s.advice;

       #if MPEROLL_HOST_CLAP
        profile.advice += s.clapAdvice;
       #endif

        return profile;
    }

    return std::nullopt;
}

} // namespace mperoll
