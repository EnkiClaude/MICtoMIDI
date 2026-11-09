# mictomidi_core: all DSP and MIDI logic. Must NOT depend on JUCE so it can be
# built and tested anywhere (Linux CI, the offline CLI tool, the plugin).

add_library(mictomidi_core STATIC
    src/audio/EnvelopeFollower.cpp
    src/midi/MidiEvent.cpp
    src/trigger/LevelTrigger.cpp)

target_include_directories(mictomidi_core PUBLIC include)
target_compile_features(mictomidi_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(mictomidi_core PRIVATE /W4)
else()
    target_compile_options(mictomidi_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

set_target_properties(mictomidi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
