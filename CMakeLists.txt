cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) y runtime estático en Windows (no requiere VC++ Redistributable)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Driftbox VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DRIFTBOX_BUILD_TESTS "Compilar las pruebas automáticas" ON)

# --- JUCE: se descarga sola. Para usar una copia local: -DFETCHCONTENT_SOURCE_DIR_JUCE=/ruta/a/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

option(DRIFTBOX_STANDALONE_ONLY "Compilar solo la app Standalone" OFF)

set(DRIFTBOX_FORMATS VST3 Standalone)
if(DRIFTBOX_STANDALONE_ONLY)
    set(DRIFTBOX_FORMATS Standalone)
elseif(APPLE)
    list(APPEND DRIFTBOX_FORMATS AU)
endif()

juce_add_plugin(Driftbox
    COMPANY_NAME                "Driftbox Audio"
    COMPANY_WEBSITE             "https://example.com"
    BUNDLE_ID                   com.driftboxaudio.driftbox
    PLUGIN_MANUFACTURER_CODE    Drbx
    PLUGIN_CODE                 Drfx
    PRODUCT_NAME                "Driftbox"
    VERSION                     ${PROJECT_VERSION}
    FORMATS                     ${DRIFTBOX_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    HARDENED_RUNTIME_ENABLED    TRUE
    HARDENED_RUNTIME_OPTIONS    com.apple.security.device.audio-input
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "Driftbox procesa la entrada de audio (microfono o interfaz) en tiempo real."
    VST3_CATEGORIES             Fx Modulation
    AU_MAIN_TYPE                kAudioUnitType_Effect)

set(DRIFTBOX_SOURCES
    Source/Params.cpp
    Source/random/SmartRandomizer.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(Driftbox PRIVATE ${DRIFTBOX_SOURCES})
target_include_directories(Driftbox PRIVATE Source)

target_compile_definitions(Driftbox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Driftbox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Pruebas: randomizer (rangos, reglas, novedad, energía) + render offline del DSP
if(DRIFTBOX_BUILD_TESTS)
    juce_add_console_app(DriftboxTests PRODUCT_NAME "DriftboxTests")
    target_sources(DriftboxTests PRIVATE Tests/DriftboxTests.cpp ${DRIFTBOX_SOURCES})
    target_include_directories(DriftboxTests PRIVATE Source)
    target_compile_definitions(DriftboxTests PRIVATE
        JucePlugin_Name="Driftbox"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(DriftboxTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME DriftboxTests COMMAND DriftboxTests)
endif()
