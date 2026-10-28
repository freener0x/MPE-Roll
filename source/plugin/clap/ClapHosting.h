#pragma once

#include "core/MpeSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mperoll
{

/** Lets JUCE's plugin scanning and loading find and host CLAP plugins. */
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override                      { return formatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override                     { return true; }
    bool isTrivialToScan() const override                       { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override  { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate,
                               int initialBufferSize, PluginCreationCallback) override;
};

/** What MPE Roll needs from a hosted CLAP synth beyond the AudioPluginInstance interface. */
class ClapSynthControls
{
public:
    virtual ~ClapSynthControls() = default;

    /** Audio thread, just before processBlock: the MPE layout MPE Roll is sending, so the synth
        can be played with CLAP notes and note expressions instead.
    */
    virtual void setMpeOutputSettings (const OutputSettings&) noexcept = 0;

    /** True if, with these settings, the synth is played with CLAP note events and expressions
        rather than MIDI. That depends on what its note input accepts, as well as the settings.
    */
    virtual bool playsNoteExpressions (const OutputSettings&) const noexcept = 0;

    /** The synth crashed (where that's caught): from now on nothing calls into it, not its own
        timers, file descriptors or main-thread callbacks either. It's left alone, never deleted.
        Any thread, at once by the thread that caught the crash; called on the message thread too,
        where its timers and file descriptors are let go of.
    */
    virtual void quarantine() noexcept = 0;
};

} // namespace mperoll
