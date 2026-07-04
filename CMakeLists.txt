cmake_minimum_required(VERSION 3.22)

project(FableStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# JUCE can be provided locally (offline builds / CI cache) via -DFABLE_JUCE_SOURCE_DIR=<path>
if(DEFINED FABLE_JUCE_SOURCE_DIR)
    add_subdirectory(${FABLE_JUCE_SOURCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.14.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

set(FABLE_COMMON_SOURCES
    Source/Model/Project.cpp
    Source/Model/Serialization.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/Sequencer.cpp
    Source/Engine/ChannelNode.cpp
    Source/Engine/MixerBus.cpp
    Source/Engine/BuiltinSynth.cpp
    Source/Engine/SamplerSound.cpp
    Source/Engine/BuiltinEffects.cpp
    Source/Engine/DrumSynthesis.cpp
    Source/Plugins/PluginManager.cpp
)

set(FABLE_UI_SOURCES
    Source/Main.cpp
    Source/UI/FableLookAndFeel.cpp
    Source/UI/MainComponent.cpp
    Source/UI/TransportBar.cpp
    Source/UI/FloatingPanel.cpp
    Source/UI/ChannelRackPanel.cpp
    Source/UI/PianoRollPanel.cpp
    Source/UI/PlaylistPanel.cpp
    Source/UI/MixerPanel.cpp
    Source/UI/BrowserPanel.cpp
    Source/UI/SettingsDialog.cpp
    Source/UI/PluginWindow.cpp
)

set(FABLE_JUCE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

set(FABLE_JUCE_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FableStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FableStudio,JUCE_VERSION>"
)

option(FABLE_BUILD_GUI "Build the FableStudio GUI app (OFF builds only the headless FableTool)" ON)

# ---------------------------------------------------------------- main app
if(FABLE_BUILD_GUI)
juce_add_gui_app(FableStudio
    PRODUCT_NAME "FableStudio"
    COMPANY_NAME "FableStudio"
)

target_sources(FableStudio PRIVATE ${FABLE_COMMON_SOURCES} ${FABLE_UI_SOURCES})
target_include_directories(FableStudio PRIVATE Source)
target_compile_definitions(FableStudio PRIVATE ${FABLE_JUCE_DEFINES})
target_link_libraries(FableStudio PRIVATE ${FABLE_JUCE_MODULES}
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
endif()

# ------------------------------------------------- console tool (tests + offline render)
juce_add_console_app(FableTool PRODUCT_NAME "FableTool")

target_sources(FableTool PRIVATE
    ${FABLE_COMMON_SOURCES}
    Tests/ToolMain.cpp
    Tests/ModelTests.cpp
    Tests/SequencerTests.cpp
)
target_include_directories(FableTool PRIVATE Source)
target_compile_definitions(FableTool PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)
target_link_libraries(FableTool PRIVATE ${FABLE_JUCE_MODULES}
    juce::juce_recommended_config_flags)
