cmake_minimum_required(VERSION 3.22)

project(MPE_ROLL VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(MPEROLL_BUILD_PLUGIN "Build the plugin (VST3/AU/Standalone)" ON)
option(MPEROLL_BUILD_TESTS "Build the core unit tests" ON)
option(MPEROLL_BUILD_DEV_TOOLS "Build the developer harness (UI snapshots, offline renders)" OFF)
option(MPEROLL_COPY_PLUGIN_AFTER_BUILD "Copy the built plugin into the system plugin folder" OFF)

# ---------------------------------------------------------------------------------------------
# JUCE (AGPLv3). Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to use a local checkout.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(JUCE)

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
    source/core/FlScriptExporter.cpp)

set(MPEROLL_PLUGIN_SOURCES
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.cpp
    source/plugin/ClipDocument.cpp
    source/plugin/HostedSynth.cpp
    source/plugin/ui/MpeLookAndFeel.cpp
    source/plugin/ui/NoteGrid.cpp
    source/plugin/ui/ExpressionLane.cpp
    source/plugin/ui/PianoKeys.cpp
    source/plugin/ui/TimeRuler.cpp
    source/plugin/ui/Toolbar.cpp
    source/plugin/ui/SettingsPanel.cpp)

set(MPEROLL_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_ARA=0)

if(APPLE)
    set(MPEROLL_FORMATS VST3 AU Standalone)
    list(APPEND MPEROLL_COMMON_DEFINITIONS JUCE_PLUGINHOST_AU=1)
else()
    set(MPEROLL_FORMATS VST3 Standalone)
    list(APPEND MPEROLL_COMMON_DEFINITIONS JUCE_PLUGINHOST_AU=0)
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
    COPY_PLUGIN_AFTER_BUILD ${MPEROLL_COPY_PLUGIN_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(MpeRoll PRIVATE ${MPEROLL_CORE_SOURCES} ${MPEROLL_PLUGIN_SOURCES})
target_include_directories(MpeRoll PRIVATE source)
target_compile_definitions(MpeRoll PUBLIC ${MPEROLL_COMMON_DEFINITIONS})
target_link_libraries(MpeRoll
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
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
        tests/FlScriptExporterTests.cpp)
    target_include_directories(MpeRollTests PRIVATE source)
    target_compile_definitions(MpeRollTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(MpeRollTests
        PRIVATE
            juce::juce_core
            juce::juce_audio_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

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
    juce_add_plugin(MpeRollTestSynth
        COMPANY_NAME "Glowing Pancake"
        PLUGIN_MANUFACTURER_CODE Glpk
        PLUGIN_CODE Mpt1
        FORMATS VST3
        PRODUCT_NAME "MPE Roll Test Synth"
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

    juce_add_console_app(MpeRollHarness PRODUCT_NAME "MpeRollHarness")
    target_sources(MpeRollHarness PRIVATE
        ${MPEROLL_CORE_SOURCES}
        ${MPEROLL_PLUGIN_SOURCES}
        tools/Harness.cpp)
    target_include_directories(MpeRollHarness PRIVATE source)
    target_compile_definitions(MpeRollHarness PRIVATE
        ${MPEROLL_COMMON_DEFINITIONS}
        MPEROLL_HARNESS=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="MPE Roll"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
        JucePlugin_IsMidiEffect=0)
    target_link_libraries(MpeRollHarness
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
