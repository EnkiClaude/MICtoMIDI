#pragma once

#include "m2m/Engine.h"
#include "m2m/test/Notes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace m2m::test
{

struct RunOptions
{
    int blockSize = 256;               // samples per process() call (last block may be shorter)
    std::size_t eventCapacity = 1024;  // MidiEventBuffer capacity per block
    bool flushAtEnd = true;            // call flush() after the last block

    /** Optional: called after every process() call (and after the final
        flush) with the absolute sample position just past that block, e.g.
        to drain an engine's diagnostics FIFO. Not timed. */
    std::function<void (std::int64_t endSample)> afterBlock;
};

struct RunResult
{
    double sampleRate = 0.0;
    std::int64_t numSamples = 0;
    int blockSize = 0;
    int numBlocks = 0;
    int latencySamples = 0;            // engine->latencySamples() after prepare()

    std::vector<TimedEvent> events;    // absolute sample positions, in emission order
    std::vector<DetectedNote> notes;   // pairNotes (events), sorted by onset

    /** Blocks whose event buffer was full afterwards (events may have been dropped). */
    int fullBufferBlocks = 0;

    // Timing of process() calls (flush excluded), wall clock.
    double meanBlockMicros = 0.0;
    double maxBlockMicros = 0.0;
    double totalProcessSec = 0.0;
    /** totalProcessSec / audio duration. 0.01 means 1 % of real time
        (100x faster than real time). Lower is better. */
    double realtimeFactor = 0.0;

    double durationSec() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double> (numSamples) / sampleRate : 0.0;
    }
};

/** Run an engine over a whole mono buffer: prepare (sampleRate, blockSize),
    reset, process() block by block, then flush() at the end (flush events are
    placed at sample position numSamples). */
RunResult runEngine (IEngine& engine, const float* audio, std::size_t numSamples,
                     double sampleRate, const RunOptions& options = {});

RunResult runEngine (IEngine& engine, const std::vector<float>& audio,
                     double sampleRate, const RunOptions& options = {});

} // namespace m2m::test
