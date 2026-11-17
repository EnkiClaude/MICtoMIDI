#include "m2m/test/EngineRunner.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <tuple>

namespace m2m::test
{

std::vector<DetectedNote> pairNotes (const std::vector<TimedEvent>& events, double sampleRate,
                                     std::int64_t endSample)
{
    struct Open { std::int64_t onSample; int velocity; };
    std::map<std::pair<int, int>, std::vector<Open>> open; // (channel, note) -> FIFO
    std::vector<DetectedNote> notes;

    const auto toSec = [sampleRate] (std::int64_t s) { return static_cast<double> (s) / sampleRate; };
    const auto close = [&] (int ch, int note, const Open& o, std::int64_t offSample)
    {
        notes.push_back ({ toSec (o.onSample), toSec (std::max (offSample, o.onSample)), note, ch, o.velocity });
    };

    for (const auto& te : events)
    {
        const int ch = te.event.channel;
        const int note = te.event.note;
        auto& stack = open[{ ch, note }];
        const bool isOn = te.event.type == MidiEvent::Type::NoteOn && te.event.velocity > 0;

        if (isOn)
        {
            for (const auto& o : stack)
                close (ch, note, o, te.samplePos);
            stack.clear();
            stack.push_back ({ te.samplePos, te.event.velocity });
        }
        else if (! stack.empty())
        {
            close (ch, note, stack.front(), te.samplePos);
            stack.erase (stack.begin());
        }
    }

    for (const auto& [key, stack] : open)
        for (const auto& o : stack)
            close (key.first, key.second, o, endSample >= 0 ? endSample : o.onSample);

    std::stable_sort (notes.begin(), notes.end(), [] (const DetectedNote& a, const DetectedNote& b)
    {
        return std::tie (a.onsetSec, a.midiNote, a.channel) < std::tie (b.onsetSec, b.midiNote, b.channel);
    });
    return notes;
}

RunResult runEngine (IEngine& engine, const float* audio, std::size_t numSamples,
                     double sampleRate, const RunOptions& options)
{
    using Clock = std::chrono::steady_clock;

    RunResult r;
    r.sampleRate = sampleRate;
    r.numSamples = static_cast<std::int64_t> (numSamples);
    r.blockSize = std::max (1, options.blockSize);

    engine.prepare (sampleRate, r.blockSize);
    engine.reset();
    r.latencySamples = engine.latencySamples();

    MidiEventBuffer buf;
    buf.reserve (std::max<std::size_t> (1, options.eventCapacity));

    const auto collect = [&] (std::int64_t blockStart)
    {
        for (auto e : buf)
        {
            const std::int64_t pos = blockStart + e.sampleOffset;
            e.sampleOffset = 0;
            r.events.push_back ({ pos, e });
        }
        if (buf.size() >= buf.capacity())
            ++r.fullBufferBlocks;
        buf.clear();
    };

    double total = 0.0, maxSec = 0.0;
    const auto block = static_cast<std::size_t> (r.blockSize);
    for (std::size_t start = 0; start < numSamples; start += block)
    {
        const auto n = static_cast<int> (std::min (block, numSamples - start));
        const auto t0 = Clock::now();
        engine.process (audio + start, n, buf);
        const double dt = std::chrono::duration<double> (Clock::now() - t0).count();
        total += dt;
        maxSec = std::max (maxSec, dt);
        ++r.numBlocks;
        collect (static_cast<std::int64_t> (start));
    }

    if (options.flushAtEnd)
    {
        engine.flush (buf, 0);
        collect (r.numSamples);
    }

    r.totalProcessSec = total;
    r.meanBlockMicros = r.numBlocks > 0 ? 1e6 * total / r.numBlocks : 0.0;
    r.maxBlockMicros = 1e6 * maxSec;
    r.realtimeFactor = r.durationSec() > 0.0 ? total / r.durationSec() : 0.0;
    r.notes = pairNotes (r.events, sampleRate, r.numSamples);
    return r;
}

RunResult runEngine (IEngine& engine, const std::vector<float>& audio,
                     double sampleRate, const RunOptions& options)
{
    return runEngine (engine, audio.data(), audio.size(), sampleRate, options);
}

} // namespace m2m::test
