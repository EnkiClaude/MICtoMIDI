#include "m2m/beatbox/BeatboxEngine.h"

#include "BeatPatterns.h"
#include "ClassifierTestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using namespace m2m;
using namespace m2m::beatboxtest;

namespace
{
    constexpr double sr = 48000.0;

    struct TimedEvent
    {
        std::int64_t time;
        MidiEvent ev;
        bool operator== (const TimedEvent&) const = default;
    };

    struct RunResult
    {
        std::vector<TimedEvent> events;
        std::vector<HitInfo> hits;
    };

    RunResult run (BeatboxEngine& e, const std::vector<float>& audio, int blockSize, bool flushAtEnd = true)
    {
        RunResult r;
        MidiEventBuffer buf;
        buf.reserve (256);
        std::int64_t t = 0;
        HitInfo h[64];

        for (std::size_t i = 0; i < audio.size(); i += static_cast<std::size_t> (blockSize))
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize), audio.size() - i));
            buf.clear();
            e.process (audio.data() + i, n, buf);
            for (const auto& ev : buf)
            {
                REQUIRE (ev.sampleOffset >= 0);
                REQUIRE (ev.sampleOffset < n);
                MidiEvent norm = ev;
                norm.sampleOffset = 0; // compare in absolute time only
                r.events.push_back ({ t + ev.sampleOffset, norm });
            }
            t += n;
            const int got = e.getRecentHits (h, 64);
            r.hits.insert (r.hits.end(), h, h + got);
        }

        if (flushAtEnd)
        {
            buf.clear();
            e.flush (buf, 0);
            for (const auto& ev : buf)
                r.events.push_back ({ t, ev });
        }
        return r;
    }

    std::vector<int> noteOns (const RunResult& r)
    {
        std::vector<int> notes;
        for (const auto& te : r.events)
            if (te.ev.type == MidiEvent::Type::NoteOn)
                notes.push_back (te.ev.note);
        return notes;
    }

    int gmNote (SynthDrum d)
    {
        switch (d)
        {
            case SynthDrum::Kick:      return 36;
            case SynthDrum::Snare:     return 38;
            case SynthDrum::Clap:      return 39;
            case SynthDrum::ClosedHat: return 42;
            case SynthDrum::OpenHat:   return 46;
            case SynthDrum::Tone440:   break;
        }
        return -1;
    }

    std::unique_ptr<BeatboxEngine> makeEngine (const BeatboxParams& p = {})
    {
        auto e = std::make_unique<BeatboxEngine>();
        e->setParams (p);
        e->prepare (sr, 2048);
        return e;
    }

    std::vector<float> isolated (SynthDrum d, Rng& rng, float db, std::size_t gap = 24000)
    {
        std::vector<float> a (4800, 0.0f);
        mixInto (a, renderDrum (d, sr, rng, db), a.size());
        a.resize (a.size() + gap, 0.0f);
        return a;
    }

    /** Every NoteOn is followed by exactly one NoteOff of the same note, gate later. */
    void checkPairs (const RunResult& r, std::int64_t gate)
    {
        std::vector<bool> used (r.events.size(), false);
        for (std::size_t i = 0; i < r.events.size(); ++i)
        {
            const auto& on = r.events[i];
            if (on.ev.type != MidiEvent::Type::NoteOn)
                continue;
            bool found = false;
            for (std::size_t j = i + 1; j < r.events.size(); ++j)
            {
                const auto& off = r.events[j];
                if (! used[j] && off.ev.type == MidiEvent::Type::NoteOff && off.ev.note == on.ev.note
                    && off.ev.channel == on.ev.channel)
                {
                    used[j] = true;
                    found = true;
                    CHECK (off.time - on.time <= gate);
                    CHECK (off.time >= on.time);
                    break;
                }
            }
            CHECK (found);
        }
        for (std::size_t j = 0; j < r.events.size(); ++j)
            if (r.events[j].ev.type == MidiEvent::Type::NoteOff)
                CHECK (used[j]);

        // events are time-ordered
        for (std::size_t i = 1; i < r.events.size(); ++i)
            CHECK (r.events[i].time >= r.events[i - 1].time);
    }
}

TEST_CASE ("BeatboxEngine: 2-bar kick/snare/hat beat -> 36/38/42", "[beatbox][engine]")
{
    for (double bpm : { 90.0, 120.0, 160.0 })
    {
        Rng rng (static_cast<std::uint64_t> (bpm));
        const auto p = makePattern (sr, bpm, 2, rng, 1.0, -24.0f, -6.0f, false);
        auto e = makeEngine();
        REQUIRE (e->latencySamples() > 0);
        const auto r = run (*e, p.audio, 128);

        std::vector<int> expected;
        for (const auto& h : p.truth)
            expected.push_back (gmNote (h.type));
        const auto got = noteOns (r);

        INFO ("bpm " << bpm);
        CHECK (got.size() == expected.size());
        CHECK (got == expected);
        checkPairs (r, static_cast<std::int64_t> (0.06 * sr) + 1);

        for (const auto& te : r.events)
            CHECK (te.ev.channel == 10);
    }
}

TEST_CASE ("BeatboxEngine: velocity follows hit level", "[beatbox][engine]")
{
    for (auto d : { SynthDrum::Kick, SynthDrum::Snare, SynthDrum::ClosedHat })
    {
        auto e = makeEngine();
        int prev = 0;
        std::vector<int> vels;
        for (int db = -30; db <= -6; db += 3)
        {
            Rng rng (9); // same sound each time, only the level changes
            const auto r = run (*e, isolated (d, rng, static_cast<float> (db)), 256);
            const auto on = noteOns (r);
            REQUIRE (on.size() == 1);
            int v = 0;
            for (const auto& te : r.events)
                if (te.ev.type == MidiEvent::Type::NoteOn)
                    v = te.ev.velocity;
            CHECK (v >= prev);
            prev = v;
            vels.push_back (v);
        }
        INFO (synthName (d));
        CHECK (vels.back() > vels.front() + 40);
        std::printf ("[engine] %s velocity -30..-6 dB:", synthName (d));
        for (int v : vels)
            std::printf (" %d", v);
        std::printf ("\n");
    }
}

TEST_CASE ("BeatboxEngine: no events on silence, low noise or a held tone", "[beatbox][engine]")
{
    auto e = makeEngine();
    std::vector<float> silence (static_cast<std::size_t> (2 * sr), 0.0f);
    CHECK (run (*e, silence, 128).events.empty());

    Rng rng (4);
    CHECK (run (*e, makeNoise (sr, 2.0, -50.0f, rng), 128).events.empty());

    // a 440 Hz tone starting from silence may produce one onset, but it must
    // be rejected by the classifier
    e->reset();
    std::vector<float> tone (static_cast<std::size_t> (2 * sr));
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = 0.25f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 440.0 * static_cast<double> (i) / sr));
    const auto r = run (*e, tone, 128);
    CHECK (noteOns (r).empty());
}

TEST_CASE ("BeatboxEngine: identical events for any block size", "[beatbox][engine]")
{
    Rng rng (555);
    const auto p = makePattern (sr, 150.0, 2, rng); // incl. open hats and claps

    BeatboxParams params;
    params.classifier.enabled = { true, true, true, true, true, true, true }; // toms too
    auto ref = run (*makeEngine (params), p.audio, 1);
    REQUIRE (noteOns (ref).size() > 20);

    for (int bs : { 16, 64, 128, 441, 512, 2048 })
    {
        INFO ("block size " << bs);
        const auto r = run (*makeEngine (params), p.audio, bs);
        CHECK (r.events == ref.events);
        REQUIRE (r.hits.size() == ref.hits.size());
        for (std::size_t i = 0; i < r.hits.size(); ++i)
        {
            CHECK (r.hits[i].onsetSample == ref.hits[i].onsetSample);
            CHECK (r.hits[i].emitSample == ref.hits[i].emitSample);
            CHECK (r.hits[i].features == ref.hits[i].features);
        }
    }
}

TEST_CASE ("BeatboxEngine: latency and timing report", "[beatbox][engine]")
{
    for (float windowMs : { 20.0f, 10.0f })
    {
        BeatboxParams params;
        params.analysisWindowMs = windowMs;

        int correct = 0, total = 0, detected = 0;
        double sumDelay = 0.0, maxDelay = 0.0, sumOnsetErr = 0.0;
        std::int64_t maxEmitMinusEst = 0;
        int latency = 0;

        for (double bpm : { 100.0, 140.0, 180.0 })
        {
            Rng rng (static_cast<std::uint64_t> (bpm) + 17);
            const auto p = makePattern (sr, bpm, 4, rng);
            auto e = makeEngine (params);
            latency = e->latencySamples();
            const auto r = run (*e, p.audio, 128);

            std::vector<std::int64_t> truth, est;
            for (const auto& h : p.truth)
                truth.push_back (h.sample);
            for (const auto& h : r.hits)
                est.push_back (h.onsetSample);
            std::vector<int> match;
            matchOnsets (truth, est, sr, 25.0, &match);

            total += static_cast<int> (truth.size());
            for (std::size_t k = 0; k < r.hits.size(); ++k)
            {
                const auto& h = r.hits[k];
                maxEmitMinusEst = std::max (maxEmitMinusEst, h.emitSample - h.onsetSample);
                if (match[k] < 0)
                    continue;
                const auto& t = p.truth[static_cast<std::size_t> (match[k])];
                ++detected;
                const double delay = double (h.emitSample - t.sample) * 1000.0 / sr;
                sumDelay += delay;
                maxDelay = std::max (maxDelay, delay);
                sumOnsetErr += std::abs (double (h.onsetSample - t.sample)) * 1000.0 / sr;
                if (h.emitted && h.cls == expectedClass (t.type))
                    ++correct;
            }
        }

        std::printf ("[engine] window %.0f ms: latencySamples %d (%.2f ms); onset->NoteOn mean %.2f ms, max %.2f ms; "
                     "onset estimate mean|err| %.2f ms; detected %d/%d; correct class+note %d/%d (%.1f%%)\n",
                     windowMs, latency, latency * 1000.0 / sr, sumDelay / std::max (1, detected), maxDelay,
                     sumOnsetErr / std::max (1, detected), detected, total, correct, total, 100.0 * correct / total);

        CHECK (maxEmitMinusEst <= latency);
        CHECK (maxDelay <= windowMs + 3.0);
        if (windowMs == 20.0f)
            CHECK (double (correct) / total >= 0.9);
    }
}

TEST_CASE ("BeatboxEngine: calibration workflow", "[beatbox][engine]")
{
    BeatboxParams params;
    params.classifierMode = ClassifierMode::Calibrated;
    auto e = makeEngine (params);
    CHECK_FALSE (e->isCalibrationUsable());

    auto takes = [&] (DrumClass cls, SynthDrum sound, int n, std::uint64_t seed)
    {
        Rng rng (seed);
        e->beginCalibration (cls);
        std::vector<float> audio;
        for (int i = 0; i < n; ++i)
        {
            auto a = isolated (sound, rng, static_cast<float> (rng.range (-24.0, -8.0)), 12000);
            audio.insert (audio.end(), a.begin(), a.end());
        }
        const auto r = run (*e, audio, 256, false);
        CHECK (noteOns (r).empty()); // no MIDI while calibrating
        CHECK (e->isCalibrating());
        CHECK (e->calibrationClass() == cls);
        e->endCalibration();
        e->process (audio.data(), 0, *std::make_unique<MidiEventBuffer>()); // apply command
        CHECK_FALSE (e->isCalibrating());
        return r;
    };

    // Deliberately swapped labels: the user's "kick" is the snare-like sound
    // and vice versa, so calibration visibly changes the output.
    takes (DrumClass::Kick, SynthDrum::Snare, 6, 1);
    CHECK (e->calibrationExampleCount (DrumClass::Kick) == 6);
    CHECK_FALSE (e->isCalibrationUsable());
    takes (DrumClass::Snare, SynthDrum::Kick, 6, 2);
    takes (DrumClass::ClosedHat, SynthDrum::ClosedHat, 6, 3);
    CHECK (e->calibrationExampleCount (DrumClass::Snare) == 6);
    CHECK (e->calibrationExampleCount (DrumClass::ClosedHat) == 6);
    CHECK (e->isCalibrationUsable());

    Rng rng (77);
    std::vector<float> test;
    std::vector<int> expectNotes;
    for (int i = 0; i < 10; ++i)
    {
        const SynthDrum d = i % 3 == 0 ? SynthDrum::Kick : i % 3 == 1 ? SynthDrum::Snare : SynthDrum::ClosedHat;
        auto a = isolated (d, rng, -12.0f, 12000);
        test.insert (test.end(), a.begin(), a.end());
        expectNotes.push_back (d == SynthDrum::Kick ? 38 : d == SynthDrum::Snare ? 36 : 42);
    }

    const auto calibratedRun = run (*e, test, 128);
    CHECK (e->usingCalibratedClassifier());
    CHECK (noteOns (calibratedRun) == expectNotes);

    // Rules mode ignores the calibration
    {
        BeatboxParams rp = params;
        rp.classifierMode = ClassifierMode::Rules;
        e->setParams (rp);
        e->reset();
        const auto rulesRun = run (*e, test, 128);
        CHECK_FALSE (e->usingCalibratedClassifier());
        std::vector<int> gm;
        for (int n : expectNotes)
            gm.push_back (n == 38 ? 36 : n == 36 ? 38 : n);
        CHECK (noteOns (rulesRun) == gm);
        e->setParams (params);
    }

    // Cancel restores the previous examples of the class
    {
        Rng r2 (5);
        e->beginCalibration (DrumClass::ClosedHat);
        auto a = isolated (SynthDrum::OpenHat, r2, -12.0f, 12000);
        run (*e, a, 128, false);
        CHECK (e->calibrationExampleCount (DrumClass::ClosedHat) == 1);
        e->cancelCalibration();
        MidiEventBuffer b;
        b.reserve (8);
        e->process (a.data(), 0, b);
        CHECK (e->calibrationExampleCount (DrumClass::ClosedHat) == 6);
    }

    // Save / load through the string form gives identical output
    const std::string saved = e->getCalibrationString();
    REQUIRE (saved.rfind ("m2mcal 1 ", 0) == 0);

    auto e2 = makeEngine (params);
    CHECK_FALSE (e2->setCalibrationString ("garbage"));
    REQUIRE (e2->setCalibrationString (saved));
    CHECK (e2->getCalibrationString() == saved); // visible immediately
    e->reset();
    const auto a1 = run (*e, test, 128);
    const auto a2 = run (*e2, test, 128);
    CHECK (a1.events == a2.events);
    CHECK (e2->calibrationExampleCount (DrumClass::Kick) == 6);

    // Clearing makes it unusable -> falls back to the rule classifier
    e2->clearAllCalibration();
    e2->reset();
    const auto fallback = run (*e2, test, 128);
    CHECK_FALSE (e2->isCalibrationUsable());
    CHECK_FALSE (e2->usingCalibratedClassifier());
    CHECK (noteOns (fallback).size() == expectNotes.size());
}

TEST_CASE ("BeatboxEngine: mapping, channel, gate and telemetry", "[beatbox][engine]")
{
    BeatboxParams p;
    p.mapping.notes[static_cast<std::size_t> (toIndex (DrumClass::Kick))] = 60;
    p.mapping.channel = 3;
    p.gateMs = 30.0f;
    auto e = makeEngine (p);

    Rng rng (12);
    auto a = isolated (SynthDrum::Kick, rng, -10.0f);
    const auto r = run (*e, a, 64);
    REQUIRE (r.events.size() == 2);
    CHECK (r.events[0].ev.note == 60);
    CHECK (r.events[0].ev.channel == 3);
    CHECK (r.events[1].ev.type == MidiEvent::Type::NoteOff);
    CHECK (r.events[1].time - r.events[0].time == static_cast<std::int64_t> (0.03 * sr));
    REQUIRE (r.hits.size() == 1);
    CHECK (r.hits[0].emitted);
    CHECK (r.hits[0].cls == DrumClass::Kick);
    CHECK (std::abs (r.hits[0].onsetSample - 4800) < 48);
    CHECK (e->lastClassification().cls == DrumClass::Kick);
    CHECK (e->lastClassification().accepted);
    CHECK (e->inputLevelDb() < -100.0f); // last block was silent

    // disabled in the mapping: classified, but no MIDI
    p.mapping.enabled[static_cast<std::size_t> (toIndex (DrumClass::Kick))] = false;
    e->setParams (p);
    e->reset();
    const auto r2 = run (*e, a, 64);
    CHECK (r2.events.empty());
    REQUIRE (r2.hits.size() == 1);
    CHECK (r2.hits[0].accepted);
    CHECK_FALSE (r2.hits[0].emitted);

    // flush releases a held note
    p.mapping.enabled.fill (true);
    p.gateMs = 1000.0f;
    e->setParams (p);
    e->reset();
    const auto r3 = run (*e, a, 64, false);
    CHECK (noteOns (r3).size() == 1);
    MidiEventBuffer b;
    b.reserve (8);
    e->flush (b, 5);
    REQUIRE (b.size() == 1);
    CHECK (b[0].type == MidiEvent::Type::NoteOff);
    CHECK (b[0].sampleOffset == 5);
}

TEST_CASE ("BeatboxEngine: CPU benchmark", "[.benchmark][beatbox]")
{
    Rng rng (1);
    const auto p = makePattern (sr, 140.0, 16, rng);
    auto e = makeEngine();
    MidiEventBuffer buf;
    buf.reserve (256);
    HitInfo h[64];

    const int bs = 128;
    const auto t0 = std::chrono::steady_clock::now();
    int blocks = 0;
    for (int rep = 0; rep < 5; ++rep)
        for (std::size_t i = 0; i + bs <= p.audio.size(); i += bs)
        {
            buf.clear();
            e->process (p.audio.data() + i, bs, buf);
            e->getRecentHits (h, 64);
            ++blocks;
        }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / blocks;
    const double budget = bs / sr * 1.0e6;
    std::printf ("[bench] BeatboxEngine: %.2f us per %d-sample block @ 48 kHz (%.2f %% of real time, %d blocks)\n",
                 us, bs, 100.0 * us / budget, blocks);
    CHECK (us < budget);
}
