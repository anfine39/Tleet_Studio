cmake_minimum_required(VERSION 3.22)

project(TLEET_Audio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

if(MSVC)
    # Static runtime: the .vst3 must not depend on a VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(TLEET_BUILD_TESTS "Build the DSP/state test runner" ON)
option(TLEET_COPY_AFTER_BUILD "Install plugins into the system plugin folders after building" OFF)
set(TLEET_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading it")

# ---------------------------------------------------------------------------
# JUCE (AGPLv3 licence, see LICENSE and docs/THIRD_PARTY.md)
# ---------------------------------------------------------------------------
if(TLEET_JUCE_DIR)
    add_subdirectory(${TLEET_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(juce)
endif()

# Build identifier shown in the About panel.
set(TLEET_BUILD_ID "local")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE TLEET_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(TLEET_GIT_HASH)
        set(TLEET_BUILD_ID ${TLEET_GIT_HASH})
    endif()
endif()

include(cmake/TleetPlugin.cmake)

add_subdirectory(shared)
add_subdirectory(plugins/ASH)

if(TLEET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
