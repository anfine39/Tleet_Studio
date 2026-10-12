file(GLOB ASH_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(ASH
    CODE TAsh
    DISPLAY_NAME "Optical levelling amplifier"
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/AshModel.cpp
    PRESETS ${ASH_PRESETS})

# Skin assets produced by tools/skinprep/prep_ash.py from assets/reference/ASH.png
juce_add_binary_data(ASH_Skin
    NAMESPACE ASHSkin
    HEADER_NAME ASHSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/ASH/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ASH/lever.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(ASH_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(ASH PRIVATE ASH_Skin)

# DSP sources reused by the test runner.
set(ASH_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/AshModel.cpp PARENT_SCOPE)
