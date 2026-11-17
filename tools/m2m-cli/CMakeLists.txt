# m2m-cli: offline WAV -> MIDI + metrics, scenario generation and regression
# gating. See docs/m2m-cli.md.

file(GLOB M2M_CLI_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(m2m-cli ${M2M_CLI_SOURCES})
target_link_libraries(m2m-cli PRIVATE m2m_testsupport mictomidi_core)
m2m_enable_warnings(m2m-cli)
