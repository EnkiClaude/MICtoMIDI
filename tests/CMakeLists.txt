add_executable(m2m_tests
    unit/EnvelopeFollowerTests.cpp
    unit/LevelTriggerTests.cpp
    unit/MidiEventTests.cpp)

target_link_libraries(m2m_tests PRIVATE mictomidi_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(m2m_tests)
