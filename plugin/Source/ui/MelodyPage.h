#pragma once

#include "Controls.h"
#include "../Telemetry.h"

namespace m2m::ui
{

/** Big readout: note name, Hz, cents deviation, confidence, last velocity. */
class NoteDisplay final : public juce::Component
{
public:
    struct State
    {
        float hz = 0.0f;
        float pitch = 0.0f;      // fractional MIDI, 0 = unvoiced
        float confidence = 0.0f;
        int activeNote = -1;
        int lastVelocity = 0;
        float confidenceThreshold = 0.6f;
    };

    void setState (const State& s);
    void paint (juce::Graphics&) override;

private:
    State st;
    int shownNote = -1;
    float shownPitch = 0.0f;
    int holdTicks = 0;
};

/** Scrolling pitch trace (last ~5 s) with the emitted notes as bars. */
class PitchTrace final : public juce::Component
{
public:
    PitchTrace();

    void push (const TelemetryFrame& f);
    void setSampleRate (double sr) { sampleRate = sr > 0.0 ? sr : 48000.0; }
    void tick();  // updates the auto range; call at the UI rate
    void paint (juce::Graphics&) override;

    static constexpr double windowSeconds = 5.0;

private:
    float xFor (std::int64_t pos, juce::Rectangle<float> area) const;
    float yFor (float midi, juce::Rectangle<float> area) const;

    std::vector<TelemetryFrame> ring;
    size_t head = 0, count = 0;
    std::int64_t now = 0;
    double sampleRate = 48000.0;
    float lo = 50.0f, hi = 74.0f;
};

/** Melody mode: readout + trace on top, grouped controls below. */
class MelodyPage final : public juce::Component
{
public:
    explicit MelodyPage (APVTS& state);

    void update (const NoteDisplay::State& s, double latencyMs, double sampleRate);
    void pushFrame (const TelemetryFrame& f) { trace.push (f); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    APVTS& state;
    NoteDisplay display;
    PitchTrace trace;

    ControlGroup detection { "Detection", colours::melody };
    ControlGroup notes { "Notes", colours::melody };
    ControlGroup scale { "Scale", colours::melody };
    ControlGroup velocity { "Velocity & MIDI", colours::melody };
    ControlGroup advanced { "Advanced", colours::melody };
    ParamSlider* minHz = nullptr;

    juce::TextButton advancedButton { "Advanced" };
};

} // namespace m2m::ui
