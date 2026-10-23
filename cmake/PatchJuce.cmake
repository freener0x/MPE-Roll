# Fixes and additions in JUCE's source, run in its source folder as the patch step when CMake
# fetches it (cmake -P PatchJuce.cmake). Each one only changes text still in its original form, so
# running it again does nothing.

# Replaces `original` with `fixed` in `file`, once (`fixed` may contain `original`, so it's looked
# for first). Warns if neither is there: JUCE has changed.
function(mperoll_patch file original fixed what)
    file(READ "${file}" text)
    string(FIND "${text}" "${fixed}" done)

    if(NOT done EQUAL -1)
        return()
    endif()

    string(FIND "${text}" "${original}" at)

    if(at EQUAL -1)
        message(WARNING "PatchJuce.cmake: JUCE's code has changed where it is patched (${what}); check whether the patch is still needed")
        return()
    endif()

    string(REPLACE "${original}" "${fixed}" text "${text}")
    file(WRITE "${file}" "${text}")
    message(STATUS "Patched JUCE: ${what}")
endfunction()

set(processors "modules/juce_audio_processors_headless")
set(vst3_common "${processors}/format_types/juce_VST3Common.h")
set(vst3_impl "${processors}/format_types/juce_VST3PluginFormatImpl.h")
set(extensions "${processors}/utilities/juce_AudioPluginExtensions.h")
set(vst2_gui "modules/juce_audio_processors/format_types/juce_VSTPluginFormat.cpp")

# VST3 hosting: JUCE keeps a table of the parameter each MIDI controller is mapped to and sends
# every controller, pitch bend and channel pressure message through it. The table starts out
# as zeros rather than kNoParamId and is only filled in for plugins that have a MIDI mapping, so
# a synth without one had all those messages moving its parameter 0 (often its volume) instead.
# Now they're dropped, which is what any VST3 host does for such a synth.
mperoll_patch("${vst3_common}"
    "channel.resize (Steinberg::Vst::kCountCtrlNumber);"
    "channel.resize (Steinberg::Vst::kCountCtrlNumber, Steinberg::Vst::kNoParamId);"
    "VST3 MIDI mapping starts out unmapped")

# VST3 hosting: a hosted VST3's edit controller, next to the component JUCE already hands out, so
# MPE Roll can ask the synth which MIDI channels it maps pitch bend on and which note expressions
# it takes (IMidiMapping, INoteExpressionController, INoteExpressionPhysicalUIMapping).
mperoll_patch("${extensions}"
    [=[    class IComponent;
} // namespace Steinberg::Vst]=]
    [=[    class IComponent;
    class IEditController;
} // namespace Steinberg::Vst]=]
    "IEditController declared for the VST3 client extensions")

mperoll_patch("${extensions}"
    [=[        virtual bool setPreset (const MemoryBlock&) = 0;
]=]
    [=[        virtual bool setPreset (const MemoryBlock&) = 0;
        virtual Steinberg::Vst::IEditController* getIEditControllerPtr() const noexcept { return nullptr; }
]=]
    "VST3 client extensions hand out the edit controller")

mperoll_patch("${vst3_impl}"
    [=[    Steinberg::Vst::IComponent* getIComponentPtr() const noexcept override
    {
        return holder->component.get();
    }
]=]
    [=[    Steinberg::Vst::IComponent* getIComponentPtr() const noexcept override
    {
        return holder->component.get();
    }

    Steinberg::Vst::IEditController* getIEditControllerPtr() const noexcept override
    {
        return editController.get();
    }
]=]
    "hosted VST3s hand out their edit controller")

# Hosting a synth that crashed (MPE Roll catches a crash in a copy of the synth, on Windows, and
# switches that copy off): JUCE's host would still call it from the message thread on its own,
# from the timers that pass parameter changes to a VST3's edit controller and idle a VST2.
# stopCallingPlugin() stops them; nothing calls the plugin after that. It may be called from the
# thread that caught the crash, before those timers next fire.
mperoll_patch("${extensions}"
    [=[        virtual Steinberg::Vst::IEditController* getIEditControllerPtr() const noexcept { return nullptr; }
]=]
    [=[        virtual Steinberg::Vst::IEditController* getIEditControllerPtr() const noexcept { return nullptr; }
        virtual void stopCallingPlugin() noexcept {}
]=]
    "VST3 client extensions can stop calling a plugin that crashed")

mperoll_patch("${extensions}"
    [=[        virtual AEffect* getAEffectPtr() const noexcept = 0;
]=]
    [=[        virtual AEffect* getAEffectPtr() const noexcept = 0;
        virtual void stopCallingPlugin() noexcept {}
]=]
    "VST2 client extensions can stop calling a plugin that crashed")

mperoll_patch("${vst3_impl}"
    [=[    void flush()
    {
        cache.ifSet ([this] (Steinberg::int32 index, float value)]=]
    [=[    void stop()
    {
        stopped = true;
        stopTimer();
    }

    void flush()
    {
        if (stopped)
            return;

        cache.ifSet ([this] (Steinberg::int32 index, float value)]=]
    "VST3 parameter dispatcher can be stopped")

mperoll_patch("${vst3_impl}"
    [=[    CachedParamValues cache;
    Vst::IEditController* controller = nullptr;
};]=]
    [=[    CachedParamValues cache;
    Vst::IEditController* controller = nullptr;
    std::atomic<bool> stopped { false };
};]=]
    "VST3 parameter dispatcher knows it's stopped")

mperoll_patch("${vst3_impl}"
    [=[    Steinberg::Vst::IEditController* getIEditControllerPtr() const noexcept override
    {
        return editController.get();
    }
]=]
    [=[    Steinberg::Vst::IEditController* getIEditControllerPtr() const noexcept override
    {
        return editController.get();
    }

    void stopCallingPlugin() noexcept override
    {
        parameterDispatcher.stop();
    }
]=]
    "hosted VST3s stop passing parameter changes to a plugin that crashed")

mperoll_patch("${vst2_gui}"
    [=[    void needIdle() override
    {
        startTimer (50);
    }
]=]
    [=[    void needIdle() override
    {
        if (! callsStopped)
            startTimer (50);
    }

    void stopCallingPlugin() noexcept override
    {
        callsStopped = true;
        stopTimer();
        cancelPendingUpdate();
    }
]=]
    "hosted VST2s stop idling a plugin that crashed")

mperoll_patch("${vst2_gui}"
    [=[private:
    void timerCallback() override
    {
        if (dispatch (Vst2::effIdle, 0, 0, nullptr, 0) == 0)
            stopTimer();
    }]=]
    [=[private:
    std::atomic<bool> callsStopped { false };   // it crashed: never called again

    void timerCallback() override
    {
        if (callsStopped || dispatch (Vst2::effIdle, 0, 0, nullptr, 0) == 0)
            stopTimer();
    }]=]
    "hosted VST2s know they've stopped calling a plugin")

# VST3 hosting: JUCE builds a hosted VST3's event list itself, from MIDI alone, so a host has no
# way to give a synth VST3 note IDs or note expressions. MPE Roll hands them over inside the MIDI
# buffer as SysEx in a layout of its own (source/plugin/Vst3NoteEvents.h), and the patched code
# turns those back into note on/off events with IDs and note expression values. Other SysEx is
# passed on as before.
# The decoder goes in front of MidiEventList; a decoder from an earlier patch is replaced, so a
# checkout patched before keeps up with changes to it.
file(READ "${CMAKE_CURRENT_LIST_DIR}/juce/MpeRollVst3Events.inc" decoder)
file(READ "${vst3_common}" text)
string(FIND "${text}" "// MPE Roll VST3 events: begin" begin)
string(FIND "${text}" "// MPE Roll VST3 events: end" end)

if(NOT begin EQUAL -1 AND NOT end EQUAL -1)
    string(LENGTH "// MPE Roll VST3 events: end\n" marker)
    math(EXPR after "${end} + ${marker}")
    string(SUBSTRING "${text}" 0 ${begin} head)
    string(SUBSTRING "${text}" ${after} -1 tail)

    if(NOT "${head}${decoder}${tail}" STREQUAL "${text}")
        file(WRITE "${vst3_common}" "${head}${decoder}${tail}")
        message(STATUS "Patched JUCE: VST3 note events from MPE Roll, decoder updated")
    endif()
else()
    mperoll_patch("${vst3_common}"
        [=[//==============================================================================
class MidiEventList  : public Steinberg::Vst::IEventList]=]
        "${decoder}//==============================================================================
class MidiEventList  : public Steinberg::Vst::IEventList"
        "VST3 note events from MPE Roll: the decoder")
endif()

mperoll_patch("${vst3_common}"
    [=[        const auto msg = metadata.getMessage();

        if (sendMappedParameter (msg, midiMapping, std::forward<Callback> (callback)))
            return;]=]
    [=[        if (midiMapping != nullptr)     // host to plugin
        {
            if (auto noteEvent = MpeRollVst3Events::decode (metadata.data, metadata.numBytes))
            {
                noteEvent->busIndex = 0;
                noteEvent->sampleOffset = metadata.samplePosition;
                result.addEvent (*noteEvent);
                return;
            }
        }

        const auto msg = metadata.getMessage();

        if (sendMappedParameter (msg, midiMapping, std::forward<Callback> (callback)))
            return;]=]
    "VST3 note events from MPE Roll: passed to the plugin")
