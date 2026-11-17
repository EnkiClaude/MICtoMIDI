#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace m2m
{

/** Fixed-size lock-free single-producer / single-consumer queue.
    push() and pop() never allocate or block. Capacity must be a power of two. */
template <typename T, std::uint32_t Capacity>
class SpscFifo
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /** Producer side. Returns false (dropping the item) if the queue is full. */
    bool push (const T& item) noexcept
    {
        const auto head = writePos.load (std::memory_order_relaxed);
        if (head - readPos.load (std::memory_order_acquire) >= Capacity)
            return false;
        slots[head & (Capacity - 1)] = item;
        writePos.store (head + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. */
    bool pop (T& item) noexcept
    {
        const auto tail = readPos.load (std::memory_order_relaxed);
        if (tail == writePos.load (std::memory_order_acquire))
            return false;
        item = slots[tail & (Capacity - 1)];
        readPos.store (tail + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> slots {};
    std::atomic<std::uint32_t> writePos { 0 }, readPos { 0 };
};

/** A periodic snapshot of the analysis state (audio thread -> UI). */
struct TelemetryFrame
{
    std::int64_t samplePos = 0;  // samples processed since prepare
    float levelDb = -120.0f;     // input peak since the previous frame (after input gain)
    float pitchMidi = 0.0f;      // melody: smoothed fractional MIDI pitch, 0 = unvoiced
    float confidence = 0.0f;     // melody: detector confidence
    int activeNote = -1;         // melody: output note, -1 = none
    int mode = 0;                // params::Mode
};

/** One classified beatbox onset (audio thread -> UI). */
struct HitTelemetry
{
    std::int64_t samplePos = 0;  // emit position, samples since prepare
    int cls = 0;                 // m2m::DrumClass index
    int velocity = 0;
    float score = 0.0f;
    bool accepted = false;
    bool emitted = false;
    bool calibration = false;
};

} // namespace m2m
