// Hosting a CLAP plugin as a juce::AudioPluginInstance.
//
// Threads, as CLAP defines them: the "main thread" is JUCE's message thread and the "audio
// thread" is whichever thread is inside processBlock. Activation, state, GUI, timers and
// parameter text all happen on the message thread; processing on the audio thread.

#include "ClapEventList.h"
#include "ClapHosting.h"
#include "ClapModule.h"
#include "ClapNoteTranslator.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if JUCE_LINUX || JUCE_BSD
 #include <poll.h>
#endif

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "0.1.0"
#endif

namespace mperoll
{

#if JUCE_MAC
void setClapContainerView (juce::NSViewComponent&);     // ClapMacView.mm
#endif

class ClapPluginInstance;
class ClapEditor;

namespace
{
    thread_local bool onClapAudioThread = false;

    struct AudioThreadScope
    {
        AudioThreadScope() noexcept     { onClapAudioThread = true; }
        ~AudioThreadScope() noexcept    { onClapAudioThread = false; }
    };

    /** Releases a lock taken by hand, if it was. */
    struct LockRelease
    {
        juce::SpinLock* lock = nullptr;
        ~LockRelease()                  { if (lock != nullptr) lock->exit(); }
    };

    bool onMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr && mm->isThisTheMessageThread();
    }

    const char* nativeWindowApi() noexcept
    {
       #if JUCE_WINDOWS
        return CLAP_WINDOW_API_WIN32;
       #elif JUCE_MAC
        return CLAP_WINDOW_API_COCOA;
       #else
        return CLAP_WINDOW_API_X11;
       #endif
    }

    juce::AudioChannelSet channelSetFor (uint32_t channels)
    {
        if (channels == 1)  return juce::AudioChannelSet::mono();
        if (channels == 2)  return juce::AudioChannelSet::stereo();
        return juce::AudioChannelSet::discreteChannels ((int) channels);
    }

    clap_beattime toBeatTime (double beats) noexcept
    {
        return (clap_beattime) std::llround (beats * (double) CLAP_BEATTIME_FACTOR);
    }

    clap_sectime toSecTime (double seconds) noexcept
    {
        return (clap_sectime) std::llround (seconds * (double) CLAP_SECTIME_FACTOR);
    }

    struct PortLayout
    {
        std::vector<uint32_t> inputs, outputs;     // channel counts
        int mainOutput = -1;
    };

    PortLayout queryPorts (const clap_plugin_t* plugin)
    {
        PortLayout layout;
        const auto* ports = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));

        if (ports == nullptr)
            return layout;

        for (const bool isInput : { true, false })
        {
            const auto count = ports->count (plugin, isInput);

            for (uint32_t i = 0; i < count; ++i)
            {
                clap_audio_port_info_t info {};

                if (! ports->get (plugin, i, isInput, &info))
                    info.channel_count = 0;

                (isInput ? layout.inputs : layout.outputs).push_back (juce::jmin (info.channel_count, 64u));

                if (! isInput && layout.mainOutput < 0 && (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0)
                    layout.mainOutput = (int) i;
            }
        }

        if (layout.mainOutput < 0 && ! layout.outputs.empty())
            layout.mainOutput = 0;

        return layout;
    }
}

//==============================================================================
/** The host side of one plugin: the clap_host it was given, plus the timers and file
    descriptors it registers, which can start before the instance itself exists.
*/
class ClapHostContext
{
public:
    ClapHostContext();
    ~ClapHostContext();

    clap_host_t host {};
    const clap_plugin_t* plugin = nullptr;
    std::atomic<ClapPluginInstance*> owner { nullptr };
    std::atomic<bool> callbackRequested { false };     // request_callback, possibly before the instance exists

    static ClapHostContext& of (const clap_host_t* h)      { return *static_cast<ClapHostContext*> (h->host_data); }

    // timer-support
    bool registerTimer (uint32_t periodMs, clap_id* id);
    bool unregisterTimer (clap_id id);
    void clearRetiredTimers()                               { retiredTimers.clear(); }

    // posix-fd-support
    bool registerFd (int fd, clap_posix_fd_flags_t flags);
    bool modifyFd (int fd, clap_posix_fd_flags_t flags);
    bool unregisterFd (int fd);
    void fdReady (int fd, clap_posix_fd_flags_t happened);

private:
    struct PluginTimer final : public juce::Timer
    {
        PluginTimer (ClapHostContext& c, clap_id i) : context (c), id (i) {}
        void timerCallback() override       { context.timerFired (id); }

        ClapHostContext& context;
        const clap_id id;
    };

    std::map<clap_id, std::unique_ptr<PluginTimer>> timers;
    std::vector<std::unique_ptr<PluginTimer>> retiredTimers;   // unregistered, maybe from inside their own callback
    clap_id nextTimerId = 1;
    std::map<int, clap_posix_fd_flags_t> fds;

    void timerFired (clap_id id);
    void unregisterAllFds();

    JUCE_DECLARE_NON_COPYABLE (ClapHostContext)
};

//==============================================================================
class ClapParameter final : public juce::HostedAudioProcessorParameter
{
public:
    ClapParameter (ClapPluginInstance& o, const clap_param_info_t& info, double plainValue)
        : owner (o), id (info.id)
    {
        updateInfo (info, true);
        value = toNormalised (plainValue);
    }

    clap_id getClapId() const noexcept          { return id; }
    void* getCookie() const noexcept            { return cookie.load(); }
    bool isBypass() const noexcept              { return (flags.load() & CLAP_PARAM_IS_BYPASS) != 0; }

    /** A rescan(ALL) can take a parameter away. Nothing is sent to it while it's gone. */
    bool isGone() const noexcept                { return gone.load(); }
    void setGone (bool isNowGone) noexcept      { gone = isNowGone; }

    /** A rescan(ALL) invalidates the cookie; until the plugin gives a new one, none is sent. */
    void forgetCookie() noexcept                { cookie = nullptr; }

    double toPlain (float normalised) const noexcept
    {
        const auto lo = minValue.load(), hi = maxValue.load();
        auto plain = lo + (double) juce::jlimit (0.0f, 1.0f, normalised) * (hi - lo);
        return isStepped() ? std::round (plain) : plain;
    }

    float toNormalised (double plain) const noexcept
    {
        const auto lo = minValue.load(), hi = maxValue.load();
        return hi > lo ? (float) juce::jlimit (0.0, 1.0, (plain - lo) / (hi - lo)) : 0.0f;
    }

    /** Message thread. The range (and the cookie) only change while the plugin is inactive. */
    void updateInfo (const clap_param_info_t& info, bool rangeMayChange)
    {
        {
            const juce::SpinLock::ScopedLockType lock (nameLock);
            name = displayNameOf (info);
        }

        flags = info.flags;

        if (! rangeMayChange)
            return;

        cookie = info.cookie;

        if (std::isfinite (info.min_value) && std::isfinite (info.max_value))
        {
            minValue = info.min_value;
            maxValue = juce::jmax (info.min_value, info.max_value);
            defaultValue = juce::jlimit (minValue.load(), maxValue.load(), info.default_value);
        }
    }

    /** A value the plugin reports. Returns true if it changed; telling the host is up to the caller. */
    bool setValueFromPlugin (double plain) noexcept
    {
        const auto normalised = toNormalised (plain);
        return ! juce::exactlyEqual (value.exchange (normalised), normalised);
    }

    /** Audio thread (or message thread while inactive): the host's latest value, if it changed. */
    bool takePending (double& plain) noexcept
    {
        if (! pending.exchange (false))
            return false;

        plain = toPlain (value.load());
        return true;
    }

    void markPending() noexcept                 { pending = true; }

    /** A change to show the host without it counting as an edit (see ClapPluginInstance). */
    void markForReport() noexcept               { reportPending = true; }
    bool takeReport() noexcept                  { return reportPending.exchange (false); }

    //==============================================================================
    float getValue() const override             { return value.load(); }
    void setValue (float newValue) override;
    float getDefaultValue() const override      { return toNormalised (defaultValue.load()); }
    juce::String getLabel() const override      { return {}; }
    juce::String getParameterID() const override { return juce::String (id); }

    juce::String getName (int maximumStringLength) const override
    {
        const juce::SpinLock::ScopedLockType lock (nameLock);
        return name.substring (0, maximumStringLength);
    }

    juce::String getText (float normalised, int maximumStringLength) const override;
    float getValueForText (const juce::String& text) const override;

    int getNumSteps() const override
    {
        const auto range = maxValue.load() - minValue.load();

        if (isStepped() && range >= 1.0 && range < 100000.0)
            return (int) std::round (range) + 1;

        return juce::AudioProcessor::getDefaultNumParameterSteps();
    }

    bool isDiscrete() const override            { return isStepped(); }
    bool isBoolean() const override             { return isStepped() && std::abs (maxValue.load() - minValue.load() - 1.0) < 1.0e-9; }

    bool isAutomatable() const override
    {
        const auto f = flags.load();
        return (f & CLAP_PARAM_IS_AUTOMATABLE) != 0 && (f & (CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY)) == 0;
    }

private:
    ClapPluginInstance& owner;
    const clap_id id;
    std::atomic<void*> cookie { nullptr };

    std::atomic<float> value { 0.0f };
    std::atomic<bool> pending { false }, reportPending { false }, gone { false };
    std::atomic<uint32_t> flags { 0 };
    std::atomic<double> minValue { 0.0 }, maxValue { 1.0 }, defaultValue { 0.0 };

    juce::SpinLock nameLock;
    juce::String name;

    bool isStepped() const noexcept             { return (flags.load() & CLAP_PARAM_IS_STEPPED) != 0; }

    /** CLAP names needn't be unique ("Cutoff" in every filter): MPE Roll lists every parameter in
        one flat list in FL, so the innermost module goes in front ("Filter 2: Cutoff").
    */
    static juce::String displayNameOf (const clap_param_info_t& info)
    {
        const auto name = juce::String::fromUTF8 (info.name, (int) strnlen (info.name, sizeof (info.name))).trim();
        const auto module = juce::String::fromUTF8 (info.module, (int) strnlen (info.module, sizeof (info.module)))
                                .trimCharactersAtEnd ("/");
        const auto group = module.fromLastOccurrenceOf ("/", false, false).trim();

        if (group.isEmpty() || name.containsIgnoreCase (group))
            return name;

        return group + ": " + name;
    }
};

//==============================================================================
class ClapPluginInstance final : public juce::AudioPluginInstance,
                                 public ClapSynthControls,
                                 private juce::AsyncUpdater,
                                 private juce::Timer
{
public:
    ClapPluginInstance (std::shared_ptr<ClapModule> m, std::unique_ptr<ClapHostContext> context,
                        const clap_plugin_t* p, const juce::PluginDescription& desc, const PortLayout& ports)
        : AudioPluginInstance (busesFor (ports)),
          module (std::move (m)),
          hostContext (std::move (context)),
          plugin (p),
          description (desc),
          inputPorts (ports.inputs),
          outputPorts (ports.outputs),
          mainOutput (ports.mainOutput)
    {
        params = extension<clap_plugin_params_t> (CLAP_EXT_PARAMS);
        state = extension<clap_plugin_state_t> (CLAP_EXT_STATE);
        latency = extension<clap_plugin_latency_t> (CLAP_EXT_LATENCY);
        tail = extension<clap_plugin_tail_t> (CLAP_EXT_TAIL);
        notePorts = extension<clap_plugin_note_ports_t> (CLAP_EXT_NOTE_PORTS);
        gui = extension<clap_plugin_gui_t> (CLAP_EXT_GUI);

        description.numInputChannels = 0;
        description.numOutputChannels = mainOutput >= 0 ? (int) outputPorts[(size_t) mainOutput] : 0;

        outputEvents.ctx = this;
        outputEvents.try_push = [] (const clap_output_events_t* list, const clap_event_header_t* event)
        {
            return static_cast<ClapPluginInstance*> (list->ctx)->receiveOutputEvent (*event);
        };

        rescanNotePorts();
        buildParameters();

        // request_callback sets the flag before it looks for the owner, so between this and that,
        // a request made while we were being built is never missed.
        hostContext->owner = this;

        if (hostContext->callbackRequested.load())
            triggerAsyncUpdate();
    }

    ~ClapPluginInstance() override
    {
        cancelPendingUpdate();
        stopTimer();

        {
            const juce::SpinLock::ScopedLockType lock (processLock);
            deactivateLocked();
        }

        plugin->destroy (plugin);
        hostContext->owner = nullptr;
    }

    //==============================================================================
    void fillInPluginDescription (juce::PluginDescription& d) const override   { d = description; }
    const juce::String getName() const override                                 { return description.name; }

    bool acceptsMidi() const override           { return hasNoteInput.load(); }
    bool producesMidi() const override          { return false; }
    bool isMidiEffect() const override          { return false; }

    int getNumPrograms() override                                   { return 1; }
    int getCurrentProgram() override                                { return 0; }
    void setCurrentProgram (int) override                           {}
    const juce::String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const juce::String&) override      {}

    juce::AudioProcessorParameter* getBypassParameter() const override  { return bypass; }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        if (! layout.inputBuses.isEmpty())
            return false;

        if (mainOutput < 0)
            return layout.outputBuses.isEmpty();

        return layout.outputBuses.size() == 1
            && layout.outputBuses[0].size() == (int) outputPorts[(size_t) mainOutput];
    }

    double getTailLengthSeconds() const override
    {
        if (tail == nullptr || ! active.load() || activeSampleRate <= 0.0)
            return 0.0;

        const auto samples = tail->get (plugin);
        return samples >= (uint32_t) std::numeric_limits<int32_t>::max() ? 30.0 : samples / activeSampleRate;
    }

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumBlockSize) override
    {
        const juce::SpinLock::ScopedLockType lock (processLock);
        maximumBlockSize = juce::jmax (1, maximumBlockSize);

        if (active.load() && (! juce::approximatelyEqual (sampleRate, activeSampleRate) || maximumBlockSize > activeMaxFrames))
            deactivateLocked();

        if (! active.load())
            activateLocked (sampleRate, maximumBlockSize);
    }

    void releaseResources() override
    {
        const juce::SpinLock::ScopedLockType lock (processLock);
        deactivateLocked();

        // Values the host set that no audio block carried yet.
        if (parametersPending.load() && params != nullptr && params->flush != nullptr && onMessageThread())
            flushLocked();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const auto numSamples = buffer.getNumSamples();

        // Marked before the lock, so a stand-in flush (see flushParametersNow) knows audio is back.
        lastProcessTime = juce::Time::getMillisecondCounter();

        // A stand-in flush only holds the lock briefly: wait a moment for it rather than lose
        // this block and its notes.
        auto locked = processLock.tryEnter();

        for (int attempt = 0; ! locked && standingIn.load() && attempt < 1000; ++attempt)
        {
            std::this_thread::yield();
            locked = processLock.tryEnter();
        }

        const LockRelease release { locked ? &processLock : nullptr };

        if (! locked || ! active.load() || numSamples > activeMaxFrames || numSamples <= 0)
        {
            buffer.clear();
            return;
        }

        const AudioThreadScope audioThread;
        flushRequested = false;     // process() gives the plugin the chance a flush would

        if (restartPending.load())
        {
            // The plugin asked to be restarted: stop processing here, on the audio thread, and
            // let the message thread deactivate and reactivate it.
            if (processing)
            {
                plugin->stop_processing (plugin);
                processing = false;
            }

            triggerAsyncUpdate();
            buffer.clear();
            return;
        }

        if (! processing)
        {
            if (! plugin->start_processing (plugin))
            {
                buffer.clear();
                return;
            }

            processing = true;
        }

        if (translatorNeedsReset.exchange (false))
            translator.forgetNotes();   // the plugin's voices went with the (re)activation

        translator.setOutputSettings (mpeSettings);
        translator.setDialects (takesNoteEvents.load(), takesMidi.load());

        reportValueChanges();

        audioEvents.clear();
        collectParameterChanges (audioEvents);
        translator.translate (midi, audioEvents, numSamples);

        buffer.clear();
        assignAudioBuffers (buffer, numSamples);

        clap_event_transport_t transport {};
        const auto hasTransport = fillTransport (transport);

        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = (uint32_t) numSamples;
        process.transport = hasTransport ? &transport : nullptr;
        process.audio_inputs = inputBuffers.data();
        process.audio_outputs = outputBuffers.data();
        process.audio_inputs_count = (uint32_t) inputBuffers.size();
        process.audio_outputs_count = (uint32_t) outputBuffers.size();
        process.in_events = audioEvents.getInputEvents();
        process.out_events = &outputEvents;

        const auto status = plugin->process (plugin, &process);
        steadyTime += numSamples;

        if (status == CLAP_PROCESS_ERROR)
            buffer.clear();

        if (editsPending.exchange (false))
            triggerAsyncUpdate();
    }

    using AudioPluginInstance::processBlock;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override
    {
        destData.reset();

        if (state == nullptr)
            return;

        // So the state holds every value the host has set.
        if (parametersPending.load())
            flushParametersNow();

        juce::MemoryBlock saved;
        bool ok = false;

        {
            juce::MemoryOutputStream out (saved, false);    // has to be gone before `saved` is used
            clap_ostream_t stream {};
            stream.ctx = &out;
            stream.write = [] (const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t
            {
                auto* target = static_cast<juce::MemoryOutputStream*> (s->ctx);
                return target->write (data, (size_t) size) ? (int64_t) size : -1;
            };

            ok = state->save (plugin, &stream);
        }

        if (ok)
            destData.swapWith (saved);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (state == nullptr || data == nullptr || sizeInBytes <= 0)
            return;

        juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);
        clap_istream_t stream {};
        stream.ctx = &in;
        stream.read = [] (const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t
        {
            auto* source = static_cast<juce::MemoryInputStream*> (s->ctx);
            return source->read (buffer, (int) juce::jmin<uint64_t> (size, (uint64_t) std::numeric_limits<int>::max()));
        };

        if (state->load (plugin, &stream))
            refreshParameterValues();
    }

    //==============================================================================
    bool hasEditor() const override
    {
        if (gui == nullptr)
            return false;

        const auto* api = nativeWindowApi();
        return gui->is_api_supported (plugin, api, false) || gui->is_api_supported (plugin, api, true);
    }

    juce::AudioProcessorEditor* createEditor() override;

    //==============================================================================
    void setMpeOutputSettings (const OutputSettings& s) noexcept override  { mpeSettings = s; }
    bool playsNoteExpressions (const OutputSettings& s) const noexcept override
    {
        return takesNoteEvents.load() && (s.clapNoteExpressions || ! takesMidi.load());
    }

    //==============================================================================
    // Called by the host callbacks.
    const clap_plugin_t* getClapPlugin() const noexcept         { return plugin; }
    const clap_plugin_gui_t* getGui() const noexcept            { return gui; }

    void requestRestart()
    {
        restartPending = true;
        triggerAsyncUpdate();
    }

    void requestHousekeeping()
    {
        triggerAsyncUpdate();
    }

    void requestParameterFlush()
    {
        flushRequested = true;
        triggerAsyncUpdate();
    }

    void parameterChangedByHost()
    {
        parametersPending = true;

        // An active plugin gets the change with the next audio block. Otherwise, or if audio has
        // stopped, the message thread passes it on. (A change made on the host's audio thread is
        // always followed by a block, so it never needs that.)
        if (! active.load() || (onMessageThread() && audioHasStalled()))
            triggerAsyncUpdate();
    }

    void rescanParameters (clap_param_rescan_flags flags)
    {
        if (params == nullptr)
            return;

        if ((flags & (CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_ALL)) != 0)
        {
            const auto rescanAll = (flags & CLAP_PARAM_RESCAN_ALL) != 0;
            const auto inactive = ! active.load();

            std::unordered_set<ClapParameter*> listed;
            const auto count = params->count (plugin);

            for (uint32_t i = 0; i < count; ++i)
            {
                clap_param_info_t info {};

                if (params->get_info (plugin, i, &info))
                {
                    if (auto* p = findParameter (info.id))
                    {
                        p->updateInfo (info, inactive);
                        listed.insert (p);
                    }
                }
            }

            // A rescan(ALL), which the plugin may only ask for while inactive, means it may have
            // rebuilt its parameters: the old cookies are invalid (updateInfo has the new ones),
            // and any it no longer lists are gone. Parameters it still lists are never marked
            // gone, even for a moment. (JUCE's parameter list can't grow, so new ones show up
            // when the synth is reloaded.)
            if (rescanAll)
            {
                for (auto* p : clapParameters)
                {
                    const auto stillThere = listed.count (p) > 0;

                    if (! stillThere || ! inactive)
                        p->forgetCookie();

                    p->setGone (! stillThere);
                }
            }

            updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
        }

        if ((flags & (CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_ALL)) != 0)
            refreshParameterValues();
    }

    void rescanNotePorts()
    {
        bool clapNotes = false, midi = false, anyInput = false;

        if (notePorts != nullptr && notePorts->count (plugin, true) > 0)
        {
            anyInput = true;
            clap_note_port_info_t info {};

            if (notePorts->get (plugin, 0, true, &info))
            {
                clapNotes = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
                midi = (info.supported_dialects & (CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE)) != 0;
            }
        }

        takesNoteEvents = clapNotes;
        takesMidi = midi;
        hasNoteInput = anyInput;
    }

    void markStateDirty()
    {
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }

    // GUI requests. They may come from any thread; the editor is only touched on the message thread.
    bool requestResize (uint32_t width, uint32_t height);
    void resizeHintsChanged();
    void guiClosed (bool wasDestroyed);

    void editorOpened (ClapEditor* e)
    {
        editor = e;
        hintsChanged = false;
        guiClosedPending = false;
        guiDestroyed = false;
        resizeRequested = false;
    }

    void editorClosing (ClapEditor* e)          { if (editor == e) editor = nullptr; }

private:
    static BusesProperties busesFor (const PortLayout& layout)
    {
        BusesProperties buses;

        if (layout.mainOutput >= 0 && layout.outputs[(size_t) layout.mainOutput] > 0)
            buses = buses.withOutput ("Output", channelSetFor (layout.outputs[(size_t) layout.mainOutput]), true);

        return buses;
    }

    std::shared_ptr<ClapModule> module;             // released last, after the plugin is destroyed
    std::unique_ptr<ClapHostContext> hostContext;
    const clap_plugin_t* plugin = nullptr;
    juce::PluginDescription description;

    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_latency_t* latency = nullptr;
    const clap_plugin_tail_t* tail = nullptr;
    const clap_plugin_note_ports_t* notePorts = nullptr;
    const clap_plugin_gui_t* gui = nullptr;

    // Parameters: fixed once built, so the audio thread can look them up without locking.
    std::vector<ClapParameter*> clapParameters;
    std::unordered_map<clap_id, ClapParameter*> parametersById;
    ClapParameter* bypass = nullptr;
    std::atomic<bool> parametersPending { false };

    // Audio.
    const std::vector<uint32_t> inputPorts, outputPorts;
    const int mainOutput;
    juce::AudioBuffer<float> spareChannels;
    std::vector<std::vector<float*>> inputPointers, outputPointers;
    std::vector<clap_audio_buffer_t> inputBuffers, outputBuffers;

    juce::SpinLock processLock;
    std::atomic<bool> active { false };
    bool processing = false;
    double activeSampleRate = 0.0;
    int activeMaxFrames = 0;
    int64_t steadyTime = 0;
    std::atomic<juce::uint32> lastProcessTime { 0 };
    std::atomic<bool> standingIn { false };         // the message thread is flushing in place of the audio thread

    // Notes.
    ClapNoteTranslator translator;
    OutputSettings mpeSettings;                     // audio thread
    std::atomic<bool> takesNoteEvents { false }, takesMidi { false }, hasNoteInput { false };
    std::atomic<bool> translatorNeedsReset { false };

    ClapEventList audioEvents, mainThreadEvents;
    clap_output_events_t outputEvents {};

    // Requests handled on the message thread.
    std::atomic<bool> restartPending { false }, flushRequested { false };
    std::atomic<bool> resizeRequested { false }, hintsChanged { false }, guiClosedPending { false }, guiDestroyed { false };
    std::atomic<uint32_t> requestedWidth { 0 }, requestedHeight { 0 };

    ClapEditor* editor = nullptr;                   // message thread

    // Edits made in the plugin's own window reach us in process(), on the audio thread. They're
    // passed to the host from the message thread, which is where hosts take them as user edits
    // (JUCE's VST3 wrapper drops gestures from any other thread).
    struct PluginEdit
    {
        enum class Kind : uint8_t { value, gestureBegin, gestureEnd };

        ClapParameter* parameter = nullptr;
        Kind kind = Kind::value;
        float value = 0.0f;
    };

    static constexpr int editCapacity = 1024;
    juce::AbstractFifo editFifo { editCapacity };
    std::array<PluginEdit, (size_t) editCapacity> edits {};
    std::atomic<bool> editsPending { false }, reportsPending { false };

    template <typename Extension>
    const Extension* extension (const char* id) const
    {
        return static_cast<const Extension*> (plugin->get_extension (plugin, id));
    }

    ClapParameter* findParameter (clap_id id) const
    {
        const auto found = parametersById.find (id);
        return found != parametersById.end() ? found->second : nullptr;
    }

    void buildParameters()
    {
        if (params == nullptr)
            return;

        const auto count = params->count (plugin);

        for (uint32_t i = 0; i < count; ++i)
        {
            clap_param_info_t info {};

            if (! params->get_info (plugin, i, &info) || parametersById.count (info.id) > 0)
                continue;

            double value = info.default_value;
            params->get_value (plugin, info.id, &value);

            auto parameter = std::make_unique<ClapParameter> (*this, info, value);
            parametersById[info.id] = parameter.get();
            clapParameters.push_back (parameter.get());

            if (bypass == nullptr && parameter->isBypass())
                bypass = parameter.get();

            addHostedParameter (std::move (parameter));
        }
    }

    /** After a state load or a rescan(VALUES). CLAP says such changes aren't to be recorded as
        automation, so the host hears of them from the audio thread (see reportValueChanges), where
        hosts take them as changes rather than edits.
    */
    void refreshParameterValues()
    {
        if (params == nullptr)
            return;

        for (auto* p : clapParameters)
        {
            double value = 0.0;

            if (! p->isGone() && params->get_value (plugin, p->getClapId(), &value) && p->setValueFromPlugin (value))
            {
                p->markForReport();
                reportsPending = true;
            }
        }
    }

    /** Audio thread. */
    void reportValueChanges()
    {
        if (! reportsPending.exchange (false))
            return;

        for (auto* p : clapParameters)
            if (p->takeReport())
                p->sendValueChangedMessageToListeners (p->getValue());
    }

    void collectParameterChanges (ClapEventList& list)
    {
        if (! parametersPending.exchange (false))
            return;

        for (auto* p : clapParameters)
        {
            double plain = 0.0;

            if (! p->takePending (plain) || p->isGone())
                continue;

            clap_event_param_value_t event {};
            event.header = makeClapHeader<clap_event_param_value_t> (CLAP_EVENT_PARAM_VALUE, 0);
            event.param_id = p->getClapId();
            event.cookie = p->getCookie();
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plain;

            if (! list.push (event))
            {
                // Full: try again next block.
                p->markPending();
                parametersPending = true;
            }
        }
    }

    bool receiveOutputEvent (const clap_event_header_t& event)
    {
        if (event.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return true;

        if (event.type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto& e = reinterpret_cast<const clap_event_param_value_t&> (event);

            // Only the parameter itself: per-note or per-key values aren't anything the host shows.
            if (e.note_id != -1 || e.port_index != -1 || e.channel != -1 || e.key != -1)
                return true;

            if (auto* p = findParameter (e.param_id))
            {
                if (! p->setValueFromPlugin (e.value))
                    return true;

                if ((event.flags & CLAP_EVENT_DONT_RECORD) != 0 && ! onMessageThread())
                    p->sendValueChangedMessageToListeners (p->getValue());     // a change, not an edit
                else
                    passEditOn ({ p, PluginEdit::Kind::value, p->getValue() });
            }
        }
        else if (event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event.type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            const auto& e = reinterpret_cast<const clap_event_param_gesture_t&> (event);

            if (auto* p = findParameter (e.param_id))
                passEditOn ({ p, event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? PluginEdit::Kind::gestureBegin
                                                                              : PluginEdit::Kind::gestureEnd, 0.0f });
        }

        return true;
    }

    void passEditOn (const PluginEdit& edit)
    {
        if (onMessageThread())
        {
            deliver (edit);
            return;
        }

        // A full queue loses the notification only; the value itself is already set.
        editFifo.write (1).forEach ([&] (int index) { edits[(size_t) index] = edit; });
        editsPending = true;
    }

    /** Message thread. */
    void deliverPluginEdits()
    {
        editFifo.read (editFifo.getNumReady()).forEach ([this] (int index) { deliver (edits[(size_t) index]); });
    }

    static void deliver (const PluginEdit& edit)
    {
        switch (edit.kind)
        {
            case PluginEdit::Kind::value:           edit.parameter->sendValueChangedMessageToListeners (edit.value); break;
            case PluginEdit::Kind::gestureBegin:    edit.parameter->beginChangeGesture(); break;
            case PluginEdit::Kind::gestureEnd:      edit.parameter->endChangeGesture(); break;
        }
    }

    //==============================================================================
    void activateLocked (double sampleRate, int maxFrames)
    {
        prepareAudioBuffers (maxFrames);

        if (! plugin->activate (plugin, sampleRate, 1, (uint32_t) maxFrames))
            return;

        active = true;
        activeSampleRate = sampleRate;
        activeMaxFrames = maxFrames;
        steadyTime = 0;
        translatorNeedsReset = true;

        if (latency != nullptr)
            setLatencySamples ((int) latency->get (plugin));
    }

    void deactivateLocked()
    {
        // CLAP wants stop_processing on the audio thread; when the host is shutting the plugin
        // down there may not be one any more. Holding processLock, this thread is the only one
        // that can process, so it stands in as the audio thread, which the CLAP spec allows.
        if (processing)
        {
            const AudioThreadScope audioThread;
            plugin->stop_processing (plugin);
            processing = false;
        }

        if (active.load())
        {
            active = false;     // first: a rescan the plugin makes inside deactivate() is then an inactive one
            plugin->deactivate (plugin);
        }

        translatorNeedsReset = true;
    }

    void prepareAudioBuffers (int maxFrames)
    {
        int channels = 0;

        for (auto c : inputPorts)   channels += (int) c;
        for (auto c : outputPorts)  channels += (int) c;

        spareChannels.setSize (juce::jmax (1, channels), maxFrames);
        spareChannels.clear();

        // Room for a block's worth of notes and expression, even at the finest expression resolution.
        const auto events = (size_t) juce::jlimit (8192, 1 << 18, maxFrames * 16);
        audioEvents.reserve (events * sizeof (clap_event_param_value_t), events);

        auto setUp = [] (const std::vector<uint32_t>& ports, std::vector<std::vector<float*>>& pointers,
                         std::vector<clap_audio_buffer_t>& buffers)
        {
            pointers.assign (ports.size(), {});
            buffers.assign (ports.size(), {});

            for (size_t i = 0; i < ports.size(); ++i)
            {
                pointers[i].assign (juce::jmax<size_t> (1, ports[i]), nullptr);
                buffers[i].data32 = pointers[i].data();
                buffers[i].data64 = nullptr;
                buffers[i].channel_count = ports[i];
                buffers[i].latency = 0;
                buffers[i].constant_mask = 0;
            }
        };

        setUp (inputPorts, inputPointers, inputBuffers);
        setUp (outputPorts, outputPointers, outputBuffers);
    }

    void assignAudioBuffers (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        int spare = 0;

        for (size_t i = 0; i < inputPorts.size(); ++i)
        {
            for (uint32_t ch = 0; ch < inputPorts[i]; ++ch)
            {
                auto* data = spareChannels.getWritePointer (spare++);
                juce::FloatVectorOperations::clear (data, numSamples);
                inputPointers[i][ch] = data;
            }

            // Silent inputs are constant.
            inputBuffers[i].constant_mask = inputPorts[i] >= 64 ? ~uint64_t (0) : (uint64_t (1) << inputPorts[i]) - 1;
        }

        for (size_t o = 0; o < outputPorts.size(); ++o)
        {
            for (uint32_t ch = 0; ch < outputPorts[o]; ++ch)
            {
                if ((int) o == mainOutput && (int) ch < buffer.getNumChannels())
                    outputPointers[o][ch] = buffer.getWritePointer ((int) ch);
                else
                    outputPointers[o][ch] = spareChannels.getWritePointer (spare++);
            }

            outputBuffers[o].constant_mask = 0;
        }
    }

    bool fillTransport (clap_event_transport_t& t) const
    {
        t.header = makeClapHeader<clap_event_transport_t> (CLAP_EVENT_TRANSPORT, 0);
        t.tsig_num = 4;
        t.tsig_denom = 4;
        t.tempo = 120.0;

        auto* head = getPlayHead();

        if (head == nullptr)
            return false;

        const auto position = head->getPosition();

        if (! position.hasValue())
            return false;

        if (const auto bpm = position->getBpm())
        {
            t.flags |= CLAP_TRANSPORT_HAS_TEMPO;
            t.tempo = *bpm;
        }

        if (const auto ppq = position->getPpqPosition())
        {
            t.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
            t.song_pos_beats = toBeatTime (*ppq);
        }

        if (const auto barStart = position->getPpqPositionOfLastBarStart())
            t.bar_start = toBeatTime (*barStart);

        if (const auto bars = position->getBarCount())
            t.bar_number = (int32_t) *bars;

        if (const auto seconds = position->getTimeInSeconds())
        {
            t.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
            t.song_pos_seconds = toSecTime (*seconds);
        }

        if (const auto signature = position->getTimeSignature())
        {
            t.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
            t.tsig_num = (uint16_t) juce::jlimit (1, 64, signature->numerator);
            t.tsig_denom = (uint16_t) juce::jlimit (1, 64, signature->denominator);
        }

        if (const auto loop = position->getLoopPoints())
        {
            t.loop_start_beats = toBeatTime (loop->ppqStart);
            t.loop_end_beats = toBeatTime (loop->ppqEnd);
        }

        if (position->getIsPlaying())     t.flags |= CLAP_TRANSPORT_IS_PLAYING;
        if (position->getIsRecording())   t.flags |= CLAP_TRANSPORT_IS_RECORDING;
        if (position->getIsLooping())     t.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;

        return true;
    }

    //==============================================================================
    void handleAsyncUpdate() override
    {
        if (hostContext->callbackRequested.exchange (false))
            plugin->on_main_thread (plugin);

        deliverPluginEdits();

        if (restartPending.load())
            restart();

        // While the audio thread is running it passes these on; keep checking in case it stops.
        if ((flushRequested.load() || parametersPending.load()) && ! flushParametersNow())
            startTimer (100);

        handleGuiRequests();
        hostContext->clearRetiredTimers();
    }

    void handleGuiRequests();

    void timerCallback() override
    {
        stopTimer();
        handleAsyncUpdate();
    }

    void restart()
    {
        {
            const juce::SpinLock::ScopedLockType lock (processLock);

            if (processing)
            {
                // The audio thread stops processing at its next block. Only if audio has stalled
                // do we stop it from here instead, standing in for the audio thread.
                if (! audioHasStalled())
                {
                    startTimer (20);
                    return;
                }

                const AudioThreadScope audioThread;
                plugin->stop_processing (plugin);
                processing = false;
            }

            const auto wasActive = active.load();
            const auto sampleRate = activeSampleRate;
            const auto maxFrames = activeMaxFrames;

            if (wasActive)
            {
                active = false;
                plugin->deactivate (plugin);
            }

            restartPending = false;
            translatorNeedsReset = true;

            if (wasActive)
                activateLocked (sampleRate, maxFrames);
        }

        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }

    bool audioHasStalled() const noexcept
    {
        return juce::Time::getMillisecondCounter() - lastProcessTime.load() >= 500;
    }

    /** Message thread: gives the plugin the host's pending values (and the flush it may have asked
        for) right now. Returns false if that's left to the audio thread's next block.
    */
    bool flushParametersNow()
    {
        if (params == nullptr || params->flush == nullptr)
        {
            flushRequested = false;
            return true;
        }

        if (! active.load())
        {
            const juce::SpinLock::ScopedLockType lock (processLock);

            if (! active.load())
            {
                flushLocked();
                return true;
            }
        }

        // Active: process() will do it, unless audio has stopped (FL can stop calling an idle
        // plugin). Then flush here, standing in for the audio thread, as CLAP allows.
        if (! audioHasStalled())
            return false;

        // Set before the lock is tried and cleared after it's released, so an audio thread that
        // finds the lock taken knows to wait for it.
        standingIn = true;

        {
            const juce::SpinLock::ScopedTryLockType lock (processLock);

            if (! lock.isLocked())
            {
                standingIn = false;
                return false;
            }

            const AudioThreadScope audioThread;
            flushLocked();
        }

        standingIn = false;
        return true;
    }

    /** With processLock held: the plugin is inactive, or this thread stands in for the audio thread. */
    void flushLocked()
    {
        flushRequested = false;
        mainThreadEvents.clear();
        collectParameterChanges (mainThreadEvents);
        params->flush (plugin, mainThreadEvents.getInputEvents(), &outputEvents);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapPluginInstance)
};

//==============================================================================
void ClapParameter::setValue (float newValue)
{
    // The host can't change a read-only parameter, and one the plugin took away is no more.
    if ((flags.load() & CLAP_PARAM_IS_READONLY) != 0 || gone.load())
        return;

    value = juce::jlimit (0.0f, 1.0f, newValue);
    pending = true;
    owner.parameterChangedByHost();
}

// value_to_text and text_to_value may only be called on the main thread. Elsewhere, the number.
juce::String ClapParameter::getText (float normalised, int maximumStringLength) const
{
    const auto* plugin = owner.getClapPlugin();
    const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    const auto plain = toPlain (normalised);
    char text[256] = {};

    if (params != nullptr && params->value_to_text != nullptr && onMessageThread()
        && params->value_to_text (plugin, id, plain, text, (uint32_t) sizeof (text)))
        return juce::String::fromUTF8 (text, (int) strnlen (text, sizeof (text))).substring (0, maximumStringLength);

    return juce::String (plain, isStepped() ? 0 : 3).substring (0, maximumStringLength);
}

float ClapParameter::getValueForText (const juce::String& text) const
{
    const auto* plugin = owner.getClapPlugin();
    const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    double plain = text.getDoubleValue();

    if (params != nullptr && params->text_to_value != nullptr && onMessageThread())
        params->text_to_value (plugin, id, text.toRawUTF8(), &plain);

    return toNormalised (plain);
}

//==============================================================================
/** Shows the plugin's own window, embedded when it can be, otherwise floating beside us. */
class ClapEditor final : public juce::AudioProcessorEditor,
                         private juce::ComponentMovementWatcher
{
public:
    explicit ClapEditor (ClapPluginInstance& owner)
        : AudioProcessorEditor (owner),
          ComponentMovementWatcher (this),
          instance (owner),
          plugin (owner.getClapPlugin()),
          gui (owner.getGui())
    {
        setOpaque (true);
        instance.editorOpened (this);

       #if JUCE_MAC
        setClapContainerView (embedded);
       #endif

        const auto* api = nativeWindowApi();

        if (gui != nullptr && gui->is_api_supported (plugin, api, false))
            created = gui->create (plugin, api, false);

        if (! created && gui != nullptr && gui->is_api_supported (plugin, api, true))
        {
            floating = true;
            created = gui->create (plugin, api, true);
        }

        if (! created)
        {
            showMessage (owner.getName() + "'s window couldn't be opened.");
            return;
        }

        if (floating)
        {
            openedFloating();
            return;
        }

       #if ! JUCE_MAC
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            scale = (float) display->scale;

        gui->set_scale (plugin, scale);
       #endif

        uint32_t width = 0, height = 0;

        if (gui->get_size (plugin, &width, &height) && width > 0 && height > 0)
            setSize (toLogical (width), toLogical (height));
        else
            setSize (640, 400);

        setResizable (gui->can_resize (plugin), false);
    }

    ~ClapEditor() override
    {
        instance.editorClosing (this);

        if (created)
        {
           #if JUCE_LINUX || JUCE_BSD
            if (attached)
                embedded.removeClient();
           #endif

            gui->destroy (plugin);
        }

       #if JUCE_MAC
        embedded.setView (nullptr);
       #endif

        processor.editorBeingDeleted (this);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1f22));

        if (message.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (15.0f);
            g.drawFittedText (message, getLocalBounds().reduced (16).withTrimmedBottom (showButton.isVisible() ? 40 : 0),
                              juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        showButton.setBounds (getLocalBounds().removeFromBottom (44).reduced (100, 8));
        embedded.setBounds (getLocalBounds());

        if (! attached || inPluginResize || ! gui->can_resize (plugin))
            return;

        // The user resized the window: ask the plugin for the nearest size it supports, and end up
        // at whatever size it actually takes.
        auto width = toPhysical (getWidth()), height = toPhysical (getHeight());
        const auto requestsBefore = pluginResizeRequests;
        const auto accepted = gui->adjust_size (plugin, &width, &height) && gui->set_size (plugin, width, height);

        if (pluginResizeRequests != requestsBefore)
            return;     // it asked for a size of its own while we were setting it, and has that now

        if (! accepted && ! (gui->get_size (plugin, &width, &height) && width > 0 && height > 0))
        {
            updateEmbeddedBounds();
            return;
        }

        if (toLogical (width) != getWidth() || toLogical (height) != getHeight())
        {
            const juce::ScopedValueSetter<bool> guard (inPluginResize, true);
            setSize (toLogical (width), toLogical (height));
        }

        updateEmbeddedBounds();
    }

    /** Returns whether the new size was taken. */
    bool pluginRequestedResize (uint32_t width, uint32_t height)
    {
        if (floating || ! created || placementFailed || width == 0 || height == 0)
            return false;

        ++pluginResizeRequests;
        const juce::ScopedValueSetter<bool> guard (inPluginResize, true);
        setSize (toLogical (width), toLogical (height));
        embedded.setBounds (getLocalBounds());
        updateEmbeddedBounds();
        return true;
    }

    void pluginResizeHintsChanged()
    {
        if (! created || floating)
            return;

        // The window around us read this when it opened, so it's told too.
        const auto resizable = gui->can_resize (plugin);
        setResizable (resizable, false);

        if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
            window->setResizable (resizable, false);
    }

    void pluginClosedWindow (bool wasDestroyed)
    {
        // Not destroyed: the user closed a floating window, and Show brings it back.
        if (! created || ! wasDestroyed)
            return;

       #if JUCE_LINUX || JUCE_BSD
        if (attached)
            embedded.removeClient();
       #endif

        gui->destroy (plugin);
        created = attached = false;
        embedded.setVisible (false);
        showButton.setVisible (false);
        showMessage ("Close this window and open the synth again to bring its window back.");
    }

private:
    ClapPluginInstance& instance;
    const clap_plugin_t* plugin;
    const clap_plugin_gui_t* gui;

    bool created = false, floating = false, attached = false, placementFailed = false, inPluginResize = false;
    int pluginResizeRequests = 0;
    float scale = 1.0f;
    juce::String message;
    juce::TextButton showButton;

   #if JUCE_WINDOWS
    // A plain native child window for the plugin to put its own window into.
    struct ViewComponent final : public juce::HWNDComponent
    {
        ViewComponent()
        {
            setOpaque (true);
            inner.addToDesktop (0);

            if (auto* peer = inner.getPeer())
                setHWND (peer->getNativeHandle());
        }

        void paint (juce::Graphics& g) override     { g.fillAll (juce::Colours::black); }

    private:
        struct Inner final : public juce::Component
        {
            Inner() { setOpaque (true); }
            void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
        };

        Inner inner;
    };

    ViewComponent embedded;
   #elif JUCE_MAC
    juce::NSViewComponent embedded;
   #elif JUCE_LINUX || JUCE_BSD
    juce::XEmbedComponent embedded { juce::XEmbedComponentOptions{}.withWantsKeyboardFocus (true)
                                                                    .withAllowForeignWidgetToResizeComponent (false)
                                                                    .withIgnoreXembedMapped() };
   #else
    juce::Component embedded;
   #endif

    juce::NativeScaleFactorNotifier scaleNotifier { this, [this] (float newScale) { platformScaleChanged (newScale); } };

    void showMessage (const juce::String& text)
    {
        message = text;
        setSize (440, 140);
        repaint();
    }

    void openedFloating()
    {
        gui->suggest_title (plugin, instance.getName().toRawUTF8());
        makeTransient();
        gui->show (plugin);

        showButton.setButtonText ("Show " + instance.getName());
        showButton.onClick = [this] { if (created) gui->show (plugin); };
        addAndMakeVisible (showButton);
        showMessage (instance.getName() + " opened its window separately.");

        // Again once the window around us has been set up.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ClapEditor> (this)]
        {
            if (safe != nullptr)
                safe->makeTransient();
        });
    }

    /** A floating plugin window stays above the window this editor is in. */
    void makeTransient()
    {
        if (! floating || ! created)
            return;

        auto* top = getTopLevelComponent();

        if (top == nullptr || top == this)
            return;

        // An always-on-top placeholder would cover the plugin's own window.
        if (top->isAlwaysOnTop())
            top->setAlwaysOnTop (false);

        auto* peer = top->getPeer();

        if (peer == nullptr)
            return;

        clap_window_t window {};
        window.api = nativeWindowApi();

       #if JUCE_WINDOWS
        window.win32 = peer->getNativeHandle();
       #elif JUCE_MAC
        window.cocoa = peer->getNativeHandle();
       #else
        window.x11 = (clap_xwnd) (juce::pointer_sized_uint) peer->getNativeHandle();
       #endif

        gui->set_transient (plugin, &window);
    }

    int toLogical (uint32_t physical) const noexcept    { return juce::jmax (10, juce::roundToInt ((float) physical / scale)); }
    uint32_t toPhysical (int logical) const noexcept    { return (uint32_t) juce::jmax (1, juce::roundToInt ((float) logical * scale)); }

    void updateEmbeddedBounds()
    {
       #if JUCE_WINDOWS
        embedded.updateHWNDBounds();
       #elif JUCE_LINUX || JUCE_BSD
        embedded.updateEmbeddedBounds();
       #endif
    }

    void platformScaleChanged ([[maybe_unused]] float newScale)
    {
       #if ! JUCE_MAC
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ClapEditor> (this), newScale]
        {
            auto* self = safe.getComponent();

            if (self == nullptr || ! self->created || self->floating || juce::approximatelyEqual (self->scale, newScale))
                return;

            self->scale = newScale;
            self->gui->set_scale (self->plugin, newScale);

            uint32_t width = 0, height = 0;

            if (self->gui->get_size (self->plugin, &width, &height))
                self->pluginRequestedResize (width, height);
        });
       #endif
    }

    void attach()
    {
        if (attached || ! created || floating || ! isShowing() || getPeer() == nullptr)
            return;

        embedded.setBounds (getLocalBounds());
        addAndMakeVisible (embedded);

        clap_window_t window {};
        window.api = nativeWindowApi();

       #if JUCE_WINDOWS
        window.win32 = embedded.getHWND();
        const bool hasWindow = window.win32 != nullptr;
       #elif JUCE_MAC
        window.cocoa = embedded.getView();
        const bool hasWindow = window.cocoa != nullptr;
       #elif JUCE_LINUX || JUCE_BSD
        window.x11 = embedded.getHostWindowID();
        const bool hasWindow = window.x11 != 0;
       #else
        const bool hasWindow = false;
       #endif

        if (! hasWindow)
            return;

        attached = gui->set_parent (plugin, &window);

        if (! attached)
        {
            embedded.setVisible (false);

            // It can't go in here; it may be able to open a window of its own.
            const auto* api = nativeWindowApi();
            gui->destroy (plugin);
            created = false;

            if (gui->is_api_supported (plugin, api, true) && gui->create (plugin, api, true))
            {
                created = floating = true;
                openedFloating();
                return;
            }

            placementFailed = true;
            showMessage (instance.getName() + "'s window couldn't be placed here.");
            return;
        }

        gui->show (plugin);
        updateEmbeddedBounds();
    }

    void componentMovedOrResized (bool, bool) override          {}
    void componentPeerChanged() override                        { attach(); makeTransient(); }
    void componentVisibilityChanged() override                  { attach(); }
    using ComponentMovementWatcher::componentMovedOrResized;
    using ComponentMovementWatcher::componentVisibilityChanged;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapEditor)
};

juce::AudioProcessorEditor* ClapPluginInstance::createEditor()
{
    return new ClapEditor (*this);
}

bool ClapPluginInstance::requestResize (uint32_t width, uint32_t height)
{
    if (! onMessageThread())
    {
        // Allowed from any thread: pass it to the message thread.
        requestedWidth = width;
        requestedHeight = height;
        resizeRequested = true;
        triggerAsyncUpdate();
        return true;
    }

    return editor != nullptr && editor->pluginRequestedResize (width, height);
}

// These two may come from any thread, and closed() often from inside the plugin's own window
// code, so they're always handled afterwards, on the message thread.
void ClapPluginInstance::resizeHintsChanged()
{
    hintsChanged = true;
    triggerAsyncUpdate();
}

void ClapPluginInstance::guiClosed (bool wasDestroyed)
{
    if (wasDestroyed)
        guiDestroyed = true;

    guiClosedPending = true;
    triggerAsyncUpdate();
}

void ClapPluginInstance::handleGuiRequests()
{
    if (resizeRequested.exchange (false) && editor != nullptr)
        editor->pluginRequestedResize (requestedWidth.load(), requestedHeight.load());

    if (hintsChanged.exchange (false) && editor != nullptr)
        editor->pluginResizeHintsChanged();

    if (guiClosedPending.exchange (false))
    {
        const auto destroyed = guiDestroyed.exchange (false);

        if (editor != nullptr)
            editor->pluginClosedWindow (destroyed);
    }
}

//==============================================================================
// The host extensions MPE Roll offers. Every callback copes with being called before the
// instance exists (while the plugin initialises) or after it's gone.
namespace hostcallbacks
{
    static ClapPluginInstance* ownerOf (const clap_host_t* host)   { return ClapHostContext::of (host).owner.load(); }

    const clap_host_thread_check_t threadCheck
    {
        [] (const clap_host_t*) { return onMessageThread(); },
        [] (const clap_host_t*) { return onClapAudioThread; }
    };

    const clap_host_log_t logging
    {
        [] (const clap_host_t*, clap_log_severity severity, const char* text)
        {
            juce::ignoreUnused (severity, text);
            DBG ("CLAP plugin [" << (int) severity << "] " << juce::String::fromUTF8 (text != nullptr ? text : ""));
        }
    };

    const clap_host_params_t params
    {
        [] (const clap_host_t* host, clap_param_rescan_flags flags)
        {
            if (auto* o = ownerOf (host))
                o->rescanParameters (flags);
        },
        [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
        [] (const clap_host_t* host)
        {
            if (auto* o = ownerOf (host))
                o->requestParameterFlush();
        }
    };

    const clap_host_state_t state
    {
        [] (const clap_host_t* host)
        {
            if (auto* o = ownerOf (host))
                o->markStateDirty();
        }
    };

    const clap_host_latency_t latency
    {
        // Latency may only change while the plugin is inactive; it's read again on activation.
        [] (const clap_host_t*) {}
    };

    const clap_host_tail_t tail
    {
        [] (const clap_host_t*) {}
    };

    const clap_host_note_ports_t notePorts
    {
        [] (const clap_host_t*) -> uint32_t
        {
            return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
        },
        [] (const clap_host_t* host, uint32_t)
        {
            if (auto* o = ownerOf (host))
                o->rescanNotePorts();
        }
    };

    const clap_host_audio_ports_t audioPorts
    {
        [] (const clap_host_t*, uint32_t) { return false; },
        [] (const clap_host_t*, uint32_t) {}
    };

    const clap_host_gui_t gui
    {
        [] (const clap_host_t* host)
        {
            if (auto* o = ownerOf (host))
                o->resizeHintsChanged();
        },
        [] (const clap_host_t* host, uint32_t width, uint32_t height)
        {
            auto* o = ownerOf (host);
            return o != nullptr && o->requestResize (width, height);
        },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t* host, bool wasDestroyed)
        {
            if (auto* o = ownerOf (host))
                o->guiClosed (wasDestroyed);
        }
    };

    const clap_host_timer_support_t timerSupport
    {
        [] (const clap_host_t* host, uint32_t periodMs, clap_id* id)
        {
            return ClapHostContext::of (host).registerTimer (periodMs, id);
        },
        [] (const clap_host_t* host, clap_id id)
        {
            return ClapHostContext::of (host).unregisterTimer (id);
        }
    };

   #if JUCE_LINUX || JUCE_BSD
    const clap_host_posix_fd_support_t posixFdSupport
    {
        [] (const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) { return ClapHostContext::of (host).registerFd (fd, flags); },
        [] (const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) { return ClapHostContext::of (host).modifyFd (fd, flags); },
        [] (const clap_host_t* host, int fd) { return ClapHostContext::of (host).unregisterFd (fd); }
    };
   #endif

    static const void* getExtension (const clap_host_t*, const char* id)
    {
        if (id == nullptr)                                      return nullptr;
        if (std::strcmp (id, CLAP_EXT_THREAD_CHECK) == 0)       return &threadCheck;
        if (std::strcmp (id, CLAP_EXT_LOG) == 0)                return &logging;
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)             return &params;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)              return &state;
        if (std::strcmp (id, CLAP_EXT_LATENCY) == 0)            return &latency;
        if (std::strcmp (id, CLAP_EXT_TAIL) == 0)               return &tail;
        if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)         return &notePorts;
        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)        return &audioPorts;
        if (std::strcmp (id, CLAP_EXT_GUI) == 0)                return &gui;
        if (std::strcmp (id, CLAP_EXT_TIMER_SUPPORT) == 0)      return &timerSupport;
       #if JUCE_LINUX || JUCE_BSD
        if (std::strcmp (id, CLAP_EXT_POSIX_FD_SUPPORT) == 0)   return &posixFdSupport;
       #endif
        return nullptr;
    }
}

//==============================================================================
ClapHostContext::ClapHostContext()
{
    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = "MPE Roll";
    host.vendor = "Glowing Pancake";
    host.url = "https://github.com/freener0x/glowing-pancake";
    host.version = JucePlugin_VersionString;
    host.get_extension = hostcallbacks::getExtension;

    host.request_restart = [] (const clap_host_t* h)
    {
        if (auto* o = hostcallbacks::ownerOf (h))
            o->requestRestart();
    };

    // MPE Roll calls process() every block regardless, so there's nothing to wake up.
    host.request_process = [] (const clap_host_t*) {};

    host.request_callback = [] (const clap_host_t* h)
    {
        // The flag first: an instance being constructed right now checks it after setting owner.
        auto& context = of (h);
        context.callbackRequested = true;

        if (auto* o = context.owner.load())
            o->requestHousekeeping();
    };
}

ClapHostContext::~ClapHostContext()
{
    timers.clear();
    retiredTimers.clear();
    unregisterAllFds();
}

bool ClapHostContext::registerTimer (uint32_t periodMs, clap_id* id)
{
    if (id == nullptr)
        return false;

    auto timer = std::make_unique<PluginTimer> (*this, nextTimerId++);
    timer->startTimer ((int) juce::jlimit<uint32_t> (10, 60000, periodMs));
    *id = timer->id;
    timers[timer->id] = std::move (timer);
    return true;
}

bool ClapHostContext::unregisterTimer (clap_id id)
{
    const auto found = timers.find (id);

    if (found == timers.end())
        return false;

    found->second->stopTimer();
    retiredTimers.push_back (std::move (found->second));
    timers.erase (found);

    if (auto* o = owner.load())
        o->requestHousekeeping();   // deletes the retired timer once its callback has certainly returned

    return true;
}

void ClapHostContext::timerFired (clap_id id)
{
    if (plugin == nullptr)
        return;

    if (const auto* support = static_cast<const clap_plugin_timer_support_t*> (plugin->get_extension (plugin, CLAP_EXT_TIMER_SUPPORT)))
        support->on_timer (plugin, id);
}

#if JUCE_LINUX || JUCE_BSD
/** JUCE's event loop holds one callback per file descriptor for the whole process, but plugins can
    share a descriptor between their instances (one X connection, say). So every descriptor is
    registered with JUCE once, and each event goes to every context that asked for it. Main thread.
*/
namespace fdtable
{
    using Users = std::vector<std::pair<ClapHostContext*, clap_posix_fd_flags_t>>;

    struct Entry
    {
        Users users;
        bool registeredWithJuce = false;
    };

    std::map<int, Entry>& table()
    {
        static std::map<int, Entry> fds;
        return fds;
    }

    short pollEventsFor (clap_posix_fd_flags_t flags)
    {
        short events = 0;

        if ((flags & CLAP_POSIX_FD_READ) != 0)   events |= POLLIN;
        if ((flags & CLAP_POSIX_FD_WRITE) != 0)  events |= POLLOUT;
        if ((flags & CLAP_POSIX_FD_ERROR) != 0)  events |= POLLERR;

        return events;
    }

    void dispatch (int fd);

    /** (Re)registers fd with JUCE for everything its users want, or unregisters it if none are left. */
    void update (int fd)
    {
        auto& fds = table();
        const auto found = fds.find (fd);

        if (found == fds.end())
            return;

        auto& entry = found->second;

        if (entry.registeredWithJuce)
        {
            juce::LinuxEventLoop::unregisterFdCallback (fd);
            entry.registeredWithJuce = false;
        }

        clap_posix_fd_flags_t wanted = 0;

        for (const auto& user : entry.users)
            wanted |= user.second;

        if (entry.users.empty())
        {
            fds.erase (found);
            return;
        }

        if (pollEventsFor (wanted) == 0)
            return;

        // The callback only carries the number: it looks the users up when it runs, so a
        // context that has gone in the meantime is never called.
        juce::LinuxEventLoop::registerFdCallback (fd, [] (int readyFd) { dispatch (readyFd); }, pollEventsFor (wanted));
        entry.registeredWithJuce = true;
    }

    void dispatch (int fd)
    {
        const auto found = table().find (fd);

        if (found == table().end())
            return;

        // What actually happened: on_fd is to be told that, not what was asked for.
        pollfd query { fd, pollEventsFor (CLAP_POSIX_FD_READ | CLAP_POSIX_FD_WRITE | CLAP_POSIX_FD_ERROR), 0 };

        if (::poll (&query, 1, 0) <= 0)
            return;

        clap_posix_fd_flags_t happened = 0;

        // A hang-up also counts as readable, as poll() means it: reading then finds the end, so a
        // plugin that only asked to read still learns the other end has closed.
        if ((query.revents & (POLLIN | POLLHUP)) != 0)                  happened |= CLAP_POSIX_FD_READ;
        if ((query.revents & POLLOUT) != 0)                             happened |= CLAP_POSIX_FD_WRITE;
        if ((query.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)      happened |= CLAP_POSIX_FD_ERROR;

        // A plugin may unregister (or a context go away) from inside on_fd: work from a copy, and
        // check each user is still there before calling it.
        const auto users = found->second.users;

        for (const auto& [context, wanted] : users)
        {
            const auto now = table().find (fd);

            if (now == table().end())
                return;

            const auto& current = now->second.users;
            const auto stillThere = std::any_of (current.begin(), current.end(),
                                                 [c = context] (const auto& u) { return u.first == c; });

            if (stillThere && (happened & wanted) != 0)
                context->fdReady (fd, happened & wanted);
        }
    }
}

bool ClapHostContext::registerFd (int fd, clap_posix_fd_flags_t flags)
{
    if (fd < 0 || fds.count (fd) > 0)
        return false;

    fds[fd] = flags;
    fdtable::table()[fd].users.emplace_back (this, flags);
    fdtable::update (fd);
    return true;
}

bool ClapHostContext::modifyFd (int fd, clap_posix_fd_flags_t flags)
{
    if (fds.count (fd) == 0)
        return false;

    fds[fd] = flags;

    for (auto& user : fdtable::table()[fd].users)
        if (user.first == this)
            user.second = flags;

    fdtable::update (fd);
    return true;
}

bool ClapHostContext::unregisterFd (int fd)
{
    if (fds.erase (fd) == 0)
        return false;

    auto& users = fdtable::table()[fd].users;
    users.erase (std::remove_if (users.begin(), users.end(), [this] (const auto& u) { return u.first == this; }), users.end());
    fdtable::update (fd);
    return true;
}

void ClapHostContext::unregisterAllFds()
{
    while (! fds.empty())
        unregisterFd (fds.begin()->first);
}

void ClapHostContext::fdReady (int fd, clap_posix_fd_flags_t happened)
{
    if (plugin == nullptr)
        return;

    if (const auto* support = static_cast<const clap_plugin_posix_fd_support_t*> (plugin->get_extension (plugin, CLAP_EXT_POSIX_FD_SUPPORT)))
        support->on_fd (plugin, fd, happened);
}
#else
bool ClapHostContext::registerFd (int, clap_posix_fd_flags_t)  { return false; }
bool ClapHostContext::modifyFd (int, clap_posix_fd_flags_t)    { return false; }
bool ClapHostContext::unregisterFd (int)                       { return false; }
void ClapHostContext::unregisterAllFds()                       {}
void ClapHostContext::fdReady (int, clap_posix_fd_flags_t)     {}
#endif

//==============================================================================
std::unique_ptr<juce::AudioPluginInstance> createClapPluginInstance (std::shared_ptr<ClapModule> module,
                                                                     const clap_plugin_descriptor_t& descriptor,
                                                                     const juce::PluginDescription& description,
                                                                     juce::String& error)
{
    auto context = std::make_unique<ClapHostContext>();
    const auto& factory = module->getFactory();
    const auto* plugin = factory.create_plugin (&factory, &context->host, descriptor.id);

    if (plugin == nullptr)
    {
        error = description.name + " couldn't be created.";
        return {};
    }

    context->plugin = plugin;

    if (! plugin->init (plugin))
    {
        plugin->destroy (plugin);
        error = description.name + " failed to start.";
        return {};
    }

    const auto ports = queryPorts (plugin);
    return std::make_unique<ClapPluginInstance> (std::move (module), std::move (context), plugin, description, ports);
}

} // namespace mperoll
