# Unit tests. Sources are globbed (recursively, including unit/support/) so new
# test files need no CMake edits. m2m_testsupport is defined in
# tests/support/CMakeLists.txt (added from the root).

file(GLOB_RECURSE M2M_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
add_executable(m2m_tests ${M2M_TEST_SOURCES})

target_link_libraries(m2m_tests PRIVATE m2m_testsupport mictomidi_core Catch2::Catch2WithMain)
m2m_enable_warnings(m2m_tests)

include(Catch)
catch_discover_tests(m2m_tests)

# Real-time safety: replaces the global operator new/delete, so it is a
# separate executable (the override must not affect the other tests).
add_executable(m2m_rt_tests rt/AllocationTests.cpp)
target_link_libraries(m2m_rt_tests PRIVATE m2m_testsupport mictomidi_core)
m2m_enable_warnings(m2m_rt_tests)
add_test(NAME m2m_rt_no_allocation COMMAND m2m_rt_tests)
