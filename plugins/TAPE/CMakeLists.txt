file(GLOB TAPE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(TAPE
    CODE TTap
    DISPLAY_NAME "Multitrack tape machine"
    VST3_CATEGORIES Fx Distortion
    SOURCES
        PluginProcessor.cpp
        dsp/TapeEngine.cpp
        PluginEditor.cpp
    PRESETS ${TAPE_PRESETS})

# DSP sources reused by the test runner.
set(TAPE_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/TapeEngine.cpp
    PARENT_SCOPE)

juce_add_binary_data(TAPE_Skin
    NAMESPACE TAPESkin
    HEADER_NAME TAPESkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/autoGain_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/calibration_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/calibration_m3.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/calibration_p3.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/calibration_p6.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/noise_off.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/noise_on.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/reelLeft.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/reelLeft_light.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/reelRight.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/reelRight_light.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/satRed.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/satWhite.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/speed_15.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/speed_30.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/speed_7_5.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/sprites.json
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/tapeType_a.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/tapeType_b.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/tapeType_c.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/vuRecord_cap.png
        ${CMAKE_SOURCE_DIR}/assets/processed/TAPE/vuRepro_cap.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(TAPE_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(TAPE PRIVATE TAPE_Skin)
