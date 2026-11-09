#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <m2m/trigger/LevelTrigger.h>

#include "VirtualMidiOut.h"

#include <atomic>
#include <vector>

/** Instrument that takes the mic on its main input OR its sidechain input
    (both are summed to mono) and emits MIDI. Audio output is silent. */
class MicToMidiProcessor : public juce::AudioProcessor
{
public:
    MicToMidiProcessor();
    ~MicToMidiProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
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

    /** UI telemetry, written by the audio thread. */
    float getInputPeakDb() const noexcept   { return inputPeakDb.load (std::memory_order_relaxed); }
    bool  isNoteActive() const noexcept     { return noteActive.load (std::memory_order_relaxed); }
    int   getNotesSent() const noexcept     { return notesSent.load (std::memory_order_relaxed); }

    const m2m::VirtualMidiOut& getVirtualMidiOut() const noexcept { return virtualOut; }
    bool isStandalone() const noexcept { return wrapperType == wrapperType_Standalone; }

    static constexpr const char* virtualPortName = "MICtoMIDI";

private:
    void updateTriggerParams() noexcept;
    void emit (const m2m::MidiEventBuffer& events, juce::MidiBuffer& midi, int numSamples, int baseOffset);

    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* noteParam      = nullptr;
    std::atomic<float>* channelParam   = nullptr;
    std::atomic<float>* velocityParam  = nullptr;

    m2m::LevelTrigger trigger;
    m2m::MidiEventBuffer events;
    std::vector<float> mono;

    m2m::VirtualMidiOut virtualOut;

    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<bool>  noteActive  { false };
    std::atomic<int>   notesSent   { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicToMidiProcessor)
};
