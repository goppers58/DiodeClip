cmake_minimum_required(VERSION 3.22)

# project() より前に指定する必要がある設定
if(CMAKE_HOST_APPLE)
    # Apple Silicon と Intel の両方で動くユニバーサルバイナリにする
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()
if(CMAKE_HOST_WIN32)
    # VCランタイムを静的リンクにして、相手のPCにランタイムの導入を要求しない
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(DiodeClipper VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE本体はこのフォルダの JUCE/ に置く(git clone --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE)
add_subdirectory(JUCE)

# AU は macOS のときだけ作る(Logic Pro / GarageBand 用)
set(DIODECLIPPER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DIODECLIPPER_FORMATS AU)
endif()

juce_add_plugin(DiodeClipper
    COMPANY_NAME "58's Studio"
    PLUGIN_MANUFACTURER_CODE Ymnm
    PLUGIN_CODE Dclp
    FORMATS ${DIODECLIPPER_FORMATS}
    PRODUCT_NAME "Diode Clip"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

target_sources(DiodeClipper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ClipShaper.h
    Source/Presets.h)

target_compile_definitions(DiodeClipper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(MSVC)
    target_compile_options(DiodeClipper PRIVATE /utf-8)
endif()

target_link_libraries(DiodeClipper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
