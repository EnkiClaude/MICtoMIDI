#pragma once

#include "m2m/midi/MidiEvent.h"

namespace m2m
{

/** Common interface for the analysis engines (level trigger, melody, beatbox).

    Lifecycle: prepare() (may allocate) -> process()/flush() on the audio
    thread (real-time safe, never allocate) -> reset() as needed. */
class IEngine
{
public:
    virtual ~IEngine() = default;

    /** May allocate. Must be called before process(). */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Clear all analysis state without emitting events. */
    virtual void reset() noexcept = 0;

    /** Analyse a mono block; append events with offsets relative to block
        start. Real-time safe. */
    virtual void process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept = 0;

    /** Release any held notes (bypass / mode switch). */
    virtual void flush (MidiEventBuffer& out, int sampleOffset = 0) noexcept = 0;

    /** Detection delay in samples, for host latency reporting. */
    virtual int latencySamples() const noexcept = 0;
};

} // namespace m2m
