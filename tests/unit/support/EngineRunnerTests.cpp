#include "m2m/test/EngineRunner.h"
#include "m2m/test/Metrics.h"
#include "m2m/test/Scenarios.h"

#include "m2m/trigger/LevelTriggerEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace m2m::test;
using Catch::Matchers::WithinAbs;
using m2m::MidiEvent;

namespace
{
    /** Emits NoteOn at fixed absolute sample positions, NoteOff 100 samples
        later, plus a held note released only by flush(). */
    class ScriptedEngine final : public m2m::IEngine
    {
    public:
        void prepare (double, int maxBlock) override { prepared = true; maxBlockSize = maxBlock; }
        void reset() noexcept override { pos = 0; ++resets; }

        void process (const float*, int n, m2m::MidiEventBuffer& out) noexcept override
        {
            maxSeen = std::max (maxSeen, n);
            for (int i = 0; i < n; ++i, ++pos)
            {
                if (pos == 1000 || pos == 5000)
                    out.push ({ MidiEvent::Type::NoteOn, 1, 64, 90, i });
                if (pos == 1100 || pos == 5100)
                    out.push ({ MidiEvent::Type::NoteOff, 1, 64, 0, i });
                if (pos == 7000)
                {
                    out.push ({ MidiEvent::Type::NoteOn, 2, 50, 70, i });
                    held = true;
                }
            }
        }

        void flush (m2m::MidiEventBuffer& out, int offset) noexcept override
        {
            if (held)
                out.push ({ MidiEvent::Type::NoteOff, 2, 50, 0, offset });
            held = false;
        }

        int latencySamples() const noexcept override { return 17; }

        bool prepared = false, held = false;
        int maxBlockSize = 0, maxSeen = 0, resets = 0;
        std::int64_t pos = 0;
    };
}

TEST_CASE ("support/EngineRunner: absolute positions, flush and pairing")
{
    for (int block : { 1, 7, 64, 1000, 20000 })
    {
        INFO ("block " << block);
        ScriptedEngine e;
        const std::vector<float> audio (10000, 0.0f);
        const auto r = runEngine (e, audio, 1000.0, { block, 16, true });

        REQUIRE (e.prepared);
        REQUIRE (e.resets == 1);
        REQUIRE (e.maxBlockSize == block);
        REQUIRE (e.maxSeen == std::min (block, 10000));
        REQUIRE (r.latencySamples == 17);
        REQUIRE (r.numBlocks == (10000 + block - 1) / block);
        REQUIRE (r.events.size() == 6);
        REQUIRE (r.events[0].samplePos == 1000);
        REQUIRE (r.events[3].samplePos == 5100);
        REQUIRE (r.events[5].samplePos == 10000); // flush lands at the end

        REQUIRE (r.notes.size() == 3);
        REQUIRE (r.notes[0] == DetectedNote { 1.0, 1.1, 64, 1, 90 });
        REQUIRE (r.notes[1] == DetectedNote { 5.0, 5.1, 64, 1, 90 });
        REQUIRE (r.notes[2] == DetectedNote { 7.0, 10.0, 50, 2, 70 });
        REQUIRE (r.realtimeFactor >= 0.0);
        REQUIRE (r.maxBlockMicros >= r.meanBlockMicros);
    }

    // Without flush the held note ends at the end of the buffer.
    ScriptedEngine e;
    const auto r = runEngine (e, std::vector<float> (8000, 0.0f), 1000.0, { 256, 16, false });
    REQUIRE (r.events.size() == 5);
    REQUIRE (r.notes.back() == DetectedNote { 7.0, 8.0, 50, 2, 70 });
}

TEST_CASE ("support/EngineRunner: pairNotes edge cases")
{
    using T = MidiEvent::Type;
    const std::vector<TimedEvent> ev {
        { 0,   { T::NoteOn,  1, 60, 100, 0 } },
        { 10,  { T::NoteOn,  1, 60, 90, 0 } },   // retrigger closes the first
        { 20,  { T::NoteOff, 1, 60, 0, 0 } },
        { 25,  { T::NoteOff, 1, 60, 0, 0 } },    // unmatched: ignored
        { 30,  { T::NoteOn,  1, 62, 0, 0 } },    // velocity 0 = off, nothing open: ignored
        { 40,  { T::NoteOn,  3, 62, 80, 0 } },   // open at end
    };
    const auto notes = pairNotes (ev, 10.0, 100);
    REQUIRE (notes.size() == 3);
    REQUIRE (notes[0] == DetectedNote { 0.0, 1.0, 60, 1, 100 });
    REQUIRE (notes[1] == DetectedNote { 1.0, 2.0, 60, 1, 90 });
    REQUIRE (notes[2] == DetectedNote { 4.0, 10.0, 62, 3, 80 });

    REQUIRE (pairNotes (ev, 10.0)[2].offsetSec == 4.0); // no end -> zero length
}

TEST_CASE ("support/EngineRunner: LevelTriggerEngine on level_bursts is perfect and block-size independent")
{
    const auto sc = makeScenario ("level_bursts");
    REQUIRE (sc.has_value());

    std::vector<TimedEvent> reference;
    for (int block : { 32, 256, 1024, 100 })
    {
        INFO ("block " << block);
        m2m::LevelTriggerEngine engine;
        const auto r = runEngine (engine, sc->audio, sc->sampleRate, { block, 256, true });
        const auto m = evaluate (sc->notes, r.notes, sc->durationSec());

        REQUIRE (m.f1 == 1.0);
        REQUIRE (m.falseTriggersPerMinute == 0.0);
        REQUIRE (m.meanAbsOnsetErrorMs < 5.0);
        REQUIRE (m.meanSignedOnsetErrorMs >= 0.0); // no look-ahead: never early
        REQUIRE (r.fullBufferBlocks == 0);

        if (reference.empty())
            reference = r.events;
        else
            REQUIRE (r.events == reference);
    }
    REQUIRE (reference.size() == 2 * sc->notes.size());
}
