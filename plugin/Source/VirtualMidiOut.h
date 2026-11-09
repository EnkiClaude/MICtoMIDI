#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <m2m/midi/MidiEvent.h>

#include <array>
#include <atomic>

namespace m2m
{

/** Publishes a virtual MIDI output port (standalone app only).

    The audio thread pushes events into a lock-free FIFO; a background thread
    drains it and sends to the port, so the audio thread never calls into the
    OS MIDI API. Virtual ports exist on macOS (CoreMIDI) and Linux (ALSA);
    on Windows open() returns false (needs loopMIDI / Windows MIDI Services). */
class VirtualMidiOut : private juce::Thread
{
public:
    VirtualMidiOut();
    ~VirtualMidiOut() override;

    bool open (const juce::String& portName);
    void close();
    bool isOpen() const noexcept { return active.load(); }
    juce::String getPortName() const { return name; }

    /** Real-time safe. Drops the event if the FIFO is full. */
    void push (const MidiEvent& e) noexcept;

    int getDroppedCount() const noexcept { return dropped.load(); }

private:
    void run() override;

    static constexpr int fifoSize = 1024;
    juce::AbstractFifo fifo { fifoSize };
    std::array<MidiEvent, fifoSize> slots {};

    std::unique_ptr<juce::MidiOutput> port;
    juce::String name;
    std::atomic<int> dropped { 0 };
    std::atomic<bool> active { false };
};

} // namespace m2m
