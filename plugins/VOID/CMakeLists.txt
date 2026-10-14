file(GLOB VOID_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(VOID
    CODE TVoi
    DISPLAY_NAME "Algorithmic reverb"
    VST3_CATEGORIES Fx Reverb
    SOURCES
        PluginProcessor.cpp
        dsp/VoidEngine.cpp
        dsp/Networks.cpp
        PluginEditor.cpp
    PRESETS ${VOID_PRESETS})

# DSP sources reused by the test runner.
set(VOID_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/VoidEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Networks.cpp
    PARENT_SCOPE)

# Faceplate, lit lamps and buttons, the DROWN ring, the lit FREEZE face,
# where they go, and the UI map (made by tools/skinprep/prep_void.py).
juce_add_binary_data(VOID_Skin
    NAMESPACE VOIDSkin
    HEADER_NAME VOIDSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/algorithm_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/algorithm_1.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/algorithm_2.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/algorithm_3.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/algorithm_4.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/color_clean.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/color_dark.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/color_ruin.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/drown_ring.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/filterMode_loop.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/filterMode_post.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/filterMode_pre.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/freeze_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/VOID/sprites.json
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(VOID_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(VOID PRIVATE VOID_Skin)
