file(GLOB DOUBLE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(DOUBLE
    CODE TDbl
    DISPLAY_NAME "Stereo doubler"
    VST3_CATEGORIES Fx Modulation
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        VoiceDisplay.cpp
        FilterDisplay.cpp
        dsp/DoubleEngine.cpp
    PRESETS ${DOUBLE_PRESETS})

# Skin assets produced by tools/skinprep/prep_double.py from assets/reference/DOUBLE.png
juce_add_binary_data(DOUBLE_Skin
    NAMESPACE DOUBLESkin
    HEADER_NAME DOUBLESkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/voices_lit_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/voices_lit_1.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/voices_lit_2.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/mode_lit_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/mode_lit_1.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/mode_lit_2.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/toggle_lit_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/DOUBLE/toggle_lit_1.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(DOUBLE_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(DOUBLE PRIVATE DOUBLE_Skin)

# DSP sources reused by the test runner.
set(DOUBLE_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/DoubleEngine.cpp PARENT_SCOPE)
