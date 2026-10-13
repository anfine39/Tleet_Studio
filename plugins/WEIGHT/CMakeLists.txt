file(GLOB WEIGHT_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

# The "Sidechain" input bus is declared by WeightProcessor (BusesProperties);
# SIDECHAIN below only documents it (tleet_add_plugin needs no extra setup).
tleet_add_plugin(WEIGHT
    CODE TWgt
    DISPLAY_NAME "Stereo bus compressor"
    SIDECHAIN
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/WeightModel.cpp
    PRESETS ${WEIGHT_PRESETS})

# DSP sources reused by the test runner.
set(WEIGHT_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/WeightModel.cpp PARENT_SCOPE)

# Faceplate, lit lamps and the UI map (made by tools/skinprep/prep_weight.py).
juce_add_binary_data(WEIGHT_Skin
    NAMESPACE WEIGHTSkin
    HEADER_NAME WEIGHTSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/WEIGHT/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/WEIGHT/lit_thrust.png
        ${CMAKE_SOURCE_DIR}/assets/processed/WEIGHT/lit_ff.png
        ${CMAKE_SOURCE_DIR}/assets/processed/WEIGHT/lit_tone.png
        ${CMAKE_SOURCE_DIR}/assets/processed/WEIGHT/lit_grmode.png
        ${CMAKE_SOURCE_DIR}/assets/processed/WEIGHT/lamp_ext.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(WEIGHT_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(WEIGHT PRIVATE WEIGHT_Skin)
