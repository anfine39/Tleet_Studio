# Helper that turns a plugin folder into a JUCE plugin target with the
# suite-wide settings. Identifiers passed here are immutable after the first
# public build: changing them breaks every saved DAW project.
#
# tleet_add_plugin(<Name>
#     CODE <4-char plugin code>
#     DISPLAY_NAME "<name shown in the DAW>"
#     SOURCES ...
#     [PRESETS <factory preset json files>]
#     [SIDECHAIN])

set(TLEET_MANUFACTURER_CODE "Tlet")
set(TLEET_COMPANY_NAME "TLEET Audio")
set(TLEET_BUNDLE_PREFIX "audio.tleet")

if(APPLE)
    set(TLEET_FORMATS VST3 AU)
else()
    set(TLEET_FORMATS VST3)
endif()

function(tleet_add_plugin name)
    cmake_parse_arguments(ARG "SIDECHAIN" "CODE;DISPLAY_NAME" "SOURCES;PRESETS" ${ARGN})

    string(TOLOWER ${name} lower)

    juce_add_plugin(${name}
        COMPANY_NAME "${TLEET_COMPANY_NAME}"
        COMPANY_WEBSITE ""
        BUNDLE_ID "${TLEET_BUNDLE_PREFIX}.${lower}"
        PLUGIN_MANUFACTURER_CODE ${TLEET_MANUFACTURER_CODE}
        PLUGIN_CODE ${ARG_CODE}
        FORMATS ${TLEET_FORMATS}
        PRODUCT_NAME "TLEET ${name}"
        DESCRIPTION "${ARG_DISPLAY_NAME}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Dynamics
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${TLEET_COPY_AFTER_BUILD})

    target_sources(${name} PRIVATE ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    if(ARG_PRESETS)
        juce_add_binary_data(${name}_Presets
            NAMESPACE ${name}Presets
            HEADER_NAME ${name}Presets.h
            SOURCES ${ARG_PRESETS})
        set_target_properties(${name}_Presets PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_link_libraries(${name} PRIVATE ${name}_Presets)
    endif()

    target_compile_definitions(${name} PUBLIC
        TLEET_PLUGIN_NAME="${name}"
        TLEET_PLUGIN_DISPLAY_NAME="${ARG_DISPLAY_NAME}"
        TLEET_VERSION_STRING="${PROJECT_VERSION}"
        TLEET_BUILD_ID="${TLEET_BUILD_ID}")

    target_link_libraries(${name}
        PRIVATE
            tleet::shared
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()
