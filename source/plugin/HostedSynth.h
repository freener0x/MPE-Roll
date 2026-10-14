#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>

namespace mperoll
{

/** The synth MPE Roll plays: a VST3 or VST2 (or AU on macOS) instrument hosted inside the plugin.

    Loading, unloading and the editor window happen on the message thread. process() is called
    on the audio thread and never blocks: while a plugin is being swapped it outputs silence.
*/
class HostedSynth : private juce::ChangeListener
{
public:
    HostedSynth();
    ~HostedSynth() override;

    //==============================================================================
    using LoadCallback = std::function<void (const juce::String& error)>;

    void load (const juce::PluginDescription& description, LoadCallback onDone = {});
    void loadFromFile (const juce::File& file, LoadCallback onDone = {});
    void unload();

    /** File patterns for the plugin formats this build can host, for a file chooser. */
    static juce::String getPluginFilePatterns();

    bool isLoaded() const;
    bool isLoading() const noexcept                     { return loading; }
    juce::String getName() const;

    /** The name of a synth saved in the project that couldn't be loaded (e.g. not installed here). */
    juce::String getMissingSynthName() const;
    juce::AudioPluginInstance* getInstance() const      { return instance.get(); }

    void showEditor();
    void closeEditor();
    bool isEditorShowing() const noexcept               { return editorWindow != nullptr; }

    //==============================================================================
    void prepare (double sampleRate, int maximumBlockSize);
    void release();

    /** Renders the synth into the buffer. The MIDI buffer may be modified by the plugin. */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    int getLatencySamples() const;
    double getTailLengthSeconds() const;

    //==============================================================================
    std::unique_ptr<juce::XmlElement> saveState() const;
    void restoreState (const juce::XmlElement& xml, LoadCallback onDone = {});

    //==============================================================================
    juce::AudioPluginFormatManager& getFormatManager() noexcept     { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept               { return knownPlugins; }
    juce::PropertiesFile* getUserSettings()                         { return appProperties.getUserSettings(); }
    juce::File getDeadMansPedalFile();

    /** Instrument types from the scanned plugin list. */
    juce::Array<juce::PluginDescription> getInstrumentTypes() const;

    /** Calls fn with the synth's parameter at this index, without ever blocking (safe on any thread).
        Returns false if there's no such parameter, or the synth is being swapped right now.
    */
    template <typename Fn>
    bool withParameter (int index, Fn&& fn) const
    {
        const juce::SpinLock::ScopedTryLockType lock (instanceLock);

        if (! lock.isLocked() || instance == nullptr)
            return false;

        const auto& params = instance->getParameters();

        if (! juce::isPositiveAndBelow (index, params.size()))
            return false;

        fn (*params.getUnchecked (index));
        return true;
    }

    /** Called on the message thread whenever a plugin is loaded or unloaded. */
    std::function<void()> onPluginChanged;

    /** Called on the message thread just before a plugin instance is removed and deleted. */
    std::function<void (juce::AudioPluginInstance&)> onPluginRemoving;

private:
    class EditorWindow;

    juce::ApplicationProperties appProperties;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    mutable juce::SpinLock instanceLock;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<juce::PluginDescription> description;
    std::unique_ptr<EditorWindow> editorWindow;

    juce::AudioBuffer<float> scratch;
    double sampleRate = 44100.0;
    int blockSize = 512;
    bool prepared = false;
    bool loading = false;
    int loadGeneration = 0;
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);  // lets async callbacks detect that we're gone

    // A synth from the project that hasn't loaded (yet). Saving keeps it, so a project opened on a
    // machine without that synth - or saved while it's still loading - doesn't forget it.
    std::unique_ptr<juce::XmlElement> unresolvedState;

    void install (std::unique_ptr<juce::AudioPluginInstance> newInstance, const juce::PluginDescription& desc);
    void configure (juce::AudioPluginInstance& plugin);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedSynth)
};

} // namespace mperoll
