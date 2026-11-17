#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/BeatboxPage.h"
#include "ui/HeaderBar.h"
#include "ui/MelodyPage.h"
#include "ui/Theme.h"

/** Main window: header (mode switch, meter, latency, status), the Melody or
    Beatbox page, and a footer hint. Everything refreshes from a 30 Hz timer
    that drains the processor's lock-free telemetry. */
class MicToMidiEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MicToMidiEditor (MicToMidiProcessor&);
    ~MicToMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int minWidth = 720, minHeight = 480;
    static constexpr int defaultWidth = 960, defaultHeight = 640;

private:
    void timerCallback() override;
    void showPage (m2m::params::Mode m);

    m2m::ui::LookAndFeel lookAndFeel;
    MicToMidiProcessor& processor;

    m2m::ui::HeaderBar header;
    m2m::ui::MelodyPage melodyPage;
    m2m::ui::BeatboxPage beatboxPage;
    juce::Label footer;
    juce::TooltipWindow tooltips { this, 600 };

    std::int64_t lastSamplePos = 0;
    float frameLevelDb = -120.0f;
    int shownMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicToMidiEditor)
};
