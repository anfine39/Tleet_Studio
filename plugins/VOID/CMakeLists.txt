file(GLOB VOID_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(VOID
    CODE TVoi
    DISPLAY_NAME "Algorithmic reverb"
    VST3_CATEGORIES Fx Reverb
    SOURCES
        PluginProcessor.cpp
        dsp/VoidEngine.cpp
        dsp/Networks.cpp
        # PARENT: append PluginEditor.cpp here
    PRESETS ${VOID_PRESETS})

# DSP sources reused by the test runner.
set(VOID_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/VoidEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Networks.cpp
    PARENT_SCOPE)

# PARENT: VOID_Skin binary-data target goes here
