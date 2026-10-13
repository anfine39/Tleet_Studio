file(GLOB WEIGHT_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

# The "Sidechain" input bus is declared by WeightProcessor (BusesProperties);
# SIDECHAIN below only documents it (tleet_add_plugin needs no extra setup).
tleet_add_plugin(WEIGHT
    CODE TWgt
    DISPLAY_NAME "Stereo bus compressor"
    SIDECHAIN
    SOURCES
        PluginProcessor.cpp
        # (editor integration: PluginEditor.cpp is appended here)
        dsp/WeightModel.cpp
    PRESETS ${WEIGHT_PRESETS})

# DSP sources reused by the test runner.
set(WEIGHT_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/WeightModel.cpp PARENT_SCOPE)

# (editor integration: the WEIGHT_Skin binary-data target, built from
#  assets/processed/WEIGHT/plate.png and UI_MAP.json, is added here and
#  linked to WEIGHT, as in plugins/GLUE/CMakeLists.txt)
