#pragma once

// Deterministic test signals shared by the hosting smoke test and the UI
// snapshot tool. Each function appends to `out` at sample rate `sr`.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace m2m::smoke
{

inline constexpr double twoPi = 2.0 * std::numbers::pi;

inline void silence (std::vector<float>& out, double sr, double seconds)
{
    out.insert (out.end(), (size_t) std::lround (seconds * sr), 0.0f);
}

/** Voice-like harmonic tone (5 harmonics, 1/k amplitudes) with 10 ms
    fades and optional vibrato (cents, 5.5 Hz). */
inline void harmonicTone (std::vector<float>& out, double sr, double hz, double seconds,
                          float amplitude = 0.3f, double vibratoCents = 0.0)
{
    const auto n = (size_t) std::lround (seconds * sr);
    const auto fade = (size_t) std::lround (0.01 * sr);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double f = hz * std::pow (2.0, vibratoCents * std::sin (twoPi * 5.5 * t) / 1200.0);
        double x = 0.0;
        for (int k = 1; k <= 5; ++k)
            x += std::sin (twoPi * phase * k) / k;
        phase += f / sr;
        phase -= std::floor (phase);
        double env = 1.0;
        if (i < fade)
            env = (double) i / (double) fade;
        else if (n - i < fade)
            env = (double) (n - i) / (double) fade;
        out.push_back ((float) (amplitude * 0.6 * x * env));
    }
}

/** Voice-like tone whose pitch wanders slowly by +-wanderCents (0.8 Hz,
    starting on pitch) plus a little 7 Hz jitter: an unsteady held note. */
inline void wanderingTone (std::vector<float>& out, double sr, double hz, double seconds,
                           double wanderCents, float amplitude = 0.3f)
{
    const auto n = (size_t) std::lround (seconds * sr);
    const auto fade = (size_t) std::lround (0.01 * sr);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double cents = wanderCents * std::sin (twoPi * 0.8 * t) + 12.0 * std::sin (twoPi * 7.0 * t);
        const double f = hz * std::pow (2.0, cents / 1200.0);
        double x = 0.0;
        for (int k = 1; k <= 5; ++k)
            x += std::sin (twoPi * phase * k) / k;
        phase += f / sr;
        phase -= std::floor (phase);
        double env = 1.0;
        if (i < fade)
            env = (double) i / (double) fade;
        else if (n - i < fade)
            env = (double) (n - i) / (double) fade;
        out.push_back ((float) (amplitude * 0.6 * x * env));
    }
}

/** Small deterministic noise source (xorshift). */
struct Noise
{
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    double next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (double) (s >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }
};

/** RBJ biquad (high-pass / low-pass). */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad make (double sr, double hz, bool highPass)
    {
        const double w = twoPi * hz / sr, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
        const double a0 = 1.0 + al;
        Biquad q;
        if (highPass)
        {
            q.b0 = (1.0 + c) / 2.0 / a0; q.b1 = -(1.0 + c) / a0; q.b2 = q.b0;
        }
        else
        {
            q.b0 = (1.0 - c) / 2.0 / a0; q.b1 = (1.0 - c) / a0; q.b2 = q.b0;
        }
        q.a1 = -2.0 * c / a0;
        q.a2 = (1.0 - al) / a0;
        return q;
    }

    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

inline void normaliseTail (std::vector<float>& out, size_t start, float peak)
{
    float mx = 0.0f;
    for (size_t i = start; i < out.size(); ++i)
        mx = std::max (mx, std::abs (out[i]));
    if (mx > 0.0f)
        for (size_t i = start; i < out.size(); ++i)
            out[i] *= peak / mx;
}

/** Kick-like burst: sine sweeping ~150 -> 50 Hz with a short click, 300 ms. */
inline void kick (std::vector<float>& out, double sr, float peak = 0.5f)
{
    const size_t start = out.size();
    const auto n = (size_t) std::lround (0.3 * sr);
    const auto click = (size_t) std::lround (0.002 * sr);
    Noise noise;
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double f = 50.0 + 100.0 * std::exp (-t / 0.04);
        double x = std::sin (twoPi * phase) * std::exp (-t / 0.09);
        phase += f / sr;
        phase -= std::floor (phase);
        if (i < click)
            x += 0.5 * noise.next() * (1.0 - (double) i / (double) click);
        out.push_back ((float) x);
    }
    normaliseTail (out, start, peak);
}

/** Hat-like burst: noise high-passed twice at 6.5 kHz, fast decay. */
inline void hat (std::vector<float>& out, double sr, double seconds = 0.05, double tau = 0.008, float peak = 0.5f)
{
    const size_t start = out.size();
    auto h1 = Biquad::make (sr, 6500.0, true), h2 = Biquad::make (sr, 6500.0, true);
    Noise noise;
    for (int i = 0; i < (int) (0.01 * sr); ++i) // settle the filters
        h2.process (h1.process (noise.next()));
    const auto n = (size_t) std::lround (seconds * sr);
    for (size_t i = 0; i < n; ++i)
        out.push_back ((float) (h2.process (h1.process (noise.next())) * std::exp (-((double) i / sr) / tau)));
    normaliseTail (out, start, peak);
}

/** Snare-like burst: 200 Hz tone + band-passed noise, 200 ms. */
inline void snare (std::vector<float>& out, double sr, float peak = 0.5f)
{
    const size_t start = out.size();
    auto hp = Biquad::make (sr, 1000.0, true), lp = Biquad::make (sr, 6000.0, false);
    Noise noise;
    const auto n = (size_t) std::lround (0.2 * sr);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double nz = lp.process (hp.process (noise.next())) * std::exp (-t / 0.05);
        const double tone = std::sin (twoPi * 200.0 * t) * std::exp (-t / 0.03);
        out.push_back ((float) (0.7 * tone + 2.0 * nz));
    }
    normaliseTail (out, start, peak);
}

} // namespace m2m::smoke
