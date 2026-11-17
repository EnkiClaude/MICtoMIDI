#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <m2m/beatbox/BeatboxEngine.h>
#include <m2m/melody/MelodyEngine.h>

#include "Parameters.h"
#include "Telemetry.h"
#include "VirtualMidiOut.h"

#include <array>
#include <atomic>
#include <vector>

/** Instrument that takes the mic on its main input OR its sidechain input
    (both are summed to mono), runs the Melody or the Beatbox engine on it and
    emits MIDI. Audio output is silent.

    Threads
      Audio thread: processBlock / processBlockBypassed only. No allocation,
        locks or I/O; telemetry goes out through atomics and SPSC FIFOs.
      Message thread: editor, latency reporting (timer), calibration calls.
      get/setStateInformation may come from any non-audio thread; the
        calibration calls they make are serialised with the UI's by a lock
        that the audio thread never takes. */
class MicToMidiProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MicToMidiProcessor();
    ~MicToMidiProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;          // double-precision overloads stay as-is
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    m2m::params::Mode getModeParam() const noexcept;

    // --- UI telemetry (any thread; written by the audio thread) ----------------
    float getInputPeakDb() const noexcept      { return inputPeakDb.load (std::memory_order_relaxed); }
    float getMelodyHz() const noexcept         { return melodyHz.load (std::memory_order_relaxed); }
    float getMelodyPitch() const noexcept      { return melodyPitch.load (std::memory_order_relaxed); }
    float getMelodyConfidence() const noexcept { return melodyConfidence.load (std::memory_order_relaxed); }
    int   getActiveNote() const noexcept       { return activeNote.load (std::memory_order_relaxed); }
    int   getLastVelocity() const noexcept     { return lastVelocity.load (std::memory_order_relaxed); }
    int   getNotesSent() const noexcept        { return notesSent.load (std::memory_order_relaxed); }
    double getCurrentSampleRate() const noexcept { return sampleRateAtomic.load (std::memory_order_relaxed); }
    /** Latency of the active engine in ms (what is / will be reported to the host). */
    double getLatencyMs() const noexcept;

    /** Single consumer (the editor). */
    bool popTelemetry (m2m::TelemetryFrame& f) noexcept  { return frames.pop (f); }
    bool popHit (m2m::HitTelemetry& h) noexcept          { return hitFifo.pop (h); }

    // --- Beatbox calibration (message thread) -------------------------------
    void beginCalibration (m2m::DrumClass c);
    void endCalibration();
    void cancelCalibration();
    void clearCalibration (m2m::DrumClass c);
    void clearAllCalibration();
    juce::String getCalibrationString();
    /** Returns false if the text is not a valid calibration. */
    bool setCalibrationString (const juce::String& text);

    bool isCalibrating() const noexcept                       { return beatbox.isCalibrating(); }
    m2m::DrumClass calibrationClass() const noexcept          { return beatbox.calibrationClass(); }
    int calibrationExampleCount (m2m::DrumClass c) const noexcept { return beatbox.calibrationExampleCount (c); }
    bool isCalibrationUsable() const noexcept                 { return beatbox.isCalibrationUsable(); }
    bool usingCalibratedClassifier() const noexcept           { return beatbox.usingCalibratedClassifier(); }

    //==============================================================================
    const m2m::VirtualMidiOut& getVirtualMidiOut() const noexcept { return virtualOut; }
    bool isStandalone() const noexcept { return wrapperType == wrapperType_Standalone; }

    static constexpr const char* virtualPortName = "MICtoMIDI";

private:
    void timerCallback() override;
    void applyParams() noexcept;
    m2m::IEngine& engineFor (m2m::params::Mode m) noexcept;
    void emit (const m2m::MidiEventBuffer& events, juce::MidiBuffer& midi, int numSamples, int baseOffset) noexcept;
    void publishTelemetry (int numSamples, float chunkPeak) noexcept;
    void drainHits() noexcept;

    juce::AudioProcessorValueTreeState state;
    m2m::params::ParamCache paramCache;
    std::atomic<float>* modeParam = nullptr;

    m2m::MelodyEngine melody;
    m2m::BeatboxEngine beatbox;
    m2m::MelodyParams melodyParams;
    m2m::BeatboxParams beatboxParams;
    m2m::params::Mode activeMode = m2m::params::Mode::Melody;

    m2m::MidiEventBuffer events;
    std::vector<float> mono;
    std::array<m2m::HitInfo, 16> hitScratch {};

    // audio-thread bookkeeping
    std::int64_t samplePos = 0;      // samples processed since prepare
    std::int64_t beatboxBase = 0;    // samplePos at the beatbox engine's last reset
    int frameInterval = 240;
    int samplesSinceFrame = 0;
    float framePeak = 0.0f;

    m2m::SpscFifo<m2m::TelemetryFrame, 4096> frames;
    m2m::SpscFifo<m2m::HitTelemetry, 256> hitFifo;

    std::atomic<float> inputPeakDb { -120.0f };
    std::atomic<float> melodyHz { 0.0f }, melodyPitch { 0.0f }, melodyConfidence { 0.0f };
    std::atomic<int>   activeNote { -1 }, lastVelocity { 0 }, notesSent { 0 };
    std::atomic<int>   wantedLatency { 0 };
    std::atomic<double> sampleRateAtomic { 48000.0 };
    std::atomic<std::uint32_t> beatboxProcessCount { 0 };

    // calibration calls from the message thread / state calls
    juce::CriticalSection calibLock;
    juce::String restoredCalibration;
    bool restorePending = false;
    std::uint32_t restoreMark = 0;

    m2m::VirtualMidiOut virtualOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicToMidiProcessor)
};
