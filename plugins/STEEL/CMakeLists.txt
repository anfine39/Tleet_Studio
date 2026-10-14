file(GLOB STEEL_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(STEEL
    CODE TStl
    DISPLAY_NAME "True-peak mastering limiter"
    VST3_CATEGORIES Fx Dynamics Mastering
    SOURCES
        PluginProcessor.cpp
        dsp/SteelEngine.cpp
        PluginEditor.cpp
    PRESETS ${STEEL_PRESETS})

# DSP sources reused by the test runner.
set(STEEL_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/SteelEngine.cpp
    PARENT_SCOPE)

juce_add_binary_data(STEEL_Skin
    NAMESPACE STEELSkin
    HEADER_NAME STEELSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/delta_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/levelMatch_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/mode_clean.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/mode_dense.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/mode_punch.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/mode_steel.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/oversampling_1x.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/oversampling_2x.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/oversampling_4x.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/oversampling_8x.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/releaseMode_auto.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/releaseMode_manual.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/softClip_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/subFilter_20.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/subFilter_25.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/subFilter_30.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/subFilter_off.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/truePeak_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/STEEL/sprites.json
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(STEEL_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(STEEL PRIVATE STEEL_Skin)
