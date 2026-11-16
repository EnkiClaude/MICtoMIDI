#pragma once

#include "m2m/Engine.h"
#include "m2m/trigger/LevelTrigger.h"

namespace m2m
{

/** Thin IEngine adapter around LevelTrigger. Configure via trigger(). */
class LevelTriggerEngine final : public IEngine
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/) override { trig.prepare (sampleRate); }
    void reset() noexcept override { trig.reset(); }

    void process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept override
    {
        trig.process (mono, numSamples, out);
    }

    void flush (MidiEventBuffer& out, int sampleOffset = 0) noexcept override
    {
        trig.flush (out, sampleOffset);
    }

    /** Sample-accurate envelope detection with no look-ahead. */
    int latencySamples() const noexcept override { return 0; }

    LevelTrigger& trigger() noexcept { return trig; }
    const LevelTrigger& trigger() const noexcept { return trig; }

private:
    LevelTrigger trig;
};

} // namespace m2m
