# Shared TLEET core. It is an INTERFACE library: its sources are compiled into
# each plugin (and the test runner) with that target's JUCE configuration.

add_library(tleet_shared INTERFACE)
add_library(tleet::shared ALIAS tleet_shared)

target_sources(tleet_shared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/state/PluginState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/state/StateHistory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/TleetProcessorBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/TleetLookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Knob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ChoiceSwitch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/VUMeter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SkinVU.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LeverSwitch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LampButtonGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/Toolbar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AboutPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/TleetEditorBase.cpp)

target_include_directories(tleet_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tleet_shared INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp)

target_compile_definitions(tleet_shared INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)
