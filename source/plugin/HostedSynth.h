#pragma once

#include "core/MpeSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace mperoll
{

class ClapSynthControls;

/** The synth MPE Roll plays: a VST3, VST2 or CLAP (or AU on macOS) instrument hosted inside the plugin.

    Loading, unloading and the editor window happen on the message thread. process() is called
    on the audio thread and never blocks: while a plugin is being swapped it outputs silence.
*/
class HostedSynth : private juce::ChangeListener,
                    private juce::AudioProcessorListener
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

    /** What the loaded synth is, or nullptr when none is loaded. Message thread. */
    const juce::PluginDescription* getDescription() const   { return instance != nullptr ? description.get() : nullptr; }

    void showEditor();
    void closeEditor();
    bool isEditorShowing() const noexcept               { return editorWindow != nullptr; }

    //==============================================================================
    void prepare (double sampleRate, int maximumBlockSize);
    void release();

    /** Renders the synth into the buffer. The MIDI buffer may be modified by the plugin. */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    /** Audio thread, before process(): the MPE layout the MIDI is in. A CLAP synth uses it to turn
        the MPE into CLAP note expressions.
    */
    void setOutputSettings (const OutputSettings& settings) noexcept   { outputSettings = settings; }

    /** How the synth is played with these settings, for showing to the user. Message thread. */
    juce::String describeConnection (const OutputSettings&) const;

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

    /** Calls fn with the synth's parameter at this index, without ever blocking (safe on any thread,
        and never in the audio thread's way). Returns false if there's no such parameter, or the
        synth is being swapped, prepared or released right now.
    */
    template <typename Fn>
    bool withParameter (int index, Fn&& fn) const
    {
        const ScopedUse use (guard);

        if (! use.entered || instance == nullptr)
            return false;

        const auto& params = instance->getParameters();

        if (! juce::isPositiveAndBelow (index, params.size()))
            return false;

        fn (*params.getUnchecked (index));
        return true;
    }

    /** Called on the message thread whenever a plugin is loaded or unloaded, or a project gives the
        loaded one new state.
    */
    std::function<void()> onPluginChanged;

    /** Called on the message thread just before a plugin instance is removed and deleted. */
    std::function<void (juce::AudioPluginInstance&)> onPluginRemoving;

    /** Called on the message thread when the synth reports a change the host should hear about:
        new latency, renamed parameters, or state that needs saving.
    */
    std::function<void (const juce::AudioProcessorListener::ChangeDetails&)> onSynthReportedChange;

private:
    class EditorWindow;

    /** Lets any number of threads use the synth at once (the audio thread, and whichever threads
        the host reads and automates parameters on) and holds them all off while the synth is
        swapped, prepared or released. Users never wait; they're turned away during those moments.
    */
    class InstanceGuard
    {
    public:
        bool tryEnter() const noexcept
        {
            users.fetch_add (1);

            if (exclusive.load())
            {
                users.fetch_sub (1);
                return false;
            }

            return true;
        }

        void exit() const noexcept      { users.fetch_sub (1); }

        /** Waits for current users to finish. Never call it while using the synth yourself. */
        void enterExclusive() noexcept
        {
            while (exclusive.exchange (true))
                std::this_thread::yield();

            while (users.load() != 0)
                std::this_thread::yield();
        }

        void exitExclusive() noexcept   { exclusive = false; }

    private:
        mutable std::atomic<int> users { 0 };
        std::atomic<bool> exclusive { false };
    };

    struct ScopedUse
    {
        explicit ScopedUse (const InstanceGuard& g) noexcept : guard (g), entered (g.tryEnter()) {}
        ~ScopedUse()                                         { if (entered) guard.exit(); }

        const InstanceGuard& guard;
        const bool entered;
    };

    struct ScopedExclusive
    {
        explicit ScopedExclusive (InstanceGuard& g) noexcept : guard (g)   { guard.enterExclusive(); }
        ~ScopedExclusive()                                                  { guard.exitExclusive(); }

        InstanceGuard& guard;
    };

    juce::ApplicationProperties appProperties;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    InstanceGuard guard;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    ClapSynthControls* clapControls = nullptr;    // the instance, when it's a CLAP synth
    OutputSettings outputSettings;                      // audio thread
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
    std::unique_ptr<juce::AudioPluginInstance> takeInstance();
    void configure (juce::AudioPluginInstance& plugin);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedSynth)
};

} // namespace mperoll
