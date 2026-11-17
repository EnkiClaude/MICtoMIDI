#include "m2m/test/Drums.h"
#include "m2m/test/SignalGen.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace m2m::test
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    /** Per-hit random variation derived from the seed (independent of the
        noise stream so changing variation does not change the noise). */
    struct Variation
    {
        double levelDb = 0.0;    // added to the level
        double decay   = 1.0;    // decay-time factor
        double freq    = 1.0;    // frequency factor
        double timing  = 0.0;    // -1..1, used for burst jitter

        Variation (const DrumParams& p)
        {
            Rng r (p.seed ^ 0xD1B54A32D192ED03ull);
            const double v = std::clamp (p.variation, 0.0, 1.0);
            levelDb = v * r.range (-2.0, 2.0);
            decay   = 1.0 + v * r.range (-0.15, 0.15);
            freq    = 1.0 + v * r.range (-0.05, 0.05);
            timing  = v * r.uniformSigned();
        }
    };

    /** White noise filtered by the given chain; the filters run over a
        pre-roll first so the output starts settled. */
    std::vector<float> filteredNoise (double sr, std::size_t n, std::uint64_t seed,
                                      std::vector<Biquad> filters)
    {
        Rng rng (seed);
        const std::size_t preroll = secondsToSamples (0.01, sr);
        std::vector<float> out (n);
        for (std::size_t i = 0; i < preroll + n; ++i)
        {
            double x = rng.uniformSigned();
            for (auto& f : filters)
                x = f.process (x);
            if (i >= preroll)
                out[i - preroll] = static_cast<float> (x);
        }
        return out;
    }

    void applyExpDecay (std::vector<float>& buf, double sr, double tauSec)
    {
        for (std::size_t i = 0; i < buf.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            buf[i] = static_cast<float> (static_cast<double> (buf[i]) * std::exp (-t / tauSec));
        }
    }

    std::vector<float> hat (double sr, const DrumParams& p, double lengthSec, double tauSec)
    {
        const Variation var (p);
        const std::size_t n = secondsToSamples (lengthSec * var.decay, sr);
        const double cutoff = std::min (6500.0 * var.freq, 0.45 * sr);
        auto out = filteredNoise (sr, n, p.seed,
                                  { Biquad::highPass (sr, cutoff), Biquad::highPass (sr, cutoff) });
        applyExpDecay (out, sr, tauSec * var.decay);
        normalisePeak (out, p.levelDb + var.levelDb);
        return out;
    }
}

int gmNote (DrumKind kind) noexcept
{
    switch (kind)
    {
        case DrumKind::Kick:      return 36;
        case DrumKind::Snare:     return 38;
        case DrumKind::Clap:      return 39;
        case DrumKind::ClosedHat: return 42;
        case DrumKind::OpenHat:   return 46;
    }
    return 0;
}

const char* drumName (DrumKind kind) noexcept
{
    switch (kind)
    {
        case DrumKind::Kick:      return "kick";
        case DrumKind::Snare:     return "snare";
        case DrumKind::Clap:      return "clap";
        case DrumKind::ClosedHat: return "closed_hat";
        case DrumKind::OpenHat:   return "open_hat";
    }
    return "?";
}

std::vector<float> kick (double sr, const DrumParams& p)
{
    const Variation var (p);
    const std::size_t n = secondsToSamples (0.3 * var.decay, sr);
    const std::size_t clickLen = secondsToSamples (0.002, sr);
    Rng rng (p.seed);

    std::vector<float> out (n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double f = var.freq * (50.0 + 100.0 * std::exp (-t / 0.04));
        double x = std::sin (twoPi * phase) * std::exp (-t / (0.09 * var.decay));
        phase += f / sr;
        phase -= std::floor (phase);

        if (i < clickLen)
            x += 0.5 * rng.uniformSigned() * (1.0 - static_cast<double> (i) / static_cast<double> (clickLen));

        out[i] = static_cast<float> (x);
    }
    normalisePeak (out, p.levelDb + var.levelDb);
    return out;
}

std::vector<float> snare (double sr, const DrumParams& p)
{
    const Variation var (p);
    const std::size_t n = secondsToSamples (0.2 * var.decay, sr);
    auto noise = filteredNoise (sr, n, p.seed,
                                { Biquad::highPass (sr, 1000.0), Biquad::lowPass (sr, std::min (6000.0, 0.45 * sr)) });
    applyExpDecay (noise, sr, 0.05 * var.decay);
    normalisePeak (noise, 0.0);

    std::vector<float> out (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double tone = std::sin (twoPi * 200.0 * var.freq * t) * std::exp (-t / (0.03 * var.decay));
        out[i] = static_cast<float> (0.7 * tone + 0.8 * static_cast<double> (noise[i]));
    }
    normalisePeak (out, p.levelDb + var.levelDb);
    return out;
}

std::vector<float> clap (double sr, const DrumParams& p)
{
    const Variation var (p);
    const std::size_t n = secondsToSamples (0.2 * var.decay, sr);
    auto out = filteredNoise (sr, n, p.seed,
                              { Biquad::highPass (sr, 1000.0), Biquad::lowPass (sr, std::min (4000.0, 0.45 * sr)) });

    // Four bursts ~10 ms apart (spacing jittered by up to +-1 ms); the last
    // one carries the reverberant tail.
    const double spacing = 0.010 + 0.001 * var.timing;
    constexpr int numBursts = 4;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        double env = 0.0;
        for (int b = 0; b < numBursts; ++b)
        {
            const double tb = spacing * b;
            if (t < tb)
                continue;
            const double tau = (b == numBursts - 1) ? 0.05 * var.decay : 0.003;
            env = std::max (env, std::exp (-(t - tb) / tau));
        }
        out[i] = static_cast<float> (static_cast<double> (out[i]) * env);
    }
    normalisePeak (out, p.levelDb + var.levelDb);
    return out;
}

std::vector<float> closedHat (double sr, const DrumParams& p)
{
    return hat (sr, p, 0.05, 0.008);
}

std::vector<float> openHat (double sr, const DrumParams& p)
{
    return hat (sr, p, 0.35, 0.065);
}

std::vector<float> drum (DrumKind kind, double sr, const DrumParams& p)
{
    switch (kind)
    {
        case DrumKind::Kick:      return kick (sr, p);
        case DrumKind::Snare:     return snare (sr, p);
        case DrumKind::Clap:      return clap (sr, p);
        case DrumKind::ClosedHat: return closedHat (sr, p);
        case DrumKind::OpenHat:   return openHat (sr, p);
    }
    return {};
}

} // namespace m2m::test
