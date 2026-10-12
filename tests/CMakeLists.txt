juce_add_console_app(tleet_tests PRODUCT_NAME "TLEET Tests")

target_sources(tleet_tests PRIVATE
    TestMain.cpp
    SharedTests.cpp
    AshTests.cpp
    BiteTests.cpp
    BiteGuiTests.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ASH/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ASH/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/ASH/dsp/AshModel.cpp
    ${CMAKE_SOURCE_DIR}/plugins/BITE/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/BITE/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/plugins/BITE/dsp/BiteModel.cpp)

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
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if(NOT MSVC)
    target_compile_options(tleet_tests PRIVATE -Wno-float-equal -Wno-mismatched-new-delete)
endif()

add_test(NAME tleet_tests COMMAND tleet_tests)
