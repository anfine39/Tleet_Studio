file(GLOB TAPE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(TAPE
    CODE TTap
    DISPLAY_NAME "Multitrack tape machine"
    VST3_CATEGORIES Fx Distortion
    SOURCES
        PluginProcessor.cpp
        dsp/TapeEngine.cpp
        # PARENT: append PluginEditor.cpp here
    PRESETS ${TAPE_PRESETS})

# DSP sources reused by the test runner.
set(TAPE_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/TapeEngine.cpp
    PARENT_SCOPE)

# PARENT: TAPE_Skin binary-data target goes here
