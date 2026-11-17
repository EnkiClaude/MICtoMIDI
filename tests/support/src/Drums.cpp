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

//==============================================================================
// Voice-like family

namespace
{
    double clampHz (double hz, double sr) { return std::clamp (hz, 20.0, 0.45 * sr); }

    /** Random value in [lo, hi] drawn around the midpoint, the range scaled by v. */
    double pick (Rng& r, double lo, double hi, double v)
    {
        const double mid = 0.5 * (lo + hi);
        return mid + v * (r.uniform() - 0.5) * (hi - lo);
    }

    /** Noise shaped by a filter chain, with envelope env(t). */
    template <typename Env>
    void addNoise (std::vector<float>& out, std::size_t start, std::size_t n, double sr, Rng& rng,
                   std::vector<Biquad> filters, double gain, Env env)
    {
        if (out.size() < start + n)
            out.resize (start + n, 0.0f);
        // settle the filters on 5 ms of noise first
        const std::size_t pre = secondsToSamples (0.005, sr);
        for (std::size_t i = 0; i < pre + n; ++i)
        {
            double x = rng.uniformSigned();
            for (auto& f : filters)
                x = f.process (x);
            if (i >= pre)
            {
                const double t = static_cast<double> (i - pre) / sr;
                out[start + i - pre] += static_cast<float> (gain * env (t) * x);
            }
        }
    }

    double attackDecay (double t, double attack, double tau) noexcept
    {
        const double a = attack > 0.0 ? std::min (1.0, t / attack) : 1.0;
        return a * std::exp (-t / tau);
    }

    double peakOf (const std::vector<float>& b, std::size_t from)
    {
        double m = 0.0;
        for (std::size_t i = from; i < b.size(); ++i)
            m = std::max (m, std::abs (static_cast<double> (b[i])));
        return m;
    }
}

VoiceDrum voiceDrum (DrumKind kind, double sr, const DrumParams& p)
{
    Rng rng (p.seed * 0x9E3779B97F4A7C15ull + 0x5851F42D4C957F2Dull);
    const double v = std::clamp (p.variation, 0.0, 1.0);

    std::vector<float> hit;
    switch (kind)
    {
        case DrumKind::Kick:
        {
            const double f0 = pick (rng, 75.0, 125.0, v);
            const double drop = pick (rng, 1.0, 1.4, v);           // starts up to 40 % higher
            const double tau = pick (rng, 0.03, 0.11, v);
            const double lp = pick (rng, 250.0, 700.0, v);
            const double burstLen = pick (rng, 0.002, 0.006, v);
            const double burstGain = std::pow (10.0, pick (rng, -16.0, -4.0, v) / 20.0);
            const double burstLp = pick (rng, 800.0, 3000.0, v);
            const double h2 = pick (rng, 0.2, 0.6, v), h3 = pick (rng, 0.05, 0.3, v);
            const std::size_t n = secondsToSamples (pick (rng, 0.08, 0.2, v), sr);

            hit.assign (n, 0.0f);
            Biquad lp1 = Biquad::lowPass (sr, clampHz (lp, sr)), lp2 = Biquad::lowPass (sr, clampHz (lp, sr));
            double phase = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double f = f0 * (1.0 + (drop - 1.0) * std::exp (-t / 0.02));
                phase += f / sr;
                phase -= std::floor (phase);
                const double x = std::sin (twoPi * phase) + h2 * std::sin (2 * twoPi * phase)
                               + h3 * std::sin (3 * twoPi * phase);
                hit[i] = static_cast<float> (lp2.process (lp1.process (x * attackDecay (t, 0.003, tau))));
            }
            const double bodyPeak = peakOf (hit, 0);
            addNoise (hit, 0, secondsToSamples (burstLen, sr), sr, rng, { Biquad::lowPass (sr, clampHz (burstLp, sr)) },
                      3.0 * burstGain * bodyPeak, [burstLen] (double t) { return 1.0 - t / burstLen; });
            break;
        }

        case DrumKind::Snare:
        {
            const double centre = pick (rng, 1200.0, 4500.0, v);
            const double q = pick (rng, 0.4, 1.2, v);
            const double tau = pick (rng, 0.02, 0.07, v);
            const double toneHz = pick (rng, 140.0, 320.0, v);
            const double toneGain = std::pow (10.0, pick (rng, -22.0, -6.0, v) / 20.0);
            const double airGain = pick (rng, 0.05, 0.35, v);
            const double attack = pick (rng, 0.0005, 0.004, v);
            const std::size_t n = secondsToSamples (pick (rng, 0.08, 0.18, v), sr);

            hit.assign (n, 0.0f);
            addNoise (hit, 0, n, sr, rng, { Biquad::bandPass (sr, clampHz (centre, sr), q) }, 2.0,
                      [&] (double t) { return attackDecay (t, attack, tau); });
            addNoise (hit, 0, n, sr, rng, { Biquad::highPass (sr, clampHz (6000.0, sr)) }, airGain,
                      [&] (double t) { return attackDecay (t, attack, 0.6 * tau); });
            const double noisePeak = peakOf (hit, 0);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double t = static_cast<double> (i) / sr;
                hit[i] += static_cast<float> (noisePeak * toneGain * std::sin (twoPi * toneHz * t)
                                              * attackDecay (t, 0.002, 0.04));
            }
            break;
        }

        case DrumKind::ClosedHat:
        case DrumKind::OpenHat:
        {
            const bool open = kind == DrumKind::OpenHat;
            const double tLen = pick (rng, 0.001, 0.004, v);
            const double hp = pick (rng, 3500.0, 6000.0, v);
            const double lp = pick (rng, 9000.0, 13000.0, v);
            const double sLen = open ? pick (rng, 0.15, 0.35, v) : pick (rng, 0.025, 0.08, v);
            const double sAttack = pick (rng, 0.001, 0.006, v);
            // closed "ts": the fricative dies away quickly (tau 6-18 ms); open
            // "tsss": sustained, then released over 80 ms.
            const double sTau = pick (rng, 0.006, 0.018, v);
            const double tGain = pick (rng, 0.5, 1.5, v);
            const std::size_t n = secondsToSamples (tLen + sLen, sr);

            hit.assign (n, 0.0f);
            // "t": broadband click
            addNoise (hit, 0, secondsToSamples (tLen, sr), sr, rng, { Biquad::highPass (sr, clampHz (1500.0, sr)) }, tGain,
                      [tLen] (double t) { return 1.0 - t / tLen; });
            // "s": fricative, fading out over its length (open: sustained, then fade)
            addNoise (hit, 0, n, sr, rng,
                      { Biquad::highPass (sr, clampHz (hp, sr)), Biquad::highPass (sr, clampHz (hp, sr)),
                        Biquad::lowPass (sr, clampHz (lp, sr)) },
                      1.0, [&] (double t)
                      {
                          const double a = std::min (1.0, t / sAttack);
                          if (open)
                              return a * std::clamp ((tLen + sLen - t) / 0.08, 0.0, 1.0);
                          return a * std::exp (-std::max (0.0, t - sAttack) / sTau);
                      });
            break;
        }

        case DrumKind::Clap:
        {
            const int bursts = 2 + static_cast<int> (rng.uniform() * 3.0); // 2..4
            const double spacing = pick (rng, 0.005, 0.011, v);
            const double centre = pick (rng, 1500.0, 4000.0, v);
            const double tailTau = pick (rng, 0.02, 0.05, v);
            const std::size_t n = secondsToSamples (spacing * bursts + 0.1, sr);
            hit.assign (n, 0.0f);
            addNoise (hit, 0, n, sr, rng, { Biquad::bandPass (sr, clampHz (centre, sr), 0.9) }, 1.0, [&] (double t)
                      {
                          const int b = std::min (bursts - 1, static_cast<int> (t / spacing));
                          const double tb = t - b * spacing;
                          return attackDecay (tb, 0.0005, b == bursts - 1 ? tailTau : 0.002);
                      });
            break;
        }
    }

    // 3 ms fade-out so the end is not a click.
    const auto fade = std::min (hit.size(), secondsToSamples (0.003, sr));
    for (std::size_t k = 0; k < fade; ++k)
        hit[hit.size() - 1 - k] *= static_cast<float> (k) / static_cast<float> (fade);

    // Quiet breath / lip pre-roll before the hit.
    VoiceDrum out;
    const std::size_t preN = secondsToSamples (pick (rng, 0.0, 0.025, v), sr);
    const double preDb = pick (rng, -45.0, -30.0, v);
    const double hitPeak = peakOf (hit, 0);
    out.audio.assign (preN, 0.0f);
    if (preN > 0)
        addNoise (out.audio, 0, preN, sr, rng, { Biquad::bandPass (sr, clampHz (2000.0, sr), 0.5) },
                  hitPeak * std::pow (10.0, preDb / 20.0) * 2.0,
                  [preN, sr] (double t) { return std::min (1.0, t * sr / static_cast<double> (preN)); });
    out.onsetSample = preN;
    out.audio.insert (out.audio.end(), hit.begin(), hit.end());

    const double level = p.levelDb + v * pick (rng, -3.0, 3.0, 1.0);
    const double pk = peakOf (out.audio, 0);
    if (pk > 0.0)
        scale (out.audio, std::pow (10.0, level / 20.0) / pk);
    return out;
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
