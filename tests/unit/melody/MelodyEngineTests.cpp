#include "m2m/melody/MelodyEngine.h"
#include "MelodySignals.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace m2m;
using namespace m2m::melodytest;
using Type = MidiEvent::Type;

namespace
{
    constexpr double sr = 48000.0;

    // A voice-like harmonic spectrum.
    const std::vector<double> voice { 1.0, 0.6, 0.4, 0.25, 0.15, 0.1 };

    std::vector<AbsEvent> run (const std::vector<float>& sig, MelodyParams p = {}, int block = 128)
    {
        MelodyEngine e;
        e.setParams (p);
        e.prepare (sr, 2048);
        return runEngine (e, sig, block);
    }

    std::string describe (const std::vector<AbsEvent>& ev)
    {
        std::string s;
        for (const auto& e : ev)
            s += (e.type == Type::NoteOn ? "on " : "off ") + std::to_string (e.note) + "@"
               + std::to_string (static_cast<double> (e.time) / sr * 1000.0).substr (0, 6) + "ms v"
               + std::to_string (e.velocity) + "\n";
        return s;
    }

    std::vector<int> expectAndCheck (const std::vector<AbsEvent>& ev, const std::vector<int>& expected)
    {
        const auto ons = noteOns (ev);
        INFO (describe (ev));
        CHECK (monophonicInvariantsHold (ev));
        CHECK (ons == expected);
        return ons;
    }
}

TEST_CASE ("MelodyEngine: sustained notes give exactly those notes", "[melody]")
{
    const std::vector<int> notes { 45, 52, 57, 60, 64, 69, 76, 81 };
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        Synth s (sr);
        s.setHarmonics (voice);
        s.silence (0.1);
        for (int n : notes)
        {
            s.tone (0.5, midiToHz (n), -12.0f);
            s.silence (0.2);
        }
        MelodyParams p;
        p.detector = algo;
        CAPTURE (static_cast<int> (algo));
        expectAndCheck (run (s.buffer(), p), notes);
    }
}

TEST_CASE ("MelodyEngine: 8-note scale with gaps and legato", "[melody]")
{
    const std::vector<int> scale { 60, 62, 64, 65, 67, 69, 71, 72 };
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        MelodyParams p;
        p.detector = algo;
        CAPTURE (static_cast<int> (algo));

        Synth gaps (sr);
        gaps.setHarmonics (voice);
        gaps.silence (0.1);
        for (int n : scale)
        {
            gaps.tone (0.25, midiToHz (n), -12.0f);
            gaps.silence (0.06);
        }
        expectAndCheck (run (gaps.buffer(), p), scale);

        Synth legato (sr);
        legato.setHarmonics (voice);
        legato.silence (0.1);
        for (int n : scale)
            legato.tone (0.25, midiToHz (n), -12.0f);
        legato.silence (0.2);
        const auto ev = run (legato.buffer(), p);
        expectAndCheck (ev, scale);

        // Legato changes: off and on at the same sample, off first.
        for (std::size_t i = 1; i + 1 < ev.size(); ++i)
            if (ev[i].type == Type::NoteOn && i > 1)
                CHECK ((ev[i - 1].type == Type::NoteOff && ev[i - 1].time == ev[i].time));

        // Descending legato too.
        Synth down (sr);
        down.setHarmonics (voice);
        for (auto it = scale.rbegin(); it != scale.rend(); ++it)
            down.tone (0.25, midiToHz (*it), -12.0f);
        down.silence (0.2);
        expectAndCheck (run (down.buffer(), p), std::vector<int> (scale.rbegin(), scale.rend()));
    }
}

TEST_CASE ("MelodyEngine: vibrato +/-50 cents gives exactly one note", "[melody]")
{
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        for (const double rate : { 5.0, 6.0, 7.0 })
        {
            for (const int n : { 50, 62, 74 })
            {
                Synth s (sr);
                s.setHarmonics (voice);
                s.silence (0.05);
                s.tone (2.0, midiToHz (n), -12.0f, 50.0, rate);
                s.silence (0.2);
                MelodyParams p;
                p.detector = algo;
                CAPTURE (static_cast<int> (algo), rate, n);
                expectAndCheck (run (s.buffer(), p), { n });
            }
        }
    }
}

TEST_CASE ("MelodyEngine: slow glide over an octave is bounded and monophonic", "[melody]")
{
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        for (const bool up : { true, false })
        {
            Synth s (sr);
            s.setHarmonics (voice);
            s.silence (0.05);
            s.glide (3.0, midiToHz (up ? 55 : 67), midiToHz (up ? 67 : 55), -12.0f);
            s.silence (0.2);
            MelodyParams p;
            p.detector = algo;
            const auto ev = run (s.buffer(), p);
            const auto ons = noteOns (ev);
            INFO (describe (ev));
            CAPTURE (static_cast<int> (algo), up, ons.size());
            CHECK (monophonicInvariantsHold (ev));
            CHECK (ons.size() >= 2);
            CHECK (ons.size() <= 13);
            for (int n : ons)
                CHECK ((n >= 55 && n <= 67));
        }
    }
}

TEST_CASE ("MelodyEngine: repeated same pitch retriggers", "[melody]")
{
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        MelodyParams p;
        p.detector = algo;
        CAPTURE (static_cast<int> (algo));

        // x6 with 70 ms silent gaps.
        Synth s (sr);
        s.setHarmonics (voice);
        s.silence (0.05);
        for (int i = 0; i < 6; ++i)
        {
            s.tone (0.18, midiToHz (62), -12.0f);
            s.silence (0.07);
        }
        expectAndCheck (run (s.buffer(), p), std::vector<int> (6, 62));

        // x4 with amplitude dips only (-24 dB for 40 ms, pitch continues):
        // exercises the onset retrigger path.
        Synth d (sr);
        d.setHarmonics (voice);
        d.silence (0.05);
        for (int i = 0; i < 4; ++i)
        {
            d.tone (0.2, midiToHz (57), -10.0f);
            d.tone (0.04, midiToHz (57), -34.0f);
        }
        d.silence (0.2);
        expectAndCheck (run (d.buffer(), p), std::vector<int> (4, 57));

        // Retrigger disabled: one long note through the dips.
        p.retriggerEnabled = false;
        expectAndCheck (run (d.buffer(), p), { 57 });
    }
}

TEST_CASE ("MelodyEngine: silence and -40 dBFS noise give no notes", "[melody]")
{
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        MelodyParams p;
        p.detector = algo;
        std::vector<float> silence (static_cast<std::size_t> (5 * sr), 0.0f);
        CHECK (run (silence, p).empty());

        for (std::uint64_t seed : { 1u, 2u, 3u })
        {
            std::vector<float> noise (static_cast<std::size_t> (5 * sr), 0.0f);
            addNoise (noise, -40.0f, seed);
            const auto ev = run (noise, p);
            INFO (describe (ev));
            CHECK (ev.empty());
        }
    }
}

TEST_CASE ("MelodyEngine: tone plus noise at 20 dB SNR", "[melody]")
{
    const std::vector<int> notes { 48, 55, 60, 67, 72 };
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        Synth s (sr);
        s.setHarmonics (voice);
        s.silence (0.1);
        for (int n : notes)
        {
            s.tone (0.4, midiToHz (n), -12.0f);
            s.silence (0.15);
        }

        // Signal RMS over the tone segments.
        double sum = 0.0;
        std::size_t cnt = 0;
        for (float v : s.buffer())
            if (v != 0.0f)
            {
                sum += static_cast<double> (v) * v;
                ++cnt;
            }
        const float rmsDb = static_cast<float> (10.0 * std::log10 (sum / static_cast<double> (cnt)));
        addNoise (s.buffer(), rmsDb - 20.0f, 77);

        MelodyParams p;
        p.detector = algo;
        CAPTURE (static_cast<int> (algo));
        expectAndCheck (run (s.buffer(), p), notes);
    }
}

TEST_CASE ("MelodyEngine: velocity is non-decreasing with level", "[melody]")
{
    Synth s (sr);
    s.setHarmonics (voice);
    s.silence (0.1);
    std::vector<float> levels;
    for (float db = -30.0f; db <= -6.0f; db += 3.0f)
    {
        levels.push_back (db);
        s.tone (0.3, midiToHz (60), db);
        s.silence (0.2);
    }

    const auto ev = run (s.buffer());
    INFO (describe (ev));
    CHECK (monophonicInvariantsHold (ev));
    std::vector<int> vels;
    for (const auto& e : ev)
        if (e.type == Type::NoteOn)
            vels.push_back (e.velocity);
    REQUIRE (vels.size() == levels.size());
    for (std::size_t i = 1; i < vels.size(); ++i)
        CHECK (vels[i] >= vels[i - 1]);
    CHECK (vels.back() > vels.front() + 40);

    MelodyParams fixed;
    fixed.useFixedVelocity = true;
    fixed.velocity.fixedVelocity = 77;
    for (const auto& e : run (s.buffer(), fixed))
        if (e.type == Type::NoteOn)
            CHECK (e.velocity == 77);
}

TEST_CASE ("MelodyEngine: scale snap, transpose and channel", "[melody]")
{
    // Sung pitches between notes: 61.3 (sharp C#), 60.7 (flat C#), 62.7 (flat D#),
    // 66.4 (sharp F#: nearer G than F in C major).
    Synth s (sr);
    s.setHarmonics (voice);
    s.silence (0.1);
    for (double m : { 61.3, 60.7, 62.7, 66.4 })
    {
        s.tone (0.3, midiToHz (m), -12.0f);
        s.silence (0.15);
    }

    MelodyParams p;
    p.scale = Scale::Major;
    p.scaleRoot = 0;
    expectAndCheck (run (s.buffer(), p), { 62, 60, 62, 67 });

    p.scale = Scale::Off;
    expectAndCheck (run (s.buffer(), p), { 61, 61, 63, 66 });

    p.scale = Scale::MinorPentatonic; // A minor pentatonic: A C D E G
    p.scaleRoot = 9;
    expectAndCheck (run (s.buffer(), p), { 62, 60, 62, 67 });

    p.scale = Scale::Major;
    p.scaleRoot = 0;
    p.transpose = 12;
    p.channel = 5;
    const auto ev = run (s.buffer(), p);
    expectAndCheck (ev, { 74, 72, 74, 79 });
    for (const auto& e : ev)
        CHECK (e.channel == 5);

    p.transpose = -24;
    expectAndCheck (run (s.buffer(), p), { 38, 36, 38, 43 });
}

TEST_CASE ("MelodyEngine: transpose/channel change mid-note releases the started note", "[melody]")
{
    Synth s (sr);
    s.setHarmonics (voice);
    s.tone (0.5, midiToHz (60), -12.0f);

    MelodyEngine e;
    e.prepare (sr, 512);
    MidiEventBuffer buf;
    buf.reserve (64);
    const int half = static_cast<int> (s.buffer().size() / 2);
    e.process (s.buffer().data(), half, buf);
    REQUIRE (e.activeNote() == 60);
    auto p = e.getParams();
    p.transpose = 7;
    p.channel = 3;
    e.setParams (p);
    e.flush (buf, 5);

    REQUIRE (buf.size() == 2);
    CHECK (buf[1].type == Type::NoteOff);
    CHECK (buf[1].note == 60);
    CHECK (buf[1].channel == 1);
    CHECK (buf[1].sampleOffset == 5);
    CHECK (e.activeNote() == -1);

    buf.clear();
    e.flush (buf);
    CHECK (buf.empty());
}

TEST_CASE ("MelodyEngine: output is identical for any block size", "[melody]")
{
    Synth s (sr);
    s.setHarmonics (voice);
    s.silence (0.05);
    for (int n : { 55, 60, 62, 62, 67 })
    {
        s.tone (0.2, midiToHz (n), -14.0f, 30.0, 5.5);
        s.silence (0.05);
    }
    s.glide (0.5, midiToHz (67), midiToHz (60), -10.0f);
    s.silence (0.1);
    addNoise (s.buffer(), -50.0f, 9);

    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        MelodyParams p;
        p.detector = algo;
        const auto reference = run (s.buffer(), p, 128);
        REQUIRE (noteOns (reference).size() >= 5);
        for (int block : { 1, 16, 64, 441, 512, 2048, 4096 })
        {
            CAPTURE (static_cast<int> (algo), block);
            CHECK (run (s.buffer(), p, block) == reference);
        }
    }
}

TEST_CASE ("MelodyEngine: other sample rates", "[melody]")
{
    for (const double rate : { 44100.0, 96000.0 })
    {
        Synth s (rate);
        s.setHarmonics (voice);
        s.silence (0.05);
        for (int n : { 57, 64, 69 })
        {
            s.tone (0.3, midiToHz (n), -12.0f);
            s.silence (0.1);
        }
        MelodyEngine e;
        e.prepare (rate, 256);
        CAPTURE (rate, e.hopSize());
        const auto ev = runEngine (e, s.buffer(), 256);
        CHECK (monophonicInvariantsHold (ev));
        CHECK (noteOns (ev) == std::vector<int> { 57, 64, 69 });
    }
}

namespace
{
    struct LatencyResult
    {
        double meanMs = 0.0, maxMs = 0.0;
        int missed = 0;
    };

    LatencyResult measureLatency (MelodyParams p, int& reportedSamples)
    {
        LatencyResult r;
        int count = 0;
        for (int n = 51; n <= 81; n += 2) // 156 Hz .. 880 Hz
        {
            Synth s (sr);
            s.setHarmonics (voice);
            s.setRampMs (1.0);
            s.silence (0.1);
            const auto onset = static_cast<std::int64_t> (s.position());
            s.tone (0.3, midiToHz (n), -12.0f);
            s.silence (0.1);

            MelodyEngine e;
            e.setParams (p);
            e.prepare (sr, 128);
            reportedSamples = e.latencySamples();
            const auto ev = runEngine (e, s.buffer(), 128);
            if (ev.empty() || ev[0].type != Type::NoteOn || ev[0].note != n)
            {
                ++r.missed;
                continue;
            }
            const double ms = static_cast<double> (ev[0].time - onset) / sr * 1000.0;
            r.meanMs += ms;
            r.maxMs = std::max (r.maxMs, ms);
            ++count;
        }
        r.meanMs /= std::max (1, count);
        return r;
    }
}

TEST_CASE ("MelodyEngine: note-on latency", "[melody][latency]")
{
    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        for (const float minHz : { 100.0f, 80.0f })
        {
            MelodyParams p;
            p.detector = algo;
            p.minHz = minHz;
            int reported = 0;
            const auto r = measureLatency (p, reported);
            std::printf ("[latency] %s minHz %.0f: mean %.2f ms, max %.2f ms, reported %.2f ms, missed %d\n",
                         algo == PitchAlgorithm::Mpm ? "MPM" : "YIN", static_cast<double> (minHz),
                         r.meanMs, r.maxMs, reported / sr * 1000.0, r.missed);
            CAPTURE (static_cast<int> (algo), minHz, r.meanMs, r.maxMs, reported);
            CHECK (r.missed == 0);
            if (minHz >= 100.0f)
                CHECK (r.meanMs <= 30.0);
        }
    }
}

TEST_CASE ("MelodyEngine: processing time per 128-sample block", "[melody][benchmark]")
{
    Synth s (sr);
    s.setHarmonics (voice);
    for (int i = 0; i < 10; ++i)
    {
        s.tone (0.4, midiToHz (55 + i), -12.0f, 30.0, 6.0);
        s.silence (0.1);
    }
    addNoise (s.buffer(), -60.0f, 5);

    for (const auto algo : { PitchAlgorithm::Yin, PitchAlgorithm::Mpm })
    {
        MelodyParams p;
        p.detector = algo;
        MelodyEngine e;
        e.setParams (p);
        e.prepare (sr, 128);
        MidiEventBuffer buf;
        buf.reserve (64);

        const auto& sig = s.buffer();
        const int blocks = static_cast<int> (sig.size() / 128);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            e.process (sig.data() + b * 128, 128, buf);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / blocks;
        const double blockUs = 128.0 / sr * 1.0e6;
        std::printf ("[benchmark] %s: %.2f us per 128-sample block (%.2f%% of real time)\n",
                     algo == PitchAlgorithm::Mpm ? "MPM" : "YIN", us, 100.0 * us / blockUs);
        CHECK (us < blockUs); // very loose: faster than real time even in Debug
    }
}
