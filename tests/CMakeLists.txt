# Sources are globbed so new test files need no CMake edits.

# m2m_testsupport: shared test helpers (signal generators, metrics, ...).
file(GLOB_RECURSE M2M_TESTSUPPORT_SOURCES CONFIGURE_DEPENDS support/*.cpp)
add_library(m2m_testsupport STATIC ${M2M_TESTSUPPORT_SOURCES})
target_include_directories(m2m_testsupport PUBLIC support)
target_link_libraries(m2m_testsupport PUBLIC mictomidi_core)

file(GLOB_RECURSE M2M_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
add_executable(m2m_tests ${M2M_TEST_SOURCES})

target_link_libraries(m2m_tests PRIVATE m2m_testsupport mictomidi_core Catch2::Catch2WithMain)

if(MSVC)
    target_compile_options(m2m_testsupport PRIVATE /W4)
    target_compile_options(m2m_tests PRIVATE /W4)
else()
    target_compile_options(m2m_testsupport PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    target_compile_options(m2m_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(Catch)
catch_discover_tests(m2m_tests)
