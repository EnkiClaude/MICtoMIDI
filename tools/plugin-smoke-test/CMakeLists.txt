# Hosting smoke test: loads the built VST3 and checks MIDI comes out.
juce_add_console_app(m2m_plugin_smoke_test)

target_sources(m2m_plugin_smoke_test PRIVATE Main.cpp)

target_compile_definitions(m2m_plugin_smoke_test PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(m2m_plugin_smoke_test PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# UI snapshot tool (developer aid): compiles the plugin sources directly,
# feeds synthetic audio and renders the editor to PNG.
#   m2m_ui_snapshot <output-dir> [width height]
juce_add_console_app(m2m_ui_snapshot)

get_target_property(M2M_PLUGIN_SOURCES MICtoMIDI SOURCES)
get_target_property(M2M_PLUGIN_DIR MICtoMIDI SOURCE_DIR)
list(FILTER M2M_PLUGIN_SOURCES INCLUDE REGEX "^Source/")
list(TRANSFORM M2M_PLUGIN_SOURCES PREPEND "${M2M_PLUGIN_DIR}/")

target_sources(m2m_ui_snapshot PRIVATE UiSnapshot.cpp ${M2M_PLUGIN_SOURCES})
target_include_directories(m2m_ui_snapshot PRIVATE "${M2M_PLUGIN_DIR}/Source")

target_compile_definitions(m2m_ui_snapshot PRIVATE
    JucePlugin_Name="MICtoMIDI"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(m2m_ui_snapshot PRIVATE
    mictomidi_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
