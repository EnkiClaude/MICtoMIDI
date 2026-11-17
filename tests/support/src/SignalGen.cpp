#include "m2m/test/SignalGen.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace m2m::test
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    std::uint64_t splitMix64 (std::uint64_t& x) noexcept
    {
        std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t rotl (std::uint64_t x, int k) noexcept
    {
        return (x << k) | (x >> (64 - k));
    }
}

//==============================================================================
Rng::Rng (std::uint64_t seed) noexcept
{
    std::uint64_t x = seed;
    for (auto& v : s)
        v = splitMix64 (x);
}

std::uint64_t Rng::nextU64() noexcept
{
    const std::uint64_t result = rotl (s[1] * 5, 7) * 9;
    const std::uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl (s[3], 45);
    return result;
}

double Rng::uniform() noexcept
{
    return static_cast<double> (nextU64() >> 11) * 0x1.0p-53;
}

//==============================================================================
Biquad Biquad::fromCoefficients (double b0_, double b1_, double b2_,
                                 double a0_, double a1_, double a2_) noexcept
{
    Biquad bq;
    bq.b0 = b0_ / a0_;
    bq.b1 = b1_ / a0_;
    bq.b2 = b2_ / a0_;
    bq.a1 = a1_ / a0_;
    bq.a2 = a2_ / a0_;
    return bq;
}

Biquad Biquad::lowPass (double sr, double hz, double q)
{
    const double w = twoPi * hz / sr;
    const double alpha = std::sin (w) / (2.0 * q);
    const double c = std::cos (w);
    return fromCoefficients ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad Biquad::highPass (double sr, double hz, double q)
{
    const double w = twoPi * hz / sr;
    const double alpha = std::sin (w) / (2.0 * q);
    const double c = std::cos (w);
    return fromCoefficients ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad Biquad::bandPass (double sr, double centreHz, double q)
{
    const double w = twoPi * centreHz / sr;
    const double alpha = std::sin (w) / (2.0 * q);
    const double c = std::cos (w);
    return fromCoefficients (alpha, 0.0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
}

double Biquad::process (double x) noexcept
{
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

void Biquad::processInPlace (std::vector<float>& buf) noexcept
{
    for (auto& v : buf)
        v = static_cast<float> (process (static_cast<double> (v)));
}

//==============================================================================
float dbToGain (double db) noexcept
{
    return static_cast<float> (std::pow (10.0, db / 20.0));
}

double gainToDb (double gain) noexcept
{
    return gain > 1e-12 ? 20.0 * std::log10 (gain) : -240.0;
}

double rms (const float* data, std::size_t n) noexcept
{
    if (n == 0)
        return 0.0;
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        acc += static_cast<double> (data[i]) * static_cast<double> (data[i]);
    return std::sqrt (acc / static_cast<double> (n));
}

double rms (const std::vector<float>& buf) noexcept
{
    return rms (buf.data(), buf.size());
}

double peak (const std::vector<float>& buf) noexcept
{
    float p = 0.0f;
    for (const float v : buf)
        p = std::max (p, std::abs (v));
    return static_cast<double> (p);
}

void scale (std::vector<float>& buf, double gain) noexcept
{
    for (auto& v : buf)
        v = static_cast<float> (static_cast<double> (v) * gain);
}

void normalisePeak (std::vector<float>& buf, double levelDb) noexcept
{
    const double p = peak (buf);
    if (p > 0.0)
        scale (buf, static_cast<double> (dbToGain (levelDb)) / p);
}

void normaliseRms (std::vector<float>& buf, double levelDb) noexcept
{
    const double r = rms (buf);
    if (r > 0.0)
        scale (buf, static_cast<double> (dbToGain (levelDb)) / r);
}

std::size_t secondsToSamples (double seconds, double sr) noexcept
{
    const double n = std::round (seconds * sr);
    return n > 0.0 ? static_cast<std::size_t> (n) : 0;
}

//==============================================================================
namespace partials
{
    std::vector<float> pure()               { return { 1.0f }; }
    std::vector<float> voiceLike()          { return { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f, 0.1f, 0.06f, 0.04f }; }
    std::vector<float> weakFundamental()    { return { 0.1f, 1.0f, 0.8f, 0.6f, 0.4f, 0.25f, 0.15f, 0.1f }; }
    std::vector<float> missingFundamental() { return { 0.0f, 1.0f, 0.8f, 0.6f, 0.4f, 0.25f }; }
}

void applyEnvelope (std::vector<float>& buf, double sr, const Adsr& env)
{
    const std::size_t n = buf.size();
    const std::size_t a = std::min (n, secondsToSamples (env.attackSec, sr));
    const std::size_t d = std::min (n - a, secondsToSamples (env.decaySec, sr));
    const std::size_t r = std::min (n - a - d, secondsToSamples (env.releaseSec, sr));
    const double sus = std::clamp (env.sustainLevel, 0.0, 1.0);

    for (std::size_t i = 0; i < n; ++i)
    {
        double g = sus;
        if (i < a)
            g = static_cast<double> (i) / static_cast<double> (a);
        else if (i < a + d)
            g = 1.0 - (1.0 - sus) * static_cast<double> (i - a) / static_cast<double> (d);

        // Release: linear ramp to 0 just past the last sample.
        if (r > 0 && i >= n - r)
            g *= static_cast<double> (n - i) / static_cast<double> (r + 1);

        buf[i] = static_cast<float> (static_cast<double> (buf[i]) * g);
    }
}

std::vector<float> renderTrack (double sr, const std::vector<double>& f0Hz,
                                const std::vector<float>& partialAmps, double levelDb)
{
    std::vector<float> out (f0Hz.size(), 0.0f);
    const double nyquist = 0.5 * sr;
    double phase = 0.0; // fundamental phase in cycles, [0, 1)

    for (std::size_t i = 0; i < f0Hz.size(); ++i)
    {
        const double f0 = f0Hz[i];
        double acc = 0.0;
        for (std::size_t k = 0; k < partialAmps.size(); ++k)
        {
            const double harmonic = static_cast<double> (k + 1);
            if (harmonic * f0 >= nyquist)
                break;
            if (partialAmps[k] == 0.0f)
                continue;
            acc += static_cast<double> (partialAmps[k]) * std::sin (twoPi * std::fmod (harmonic * phase, 1.0));
        }
        out[i] = static_cast<float> (acc);

        phase += f0 / sr;
        phase -= std::floor (phase);
    }

    normalisePeak (out, levelDb);
    return out;
}

std::vector<float> sine (double sr, double seconds, double hz, double levelDb)
{
    return harmonicTone (sr, seconds, hz, partials::pure(), levelDb);
}

std::vector<float> harmonicTone (double sr, double seconds, double hz,
                                 const std::vector<float>& partialAmps, double levelDb)
{
    return renderTrack (sr, std::vector<double> (secondsToSamples (seconds, sr), hz), partialAmps, levelDb);
}

std::vector<float> vibratoTone (double sr, double seconds, double hz, double depthCents, double rateHz,
                                const std::vector<float>& partialAmps, double levelDb)
{
    std::vector<double> f0 (secondsToSamples (seconds, sr));
    for (std::size_t i = 0; i < f0.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        f0[i] = hz * std::exp2 (depthCents / 1200.0 * std::sin (twoPi * rateHz * t));
    }
    return renderTrack (sr, f0, partialAmps, levelDb);
}

std::vector<float> glideTone (double sr, double fromHz, double toHz,
                              double holdBeforeSec, double glideSec, double holdAfterSec,
                              const std::vector<float>& partialAmps, double levelDb)
{
    const std::size_t nBefore = secondsToSamples (holdBeforeSec, sr);
    const std::size_t nGlide  = secondsToSamples (glideSec, sr);
    const std::size_t nAfter  = secondsToSamples (holdAfterSec, sr);

    std::vector<double> f0;
    f0.reserve (nBefore + nGlide + nAfter);
    f0.insert (f0.end(), nBefore, fromHz);
    const double ratio = std::log2 (toHz / fromHz);
    for (std::size_t i = 0; i < nGlide; ++i)
        f0.push_back (fromHz * std::exp2 (ratio * static_cast<double> (i) / static_cast<double> (nGlide)));
    f0.insert (f0.end(), nAfter, toHz);

    return renderTrack (sr, f0, partialAmps, levelDb);
}

//==============================================================================
std::vector<float> whiteNoise (double sr, double seconds, double rmsDb, std::uint64_t seed)
{
    Rng rng (seed);
    std::vector<float> out (secondsToSamples (seconds, sr));
    for (auto& v : out)
        v = static_cast<float> (rng.uniformSigned());
    normaliseRms (out, rmsDb);
    return out;
}

std::vector<float> pinkNoise (double sr, double seconds, double rmsDb, std::uint64_t seed)
{
    Rng rng (seed);
    std::vector<float> out (secondsToSamples (seconds, sr));
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& v : out)
    {
        const double w = rng.uniformSigned();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        v = static_cast<float> (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
        b6 = w * 0.115926;
    }
    normaliseRms (out, rmsDb);
    return out;
}

std::vector<float> silence (double sr, double seconds)
{
    return std::vector<float> (secondsToSamples (seconds, sr), 0.0f);
}

//==============================================================================
void append (std::vector<float>& dst, const std::vector<float>& src)
{
    dst.insert (dst.end(), src.begin(), src.end());
}

void mixInto (std::vector<float>& dst, const std::vector<float>& src, std::size_t offset, double gain)
{
    if (dst.size() < offset + src.size())
        dst.resize (offset + src.size(), 0.0f);
    for (std::size_t i = 0; i < src.size(); ++i)
        dst[offset + i] = static_cast<float> (static_cast<double> (dst[offset + i])
                                              + gain * static_cast<double> (src[i]));
}

std::vector<float> mixWithSnr (const std::vector<float>& signal, const std::vector<float>& noise,
                               double snrDb, double refRms)
{
    std::vector<float> out (signal);
    if (noise.empty() || signal.empty())
        return out;

    std::vector<float> n (signal.size());
    for (std::size_t i = 0; i < n.size(); ++i)
        n[i] = noise[i % noise.size()];

    const double ref = refRms > 0.0 ? refRms : rms (signal);
    const double noiseRms = rms (n);
    if (noiseRms <= 0.0 || ref <= 0.0)
        return out;

    const double gain = ref / (noiseRms * std::pow (10.0, snrDb / 20.0));
    mixInto (out, n, 0, gain);
    return out;
}

} // namespace m2m::test
