# m2m-cli: offline WAV -> MIDI + metrics, scenario generation and regression
# gating. See docs/m2m-cli.md.

file(GLOB M2M_CLI_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(m2m-cli ${M2M_CLI_SOURCES})
target_link_libraries(m2m-cli PRIVATE m2m_testsupport mictomidi_core)
m2m_enable_warnings(m2m-cli)

# Smoke tests through ctest when the test suite is enabled too.
if(M2M_BUILD_TESTS)
    add_test(NAME m2m_cli_scenarios COMMAND m2m-cli scenarios)
    add_test(NAME m2m_cli_eval_trigger_smoke
             COMMAND m2m-cli eval --mode trigger --scenarios level_bursts)
    add_test(NAME m2m_cli_gen_and_run
             COMMAND ${CMAKE_COMMAND}
                     -DCLI=$<TARGET_FILE:m2m-cli>
                     -DOUT=${CMAKE_CURRENT_BINARY_DIR}/smoke
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/SmokeTest.cmake)

    # Metric regression gates (PLAN.md section 9): exit code 4 on a regression.
    # Deterministic: no wall-clock metric is gated.
    add_test(NAME m2m_cli_gate_melody
             COMMAND m2m-cli eval --mode melody --baseline ${PROJECT_SOURCE_DIR}/tests/baselines/melody.json)
    add_test(NAME m2m_cli_gate_beatbox
             COMMAND m2m-cli eval --mode beatbox --baseline ${PROJECT_SOURCE_DIR}/tests/baselines/beatbox.json)
endif()
