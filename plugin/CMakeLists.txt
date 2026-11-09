option(M2M_COPY_PLUGIN_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)

set(M2M_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND M2M_FORMATS AU)
endif()

juce_add_plugin(MICtoMIDI
    PRODUCT_NAME                 "MICtoMIDI"
    COMPANY_NAME                 "MICtoMIDI"
    BUNDLE_ID                    "com.mictomidi.mictomidi"
    PLUGIN_MANUFACTURER_CODE     Mtmd
    PLUGIN_CODE                  M2mi
    FORMATS                      ${M2M_FORMATS}
    # Must be an instrument: Ableton Live only routes MIDI out of instruments.
    IS_SYNTH                     TRUE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            TRUE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    VST3_CATEGORIES              Instrument Tools
    AU_MAIN_TYPE                 kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT   "MICtoMIDI listens to your microphone to turn your voice into MIDI."
    COPY_PLUGIN_AFTER_BUILD      ${M2M_COPY_PLUGIN_AFTER_BUILD})

target_sources(MICtoMIDI PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/VirtualMidiOut.cpp)

target_compile_definitions(MICtoMIDI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MICtoMIDI
    PRIVATE
        mictomidi_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
