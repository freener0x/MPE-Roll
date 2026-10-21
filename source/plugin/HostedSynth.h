#pragma once

#include "core/MpeSettings.h"
#include "PluginScanner.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace mperoll
{

class ClapSynthControls;

/** The synth MPE Roll plays: a VST3, VST2 or CLAP (or AU on macOS) instrument hosted inside the plugin.

    Loading, unloading and the editor window happen on the message thread. process() is called
    on the audio thread and never blocks: while a plugin is being swapped it outputs silence.
*/
class HostedSynth : private juce::AudioProcessorListener
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

    /** The plugin formats this build can host, for showing to the user ("VST3, VST2 or CLAP"). */
    static juce::String describeHostedFormats();

    /** Whether two descriptions are the same synth: same format and plugin ID, wherever installed. */
    static bool isSameSynth (const juce::PluginDescription&, const juce::PluginDescription&);

    /** Whether the file is one of FL Studio's own plugins (Harmor, Sytrus...), which come in
        Image-Line's own format: only FL can load them, so loadFromFile finds nothing in them.
    */
    static bool isFlStudioNativePlugin (const juce::File&);

    /** Opens the window for scanning plugins into the known list, or brings it to the front if it's
        already open. The synth owns the window: it goes when the synth does, so a scan can't carry on
        into a list and settings that have been deleted. Message thread.
    */
    void showScanWindow (juce::Colour background = juce::Colour (0xff1e1f22));

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

    /** Renders the synth into the buffer. The MIDI buffer may be modified by the plugin. A block
        longer than prepare() was told is played in pieces.
    */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead);

    /** Audio thread, before process(): the MPE layout the MIDI is in. A CLAP synth uses it to turn
        the MPE into CLAP note expressions.
    */
    void setOutputSettings (const OutputSettings& settings) noexcept   { outputSettings = settings; }

    /** How the synth is played with these settings, for showing to the user. Message thread. */
    juce::String describeConnection (const OutputSettings&) const;

    /** Whether these settings play the synth with CLAP note expressions rather than MIDI. Message thread. */
    bool playsNoteExpressions (const OutputSettings&) const;

    /** True once per synth loaded (or given a project's state) if, after a few seconds' grace for
        it to load its sounds, it has been played over MPE for three seconds on end without making
        a sound: most likely a synth that isn't in MPE mode and so only listens on channel 1. A
        sound before the warning is taken withdraws it. Any thread.
    */
    bool takeSilenceWarning() noexcept                  { return silenceWarning.exchange (false); }

    int getLatencySamples() const;
    double getTailLengthSeconds() const;

    /** Tells the synth whether it's being rendered offline, where it can take its time. Message thread. */
    void setNonRealtime (bool isNonRealtime);

    //==============================================================================
    std::unique_ptr<juce::XmlElement> saveState() const;
    void restoreState (const juce::XmlElement& xml, LoadCallback onDone = {});

    //==============================================================================
    juce::AudioPluginFormatManager& getFormatManager() noexcept     { return formatManager; }
    juce::KnownPluginList& getKnownPlugins() noexcept               { return knownPlugins; }
    juce::PropertiesFile* getUserSettings()                         { return shared->getUserSettings(); }
    juce::File getDeadMansPedalFile();

    /** For tests: from now on every MPE Roll in this process keeps its settings (and the plugin
        list) in this file, starting from what's saved there.
    */
    void useSettingsFile (const juce::File&);

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

    /** Locks the message thread, unless this thread is it (or already holds it). Taken before the
        guard by anything that holds the guard while calling into the synth from another thread:
        JUCE's VST3 host locks the message thread inside prepareToPlay and its state and text calls,
        while install() holds the guard on the message thread, so the other order could deadlock.
    */
    struct MessageThreadLock
    {
        MessageThreadLock()
        {
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && ! mm->currentThreadHasLockedMessageManager())
                lock.emplace();
        }

        std::optional<juce::MessageManagerLock> lock;
    };

    /** The host's playhead, moved on to where a piece of a long block starts. Audio thread. */
    struct PieceHead final : public juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override;

        juce::AudioPlayHead* source = nullptr;
        int offset = 0;
        double sampleRate = 44100.0;
    };

    /** What every MPE Roll in the process shares: the computer's settings file ("MPE Roll.settings")
        and the list of scanned plugins kept in it. There's one for all instances, so a scan in one
        is seen by the others and none of them saves an older list over it. The list has its own
        lock, so one instance may scan while another reads it.
    */
    struct SharedSettings : private juce::ChangeListener
    {
        SharedSettings();
        ~SharedSettings() override;

        juce::PropertiesFile* getUserSettings() const noexcept      { return settings.get(); }
        void useSettingsFile (const juce::File&);

        juce::KnownPluginList knownPlugins;

    private:
        std::unique_ptr<juce::PropertiesFile> settings;

        void open (const juce::File&, const juce::PropertiesFile::Options&);
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
    };

    juce::SharedResourcePointer<SharedSettings> shared;     // first, so it outlives everything below
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList& knownPlugins;                    // the shared list

    InstanceGuard guard;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    ClapSynthControls* clapControls = nullptr;    // the instance, when it's a CLAP synth
    OutputSettings outputSettings;                      // audio thread
    std::unique_ptr<juce::PluginDescription> description;
    std::unique_ptr<EditorWindow> editorWindow;
    juce::Component::SafePointer<juce::DialogWindow> scanWindow;

    // loadFromFile checks the file in the scanner on this thread, so a plugin that hangs or shows a
    // dialog there doesn't hold up the message thread. The flag stops the check early.
    std::thread checkThread;
    std::shared_ptr<std::atomic<bool>> checkCancelled;

    /** Audio thread: whether the synth has made a sound while notes were held over MPE. */
    struct SilenceWatch
    {
        int heldNotes = 0;
        double graceSeconds = 0.0;                      // of audio since it got its state: it may still be loading
        double silentSeconds = 0.0;                     // of the notes held right now
        bool finished = false;                          // it was heard, or the warning was given
    };

    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiPiece;                         // audio thread: a long block's MIDI, one piece at a time
    PieceHead pieceHead;                                // audio thread: and its transport position
    SilenceWatch silenceWatch;
    std::atomic<bool> silenceWarning { false };
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
    void finishLoadFromFile (const juce::File&, PluginScanner::Outcome, juce::OwnedArray<juce::PluginDescription>& found, LoadCallback);
    void stopCheck();
    void configure (juce::AudioPluginInstance& plugin);
    void countHeldNotes (const juce::MidiBuffer&) noexcept;
    void listenForSound (const juce::AudioBuffer<float>&) noexcept;

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostedSynth)
};

} // namespace mperoll
