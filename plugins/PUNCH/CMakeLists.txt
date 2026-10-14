file(GLOB PUNCH_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(PUNCH
    CODE TPun
    DISPLAY_NAME "VCA compressor"
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/PunchModel.cpp
    PRESETS ${PUNCH_PRESETS})

# DSP sources reused by the test runner.
set(PUNCH_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/PunchModel.cpp PARENT_SCOPE)

# Skin assets produced by tools/skinprep/prep_punch.py from assets/reference/PUNCH.png
juce_add_binary_data(PUNCH_Skin
    NAMESPACE PUNCHSkin
    HEADER_NAME PUNCHSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/PUNCH/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/PUNCH/lamp_lit.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(PUNCH_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(PUNCH PRIVATE PUNCH_Skin)
