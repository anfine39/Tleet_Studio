file(GLOB VEIL_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

# The "Sidechain" input bus is declared by VeilProcessor (BusesProperties);
# SIDECHAIN below only documents it (tleet_add_plugin needs no extra setup).
tleet_add_plugin(VEIL
    CODE TVel
    DISPLAY_NAME "Variable-mu compressor"
    SIDECHAIN
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/VeilModel.cpp
    PRESETS ${VEIL_PRESETS})

# DSP sources reused by the test runner.
set(VEIL_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/VeilModel.cpp PARENT_SCOPE)

# Faceplate, lit lamps and the UI map (made by tools/skinprep/prep_veil.py).
juce_add_binary_data(VEIL_Skin
    NAMESPACE VEILSkin
    HEADER_NAME VEILSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/character_clean.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/character_veil.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/character_thick.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/scExternal_int.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/scExternal_ext.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/meterMode_gr.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/meterMode_vu.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/meterMode_inout.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/link_dualmono.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VEIL/link_lamp.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(VEIL_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(VEIL PRIVATE VEIL_Skin)
