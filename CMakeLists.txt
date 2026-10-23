cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel). Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(HowlGuard VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use -DJUCE_DIR=/path/to/JUCE for a local checkout, otherwise it is downloaded.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(HG_FORMATS Standalone AU VST3)
else()
    set(HG_FORMATS Standalone VST3)
endif()

juce_add_plugin(HowlGuard
    PRODUCT_NAME "HowlGuard"
    COMPANY_NAME "HowlGuard"
    BUNDLE_ID "com.howlguard.HowlGuard"
    PLUGIN_MANUFACTURER_CODE Hwlg
    PLUGIN_CODE Hwgd
    FORMATS ${HG_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "HowlGuard는 마이크/오디오 인터페이스 입력에서 하울링을 감지하고 제거하기 위해 오디오 입력이 필요합니다."
    VST3_CATEGORIES Fx Filter
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(HowlGuard PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ChannelEngine.cpp
    Source/dsp/FeedbackDetector.cpp
    Source/ui/SpectrumView.cpp)

target_include_directories(HowlGuard PRIVATE Source)

target_compile_definitions(HowlGuard PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HowlGuard
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
