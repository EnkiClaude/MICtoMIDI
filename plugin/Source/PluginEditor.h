#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

/** Phase 1 UI: input meter with threshold marker, note indicator, the four
    parameters and a routing/status line. */
class MicToMidiEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MicToMidiEditor (MicToMidiProcessor&);
    ~MicToMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct LabelledSlider
    {
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setUp (LabelledSlider&, const juce::String& name, const char* paramId);

    MicToMidiProcessor& processor;

    LabelledSlider threshold, note, channel, velocity;
    juce::Label status;

    juce::Rectangle<int> meterArea, ledArea;
    float meterDb = -100.0f;
    bool  noteOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicToMidiEditor)
};
