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

#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_map>
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
    std::atomic<bool> callbackRequestedEarly { false };

    static ClapHostContext& of (const clap_host_t* h)      { return *static_cast<ClapHostContext*> (h->host_data); }

    // timer-support
    bool registerTimer (uint32_t periodMs, clap_id* id);
    bool unregisterTimer (clap_id id);
    void clearRetiredTimers()                               { retiredTimers.clear(); }

    // posix-fd-support
    bool registerFd (int fd, clap_posix_fd_flags_t flags);
    bool modifyFd (int fd, clap_posix_fd_flags_t flags);
    bool unregisterFd (int fd);

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
    void fdReady (int fd);

    JUCE_DECLARE_NON_COPYABLE (ClapHostContext)
};

//==============================================================================
class ClapParameter final : public juce::HostedAudioProcessorParameter
{
public:
    ClapParameter (ClapPluginInstance& o, const clap_param_info_t& info, double plainValue)
        : owner (o), id (info.id), cookie (info.cookie)
    {
        updateInfo (info, true);
        value = toNormalised (plainValue);
    }

    clap_id getClapId() const noexcept          { return id; }
    void* getCookie() const noexcept            { return cookie; }
    bool isBypass() const noexcept              { return (flags.load() & CLAP_PARAM_IS_BYPASS) != 0; }

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

    /** Message thread. The range only changes while the plugin is inactive. */
    void updateInfo (const clap_param_info_t& info, bool rangeMayChange)
    {
        {
            const juce::SpinLock::ScopedLockType lock (nameLock);
            name = juce::String::fromUTF8 (info.name);
        }

        flags = info.flags;

        if (rangeMayChange && std::isfinite (info.min_value) && std::isfinite (info.max_value))
        {
            minValue = info.min_value;
            maxValue = juce::jmax (info.min_value, info.max_value);
            defaultValue = juce::jlimit (minValue.load(), maxValue.load(), info.default_value);
        }
    }

    /** A value the plugin reports: shown to the host, not sent back. */
    void setValueFromPlugin (double plain)
    {
        const auto normalised = toNormalised (plain);
        value = normalised;
        sendValueChangedMessageToListeners (normalised);
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
    void* const cookie;

    std::atomic<float> value { 0.0f };
    std::atomic<bool> pending { false };
    std::atomic<uint32_t> flags { 0 };
    std::atomic<double> minValue { 0.0 }, maxValue { 1.0 }, defaultValue { 0.0 };

    juce::SpinLock nameLock;
    juce::String name;

    bool isStepped() const noexcept             { return (flags.load() & CLAP_PARAM_IS_STEPPED) != 0; }
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

        hostContext->owner = this;

        if (hostContext->callbackRequestedEarly.exchange (false))
            requestCallback();
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
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const auto numSamples = buffer.getNumSamples();
        const juce::SpinLock::ScopedTryLockType lock (processLock);

        if (! lock.isLocked() || ! active.load() || numSamples > activeMaxFrames || numSamples <= 0)
        {
            buffer.clear();
            return;
        }

        const AudioThreadScope audioThread;
        lastProcessTime = juce::Time::getMillisecondCounter();

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
            translator.reset();

        translator.setOutputSettings (mpeSettings);
        translator.setDialects (takesNoteEvents.load(), takesMidi.load());

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
    }

    using AudioPluginInstance::processBlock;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override
    {
        if (state == nullptr)
            return;

        juce::MemoryOutputStream out (destData, false);
        clap_ostream_t stream {};
        stream.ctx = &out;
        stream.write = [] (const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t
        {
            auto* target = static_cast<juce::MemoryOutputStream*> (s->ctx);
            return target->write (data, (size_t) size) ? (int64_t) size : -1;
        };

        if (! state->save (plugin, &stream))
            destData.reset();
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

    void requestCallback()
    {
        callbackRequested = true;
        triggerAsyncUpdate();
    }

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

        if (! active.load())
            requestParameterFlush();
    }

    void rescanParameters (clap_param_rescan_flags flags)
    {
        if (params == nullptr)
            return;

        if ((flags & (CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT | CLAP_PARAM_RESCAN_ALL)) != 0)
        {
            const auto count = params->count (plugin);
            const auto rangeMayChange = ! active.load();

            for (uint32_t i = 0; i < count; ++i)
            {
                clap_param_info_t info {};

                if (params->get_info (plugin, i, &info))
                    if (auto* p = findParameter (info.id))
                        p->updateInfo (info, rangeMayChange);
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

    // GUI requests (the editor is only touched on the message thread).
    bool requestResize (uint32_t width, uint32_t height);
    void resizeHintsChanged();
    void guiClosed (bool wasDestroyed);
    void editorOpened (ClapEditor* e)           { editor = e; }
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

    // Notes.
    ClapNoteTranslator translator;
    OutputSettings mpeSettings;                     // audio thread
    std::atomic<bool> takesNoteEvents { false }, takesMidi { false }, hasNoteInput { false };
    std::atomic<bool> translatorNeedsReset { false };

    ClapEventList audioEvents, mainThreadEvents;
    clap_output_events_t outputEvents {};

    // Requests handled on the message thread.
    std::atomic<bool> callbackRequested { false }, restartPending { false }, flushRequested { false };
    std::atomic<bool> resizeRequested { false };
    std::atomic<uint32_t> requestedWidth { 0 }, requestedHeight { 0 };

    ClapEditor* editor = nullptr;

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

    void refreshParameterValues()
    {
        if (params == nullptr)
            return;

        for (auto* p : clapParameters)
        {
            double value = 0.0;

            if (params->get_value (plugin, p->getClapId(), &value))
                p->setValueFromPlugin (value);
        }
    }

    void collectParameterChanges (ClapEventList& list)
    {
        if (! parametersPending.exchange (false))
            return;

        for (auto* p : clapParameters)
        {
            double plain = 0.0;

            if (! p->takePending (plain))
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

            if (auto* p = findParameter (e.param_id))
                p->setValueFromPlugin (e.value);
        }
        else if (event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event.type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            const auto& e = reinterpret_cast<const clap_event_param_gesture_t&> (event);

            if (auto* p = findParameter (e.param_id))
            {
                if (event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
                    p->beginChangeGesture();
                else
                    p->endChangeGesture();
            }
        }

        return true;
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
        // down there may not be one any more, so it happens here.
        if (processing)
        {
            plugin->stop_processing (plugin);
            processing = false;
        }

        if (active.load())
        {
            plugin->deactivate (plugin);
            active = false;
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
        if (callbackRequested.exchange (false))
            plugin->on_main_thread (plugin);

        if (restartPending.load())
            restart();

        if (flushRequested.exchange (false) || parametersPending.load())
            flushParametersOnMessageThread();

        if (resizeRequested.exchange (false))
            requestResize (requestedWidth.load(), requestedHeight.load());

        hostContext->clearRetiredTimers();
    }

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
                // do we stop it from here instead.
                if (juce::Time::getMillisecondCounter() - lastProcessTime.load() < 500)
                {
                    startTimer (20);
                    return;
                }

                plugin->stop_processing (plugin);
                processing = false;
            }

            const auto wasActive = active.load();
            const auto sampleRate = activeSampleRate;
            const auto maxFrames = activeMaxFrames;

            if (wasActive)
            {
                plugin->deactivate (plugin);
                active = false;
            }

            restartPending = false;
            translatorNeedsReset = true;

            if (wasActive)
                activateLocked (sampleRate, maxFrames);
        }

        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }

    void flushParametersOnMessageThread()
    {
        const juce::SpinLock::ScopedLockType lock (processLock);

        // While active, changes go to the plugin with the next audio block instead.
        if (active.load() || params == nullptr || params->flush == nullptr)
            return;

        mainThreadEvents.clear();
        collectParameterChanges (mainThreadEvents);
        params->flush (plugin, mainThreadEvents.getInputEvents(), &outputEvents);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapPluginInstance)
};

//==============================================================================
void ClapParameter::setValue (float newValue)
{
    value = juce::jlimit (0.0f, 1.0f, newValue);
    pending = true;
    owner.parameterChangedByHost();
}

juce::String ClapParameter::getText (float normalised, int maximumStringLength) const
{
    const auto* plugin = owner.getClapPlugin();
    const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    const auto plain = toPlain (normalised);
    char text[256] = {};

    if (params != nullptr && params->value_to_text != nullptr
        && params->value_to_text (plugin, id, plain, text, (uint32_t) sizeof (text)))
        return juce::String::fromUTF8 (text).substring (0, maximumStringLength);

    return juce::String (plain, isStepped() ? 0 : 3).substring (0, maximumStringLength);
}

float ClapParameter::getValueForText (const juce::String& text) const
{
    const auto* plugin = owner.getClapPlugin();
    const auto* params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    double plain = text.getDoubleValue();

    if (params != nullptr && params->text_to_value != nullptr)
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
            gui->suggest_title (plugin, owner.getName().toRawUTF8());
            gui->show (plugin);

            showButton.setButtonText ("Show " + owner.getName());
            showButton.onClick = [this] { if (created) gui->show (plugin); };
            addAndMakeVisible (showButton);
            showMessage (owner.getName() + " opened its window separately.");
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

        auto width = toPhysical (getWidth()), height = toPhysical (getHeight());
        gui->adjust_size (plugin, &width, &height);
        gui->set_size (plugin, width, height);

        if (toLogical (width) != getWidth() || toLogical (height) != getHeight())
        {
            const juce::ScopedValueSetter<bool> guard (inPluginResize, true);
            setSize (toLogical (width), toLogical (height));
        }

        updateEmbeddedBounds();
    }

    void pluginRequestedResize (uint32_t width, uint32_t height)
    {
        if (floating || ! created)
            return;

        const juce::ScopedValueSetter<bool> guard (inPluginResize, true);
        setSize (toLogical (width), toLogical (height));
        embedded.setBounds (getLocalBounds());
        updateEmbeddedBounds();
    }

    void pluginResizeHintsChanged()
    {
        if (created && ! floating)
            setResizable (gui->can_resize (plugin), false);
    }

    void pluginClosedWindow (bool wasDestroyed)
    {
        if (! floating || ! created)
            return;

        if (wasDestroyed)
        {
            gui->destroy (plugin);
            created = false;
            showButton.setVisible (false);
            showMessage ("Close this window and open the synth again to bring its window back.");
        }
    }

private:
    ClapPluginInstance& instance;
    const clap_plugin_t* plugin;
    const clap_plugin_gui_t* gui;

    bool created = false, floating = false, attached = false, inPluginResize = false;
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
            showMessage (instance.getName() + "'s window couldn't be placed here.");
            return;
        }

        gui->show (plugin);
        updateEmbeddedBounds();
    }

    void componentMovedOrResized (bool, bool) override          {}
    void componentPeerChanged() override                        { attach(); }
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

    if (editor == nullptr)
        return false;

    editor->pluginRequestedResize (width, height);
    return true;
}

void ClapPluginInstance::resizeHintsChanged()
{
    if (editor != nullptr)
        editor->pluginResizeHintsChanged();
}

void ClapPluginInstance::guiClosed (bool wasDestroyed)
{
    if (editor != nullptr)
        editor->pluginClosedWindow (wasDestroyed);
}

//==============================================================================
// The host extensions MPE Roll offers. Every callback copes with being called before the
// instance exists (while the plugin initialises) or after it's gone.
namespace hostcallbacks
{
    ClapPluginInstance* ownerOf (const clap_host_t* host)      { return ClapHostContext::of (host).owner.load(); }

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

    const void* getExtension (const clap_host_t*, const char* id)
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
        if (auto* o = hostcallbacks::ownerOf (h))
            o->requestCallback();
        else
            of (h).callbackRequestedEarly = true;
    };
}

ClapHostContext::~ClapHostContext()
{
    timers.clear();
    retiredTimers.clear();

   #if JUCE_LINUX || JUCE_BSD
    for (const auto& entry : fds)
        juce::LinuxEventLoop::unregisterFdCallback (entry.first);
   #endif
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
static short pollEventsFor (clap_posix_fd_flags_t flags)
{
    short events = 0;

    if ((flags & CLAP_POSIX_FD_READ) != 0)   events |= POLLIN;
    if ((flags & CLAP_POSIX_FD_WRITE) != 0)  events |= POLLOUT;
    if ((flags & CLAP_POSIX_FD_ERROR) != 0)  events |= POLLERR;

    return events;
}

bool ClapHostContext::registerFd (int fd, clap_posix_fd_flags_t flags)
{
    if (fds.count (fd) > 0)
        return false;

    fds[fd] = flags;
    juce::LinuxEventLoop::registerFdCallback (fd, [this] (int readyFd) { fdReady (readyFd); }, pollEventsFor (flags));
    return true;
}

bool ClapHostContext::modifyFd (int fd, clap_posix_fd_flags_t flags)
{
    if (fds.count (fd) == 0)
        return false;

    juce::LinuxEventLoop::unregisterFdCallback (fd);
    fds.erase (fd);
    return registerFd (fd, flags);
}

bool ClapHostContext::unregisterFd (int fd)
{
    if (fds.count (fd) == 0)
        return false;

    juce::LinuxEventLoop::unregisterFdCallback (fd);
    fds.erase (fd);
    return true;
}

void ClapHostContext::fdReady (int fd)
{
    const auto found = fds.find (fd);

    if (plugin == nullptr || found == fds.end())
        return;

    if (const auto* support = static_cast<const clap_plugin_posix_fd_support_t*> (plugin->get_extension (plugin, CLAP_EXT_POSIX_FD_SUPPORT)))
        support->on_fd (plugin, fd, found->second);
}
#else
bool ClapHostContext::registerFd (int, clap_posix_fd_flags_t)  { return false; }
bool ClapHostContext::modifyFd (int, clap_posix_fd_flags_t)    { return false; }
bool ClapHostContext::unregisterFd (int)                       { return false; }
void ClapHostContext::fdReady (int)                            {}
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
