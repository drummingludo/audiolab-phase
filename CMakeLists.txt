cmake_minimum_required(VERSION 3.22)

# Mac : un seul binaire pour Apple Silicon et Intel
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(AudioLabPhase VERSION 2.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8 (licence AGPLv3 : le code de ce plugin est public)
set(ALP_JUCE_PATH "" CACHE PATH "Dossier JUCE local (sinon téléchargé)")
if(ALP_JUCE_PATH)
    add_subdirectory(${ALP_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ALP_FORMATS VST3)
if(APPLE)
    list(APPEND ALP_FORMATS AU)
endif()
# AAX (Pro Tools) : SDK Avid fourni avec JUCE (licence GPL v3, compatible AGPLv3).
# Non signé, il ne se charge que dans Pro Tools Developer ; signature PACE à faire avant diffusion.
option(ALP_AAX "Fabriquer le format AAX (Mac et Windows)" ON)
if(ALP_AAX AND (APPLE OR WIN32))
    list(APPEND ALP_FORMATS AAX)
endif()
if(ALP_STANDALONE)
    list(APPEND ALP_FORMATS Standalone)
endif()

juce_add_plugin(AudioLabPhase
    PRODUCT_NAME "Audio Lab Phase"
    PLUGIN_NAME "Lab Phase"
    DESCRIPTION "Polarity and time offset from Audio Lab (Chimiste du son)"
    COMPANY_NAME "Transversal Studio"
    COMPANY_WEBSITE "https://transversalstudio.fr"
    COMPANY_EMAIL "ludo@transversalstudio.fr"
    BUNDLE_ID "fr.transversalstudio.audiolab.phase"
    PLUGIN_MANUFACTURER_CODE Trsv
    PLUGIN_CODE AlPh
    FORMATS ${ALP_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Delay
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(ALPData SOURCES
    Resources/Poppins-Regular.ttf
    Resources/Poppins-Medium.ttf
    Resources/Poppins-SemiBold.ttf
    Resources/Poppins-Bold.ttf)

target_sources(AudioLabPhase PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Display.cpp)

target_include_directories(AudioLabPhase PRIVATE Source)
if(MSVC)
    target_compile_options(AudioLabPhase PRIVATE /utf-8)
else()
    # même arrondi que le banc de test (pas de fusion multiplication-addition) sur Apple Silicon comme sur Intel
    set_source_files_properties(Source/PluginProcessor.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

target_compile_definitions(AudioLabPhase PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(AudioLabPhase
    PRIVATE
        ALPData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Outils de test (captures de l'interface, tests du processeur hors DAW) — non livrés
option(ALP_TESTS "Construire les outils de test" OFF)
if(ALP_TESTS)
    foreach(T ALPRenderUI ALPProcessorTest)
        juce_add_console_app(${T} PRODUCT_NAME "${T}")
        target_include_directories(${T} PRIVATE Source)
        target_compile_definitions(${T} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${T} PRIVATE AudioLabPhase ALPData juce::juce_audio_utils juce::juce_dsp juce::juce_gui_basics juce::juce_recommended_config_flags)
    endforeach()
    target_sources(ALPRenderUI PRIVATE tests/render_ui.cpp)
    target_sources(ALPProcessorTest PRIVATE tests/processor_test.cpp)
endif()
