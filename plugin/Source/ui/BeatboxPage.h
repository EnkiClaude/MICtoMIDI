#pragma once

#include "Controls.h"
#include "../Telemetry.h"

#include <array>

class MicToMidiProcessor;

namespace m2m::ui
{

/** One drum class: name, hit history (last few seconds) and a flash on hits. */
class DrumLane final : public juce::Component
{
public:
    DrumLane (int slot, const juce::String& name);

    void addHit (const HitTelemetry& h);
    void setNow (std::int64_t samplePos, double sampleRate);
    void setInfo (int calibrationExamples, bool calibratingThis, bool enabled);
    void tick();

    void paint (juce::Graphics&) override;

    static constexpr double windowSeconds = 4.0;

private:
    int slot;
    juce::String name;
    juce::Colour colour;
    std::array<HitTelemetry, 64> hits {};
    size_t head = 0, count = 0;
    std::int64_t now = 0;
    double sampleRate = 48000.0;
    float flash = 0.0f;
    int lastVelocity = 0;
    int examples = 0;
    bool calibrating = false, enabled = true;
};

/** Beatbox mode: lanes + per-class mapping, classifier and calibration flow,
    detection / output settings. */
class BeatboxPage final : public juce::Component
{
public:
    BeatboxPage (MicToMidiProcessor& processor, APVTS& state);
    ~BeatboxPage() override;

    void addHit (const HitTelemetry& h);
    void update (std::int64_t nowSamples, double sampleRate, double latencyMs);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int examplesToAsk = 6;

private:
    struct ClassRow
    {
        std::unique_ptr<DrumLane> lane;
        juce::ToggleButton enable;
        juce::Slider sensitivity { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        juce::Slider note { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        std::unique_ptr<APVTS::ButtonAttachment> enableAttachment;
        std::unique_ptr<APVTS::SliderAttachment> sensAttachment, noteAttachment;
    };

    void startCalibration();
    void exportCalibration();
    void importCalibration();
    void refreshCalibrationUi();
    void layoutCalibrationPanel (juce::Rectangle<int> area);

    MicToMidiProcessor& processor;
    APVTS& state;

    std::array<ClassRow, 5> rows;
    juce::Label colOn, colSound, colSens, colNote;
    juce::Rectangle<int> lanesArea, calibArea;

    // classifier + calibration panel
    std::unique_ptr<ParamCombo> classifier;
    juce::Label classifierStatus, calibTitle, calibPrompt, calibCount;
    juce::ComboBox calibClass;
    juce::TextButton calibrateButton { "Calibrate" }, doneButton { "Done" }, cancelButton { "Cancel" },
                     clearButton { "Clear" }, clearAllButton { "Clear All" },
                     exportButton { "Export..." }, importButton { "Import..." };
    bool wasCalibrating = false;
    std::unique_ptr<juce::FileChooser> chooser;

    ControlGroup detection { "Detection", colours::beatbox };
    ControlGroup output { "Output", colours::beatbox };
    ControlGroup velocity { "Velocity", colours::beatbox };
    ParamSlider* windowSlider = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BeatboxPage)
};

} // namespace m2m::ui
