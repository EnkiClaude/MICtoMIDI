#include "m2m/test/Drums.h"
#include "m2m/test/SignalGen.h"
#include "SupportTestUtil.h"

#include "m2m/audio/MathUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace m2m::test;
using Catch::Matchers::WithinAbs;
using namespace m2m::test::util;

namespace
{
    constexpr double sr = 48000.0;
}

TEST_CASE ("support/Rng: pinned sequence (platform independent)")
{
    Rng a (1), b (1), c (2);
    const std::uint64_t first = a.nextU64();
    REQUIRE (first == b.nextU64());
    REQUIRE (first != c.nextU64());

    // Pinned values: the generator is integer-only, so these must hold on
    // every platform. If they change, all noise-based scenarios change too.
    Rng pinned (12345);
    const std::uint64_t v0 = pinned.nextU64();
    const std::uint64_t v1 = pinned.nextU64();
    Rng again (12345);
    REQUIRE (again.nextU64() == v0);
    REQUIRE (again.nextU64() == v1);
    REQUIRE (v0 == 0xBE6A36374160D49Bull);
    REQUIRE (v1 == 0x214AAA0637A688C6ull);

    for (int i = 0; i < 1000; ++i)
    {
        const double u = pinned.uniform();
        REQUIRE (u >= 0.0);
        REQUIRE (u < 1.0);
    }
}

TEST_CASE ("support/SignalGen: sine frequency and level")
{
    for (double hz : { 82.41, 220.0, 440.0, 1000.0, 3520.0 })
    {
        const auto s = sine (sr, 1.0, hz, -12.0);
        REQUIRE (s.size() == 48000);
        REQUIRE_THAT (zeroCrossingFrequency (s, sr), WithinAbs (hz, 0.05 * hz / 100.0 + 0.1));
        REQUIRE_THAT (gainToDb (peak (s)), WithinAbs (-12.0, 0.01));
        REQUIRE_THAT (gainToDb (rms (s)), WithinAbs (-12.0 - 3.0103, 0.05));
    }
}

TEST_CASE ("support/SignalGen: harmonic tone spectrum")
{
    using namespace m2m::test::util;
    const int n = 8192;
    const double f0 = m2m::midiToHz (57.0); // 220 Hz

    const auto voice = harmonicTone (sr, 1.0, f0, partials::voiceLike(), -6.0);
    const auto pv = powerSpectrum (voice, n);
    REQUIRE_THAT (peakFrequency (pv, sr, n), WithinAbs (f0, 0.5));
    REQUIRE_THAT (gainToDb (peak (voice)), WithinAbs (-6.0, 0.01));

    // Weak fundamental: the strongest partial is the 2nd harmonic, but the
    // fundamental is still present ~20 dB down.
    const auto weak = harmonicTone (sr, 1.0, f0, partials::weakFundamental(), -6.0);
    const auto pw = powerSpectrum (weak, n);
    REQUIRE_THAT (peakFrequency (pw, sr, n), WithinAbs (2.0 * f0, 1.0));
    const double fund = bandPower (pw, sr, n, f0 - 20.0, f0 + 20.0);
    const double second = bandPower (pw, sr, n, 2 * f0 - 20.0, 2 * f0 + 20.0);
    REQUIRE_THAT (10.0 * std::log10 (second / fund), WithinAbs (20.0, 0.5));

    // Partials above Nyquist are dropped (no aliasing): 5 kHz tone at 12 kHz rate.
    const auto high = harmonicTone (12000.0, 0.5, 5000.0, partials::voiceLike(), -6.0);
    REQUIRE_THAT (zeroCrossingFrequency (high, 12000.0), WithinAbs (5000.0, 5.0));
}

TEST_CASE ("support/SignalGen: vibrato and glide pitch tracks")
{
    using namespace m2m::test::util;

    // Vibrato: mean frequency over whole cycles equals the centre; the
    // instantaneous frequency swings by +-depth.
    const auto v = vibratoTone (sr, 2.0, 440.0, 50.0, 5.0, partials::pure(), -6.0);
    REQUIRE_THAT (zeroCrossingFrequency (v, sr), WithinAbs (440.0, 1.0));
    const auto seg = [&v] (double t0, double t1)
    {
        return std::vector<float> (v.begin() + static_cast<long> (t0 * sr), v.begin() + static_cast<long> (t1 * sr));
    };
    // Around the vibrato maximum (t = 0.05 s) and minimum (t = 0.15 s).
    REQUIRE (zeroCrossingFrequency (seg (0.03, 0.07), sr) > 440.0 * std::exp2 (35.0 / 1200.0));
    REQUIRE (zeroCrossingFrequency (seg (0.13, 0.17), sr) < 440.0 * std::exp2 (-35.0 / 1200.0));

    // Glide: holds before and after are at the right pitches.
    const auto g = glideTone (sr, 220.0, 330.0, 0.5, 0.2, 0.5, partials::pure(), -6.0);
    REQUIRE (g.size() == static_cast<std::size_t> (1.2 * sr));
    const std::vector<float> before (g.begin(), g.begin() + static_cast<long> (0.5 * sr));
    const std::vector<float> after (g.end() - static_cast<long> (0.5 * sr), g.end());
    REQUIRE_THAT (zeroCrossingFrequency (before, sr), WithinAbs (220.0, 0.5));
    REQUIRE_THAT (zeroCrossingFrequency (after, sr), WithinAbs (330.0, 0.5));
}

TEST_CASE ("support/SignalGen: envelope shape")
{
    std::vector<float> ones (1000, 1.0f);
    applyEnvelope (ones, 1000.0, { 0.1, 0.1, 0.5, 0.2 });
    REQUIRE (ones[0] == 0.0f);
    REQUIRE_THAT (ones[50], WithinAbs (0.5, 1e-6));
    REQUIRE_THAT (ones[100], WithinAbs (1.0, 1e-6));
    REQUIRE_THAT (ones[150], WithinAbs (0.75, 1e-6));
    REQUIRE_THAT (ones[500], WithinAbs (0.5, 1e-6));
    REQUIRE (ones[999] > 0.0f);
    REQUIRE (ones[999] < 0.01f);
    REQUIRE_THAT (ones[900], WithinAbs (0.5 * 100.0 / 201.0, 1e-6));
}

TEST_CASE ("support/SignalGen: noise is deterministic, levelled and coloured")
{
    using namespace m2m::test::util;

    const auto w1 = whiteNoise (sr, 2.0, -20.0, 42);
    const auto w2 = whiteNoise (sr, 2.0, -20.0, 42);
    const auto w3 = whiteNoise (sr, 2.0, -20.0, 43);
    REQUIRE (w1 == w2);
    REQUIRE (w1 != w3);
    REQUIRE_THAT (gainToDb (rms (w1)), WithinAbs (-20.0, 0.01));

    const auto p1 = pinkNoise (sr, 4.0, -20.0, 7);
    REQUIRE (p1 == pinkNoise (sr, 4.0, -20.0, 7));
    REQUIRE_THAT (gainToDb (rms (p1)), WithinAbs (-20.0, 0.01));

    // White: equal power per Hz -> the 3.2-6.4 kHz octave has 16x the power
    // of the 200-400 Hz octave. Pink: equal power per octave.
    const int n = 4096;
    const auto pw = powerSpectrum (w1, n);
    const auto pp = powerSpectrum (p1, n);
    const double whiteRatio = 10.0 * std::log10 (bandPower (pw, sr, n, 3200, 6400) / bandPower (pw, sr, n, 200, 400));
    const double pinkRatio  = 10.0 * std::log10 (bandPower (pp, sr, n, 3200, 6400) / bandPower (pp, sr, n, 200, 400));
    REQUIRE_THAT (whiteRatio, WithinAbs (12.04, 1.0));
    REQUIRE_THAT (pinkRatio, WithinAbs (0.0, 1.5));
}

TEST_CASE ("support/SignalGen: mixWithSnr and assembly helpers")
{
    const auto s = sine (sr, 1.0, 440.0, -6.0);
    const auto n = whiteNoise (sr, 0.3, -20.0, 1); // shorter: gets repeated
    const auto mixed = mixWithSnr (s, n, 20.0);
    REQUIRE (mixed.size() == s.size());

    std::vector<float> noisePart (mixed.size());
    for (std::size_t i = 0; i < mixed.size(); ++i)
        noisePart[i] = mixed[i] - s[i];
    REQUIRE_THAT (20.0 * std::log10 (rms (s) / rms (noisePart)), WithinAbs (20.0, 0.05));

    // Explicit reference RMS.
    const auto mixed2 = mixWithSnr (s, n, 10.0, 0.1);
    for (std::size_t i = 0; i < mixed.size(); ++i)
        noisePart[i] = mixed2[i] - s[i];
    REQUIRE_THAT (20.0 * std::log10 (0.1 / rms (noisePart)), WithinAbs (10.0, 0.05));

    std::vector<float> buf = silence (sr, 0.5);
    REQUIRE (buf.size() == 24000);
    REQUIRE (peak (buf) == 0.0);
    mixInto (buf, std::vector<float> (10, 0.5f), 23995);
    REQUIRE (buf.size() == 24005);
    REQUIRE (buf[23995] == 0.5f);
    append (buf, { 1.0f });
    REQUIRE (buf.back() == 1.0f);
}

TEST_CASE ("support/Drums: GM notes, levels, determinism")
{
    REQUIRE (gmNote (DrumKind::Kick) == 36);
    REQUIRE (gmNote (DrumKind::Snare) == 38);
    REQUIRE (gmNote (DrumKind::Clap) == 39);
    REQUIRE (gmNote (DrumKind::ClosedHat) == 42);
    REQUIRE (gmNote (DrumKind::OpenHat) == 46);

    for (auto k : { DrumKind::Kick, DrumKind::Snare, DrumKind::Clap, DrumKind::ClosedHat, DrumKind::OpenHat })
    {
        INFO (drumName (k));
        const auto a = drum (k, sr, { -12.0, 0.0, 5 });
        REQUIRE (a == drum (k, sr, { -12.0, 0.0, 5 }));
        REQUIRE (a != drum (k, sr, { -12.0, 0.0, 6 }));
        REQUIRE_THAT (gainToDb (peak (a)), WithinAbs (-12.0, 0.01));

        // The sound starts at sample 0 (the truth onset): energy in the first 5 ms.
        REQUIRE (rms (a.data(), static_cast<std::size_t> (0.005 * sr)) > 0.01 * peak (a));

        // Variation changes the sound but stays within +-2 dB of the level.
        const auto v = drum (k, sr, { -12.0, 1.0, 5 });
        REQUIRE (v != a);
        REQUIRE_THAT (gainToDb (peak (v)), WithinAbs (-12.0, 2.01));
    }
}

TEST_CASE ("support/Drums: spectral character")
{
    using namespace m2m::test::util;
    const int n = 1024;
    const auto share = [&] (const std::vector<float>& x, double lo, double hi)
    {
        const auto p = powerSpectrum (x, n);
        return bandPower (p, sr, n, lo, hi) / bandPower (p, sr, n, 0.0, sr / 2);
    };

    const auto k = kick (sr);
    REQUIRE (share (k, 0, 250) > 0.8);

    const auto hc = closedHat (sr);
    const auto ho = openHat (sr);
    REQUIRE (share (hc, 5000, 24000) > 0.9);
    REQUIRE (share (ho, 5000, 24000) > 0.9);

    // Closed hat ~30 ms, open hat ~250 ms (time to fall 30 dB below peak).
    const auto decayTime = [] (const std::vector<float>& x)
    {
        const float thr = static_cast<float> (peak (x) * std::pow (10.0, -30.0 / 20.0));
        std::size_t last = 0;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > thr)
                last = i;
        return static_cast<double> (last) / sr;
    };
    REQUIRE (decayTime (hc) > 0.015);
    REQUIRE (decayTime (hc) < 0.045);
    REQUIRE (decayTime (ho) > 0.15);
    REQUIRE (decayTime (ho) < 0.35);

    const auto c = clap (sr);
    REQUIRE (share (c, 800, 5000) > 0.8);

    // Snare: 200 Hz tone plus broadband noise.
    const auto s = snare (sr);
    REQUIRE (share (s, 150, 250) > 0.1);
    REQUIRE (share (s, 1000, 8000) > 0.3);
}
