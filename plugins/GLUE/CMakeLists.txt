file(GLOB GLUE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

# The "Sidechain" input bus is declared by GlueProcessor (BusesProperties);
# SIDECHAIN below only documents it (tleet_add_plugin needs no extra setup).
tleet_add_plugin(GLUE
    CODE TGlu
    DISPLAY_NAME "Bus compressor"
    SIDECHAIN
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/GlueModel.cpp
    PRESETS ${GLUE_PRESETS})

# DSP sources reused by the test runner.
set(GLUE_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/GlueModel.cpp PARENT_SCOPE)

# Skin assets produced by tools/skinprep/prep_glue.py from assets/reference/GLUE.png
juce_add_binary_data(GLUE_Skin
    NAMESPACE GLUESkin
    HEADER_NAME GLUESkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/GLUE/plate.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(GLUE_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(GLUE PRIVATE GLUE_Skin)
