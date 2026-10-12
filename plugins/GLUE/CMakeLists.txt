file(GLOB GLUE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

# The "Sidechain" input bus is declared by GlueProcessor (BusesProperties);
# SIDECHAIN below only documents it (tleet_add_plugin needs no extra setup).
tleet_add_plugin(GLUE
    CODE TGlu
    DISPLAY_NAME "Bus compressor"
    SIDECHAIN
    SOURCES
        PluginProcessor.cpp
        dsp/GlueModel.cpp
    PRESETS ${GLUE_PRESETS})

# DSP sources reused by the test runner.
set(GLUE_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/GlueModel.cpp PARENT_SCOPE)

# The skinned editor (PluginEditor.cpp) and the GLUE_Skin binary-data target
# are appended here when the skin arrives; until then createEditor() returns
# a juce::GenericAudioProcessorEditor.
