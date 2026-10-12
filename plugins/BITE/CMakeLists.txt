file(GLOB BITE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/*.json)

tleet_add_plugin(BITE
    CODE TBit
    DISPLAY_NAME "FET limiting amplifier"
    SOURCES
        PluginProcessor.cpp
        PluginEditor.cpp
        dsp/BiteModel.cpp
    PRESETS ${BITE_PRESETS})

# Skin assets produced by tools/skinprep/prep_bite.py from assets/reference/BITE.png
juce_add_binary_data(BITE_Skin
    NAMESPACE BITESkin
    HEADER_NAME BITESkin.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/processed/BITE/plate.png
        ${CMAKE_SOURCE_DIR}/assets/processed/BITE/ratio_lit.png
        ${CMAKE_CURRENT_SOURCE_DIR}/UI_MAP.json)
set_target_properties(BITE_Skin PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(BITE PRIVATE BITE_Skin)

# DSP sources reused by the test runner.
set(BITE_DSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dsp/BiteModel.cpp PARENT_SCOPE)
