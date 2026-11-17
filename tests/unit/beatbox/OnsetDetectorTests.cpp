#include "m2m/onset/OnsetDetector.h"

#include "BeatPatterns.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <vector>

using namespace m2m::beatboxtest;

namespace
{
    constexpr double sr = 48000.0;

    std::vector<std::int64_t> detect (const std::vector<float>& audio, int blockSize,
                                      const m2m::OnsetParams& params = {}, double rate = sr)
    {
        m2m::OnsetDetector det;
        det.prepare (rate);
        det.setParams (params);

        std::vector<std::int64_t> onsets;
        m2m::OnsetEvent evs[16];
        for (std::size_t i = 0; i < audio.size(); i += static_cast<std::size_t> (blockSize))
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize), audio.size() - i));
            const int got = det.process (audio.data() + i, n, evs, 16);
            for (int k = 0; k < got; ++k)
                onsets.push_back (evs[k].samplePosition);
        }
        return onsets;
    }

    std::vector<std::int64_t> truthSamples (const Pattern& p)
    {
        std::vector<std::int64_t> t;
        for (const auto& h : p.truth)
            t.push_back (h.sample);
        return t;
    }
}

TEST_CASE ("OnsetDetector: mixed synthetic patterns 90-180 BPM", "[onset][beatbox]")
{
    Rng rng (1234);
    MatchStats total;

    for (double bpm : { 90.0, 110.0, 130.0, 150.0, 180.0 })
    {
        for (int rep = 0; rep < 3; ++rep)
        {
            const auto p = makePattern (sr, bpm, 4, rng);
            const auto det = detect (p.audio, 128);
            const auto s = matchOnsets (truthSamples (p), det, sr, 25.0);
            total.add (s);
        }
    }

    std::printf ("[onset] mixed patterns: hits=%d TP=%d FP=%d FN=%d  P=%.4f R=%.4f F1=%.4f  "
                 "mean|err|=%.2f ms  meanErr=%.2f ms  max|err|=%.2f ms\n",
                 total.truePositives + total.falseNegatives, total.truePositives, total.falsePositives,
                 total.falseNegatives, total.precision(), total.recall(), total.f1(),
                 total.meanAbsErrMs(), total.meanErrMs(), total.maxAbsErrMs);

    CHECK (total.f1() >= 0.95);
    CHECK (total.meanAbsErrMs() <= 5.0);
}

TEST_CASE ("OnsetDetector: patterns over a -40 dBFS noise floor", "[onset][beatbox]")
{
    Rng rng (4321);
    MatchStats total;
    for (double bpm : { 100.0, 140.0, 180.0 })
    {
        auto p = makePattern (sr, bpm, 4, rng);
        const auto noise = makeNoise (sr, static_cast<double> (p.audio.size()) / sr, -40.0f, rng);
        for (std::size_t i = 0; i < p.audio.size(); ++i)
            p.audio[i] += noise[i];
        total.add (matchOnsets (truthSamples (p), detect (p.audio, 128), sr, 25.0));
    }
    std::printf ("[onset] patterns + -40 dBFS noise: TP=%d FP=%d FN=%d F1=%.4f mean|err|=%.2f ms\n",
                 total.truePositives, total.falsePositives, total.falseNegatives, total.f1(), total.meanAbsErrMs());
    CHECK (total.f1() >= 0.95);
    CHECK (total.meanAbsErrMs() <= 5.0);
}

TEST_CASE ("OnsetDetector: per-sound timing and single trigger", "[onset][beatbox]")
{
    Rng rng (77);

    for (auto type : { SynthDrum::Kick, SynthDrum::Snare, SynthDrum::Clap, SynthDrum::ClosedHat, SynthDrum::OpenHat })
    {
        MatchStats s;
        for (int i = 0; i < 40; ++i)
        {
            // isolated hits, 600 ms apart, so long open hats ring out fully
            std::vector<float> audio (static_cast<std::size_t> (0.1 * sr), 0.0f);
            std::vector<std::int64_t> truth;
            for (int k = 0; k < 3; ++k)
            {
                const auto start = audio.size() + static_cast<std::size_t> (rng.range (0, 64));
                mixInto (audio, renderDrum (type, sr, rng, static_cast<float> (rng.range (-30.0, -6.0))), start);
                audio.resize (start + static_cast<std::size_t> (0.6 * sr), 0.0f);
                truth.push_back (static_cast<std::int64_t> (start));
            }
            s.add (matchOnsets (truth, detect (audio, 64), sr, 25.0));
        }

        std::printf ("[onset] %-9s isolated: TP=%d FP=%d FN=%d  mean|err|=%.2f ms meanErr=%.2f ms max|err|=%.2f ms\n",
                     synthName (type), s.truePositives, s.falsePositives, s.falseNegatives,
                     s.meanAbsErrMs(), s.meanErrMs(), s.maxAbsErrMs);
        INFO (synthName (type));
        CHECK (s.falsePositives == 0); // no double triggers (a clap is ONE onset)
        CHECK (s.falseNegatives == 0);
        CHECK (s.meanAbsErrMs() <= 5.0);
    }
}

TEST_CASE ("OnsetDetector: no onsets on silence, low noise or a steady tone", "[onset][beatbox]")
{
    const auto n = static_cast<std::size_t> (3.0 * sr);

    SECTION ("silence")
    {
        std::vector<float> s (n, 0.0f);
        CHECK (detect (s, 128).empty());
    }

    SECTION ("-50 dBFS white noise")
    {
        Rng rng (5);
        const auto s = makeNoise (sr, 3.0, -50.0f, rng);
        CHECK (detect (s, 128).empty());

        // also without the absolute level floor: the adaptive threshold alone
        m2m::OnsetParams noFloor;
        noFloor.minLevelDb = -120.0f;
        const auto d = detect (s, 128, noFloor);
        std::printf ("[onset] -50 dBFS noise, no level floor: %zu onsets in 3 s\n", d.size());
        CHECK (d.empty());

        // louder steady noise is above the floor, so this is the adaptive threshold
        for (float db : { -30.0f, -10.0f })
        {
            Rng rng2 (6);
            INFO (db << " dBFS noise");
            CHECK (detect (makeNoise (sr, 3.0, db, rng2), 128).empty());
        }
    }

    SECTION ("steady 440 Hz tone: at most the start, nothing afterwards")
    {
        std::vector<float> s (n);
        const float g = dbToGainT (-12.0f);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            s[i] = g * static_cast<float> (std::min (1.0, t / 0.002) * std::sin (2.0 * std::numbers::pi * 440.0 * t));
        }
        const auto det = detect (s, 128);
        CHECK (det.size() <= 1);
        for (auto d : det)
            CHECK (d < static_cast<std::int64_t> (0.05 * sr));
    }
}

TEST_CASE ("OnsetDetector: identical output for any block size", "[onset][beatbox]")
{
    Rng rng (99);
    const auto p = makePattern (sr, 140.0, 2, rng);
    const auto ref = detect (p.audio, 1);
    REQUIRE (! ref.empty());
    for (int bs : { 7, 16, 64, 128, 441, 512, 4096 })
        CHECK (detect (p.audio, bs) == ref);
}

TEST_CASE ("OnsetDetector: works at 44.1 and 96 kHz", "[onset][beatbox]")
{
    for (double rate : { 44100.0, 96000.0 })
    {
        Rng rng (2024);
        const auto p = makePattern (rate, 120.0, 2, rng);
        std::vector<std::int64_t> truth;
        for (const auto& h : p.truth)
            truth.push_back (h.sample);
        const auto s = matchOnsets (truth, detect (p.audio, 256, {}, rate), rate, 25.0);
        std::printf ("[onset] %.0f Hz: F1=%.4f mean|err|=%.2f ms\n", rate, s.f1(), s.meanAbsErrMs());
        CHECK (s.f1() >= 0.95);
        CHECK (s.meanAbsErrMs() <= 5.0);
    }
}

TEST_CASE ("OnsetDetector: refractory period and level floor", "[onset][beatbox]")
{
    Rng rng (3);
    std::vector<float> audio (static_cast<std::size_t> (0.1 * sr), 0.0f);
    // two closed hats 25 ms apart: the second falls inside the 40 ms refractory period
    const auto a = audio.size();
    mixInto (audio, renderDrum (SynthDrum::ClosedHat, sr, rng, -12.0f), a);
    mixInto (audio, renderDrum (SynthDrum::ClosedHat, sr, rng, -12.0f), a + static_cast<std::size_t> (0.025 * sr));
    audio.resize (audio.size() + static_cast<std::size_t> (0.3 * sr), 0.0f);
    CHECK (detect (audio, 64).size() == 1);

    m2m::OnsetParams shortRefractory;
    shortRefractory.refractoryMs = 15.0f;
    CHECK (detect (audio, 64, shortRefractory).size() == 2);

    // a -55 dBFS hit is below the default -45 dB floor
    std::vector<float> quiet (static_cast<std::size_t> (0.1 * sr), 0.0f);
    mixInto (quiet, renderDrum (SynthDrum::Snare, sr, rng, -55.0f), quiet.size());
    quiet.resize (quiet.size() + static_cast<std::size_t> (0.3 * sr), 0.0f);
    CHECK (detect (quiet, 64).empty());
    m2m::OnsetParams lowFloor;
    lowFloor.minLevelDb = -70.0f;
    CHECK (detect (quiet, 64, lowFloor).size() == 1);
}
