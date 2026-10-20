# Fixes in JUCE's source, run in its source folder as the patch step when CMake fetches it
# (cmake -P PatchJuce.cmake). Each one only changes text still in its original form, so running
# it again does nothing.
#
# VST3 hosting: JUCE keeps a table of the parameter each MIDI controller is mapped to and sends
# every controller, pitch bend and channel pressure message through it. The table starts out
# as zeros rather than kNoParamId and is only filled in for plugins that have a MIDI mapping, so
# a synth without one had all those messages moving its parameter 0 (often its volume) instead.
# Now they're dropped, which is what any VST3 host does for such a synth.

set(vst3_common "modules/juce_audio_processors_headless/format_types/juce_VST3Common.h")
set(original "channel.resize (Steinberg::Vst::kCountCtrlNumber);")
set(fixed "channel.resize (Steinberg::Vst::kCountCtrlNumber, Steinberg::Vst::kNoParamId);")

file(READ "${vst3_common}" text)
string(FIND "${text}" "${original}" at)

if(NOT at EQUAL -1)
    string(REPLACE "${original}" "${fixed}" text "${text}")
    file(WRITE "${vst3_common}" "${text}")
    message(STATUS "Patched JUCE: VST3 MIDI mapping starts out unmapped")
else()
    string(FIND "${text}" "${fixed}" done)

    if(done EQUAL -1)
        message(WARNING "PatchJuce.cmake: JUCE's VST3 MIDI mapping code has changed; check whether the fix is still needed")
    endif()
endif()
