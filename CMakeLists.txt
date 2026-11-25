cmake_minimum_required(VERSION 3.22)
project(DeluxeBassFuzz VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# GoogleTest for unit tests
include(FetchContent)
# JUCE targets use the DLL MSVC runtime (/MD); make gtest match so they link together.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# JUCE setup - use git submodule
set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE submodule not found at ${JUCE_DIR}. Run: git submodule update --init --recursive")
endif()

add_subdirectory("${JUCE_DIR}" JUCE_BUILD)

# AU only exists on Apple platforms
set(BMD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BMD_FORMATS AU)
endif()

# Plugin configuration
juce_add_plugin(DeluxeBassFuzz
    COMPANY_NAME "Distant Nebula"
    PRODUCT_NAME "Deluxe Bass Fuzz"
    BUNDLE_ID "com.distantnebula.bassfuzzdeluxe"
    VERSION ${PROJECT_VERSION}
    FORMATS ${BMD_FORMATS}
    PLUGIN_MANUFACTURER_CODE DNeb
    PLUGIN_CODE BFDx
    PLUGIN_NAME "Deluxe Bass Fuzz"
    DESCRIPTION "Circuit-modeled 4-transistor bass fuzz with crossover"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    ICON_BIG "${CMAKE_SOURCE_DIR}/Assets/icon/app_icon_512.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/Assets/icon/app_icon_128.png"
)

# Sources include <JuceHeader.h>
juce_generate_juce_header(DeluxeBassFuzz)

# DSP sources shared by the plugin and the unit tests
set(BMD_DSP_SOURCES
    Source/dsp/InputPad.cpp
    Source/dsp/BmpInputStage.cpp
    Source/dsp/DiodeClipper.cpp
    Source/dsp/BmpClippingStage.cpp
    Source/dsp/BmpToneStack.cpp
    Source/dsp/BmpOutputStage.cpp
    Source/dsp/CrossoverFilters.cpp
    Source/dsp/NoiseGate.cpp
    Source/dsp/Blend.cpp
    Source/dsp/Oversampler.cpp
)

# Embedded photorealistic UI artwork assets
juce_add_binary_data(DeluxeBassFuzzAssets
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        Assets/ui/pedal_chassis.png
        Assets/ui/knob_shadow.png
        Assets/ui/knob_skirt_strip.png
        Assets/ui/knob_wall_strip.png
        Assets/ui/knob_cap.png
        Assets/ui/knob_lighting.png
        Assets/ui/toggle_up.png
        Assets/ui/toggle_down.png
        Assets/ui/led_green_off.png
        Assets/ui/led_green_on.png
        Assets/ui/led_red_off.png
        Assets/ui/led_red_on.png
        Assets/ui/footswitch_left_pressed.png
        Assets/ui/footswitch_right_pressed.png
)
set_target_properties(DeluxeBassFuzzAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Source files
target_sources(DeluxeBassFuzz PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/PedalControls.cpp
    ${BMD_DSP_SOURCES}
)

# Include directories
target_include_directories(DeluxeBassFuzz PRIVATE
    Source
    Source/dsp
    Source/ui
)

target_compile_features(DeluxeBassFuzz PRIVATE cxx_std_20)

# Compile definitions
target_compile_definitions(DeluxeBassFuzz PUBLIC
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Link JUCE modules
target_link_libraries(DeluxeBassFuzz
    PRIVATE
        DeluxeBassFuzzAssets
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# CLAP via clap-juce-extensions (same pinned commit as the sibling Subdoom repo)
option(BMD_BUILD_CLAP "Build the CLAP target" ON)
if(BMD_BUILD_CLAP)
    FetchContent_Declare(
        clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8
    )
    FetchContent_MakeAvailable(clap-juce-extensions)
    clap_juce_extensions_plugin(
        TARGET DeluxeBassFuzz
        CLAP_ID "com.distantnebula.bassfuzzdeluxe"
        CLAP_FEATURES audio-effect distortion
    )
endif()

# Unit tests: a JUCE console app (DSP headers use JUCE) with GoogleTest's main
juce_add_console_app(DeluxeBassFuzzTests
    PRODUCT_NAME "DeluxeBassFuzzTests"
)
juce_generate_juce_header(DeluxeBassFuzzTests)

target_sources(DeluxeBassFuzzTests PRIVATE
    tests/ToneStackTest.cpp
    tests/DiodeClipperTest.cpp
    tests/StageTest.cpp
    ${BMD_DSP_SOURCES}
)

target_include_directories(DeluxeBassFuzzTests PRIVATE
    Source
    Source/dsp
)

target_compile_definitions(DeluxeBassFuzzTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(DeluxeBassFuzzTests
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
        gtest_main
        gtest
    PUBLIC
        juce::juce_recommended_config_flags
)

target_compile_features(DeluxeBassFuzzTests PRIVATE cxx_std_20)

add_test(NAME DeluxeBassFuzzToneStackTest COMMAND DeluxeBassFuzzTests --gtest_filter=ToneStackTest.*)
add_test(NAME DeluxeBassFuzzDiodeClipperTest COMMAND DeluxeBassFuzzTests --gtest_filter=DiodeClipperTest.*)
add_test(NAME DeluxeBassFuzzOversamplerTest COMMAND DeluxeBassFuzzTests --gtest_filter=OversamplerTest.*)
add_test(NAME DeluxeBassFuzzStageTest COMMAND DeluxeBassFuzzTests --gtest_filter=StageTest.*)
# ---------------------------------------------------------------------------
# SPICE-vs-DSP validation
#   DeluxeBassFuzzRender renders tests/ReferenceData/cases.json through the DSP
#   blocks; tools/compare_spice.py compares against the committed ngspice data in
#   tests/ReferenceData/spice (regenerate with tools/run_spice.py).
#   Run with: ctest -C Release -L validation   (or .\build.ps1 -Validate)
juce_add_console_app(DeluxeBassFuzzRender
    PRODUCT_NAME "DeluxeBassFuzzRender"
)
juce_generate_juce_header(DeluxeBassFuzzRender)

target_sources(DeluxeBassFuzzRender PRIVATE
    tools/render/RenderMain.cpp
    ${BMD_DSP_SOURCES}
)

target_include_directories(DeluxeBassFuzzRender PRIVATE
    Source
    Source/dsp
)

target_compile_definitions(DeluxeBassFuzzRender PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(DeluxeBassFuzzRender
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
)

target_compile_features(DeluxeBassFuzzRender PRIVATE cxx_std_20)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME Validation.Render COMMAND DeluxeBassFuzzRender --root "${CMAKE_SOURCE_DIR}")
    set_tests_properties(Validation.Render PROPERTIES FIXTURES_SETUP bmd_dsp_render LABELS validation)

    foreach(check tone input_ac clip_ac output_ac core_ac clip_tran core_tran)
        add_test(NAME Validation.${check}
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/compare_spice.py" --check ${check})
        set_tests_properties(Validation.${check} PROPERTIES FIXTURES_REQUIRED bmd_dsp_render LABELS validation)
    endforeach()
endif()