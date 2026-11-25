cmake_minimum_required(VERSION 3.22)
project(FilterYF19 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so the .vst3 / .exe run without
# the Visual C++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# To build against a JUCE checkout you already have:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FilterYF19
    COMPANY_NAME "Decu"
    PLUGIN_MANUFACTURER_CODE Decu
    PLUGIN_CODE Yf19
    PRODUCT_NAME "Filter YF-19"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Filter
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(YF19_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters.h
    Source/MothFilter.h
    Source/UI/Theme.cpp
    Source/UI/Theme.h
    Source/UI/Widgets.cpp
    Source/UI/Widgets.h)
target_sources(FilterYF19 PRIVATE ${YF19_SOURCES})

# Embedded fonts (SIL OFL 1.1 - see Resources/Fonts)
juce_add_binary_data(YF19Fonts SOURCES
    Resources/Fonts/DecuDisplay-Bold.ttf
    Resources/Fonts/DecuDisplay-Black.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)

target_compile_definitions(FilterYF19 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Linux: ALSA (default) plus JACK for the Standalone's audio device list.
option(YF19_HEADLESS_TEST_BUILD "Build without ALSA/JACK and optional X11 extensions (CI/test machines only)" OFF)
set(YF19_HEADLESS_DEFS JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(YF19_HEADLESS_TEST_BUILD)
        target_compile_definitions(FilterYF19 PUBLIC ${YF19_HEADLESS_DEFS})
    else()
        target_compile_definitions(FilterYF19 PUBLIC JUCE_ALSA=1 JUCE_JACK=1)
    endif()
endif()

target_link_libraries(FilterYF19
    PRIVATE juce::juce_audio_utils
            juce::juce_dsp
            YF19Fonts
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Optional tests:
#   cmake -B build -DYF19_BUILD_TESTS=ON && cmake --build build --target YF19Tests YF19HostTest
option(YF19_BUILD_TESTS "Build the test programs" OFF)
if(YF19_BUILD_TESTS)
    juce_add_console_app(YF19Tests PRODUCT_NAME "YF19Tests")
    target_sources(YF19Tests PRIVATE Tests/plugin_tests.cpp ${YF19_SOURCES})
    target_include_directories(YF19Tests PRIVATE Source)
    target_compile_definitions(YF19Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Filter YF-19")
    if(YF19_HEADLESS_TEST_BUILD)
        target_compile_definitions(YF19Tests PRIVATE ${YF19_HEADLESS_DEFS})
    endif()
    target_link_libraries(YF19Tests PRIVATE juce::juce_audio_utils juce::juce_dsp YF19Fonts
                                            juce::juce_recommended_config_flags)

    juce_add_console_app(YF19HostTest PRODUCT_NAME "YF19HostTest")
    target_sources(YF19HostTest PRIVATE Tests/vst3_host_test.cpp)
    target_compile_definitions(YF19HostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(YF19_HEADLESS_TEST_BUILD)
        target_compile_definitions(YF19HostTest PRIVATE ${YF19_HEADLESS_DEFS})
    endif()
    target_link_libraries(YF19HostTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)

    add_executable(YF19DspTest Tests/dsp_test.cpp)
    target_include_directories(YF19DspTest PRIVATE Source)
endif()
