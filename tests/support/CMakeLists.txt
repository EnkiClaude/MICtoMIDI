# m2m_testsupport: offline test harness shared by the unit tests and m2m-cli.
# Signal generators, drum synths, scenario library, WAV/MIDI/JSON I/O,
# EngineRunner and metrics. Depends only on mictomidi_core (no JUCE, no
# third-party code). Sources are globbed so new files need no CMake edits.

file(GLOB_RECURSE M2M_TESTSUPPORT_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(m2m_testsupport STATIC ${M2M_TESTSUPPORT_SOURCES})
target_include_directories(m2m_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(m2m_testsupport PUBLIC mictomidi_core)
target_compile_features(m2m_testsupport PUBLIC cxx_std_20)
m2m_enable_warnings(m2m_testsupport)

# Generated signals (noise, drums) must be bit-identical on every platform:
# forbid fused multiply-add contraction, which some compilers enable by default
# on arm64.
if(NOT MSVC)
    target_compile_options(m2m_testsupport PRIVATE -ffp-contract=off)
else()
    target_compile_options(m2m_testsupport PRIVATE /fp:precise)
endif()
