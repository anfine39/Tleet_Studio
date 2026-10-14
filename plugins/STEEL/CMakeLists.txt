file(GLOB STEEL_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(STEEL
    CODE TStl
    DISPLAY_NAME "True-peak mastering limiter"
    VST3_CATEGORIES Fx Dynamics Mastering
    SOURCES
        PluginProcessor.cpp
        dsp/SteelEngine.cpp
        # PARENT: append PluginEditor.cpp here
    PRESETS ${STEEL_PRESETS})

# DSP sources reused by the test runner.
set(STEEL_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/SteelEngine.cpp
    PARENT_SCOPE)

# PARENT: STEEL_Skin binary-data target goes here
