file(GLOB ECHO_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(ECHO
    CODE TEch
    DISPLAY_NAME "Multi-head tape echo"
    VST3_CATEGORIES Fx Delay Reverb
    SOURCES
        PluginProcessor.cpp
        dsp/EchoEngine.cpp
        # PARENT: append PluginEditor.cpp here
    PRESETS ${ECHO_PRESETS})

# DSP sources reused by the test runner.
set(ECHO_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/EchoEngine.cpp PARENT_SCOPE)

# PARENT: ECHO_Skin binary-data target goes here
