juce_add_console_app(tleet_tests PRODUCT_NAME "TLEET Tests")

target_sources(tleet_tests PRIVATE
    TestMain.cpp
    SharedTests.cpp
    AshTests.cpp
    BiteTests.cpp
    BiteGuiTests.cpp
    DoubleTests.cpp
    DoubleGuiTests.cpp
    GlueTests.cpp
    GlueGuiTests.cpp
    PunchTests.cpp
    PunchGuiTests.cpp
    WeightTests.cpp
    WeightGuiTests.cpp
    VeilTests.cpp
    VeilGuiTests.cpp
    ShapeTests.cpp
    ShapeGuiTests.cpp
    EchoTests.cpp
    EchoGuiTests.cpp
    VoidTests.cpp
    VoidGuiTests.cpp
    SteelTests.cpp
    SteelGuiTests.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ASH/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ASH/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ASH/dsp/AshModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/BITE/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/BITE/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/BITE/dsp/BiteModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/DOUBLE/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/DOUBLE/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/DOUBLE/VoiceDisplay.cpp
    ${CMAKE_SOURCE_DIR}/plugins/DOUBLE/FilterDisplay.cpp
    ${CMAKE_SOURCE_DIR}/plugins/DOUBLE/dsp/DoubleEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/GLUE/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/GLUE/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/GLUE/dsp/GlueModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/PUNCH/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/PUNCH/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/PUNCH/dsp/PunchModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/WEIGHT/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/WEIGHT/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/WEIGHT/dsp/WeightModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VEIL/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VEIL/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VEIL/dsp/VeilModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SHAPE/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SHAPE/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/SHAPE/dsp/ShapeModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ECHO/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ECHO/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ECHO/dsp/EchoEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VOID/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VOID/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VOID/dsp/VoidEngine.cpp
    ${CMAKE_SOURCE_DIR}/plugins/VOID/dsp/Networks.cpp
    ${CMAKE_SOURCE_DIR}/plugins/STEEL/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/STEEL/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/STEEL/dsp/SteelEngine.cpp)

target_include_directories(tleet_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugins/ASH ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(tleet_tests PRIVATE
    TLEET_PLUGIN_NAME="ASH"
    TLEET_PLUGIN_DISPLAY_NAME="Optical levelling amplifier"
    TLEET_VERSION_STRING="${PROJECT_VERSION}"
    TLEET_BUILD_ID="${TLEET_BUILD_ID}"
    JucePlugin_Build_Standalone=0
    TLEET_TESTING=1)

target_link_libraries(tleet_tests PRIVATE
    tleet::shared
    ASH_Presets
    ASH_Skin
    BITE_Presets
    BITE_Skin
    DOUBLE_Presets
    DOUBLE_Skin
    GLUE_Presets
    GLUE_Skin
    PUNCH_Presets
    PUNCH_Skin
    WEIGHT_Presets
    WEIGHT_Skin
    VEIL_Presets
    VEIL_Skin
    SHAPE_Presets
    SHAPE_Skin
    ECHO_Presets
    ECHO_Skin
    VOID_Presets
    VOID_Skin
    STEEL_Presets
    STEEL_Skin
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if(NOT MSVC)
    target_compile_options(tleet_tests PRIVATE -Wno-float-equal -Wno-mismatched-new-delete)
endif()

add_test(NAME tleet_tests COMMAND tleet_tests)
