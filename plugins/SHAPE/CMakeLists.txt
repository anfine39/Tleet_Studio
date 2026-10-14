file(GLOB SHAPE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(SHAPE
    CODE TShp
    DISPLAY_NAME "Passive program EQ"
    VST3_CATEGORIES Fx EQ
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/ShapeModel.cpp
    PRESETS ${SHAPE_PRESETS})

# DSP sources reused by the test runner.
set(SHAPE_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/ShapeModel.cpp PARENT_SCOPE)

# Faceplate, the lit EQ IN lamp and the UI map (made by tools/skinprep/prep_shape.py).
juce_add_binary_data(SHAPE_Skin
    NAMESPACE SHAPESkin
    HEADER_NAME SHAPESkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/SHAPE/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/SHAPE/eqin_lamp.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(SHAPE_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(SHAPE PRIVATE SHAPE_Skin)
