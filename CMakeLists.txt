cmake_minimum_required(VERSION 3.22)

project(PeakTime VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------ JUCE
# Uses a local JUCE checkout if PEAKTIME_JUCE_DIR is given, otherwise downloads JUCE.
set(PEAKTIME_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
if(PEAKTIME_JUCE_DIR)
    add_subdirectory(${PEAKTIME_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(PEAKTIME_DEFAULT_FORMATS VST3 AU Standalone)
else()
    set(PEAKTIME_DEFAULT_FORMATS VST3 Standalone)
endif()
set(PEAKTIME_FORMATS ${PEAKTIME_DEFAULT_FORMATS} CACHE STRING "Plugin formats to build")

# ------------------------------------------------------------------ plugin
juce_add_plugin(PeakTime
    COMPANY_NAME                 "PeakTime Audio"
    COMPANY_WEBSITE              "https://github.com"
    BUNDLE_ID                    com.peaktimeaudio.peaktime
    PRODUCT_NAME                 "PeakTime"
    PLUGIN_MANUFACTURER_CODE     Pktm
    PLUGIN_CODE                  Pkvn
    DESCRIPTION                  "Hear your track on a club system, from your studio"
    FORMATS                      ${PEAKTIME_FORMATS}
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  TRUE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              Fx Analyzer Spatial
    AU_MAIN_TYPE                 kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT   "PeakTime's standalone app can process live input.")

target_sources(PeakTime PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TrackPlayer.cpp
    Source/ui/Components.cpp
    Source/dsp/IRGenerator.cpp
    Source/dsp/VenueEngine.cpp)

target_include_directories(PeakTime PRIVATE Source)

target_compile_definitions(PeakTime PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(PeakTime
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ engine tests (no JUCE)
option(PEAKTIME_BUILD_TESTS "Build the offline DSP engine tests" OFF)
if(PEAKTIME_BUILD_TESTS)
    add_executable(engine_test tests/engine_test.cpp Source/dsp/IRGenerator.cpp Source/dsp/VenueEngine.cpp)
    target_include_directories(engine_test PRIVATE Source)
endif()

# ------------------------------------------------------------------ headless host test (developer only)
option(PEAKTIME_HOST_TEST "Build a headless host that runs the processor + editor" OFF)
if(PEAKTIME_HOST_TEST)
    juce_add_console_app(PeakTimeHostTest PRODUCT_NAME "PeakTimeHostTest")
    target_sources(PeakTimeHostTest PRIVATE
        tests/host_test.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/TrackPlayer.cpp
        Source/ui/Components.cpp Source/dsp/IRGenerator.cpp Source/dsp/VenueEngine.cpp)
    target_include_directories(PeakTimeHostTest PRIVATE Source)
    target_compile_definitions(PeakTimeHostTest PRIVATE
        JucePlugin_Name="PeakTime" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(PeakTimeHostTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
