#pragma once

// Synthetic drum-like test signals for the beatbox tests (deterministic, seeded).
// These are crude stand-ins for vocal percussion: they are easier to separate
// than real voices and exist to check the DSP plumbing and the decision logic.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace m2m::beatboxtest
{

/** Small seeded PRNG (splitmix64) so results never depend on the std library. */
class Rng
{
public:
    explicit Rng (std::uint64_t seed) : state (seed) {}

    std::uint64_t next() noexcept
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /** Uniform in [0, 1). */
    double uniform() noexcept { return static_cast<double> (next() >> 11) * 0x1.0p-53; }

    /** Uniform in [lo, hi). */
    double range (double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }

    /** White noise sample in [-1, 1). */
    float noise() noexcept { return static_cast<float> (2.0 * uniform() - 1.0); }

    /** Standard normal sample (Box-Muller). */
    float gauss() noexcept
    {
        const double u1 = std::max (uniform(), 1.0e-300), u2 = uniform();
        return static_cast<float> (std::sqrt (-2.0 * std::log (u1)) * std::cos (2.0 * std::numbers::pi * u2));
    }

    int intRange (int lo, int hiInclusive) noexcept
    {
        return lo + static_cast<int> (uniform() * (hiInclusive - lo + 1));
    }

private:
    std::uint64_t state;
};

/** RBJ biquad for shaping test noise. */
class Biquad
{
public:
    static Biquad highPass (double sr, double hz, double q = 0.7071) { return make (sr, hz, q, 0); }
    static Biquad lowPass (double sr, double hz, double q = 0.7071)  { return make (sr, hz, q, 1); }
    static Biquad bandPass (double sr, double hz, double q)          { return make (sr, hz, q, 2); }

    float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return static_cast<float> (y);
    }

private:
    static Biquad make (double sr, double hz, double q, int type)
    {
        const double w0 = 2.0 * std::numbers::pi * std::min (hz, 0.45 * sr) / sr;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        if (type == 0)      { f.b0 = (1 + c) / 2; f.b1 = -(1 + c); f.b2 = (1 + c) / 2; }
        else if (type == 1) { f.b0 = (1 - c) / 2; f.b1 = 1 - c;    f.b2 = (1 - c) / 2; }
        else                { f.b0 = alpha;       f.b1 = 0;        f.b2 = -alpha; }
        f.b0 /= a0; f.b1 /= a0; f.b2 /= a0;
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;
};

enum class SynthDrum { Kick, Snare, Clap, ClosedHat, OpenHat, Tone440 };

inline const char* synthName (SynthDrum d)
{
    switch (d)
    {
        case SynthDrum::Kick:      return "kick";
        case SynthDrum::Snare:     return "snare";
        case SynthDrum::Clap:      return "clap";
        case SynthDrum::ClosedHat: return "closedhat";
        case SynthDrum::OpenHat:   return "openhat";
        case SynthDrum::Tone440:   return "tone440";
    }
    return "?";
}

inline float dbToGainT (float db) { return std::pow (10.0f, db / 20.0f); }

/** Normalise so the absolute peak equals peakDb (dBFS). */
inline void normalisePeak (std::vector<float>& s, float peakDb)
{
    float peak = 0.0f;
    for (float v : s)
        peak = std::max (peak, std::abs (v));
    if (peak <= 0.0f)
        return;
    const float g = dbToGainT (peakDb) / peak;
    for (auto& v : s)
        v *= g;
}

/** Render one hit (sound starts at index 0, first sample may be non-zero).
    'variation' in 0..1 scales the random per-hit variation of pitch, length
    and timbre. Level is set separately (peak normalised to peakDb). */
inline std::vector<float> renderDrum (SynthDrum type, double sr, Rng& rng, float peakDb,
                                      double variation = 1.0)
{
    auto vary = [&] (double amount) { return 1.0 + variation * rng.range (-amount, amount); };
    const double twoPi = 2.0 * std::numbers::pi;
    std::vector<float> s;

    switch (type)
    {
        case SynthDrum::Kick:
        {
            // pitch-dropping sine 150 -> 50 Hz + short click
            const double len = 0.25 * vary (0.2);
            const double fStart = 150.0 * vary (0.15), fEnd = 50.0 * vary (0.15);
            const double pitchTau = 0.03 * vary (0.3), ampTau = 0.12 * vary (0.3);
            const double clickLevel = 0.3 * vary (0.5);
            s.resize (static_cast<std::size_t> (len * sr));
            Biquad clickLp = Biquad::lowPass (sr, 3000.0 * vary (0.3));
            double phase = 0.0;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double f = fEnd + (fStart - fEnd) * std::exp (-t / pitchTau);
                phase += twoPi * f / sr;
                const double atk = std::min (1.0, t / 0.001);
                double v = atk * std::exp (-t / ampTau) * std::sin (phase);
                if (t < 0.003)
                    v += clickLevel * clickLp.process (rng.noise()) * (1.0 - t / 0.003);
                s[i] = static_cast<float> (v);
            }
            break;
        }

        case SynthDrum::Snare:
        {
            // ~200 Hz tone + band-passed noise
            const double len = 0.2 * vary (0.2);
            const double f = 200.0 * vary (0.12);
            const double toneTau = 0.04 * vary (0.3), noiseTau = 0.06 * vary (0.3);
            const double noiseLevel = 0.8 * vary (0.3), toneLevel = 0.6 * vary (0.3);
            Biquad bp = Biquad::bandPass (sr, 2500.0 * vary (0.3), 0.6);
            s.resize (static_cast<std::size_t> (len * sr));
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double atk = std::min (1.0, t / 0.0005);
                const double tone = toneLevel * std::exp (-t / toneTau) * std::sin (twoPi * f * t);
                const double nz = noiseLevel * std::exp (-t / noiseTau) * bp.process (rng.noise()) * 2.0;
                s[i] = static_cast<float> (atk * (tone + nz));
            }
            break;
        }

        case SynthDrum::ClosedHat:
        case SynthDrum::OpenHat:
        {
            // high-passed noise; closed ~30 ms, open ~250 ms
            const bool open = type == SynthDrum::OpenHat;
            const double tau = (open ? 0.08 : 0.008) * vary (0.25);
            const double len = open ? 0.3 * vary (0.15) : 0.035 * vary (0.15);
            Biquad hp1 = Biquad::highPass (sr, 6000.0 * vary (0.2));
            Biquad hp2 = Biquad::highPass (sr, 6000.0 * vary (0.2));
            s.resize (static_cast<std::size_t> (len * sr));
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double atk = std::min (1.0, t / 0.0005);
                s[i] = static_cast<float> (atk * std::exp (-t / tau) * hp2.process (hp1.process (rng.noise())));
            }
            break;
        }

        case SynthDrum::Clap:
        {
            // 3-4 band-passed noise bursts ~10 ms apart (1-4 kHz), last one with a tail
            const int bursts = rng.intRange (3, 4);
            const double spacing = 0.0095 * vary (0.1);
            const double tailTau = 0.025 * vary (0.3);
            const double len = spacing * bursts + 0.12;
            Biquad bp = Biquad::bandPass (sr, 2000.0 * vary (0.2), 0.8);
            s.resize (static_cast<std::size_t> (len * sr));
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const int b = std::min (bursts - 1, static_cast<int> (t / spacing));
                const double tb = t - b * spacing;
                const double tau = (b == bursts - 1) ? tailTau : 0.0025;
                const double gain = (b == bursts - 1) ? 1.0 : 0.8;
                const double atk = std::min (1.0, tb / 0.0003);
                s[i] = static_cast<float> (gain * atk * std::exp (-tb / tau) * bp.process (rng.noise()));
            }
            break;
        }

        case SynthDrum::Tone440:
        {
            const double len = 0.5;
            s.resize (static_cast<std::size_t> (len * sr));
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                s[i] = static_cast<float> (std::min (1.0, t / 0.002) * std::sin (twoPi * 440.0 * t));
            }
            break;
        }
    }

    // 5 ms fade-out so the end of a sound is not a click (a hard cut of a
    // ringing kick is a genuine broadband transient).
    const auto fade = std::min (s.size(), static_cast<std::size_t> (0.005 * sr));
    for (std::size_t k = 0; k < fade; ++k)
        s[s.size() - 1 - k] *= static_cast<float> (k) / static_cast<float> (fade);

    normalisePeak (s, peakDb);
    return s;
}

/** Add 'src' into 'dst' starting at 'start' (dst grows if needed). */
inline void mixInto (std::vector<float>& dst, const std::vector<float>& src, std::size_t start)
{
    if (dst.size() < start + src.size())
        dst.resize (start + src.size(), 0.0f);
    for (std::size_t i = 0; i < src.size(); ++i)
        dst[start + i] += src[i];
}

/** Gaussian white noise at the given RMS level, with a linear fade-in (a
    noise floor does not start with a click). */
inline std::vector<float> makeNoise (double sr, double seconds, float rmsDb, Rng& rng, double fadeInS = 0.2)
{
    std::vector<float> s (static_cast<std::size_t> (seconds * sr));
    const float g = dbToGainT (rmsDb);
    const auto fade = static_cast<std::size_t> (fadeInS * sr);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = g * rng.gauss() * (i < fade ? static_cast<float> (i) / static_cast<float> (fade) : 1.0f);
    return s;
}

struct TruthHit
{
    std::int64_t sample;
    SynthDrum type;
    float peakDb;
};

} // namespace m2m::beatboxtest
