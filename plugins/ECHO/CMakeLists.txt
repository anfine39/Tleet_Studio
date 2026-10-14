file(GLOB ECHO_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(ECHO
    CODE TEch
    DISPLAY_NAME "Multi-head tape echo"
    VST3_CATEGORIES Fx Delay Reverb
    SOURCES
        PluginProcessor.cpp
        dsp/EchoEngine.cpp
        PluginEditor.cpp
    PRESETS ${ECHO_PRESETS})

# DSP sources reused by the test runner.
set(ECHO_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/EchoEngine.cpp PARENT_SCOPE)

# Faceplate, lit buttons, lamps, switch positions, where they go, and the UI
# map (made by tools/skinprep/prep_echo.py).
juce_add_binary_data(ECHO_Skin
    NAMESPACE ECHOSkin
    HEADER_NAME ECHOSkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/bpmSource_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/bpmSource_1.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/character_burn.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/character_clean.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/character_original.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/feel_dotted.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/feel_straight.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/feel_triplet.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/filterMode_lit.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/mode_lamp.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/noteDivision_11.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/noteDivision_116.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/noteDivision_12.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/noteDivision_132.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/noteDivision_14.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/noteDivision_18.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/tap_lamp.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/timeMode_0.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/timeMode_1.png
        ${CMAKE_SOURCE_DIR}/assets/processed/ECHO/sprites.json
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(ECHO_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(ECHO PRIVATE ECHO_Skin)
