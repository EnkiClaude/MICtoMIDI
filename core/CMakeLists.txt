# mictomidi_core: all DSP and MIDI logic. Must NOT depend on JUCE so it can be
# built and tested anywhere (Linux CI, the offline CLI tool, the plugin).

# Sources are globbed so new files under src/ need no CMake edits.
file(GLOB_RECURSE M2M_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(mictomidi_core STATIC ${M2M_CORE_SOURCES})

target_include_directories(mictomidi_core PUBLIC include)
target_compile_features(mictomidi_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(mictomidi_core PRIVATE /W4)
else()
    target_compile_options(mictomidi_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

set_target_properties(mictomidi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
