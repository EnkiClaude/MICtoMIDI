#pragma once

#include "Controls.h"

namespace m2m::ui
{

/** Horizontal input meter (-60..0 dBFS) with a gate / threshold marker. */
class InputMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLevels (float levelDb, float gateDb, juce::Colour accent);
    void paint (juce::Graphics&) override;

    static constexpr float minDb = -60.0f;

private:
    float level = -120.0f, hold = -120.0f, gate = -45.0f;
    int holdTicks = 0;
    juce::Colour accent = colours::melody;
};

/** Title, mode switch, meter + input gain, latency, notes-sent and status. */
class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar (APVTS& state);

    struct Info
    {
        int mode = 0;            // params::Mode
        float levelDb = -120.0f;
        float gateDb = -45.0f;
        double latencyMs = 0.0;
        int notesSent = 0;
        juce::String status;
        bool statusOk = true;
    };

    void update (const Info& info);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 86;

private:
    juce::TextButton melodyButton { "Melody" }, beatboxButton { "Beatbox" };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    InputMeter meter;
    juce::Label gainLabel;
    juce::Slider gain { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    std::unique_ptr<APVTS::SliderAttachment> gainAttachment;

    juce::Label latency, notes, status, meterCaption;
    int currentMode = 0;
};

} // namespace m2m::ui
