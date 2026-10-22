cmake_minimum_required(VERSION 3.22)

project(MPE_ROLL VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Windows: link the C++ runtime into the plugin, so it loads without the Visual C++
# Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MPEROLL_BUILD_PLUGIN "Build the plugin (VST3/AU/Standalone)" ON)
option(MPEROLL_BUILD_TESTS "Build the core unit tests" ON)
option(MPEROLL_BUILD_DEV_TOOLS "Build the developer harness (UI snapshots, offline renders)" OFF)
option(MPEROLL_COPY_PLUGIN_AFTER_BUILD "Copy the built plugin into the system plugin folder" OFF)
option(MPEROLL_HOST_VST2 "Host VST2 synths (uses the clean-room FST headers in third_party/fst)" ON)
option(MPEROLL_HOST_CLAP "Host CLAP synths, playing them with CLAP note expressions" ON)
option(MPEROLL_BUILD_VST2 "On Windows, also build MPE Roll as a VST2 plugin (.dll, uses the FST headers)" ON)
option(MPEROLL_MP3 "Export audio as MP3 too (builds the LAME encoder, LGPL)" ON)

# ---------------------------------------------------------------------------------------------
# JUCE (AGPLv3), with the fixes in cmake/PatchJuce.cmake. Pass
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to use a local checkout; run that script in it
# (cmake -P) to apply them there.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuce.cmake)
FetchContent_MakeAvailable(JUCE)

# CLAP headers (MIT). Pass -DFETCHCONTENT_SOURCE_DIR_CLAP=/path/to/clap to use a local checkout.
# Only fetched when CLAP hosting is built.
set(MPEROLL_CLAP_LIBRARY "")

if(MPEROLL_HOST_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG 1.2.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(clap)
    set(MPEROLL_CLAP_LIBRARY clap)
endif()

# LAME (LGPL), for exporting audio as MP3. Only the encoding library is built, from the release
# tarball (LAME has no CMake build of its own), with a config header of ours. Pass
# -DFETCHCONTENT_SOURCE_DIR_LAME=/path/to/lame-3.100 to use a local copy.
set(MPEROLL_MP3_LIBRARY "")

if(MPEROLL_MP3)
    FetchContent_Declare(lame
        URL https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
        URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR no-cmake-build)
    FetchContent_MakeAvailable(lame)

    file(GLOB MPEROLL_LAME_SOURCES ${lame_SOURCE_DIR}/libmp3lame/*.c)
    add_library(mperoll_lame STATIC ${MPEROLL_LAME_SOURCES})
    target_include_directories(mperoll_lame
        PUBLIC ${lame_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lame ${lame_SOURCE_DIR}/libmp3lame)
    target_compile_definitions(mperoll_lame PRIVATE HAVE_CONFIG_H)
    set_target_properties(mperoll_lame PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER third_party)

    # Third-party code: its warnings aren't ours to fix.
    if(MSVC)
        target_compile_options(mperoll_lame PRIVATE /w)
    else()
        target_compile_options(mperoll_lame PRIVATE -w)
    endif()

    set(MPEROLL_MP3_LIBRARY mperoll_lame)
endif()

# ---------------------------------------------------------------------------------------------
# Core: the MPE model, encoder/decoder, player, MIDI file IO and FL Studio bridge.
# Depends only on juce_core and juce_audio_basics so it can be unit tested without a GUI.
set(MPEROLL_CORE_SOURCES
    source/core/ExpressionCurve.cpp
    source/core/MpeClip.cpp
    source/core/MpeSettings.cpp
    source/core/MpeEncoder.cpp
    source/core/MpeDecoder.cpp
    source/core/ClipBuilder.cpp
    source/core/ClipPlayer.cpp
    source/core/MidiFileIO.cpp
    source/core/FlScriptExporter.cpp
    source/core/ControllerPresets.cpp
    source/core/NoteTools.cpp)

set(MPEROLL_PLUGIN_SOURCES
    source/plugin/AudioExport.cpp
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
    source/plugin/ClipDocument.cpp
    source/plugin/CrashReport.cpp
    source/plugin/HostedSynth.cpp
    source/plugin/PluginLog.cpp
    source/plugin/PluginScanner.cpp
    source/plugin/SynthParameters.cpp
    source/plugin/ui/MpeLookAndFeel.cpp
    source/plugin/ui/NoteGrid.cpp
    source/plugin/ui/ExpressionLane.cpp
    source/plugin/ui/PianoKeys.cpp
    source/plugin/ui/TimeRuler.cpp
    source/plugin/ui/Toolbar.cpp
    source/plugin/ui/Icons.cpp
    source/plugin/ui/WindowPlacement.cpp
    source/plugin/ui/DonatePanel.cpp
    source/plugin/ui/NoteToolDialog.cpp
    source/plugin/ui/SettingsPanel.cpp
    source/plugin/ui/Skins.cpp
    source/plugin/ui/WindowDrawing.cpp)

set(MPEROLL_CLAP_SOURCES "")

if(MPEROLL_HOST_CLAP)
    list(APPEND MPEROLL_CLAP_SOURCES
        source/plugin/clap/ClapNoteTranslator.cpp
        source/plugin/clap/ClapPluginFormat.cpp
        source/plugin/clap/ClapPluginInstance.cpp)

    if(APPLE)
        list(APPEND MPEROLL_CLAP_SOURCES source/plugin/clap/ClapMacView.mm)
    endif()

    list(APPEND MPEROLL_PLUGIN_SOURCES ${MPEROLL_CLAP_SOURCES})
endif()

set(MPEROLL_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_ARA=0)

if(MPEROLL_HOST_VST2)
    list(APPEND MPEROLL_COMMON_DEFINITIONS JUCE_PLUGINHOST_VST=1)
else()
    list(APPEND MPEROLL_COMMON_DEFINITIONS JUCE_PLUGINHOST_VST=0)
endif()

if(MPEROLL_HOST_CLAP)
    list(APPEND MPEROLL_COMMON_DEFINITIONS MPEROLL_HOST_CLAP=1)
else()
    list(APPEND MPEROLL_COMMON_DEFINITIONS MPEROLL_HOST_CLAP=0)
endif()

if(MPEROLL_MP3)
    list(APPEND MPEROLL_COMMON_DEFINITIONS MPEROLL_MP3=1)
else()
    list(APPEND MPEROLL_COMMON_DEFINITIONS MPEROLL_MP3=0)
endif()

if(APPLE)
    set(MPEROLL_FORMATS VST3 AU Standalone)
    list(APPEND MPEROLL_COMMON_DEFINITIONS JUCE_PLUGINHOST_AU=1)
else()
    set(MPEROLL_FORMATS VST3 Standalone)
    list(APPEND MPEROLL_COMMON_DEFINITIONS JUCE_PLUGINHOST_AU=0)
endif()

# VST2 plugins built against the FST headers (JUCE takes the header path only once): MPE Roll itself
# on Windows, where many FL users keep their plugins as .dll files, and the dev tools' test synth.
if(WIN32 AND MPEROLL_BUILD_VST2)
    list(APPEND MPEROLL_FORMATS VST)
endif()

if((WIN32 AND MPEROLL_BUILD_VST2) OR (MPEROLL_BUILD_DEV_TOOLS AND MPEROLL_HOST_VST2))
    juce_set_vst2_sdk_path(${CMAKE_CURRENT_SOURCE_DIR}/third_party/fst)
endif()

# ---------------------------------------------------------------------------------------------
# The plugin.
if(MPEROLL_BUILD_PLUGIN)
juce_add_plugin(MpeRoll
    COMPANY_NAME "Glowing Pancake"
    BUNDLE_ID com.glowingpancake.mperoll
    PLUGIN_MANUFACTURER_CODE Glpk
    PLUGIN_CODE Mpr1
    FORMATS ${MPEROLL_FORMATS}
    PRODUCT_NAME "MPE Roll"
    DESCRIPTION "MPE piano roll that hosts your synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE   # enabled below, once the scanner is in place
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(MpeRoll PRIVATE ${MPEROLL_CORE_SOURCES} ${MPEROLL_PLUGIN_SOURCES})
target_include_directories(MpeRoll PRIVATE source third_party/fst)
target_compile_definitions(MpeRoll PUBLIC ${MPEROLL_COMMON_DEFINITIONS})
target_link_libraries(MpeRoll
    PRIVATE
        ${MPEROLL_CLAP_LIBRARY}
        ${MPEROLL_MP3_LIBRARY}
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Windows: the standalone app comes with its .pdb symbols, so its crash report can name the
# functions involved. The plugins don't need them: inside a DAW, the DAW reports crashes.
if(MSVC)
    target_compile_options(MpeRoll PRIVATE "$<$<CONFIG:Release>:/Zi;/FS>")
    target_link_options(MpeRoll_Standalone PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()

# The scanner MPE Roll looks inside plugin files with, in its own process, so a plugin that crashes or
# hangs while being looked at can't take the DAW with it. It's copied next to each plugin binary.
# Not on macOS, where adding a file would invalidate the signed bundles; there plugins are looked at
# in process, as before.
if(NOT APPLE)
    juce_add_console_app(MpeRollScanner PRODUCT_NAME "MPE Roll Scanner")
    set_target_properties(MpeRollScanner PROPERTIES OUTPUT_NAME "MPE Roll Scanner")
    target_sources(MpeRollScanner PRIVATE source/scanner/ScannerMain.cpp ${MPEROLL_CORE_SOURCES} ${MPEROLL_CLAP_SOURCES})
    target_include_directories(MpeRollScanner PRIVATE source third_party/fst)
    target_compile_definitions(MpeRollScanner PRIVATE ${MPEROLL_COMMON_DEFINITIONS})
    target_link_libraries(MpeRollScanner
        PRIVATE
            ${MPEROLL_CLAP_LIBRARY}
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    foreach(format_target MpeRoll_VST3 MpeRoll_VST MpeRoll_Standalone)
        if(TARGET ${format_target})
            add_dependencies(${format_target} MpeRollScanner)
            add_custom_command(TARGET ${format_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MpeRollScanner>" "$<TARGET_FILE_DIR:${format_target}>"
                VERBATIM)
        endif()
    endforeach()
endif()

# After the scanner copy above, so the installed plugins have it from the first build.
if(MPEROLL_COPY_PLUGIN_AFTER_BUILD)
    juce_enable_copy_plugin_step(MpeRoll)
endif()
endif()

# ---------------------------------------------------------------------------------------------
# Tests.
if(MPEROLL_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MpeRollTests PRODUCT_NAME "MpeRollTests")
    target_sources(MpeRollTests PRIVATE
        ${MPEROLL_CORE_SOURCES}
        tests/TestMain.cpp
        tests/ExpressionCurveTests.cpp
        tests/MpeEncoderTests.cpp
        tests/MpeDecoderTests.cpp
        tests/ClipPlayerTests.cpp
        tests/RoundTripTests.cpp
        tests/FlScriptExporterTests.cpp
        tests/NoteToolsTests.cpp)

    if(MPEROLL_HOST_CLAP)
        target_sources(MpeRollTests PRIVATE
            source/plugin/clap/ClapNoteTranslator.cpp
            tests/ClapNoteTranslatorTests.cpp)
    endif()

    target_include_directories(MpeRollTests PRIVATE source)
    target_compile_definitions(MpeRollTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(MpeRollTests
        PRIVATE
            ${MPEROLL_CLAP_LIBRARY}
            juce::juce_core
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    if(MSVC)
        # Each test class is one function whose tests keep their encoders and translators as locals,
        # and MSVC gives every local its own stack slot. Give the tests the 8 MB main-thread stack
        # that macOS and Linux have, rather than Windows' 1 MB.
        target_link_options(MpeRollTests PRIVATE /STACK:8388608)
    endif()

    add_test(NAME core_unit_tests COMMAND MpeRollTests)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME fl_script_executes
            COMMAND MpeRollTests --write-fl-sample ${CMAKE_CURRENT_BINARY_DIR}/fl_sample.pyscript)
        add_test(NAME fl_script_mock_run
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/fl_mock/run_script.py
                    ${CMAKE_CURRENT_BINARY_DIR}/fl_sample.pyscript)
        set_tests_properties(fl_script_mock_run PROPERTIES DEPENDS fl_script_executes)
    endif()
endif()

# ---------------------------------------------------------------------------------------------
# Developer harness: renders editor snapshots and offline audio through a hosted synth.
if(MPEROLL_BUILD_DEV_TOOLS)
    # A tiny MPE sine synth on JUCE's own MPE implementation, for checking MPE Roll's output.
    # Also built as VST2 (through the FST headers) to test VST2 hosting.
    set(MPEROLL_TEST_SYNTH_FORMATS VST3)

    if(MPEROLL_HOST_VST2)
        list(APPEND MPEROLL_TEST_SYNTH_FORMATS VST)
    endif()

    juce_add_plugin(MpeRollTestSynth
        COMPANY_NAME "Glowing Pancake"
        PLUGIN_MANUFACTURER_CODE Glpk
        PLUGIN_CODE Mpt1
        FORMATS ${MPEROLL_TEST_SYNTH_FORMATS}
        PRODUCT_NAME "MPE Roll Test Synth"
        BUNDLE_ID com.glowingpancake.mperolltestsynth
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Instrument Synth
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)
    target_sources(MpeRollTestSynth PRIVATE tools/TestSynth.cpp)
    target_compile_definitions(MpeRollTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(MpeRollTestSynth
        PRIVATE juce::juce_audio_processors
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # The same sine synth as a CLAP plugin, written straight against the CLAP API. It only takes
    # pitch from CLAP note expressions, so it checks MPE Roll's MPE to note expression path.
    if(MPEROLL_HOST_CLAP)
        add_library(MpeRollClapTestSynth MODULE tools/ClapTestSynth.cpp)
        target_link_libraries(MpeRollClapTestSynth
            PRIVATE clap
            PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        set_target_properties(MpeRollClapTestSynth PROPERTIES
            PREFIX ""
            SUFFIX ".clap"
            OUTPUT_NAME "MPE Roll Test Synth"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MpeRollClapTestSynth_artefacts/$<CONFIG>"
            CXX_VISIBILITY_PRESET hidden)

        if(APPLE)
            set_target_properties(MpeRollClapTestSynth PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
        endif()

        # Plugin files that crash or hang as soon as they're loaded, for the scanner test.
        foreach(kind Crashing Hanging)
            add_library(MpeRoll${kind}TestPlugin MODULE tools/BrokenTestPlugin.cpp)
            target_link_libraries(MpeRoll${kind}TestPlugin
                PRIVATE clap
                PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
            set_target_properties(MpeRoll${kind}TestPlugin PROPERTIES
                PREFIX ""
                SUFFIX ".clap"
                OUTPUT_NAME "MPE Roll ${kind} Test Plugin"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MpeRollClapTestSynth_artefacts/$<CONFIG>"
                CXX_VISIBILITY_PRESET hidden)
        endforeach()

        target_compile_definitions(MpeRollHangingTestPlugin PRIVATE MPEROLL_TEST_PLUGIN_HANGS=1)
    endif()

    juce_add_console_app(MpeRollHarness PRODUCT_NAME "MpeRollHarness")
    target_sources(MpeRollHarness PRIVATE
        ${MPEROLL_CORE_SOURCES}
        ${MPEROLL_PLUGIN_SOURCES}
        tools/Harness.cpp)
    target_include_directories(MpeRollHarness PRIVATE source third_party/fst)
    target_compile_definitions(MpeRollHarness PRIVATE
        ${MPEROLL_COMMON_DEFINITIONS}
        MPEROLL_HARNESS=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="MPE Roll"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_IsMidiEffect=0
        JUCE_USE_MP3AUDIOFORMAT=1)  # to read back the MP3s the export test writes
    target_link_libraries(MpeRollHarness
        PRIVATE
            ${MPEROLL_CLAP_LIBRARY}
            ${MPEROLL_MP3_LIBRARY}
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Symbols, so the crash report test can check that functions are named.
    if(MSVC)
        target_compile_options(MpeRollHarness PRIVATE "$<$<CONFIG:Release>:/Zi;/FS>")
        target_link_options(MpeRollHarness PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
    endif()
endif()
