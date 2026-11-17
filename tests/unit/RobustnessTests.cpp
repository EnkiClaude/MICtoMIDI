// Robustness of both engines against hostile hosts and input: tiny event
// buffers, NaN/Inf/denormal samples, clipping, DC, odd sample rates and
// block sizes, empty process() calls, reset()/prepare() mid-note.
// The invariant everywhere: every NoteOn that reached the host gets a
// NoteOff, a sounding note is never switched on twice, telemetry is finite.

#include "m2m/beatbox/BeatboxEngine.h"
#include "m2m/melody/MelodyEngine.h"
#include "m2m/test/EngineRunner.h"
#include "m2m/test/Metrics.h"
#include "m2m/test/Scenarios.h"
#include "m2m/test/SignalGen.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace m2m;
using namespace m2m::test;

namespace
{
    struct Collected
    {
        std::vector<TimedEvent> events;
        int maxFill = 0;
    };

    /** Runs audio through the engine in blocks with a buffer of the given
        capacity (the engine was prepared by the caller), appending events. */
    void run (IEngine& e, const float* audio, std::size_t n, int block, std::size_t capacity, Collected& c,
              std::int64_t& pos)
    {
        MidiEventBuffer buf;
        buf.reserve (capacity);
        for (std::size_t i = 0; i < n; i += static_cast<std::size_t> (block))
        {
            buf.clear();
            const auto len = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), n - i));
            e.process (audio + i, len, buf);
            for (const auto& ev : buf)
                c.events.push_back ({ pos + ev.sampleOffset, { ev.type, ev.channel, ev.note, ev.velocity, 0 } });
            c.maxFill = std::max (c.maxFill, static_cast<int> (buf.size()));
            pos += len;
        }
    }

    /** flush() until nothing more comes out (a tiny buffer may need several calls). */
    void flushAll (IEngine& e, std::size_t capacity, Collected& c, std::int64_t pos)
    {
        MidiEventBuffer buf;
        buf.reserve (capacity);
        for (int k = 0; k < 64; ++k)
        {
            buf.clear();
            e.flush (buf, 0);
            if (buf.empty())
                return;
            for (const auto& ev : buf)
                c.events.push_back ({ pos + ev.sampleOffset, { ev.type, ev.channel, ev.note, ev.velocity, 0 } });
        }
    }

    /** Every NoteOn is closed by a NoteOff; no NoteOn on a sounding note. */
    std::string pairingProblem (const std::vector<TimedEvent>& events)
    {
        std::map<std::pair<int, int>, bool> on;
        for (const auto& te : events)
        {
            const auto key = std::make_pair (static_cast<int> (te.event.channel), static_cast<int> (te.event.note));
            if (te.event.type == MidiEvent::Type::NoteOn)
            {
                if (on[key])
                    return "NoteOn on sounding note " + std::to_string (key.second) + " at " + std::to_string (te.samplePos);
                on[key] = true;
            }
            else
            {
                on[key] = false;
            }
        }
        for (const auto& [k, v] : on)
            if (v)
                return "stuck note " + std::to_string (k.second);
        return {};
    }

    int countOns (const std::vector<TimedEvent>& events)
    {
        int n = 0;
        for (const auto& te : events)
            n += te.event.type == MidiEvent::Type::NoteOn ? 1 : 0;
        return n;
    }

    bool finite (float v) { return std::isfinite (v); }

    void checkTelemetry (const MelodyEngine& e)
    {
        CHECK (finite (e.currentFrequencyHz()));
        CHECK (finite (e.currentPitchMidi()));
        CHECK (finite (e.currentConfidence()));
        CHECK (finite (e.inputLevelDb()));
    }

    void checkTelemetry (const BeatboxEngine& e)
    {
        CHECK (finite (e.inputLevelDb()));
        CHECK (finite (e.lastClassification().score));
    }

    std::unique_ptr<IEngine> make (bool melody)
    {
        if (melody)
            return std::make_unique<MelodyEngine>();
        auto b = std::make_unique<BeatboxEngine>();
        auto p = b->getParams();
        p.gateMs = 300.0f; // long gates: overlapping and retriggered notes
        b->setParams (p);
        return b;
    }

    const char* melodyScenarios[] = { "chromatic_scale", "major_scale_legato", "repeated_same_pitch", "fast_16ths_legato" };
    const char* beatboxScenarios[] = { "basic_beat_120bpm", "fast_16ths_200bpm", "flams_50ms", "voice_beat_100bpm" };
}

TEST_CASE ("Robustness: tiny MIDI buffers never leave a note stuck", "[robustness]")
{
    for (const bool melody : { true, false })
        for (const char* name : melody ? std::vector<const char*> (std::begin (melodyScenarios), std::end (melodyScenarios))
                                       : std::vector<const char*> (std::begin (beatboxScenarios), std::end (beatboxScenarios)))
            for (const std::size_t capacity : { std::size_t { 1 }, std::size_t { 2 } })
                for (const int block : { 64, 512, 8192 })
                {
                    INFO (name << ", capacity " << capacity << ", block " << block);
                    const auto sc = makeScenario (name);
                    auto e = make (melody);
                    e->prepare (sc->sampleRate, block);
                    Collected c;
                    std::int64_t pos = 0;
                    run (*e, sc->audio.data(), sc->audio.size(), block, capacity, c, pos);
                    flushAll (*e, capacity, c, pos);
                    CHECK (c.maxFill <= static_cast<int> (capacity));
                    CHECK (pairingProblem (c.events).empty());
                    if (block <= 512) // (a 1-event buffer per 170 ms block must drop notes)
                        CHECK (countOns (c.events) >= static_cast<int> (sc->notes.size()) / 2); // still produces notes
                }
}

TEST_CASE ("Robustness: NaN, Inf and denormal input", "[robustness]")
{
    const double sr = 48000.0;
    const auto tone = harmonicTone (sr, 0.5, 220.0, partials::voiceLike(), -12.0);
    const auto beat = makeScenario ("basic_beat_120bpm", sr)->audio;

    std::vector<float> garbage (4096);
    for (std::size_t i = 0; i < garbage.size(); ++i)
    {
        switch (i % 5)
        {
            case 0: garbage[i] = std::numeric_limits<float>::quiet_NaN(); break;
            case 1: garbage[i] = std::numeric_limits<float>::infinity(); break;
            case 2: garbage[i] = -std::numeric_limits<float>::infinity(); break;
            case 3: garbage[i] = std::numeric_limits<float>::denorm_min(); break;
            default: garbage[i] = 1.0e-39f; break;
        }
    }
    const std::vector<float> denormals (48000, 1.0e-41f);

    for (const bool melody : { true, false })
    {
        INFO ((melody ? "melody" : "beatbox"));
        auto e = make (melody);
        e->prepare (sr, 256);
        Collected c;
        std::int64_t pos = 0;
        const auto& music = melody ? tone : beat;

        run (*e, music.data(), music.size(), 256, 64, c, pos);
        run (*e, garbage.data(), garbage.size(), 256, 64, c, pos);
        run (*e, denormals.data(), denormals.size(), 256, 64, c, pos);
        const int before = countOns (c.events);
        run (*e, music.data(), music.size(), 256, 64, c, pos); // recovers: still detects
        CHECK (countOns (c.events) > before);
        flushAll (*e, 64, c, pos);
        CHECK (pairingProblem (c.events).empty());

        if (melody)
            checkTelemetry (static_cast<const MelodyEngine&> (*e));
        else
            checkTelemetry (static_cast<const BeatboxEngine&> (*e));
    }
}

TEST_CASE ("Robustness: +12 dBFS clipped input and DC offset", "[robustness]")
{
    const double sr = 48000.0;

    // Melody: a loud, hard-clipped tone still gives the right note.
    {
        auto loud = harmonicTone (sr, 0.6, 220.0, partials::voiceLike(), 12.0); // peak 4.0
        for (auto& v : loud)
            v = std::clamp (v, -2.5f, 2.5f);
        std::vector<float> audio (static_cast<std::size_t> (0.2 * sr), 0.0f);
        audio.insert (audio.end(), loud.begin(), loud.end());
        audio.resize (audio.size() + static_cast<std::size_t> (0.3 * sr), 0.0f);
        MelodyEngine e;
        const auto r = runEngine (e, audio, sr);
        REQUIRE (r.notes.size() == 1);
        CHECK (r.notes[0].midiNote == 57);
        CHECK (r.notes[0].velocity >= 1);
        CHECK (r.notes[0].velocity <= 127);
        checkTelemetry (e);
    }

    // DC offset under the music: same result as without it; DC alone: nothing.
    for (const bool melody : { true, false })
    {
        INFO ((melody ? "melody" : "beatbox"));
        const auto sc = makeScenario (melody ? "sustained_notes" : "basic_beat_120bpm", sr);
        auto withDc = sc->audio;
        for (auto& v : withDc)
            v += 0.3f;
        auto e1 = make (melody);
        const auto m = evaluate (sc->notes, runEngine (*e1, withDc, sr).notes, sc->durationSec());
        CHECK (m.f1 >= 0.95);

        auto e2 = make (melody);
        const auto dcOnly = runEngine (*e2, std::vector<float> (static_cast<std::size_t> (2.0 * sr), 0.3f), sr);
        CHECK (dcOnly.notes.empty());
    }
}

TEST_CASE ("Robustness: sample rates 22.05 kHz .. 192 kHz", "[robustness]")
{
    for (const double sr : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (const bool melody : { true, false })
        {
            INFO ((melody ? "melody" : "beatbox") << " @ " << sr);
            const auto sc = makeScenario (melody ? "sustained_notes" : "basic_beat_120bpm", sr);
            auto e = make (melody);
            const auto r = runEngine (*e, sc->audio, sr);
            const auto m = evaluate (sc->notes, r.notes, sc->durationSec());
            CHECK (m.f1 >= 0.9);
            CHECK (pairingProblem (r.events).empty());
            CHECK (e->latencySamples() > 0);
            CHECK (e->latencySamples() < static_cast<int> (0.1 * sr));
        }
}

TEST_CASE ("Robustness: block sizes 1 and 8192, empty blocks, blocks larger than prepared", "[robustness]")
{
    const double sr = 48000.0;
    for (const bool melody : { true, false })
    {
        INFO ((melody ? "melody" : "beatbox"));
        const auto sc = makeScenario (melody ? "chromatic_scale" : "basic_beat_120bpm", sr);

        auto ref = make (melody);
        const auto r256 = runEngine (*ref, sc->audio, sr, { 256, 1024, true, {} });

        for (const int block : { 1, 8192 })
        {
            auto e = make (melody);
            const auto r = runEngine (*e, sc->audio, sr, { block, 1024, true, {} });
            CHECK (r.events == r256.events);
        }

        // Prepared for 64-sample blocks, then given 5000 samples at once,
        // and empty / null blocks in between.
        auto e = make (melody);
        e->prepare (sr, 64);
        Collected c;
        std::int64_t pos = 0;
        MidiEventBuffer buf;
        buf.reserve (16);
        e->process (sc->audio.data(), 0, buf);
        e->process (nullptr, 0, buf);
        CHECK (buf.empty());
        run (*e, sc->audio.data(), sc->audio.size(), 5000, 1024, c, pos);
        flushAll (*e, 1024, c, pos);
        CHECK (pairingProblem (c.events).empty());
        CHECK (c.events == r256.events); // same output whatever the block size
    }
}

TEST_CASE ("Robustness: reset() and prepare() mid-note release the note on the next call", "[robustness]")
{
    const double sr = 48000.0;
    const auto tone = harmonicTone (sr, 1.0, 220.0, partials::voiceLike(), -12.0);
    const auto kick = makeScenario ("kick_only", sr)->audio;

    for (const bool melody : { true, false })
        for (const bool viaPrepare : { false, true })
        {
            INFO ((melody ? "melody" : "beatbox") << (viaPrepare ? " prepare" : " reset"));
            auto e = make (melody);
            e->prepare (sr, 256);
            Collected c;
            std::int64_t pos = 0;
            const auto& audio = melody ? tone : kick;
            // Up to the middle of a note (melody: the tone; beatbox: 300 ms
            // gate after the first kick at 0.25 s).
            const std::size_t half = melody ? tone.size() / 2 : static_cast<std::size_t> (0.35 * sr);
            run (*e, audio.data(), half, 256, 64, c, pos);
            REQUIRE (countOns (c.events) == 1);
            REQUIRE_FALSE (pairingProblem (c.events).empty()); // the note is sounding

            if (viaPrepare)
                e->prepare (96000.0, 128); // different rate
            else
                e->reset();

            const std::vector<float> quiet (4096, 0.0f);
            run (*e, quiet.data(), quiet.size(), 128, 64, c, pos);
            CHECK (pairingProblem (c.events).empty()); // released without a flush
            flushAll (*e, 64, c, pos);
            CHECK (pairingProblem (c.events).empty());

            if (melody)
                checkTelemetry (static_cast<const MelodyEngine&> (*e));
            else
                checkTelemetry (static_cast<const BeatboxEngine&> (*e));
        }
}

TEST_CASE ("latencySamples() matches the measured note-on delay", "[latency]")
{
    // The reported latency is a typical value for host compensation; it must
    // be within 4 ms of the mean delay measured on plain material.
    const double sr = 48000.0;
    struct Case { bool melody; PitchAlgorithm algo; const char* scenario; };
    for (const auto& c : { Case { true, PitchAlgorithm::Mpm, "sustained_notes" },
                           Case { true, PitchAlgorithm::Yin, "sustained_notes" },
                           Case { false, PitchAlgorithm::Mpm, "basic_beat_120bpm" } })
    {
        INFO (c.scenario << (c.melody ? (c.algo == PitchAlgorithm::Mpm ? " mpm" : " yin") : ""));
        const auto sc = makeScenario (c.scenario, sr);
        std::unique_ptr<IEngine> e;
        if (c.melody)
        {
            auto m = std::make_unique<MelodyEngine>();
            auto p = m->getParams();
            p.detector = c.algo;
            m->setParams (p);
            e = std::move (m);
        }
        else
        {
            e = std::make_unique<BeatboxEngine>();
        }
        const auto r = runEngine (*e, sc->audio, sr);
        const auto m = evaluate (sc->notes, r.notes, sc->durationSec());
        REQUIRE (m.truePositives > 0);
        const double reportedMs = 1000.0 * r.latencySamples / sr;
        INFO ("reported " << reportedMs << " ms, measured " << m.meanSignedOnsetErrorMs << " ms");
        CHECK (std::abs (reportedMs - m.meanSignedOnsetErrorMs) <= 4.0);
    }
}
