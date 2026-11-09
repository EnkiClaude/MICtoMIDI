juce_add_console_app(m2m_plugin_smoke_test)

target_sources(m2m_plugin_smoke_test PRIVATE Main.cpp)

target_compile_definitions(m2m_plugin_smoke_test PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(m2m_plugin_smoke_test PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
