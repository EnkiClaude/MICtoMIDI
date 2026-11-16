cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect on Apple platforms.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(MICtoMIDI VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(M2M_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU) and standalone app" ON)
option(M2M_BUILD_TESTS  "Build unit tests for the core library"              ON)
option(M2M_BUILD_TOOLS  "Build offline command-line tools"                   ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

add_subdirectory(core)

if(M2M_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_subdirectory(tests)
endif()

if(M2M_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
    add_subdirectory(tools/plugin-smoke-test)
endif()

if(M2M_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/m2m-cli/CMakeLists.txt")
    add_subdirectory(tools/m2m-cli)
endif()
