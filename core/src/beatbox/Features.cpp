#include "m2m/beatbox/Features.h"

#include "m2m/audio/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace m2m
{

namespace
{
    constexpr float tiny = 1.0e-12f;

    float powerDb (double p) noexcept
    {
        return p > 1.0e-12 ? static_cast<float> (10.0 * std::log10 (p)) : -120.0f;
    }
}

const char* featureName (Feature f) noexcept
{
    switch (f)
    {
        case Feature::BandLow:          return "bandLow";
        case Feature::BandLowMid:       return "bandLowMid";
        case Feature::BandHighMid:      return "bandHighMid";
        case Feature::BandHigh:         return "bandHigh";
        case Feature::CentroidOct:      return "centroidOct";
        case Feature::SpreadOct:        return "spreadOct";
        case Feature::RolloffOct:       return "rolloffOct";
        case Feature::Flatness:         return "flatness";
        case Feature::ZeroCrossingRate: return "zcrPerMs";
        case Feature::CrestDb:          return "crestDb";
        case Feature::AttackMs:         return "attackMs";
        case Feature::EarlyDecayDb:     return "earlyDecayDb";
        case Feature::SubOnsets:        return "subOnsets";
        case Feature::LevelDb:          return "levelDb";
        case Feature::Count:            break;
    }
    return "?";
}

void FeatureExtractor::prepare (double sampleRate, float maxWindowMs)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float ms = std::clamp (std::isfinite (maxWindowMs) ? maxWindowMs : 40.0f, 5.0f, 100.0f);
    maxN = std::max (16, static_cast<int> (std::lround (ms * 0.001 * sr)));
    fftN = std::max (512, nextPowerOfTwo (maxN));
    numBins = fftN / 2 + 1;

    fft.prepare (fftN);
    hann.assign (static_cast<std::size_t> (maxN), 0.0f);
    frame.assign (static_cast<std::size_t> (fftN), 0.0f);
    mags.assign (static_cast<std::size_t> (numBins), 0.0f);
    scratch.assign (static_cast<std::size_t> (maxN), 0.0f);

    logFreq.assign (static_cast<std::size_t> (numBins), 0.0f);
    const double binHz = sr / fftN;
    for (int k = 1; k < numBins; ++k)
        logFreq[static_cast<std::size_t> (k)] = static_cast<float> (std::log2 (k * binHz / 1000.0));
    logFreq[0] = logFreq.size() > 1 ? logFreq[1] - 1.0f : 0.0f;

    hannLen = -1;
}

int FeatureExtractor::windowSamples (float ms) const noexcept
{
    const auto n = static_cast<int> (std::lround (std::max (0.0f, ms) * 0.001 * sr));
    return std::clamp (n, 16, maxN);
}

FeatureVector FeatureExtractor::extract (const float* window, int n) noexcept
{
    FeatureVector f;
    n = std::clamp (n, 0, maxN);
    if (n < 16 || fftN == 0)
        return f;

    if (n != hannLen)
    {
        // symmetric Hann over exactly n samples
        for (int i = 0; i < n; ++i)
            hann[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (i + 0.5) / n));
        hannLen = n;
    }

    std::fill (frame.begin(), frame.end(), 0.0f);
    for (int i = 0; i < n; ++i)
        frame[static_cast<std::size_t> (i)] = window[i] * hann[static_cast<std::size_t> (i)];

    computeSpectral (n, f);
    computeTemporal (window, n, f);
    return f;
}

void FeatureExtractor::computeSpectral (int n, FeatureVector& f) noexcept
{
    fft.magnitudes (frame.data(), mags.data());

    const double binHz = sr / fftN;
    const int minBin = std::max (1, static_cast<int> (20.0 / binHz)); // ignore DC / sub-audio
    const int maxBin = std::min (numBins - 1, static_cast<int> (std::min (20000.0, 0.5 * sr) / binHz));

    double bands[4] {};
    double total = 0.0, sumLogF = 0.0, sumLogF2 = 0.0, maxP = 0.0;

    for (int k = minBin; k <= maxBin; ++k)
    {
        const auto ks = static_cast<std::size_t> (k);
        const double p = static_cast<double> (mags[ks]) * mags[ks];
        mags[ks] = static_cast<float> (p); // reuse as power
        const double hz = k * binHz;
        const int band = hz < 150.0 ? 0 : hz < 1000.0 ? 1 : hz < 5000.0 ? 2 : 3;
        bands[band] += p;
        total += p;
        sumLogF += p * logFreq[ks];
        sumLogF2 += p * static_cast<double> (logFreq[ks]) * logFreq[ks];
        maxP = std::max (maxP, p);
    }

    if (total <= 0.0)
    {
        f[Feature::CentroidOct] = 0.0f;
        f[Feature::RolloffOct] = 0.0f;
        return;
    }

    for (int b = 0; b < 4; ++b)
        f[b] = static_cast<float> (bands[b] / total);

    const double meanLog = sumLogF / total;
    f[Feature::SpreadOct] = static_cast<float> (std::sqrt (std::max (0.0, sumLogF2 / total - meanLog * meanLog)));

    // Centroid on linear frequency (the usual definition), reported in octaves.
    double sumF = 0.0;
    for (int k = minBin; k <= maxBin; ++k)
        sumF += mags[static_cast<std::size_t> (k)] * (k * binHz);
    f[Feature::CentroidOct] = static_cast<float> (std::log2 (std::max (20.0, sumF / total) / 1000.0));

    // 85 % rolloff
    double acc = 0.0;
    int roll = maxBin;
    for (int k = minBin; k <= maxBin; ++k)
    {
        acc += mags[static_cast<std::size_t> (k)];
        if (acc >= 0.85 * total)
        {
            roll = k;
            break;
        }
    }
    f[Feature::RolloffOct] = logFreq[static_cast<std::size_t> (roll)];

    // Flatness over 100 Hz .. 16 kHz, with a floor 100 dB below the peak bin.
    const int fLo = std::max (minBin, static_cast<int> (100.0 / binHz));
    const int fHi = std::min (maxBin, static_cast<int> (16000.0 / binHz));
    const double floorP = maxP * 1.0e-10 + 1.0e-30;
    double sumLn = 0.0, sumP = 0.0;
    int cnt = 0;
    for (int k = fLo; k <= fHi; ++k)
    {
        const double p = std::max (static_cast<double> (mags[static_cast<std::size_t> (k)]), floorP);
        sumLn += std::log (p);
        sumP += p;
        ++cnt;
    }
    if (cnt > 0 && sumP > 0.0)
        f[Feature::Flatness] = static_cast<float> (std::exp (sumLn / cnt) / (sumP / cnt));

    (void) n;
}

void FeatureExtractor::computeTemporal (const float* x, int n, FeatureVector& f) noexcept
{
    double energy = 0.0, e1 = 0.0, e2 = 0.0;
    float peak = 0.0f;
    int crossings = 0;
    const int half = n / 2;

    for (int i = 0; i < n; ++i)
    {
        const double v = x[i];
        energy += v * v;
        (i < half ? e1 : e2) += v * v;
        peak = std::max (peak, std::abs (x[i]));
        if (i > 0 && ((x[i - 1] < 0.0f) != (x[i] < 0.0f)))
            ++crossings;
    }

    const double rms = std::sqrt (energy / n);
    const double ms = n * 1000.0 / sr;
    f[Feature::LevelDb] = powerDb (energy / n);
    f[Feature::CrestDb] = rms > 0.0 ? static_cast<float> (20.0 * std::log10 (std::max (1.0, peak / rms))) : 0.0f;
    f[Feature::ZeroCrossingRate] = static_cast<float> (crossings / ms);
    // Scaled to a 20 ms window (dB per 10 ms between the half centres), so the
    // same thresholds work for any window length.
    const float decay = powerDb (e2 + tiny) - powerDb (e1 + tiny);
    f[Feature::EarlyDecayDb] = std::clamp (decay * static_cast<float> (20.0 / ms), -60.0f, 20.0f);

    // Envelope in ~1 ms blocks of a 1 kHz high-passed copy (2nd order), with
    // a peak hold that decays 2 dB/ms. The high-pass removes the low-frequency
    // ripple of kicks and the hold fills the dips of tonal ripple, so only
    // genuine new bursts (claps) rise above it.
    const int blk = std::max (4, static_cast<int> (std::lround (0.001 * sr)));
    const double w0 = 2.0 * std::numbers::pi * std::min (1000.0, 0.45 * sr) / sr;
    const double cw = std::cos (w0), alpha = std::sin (w0) / std::numbers::sqrt2;
    const double a0 = 1.0 + alpha;
    const double hb0 = (1.0 + cw) * 0.5 / a0, hb1 = -(1.0 + cw) / a0, hb2 = hb0;
    const double ha1 = -2.0 * cw / a0, ha2 = (1.0 - alpha) / a0;
    double hs1 = 0.0, hs2 = 0.0;
    constexpr float holdDecayDbPerBlock = 2.0f;
    float hold = -120.0f;

    int numBlocks = 0;
    const int maxBlocks = static_cast<int> (scratch.size()) / 2;
    for (int b = 0; b * blk < n && b < maxBlocks; ++b)
    {
        double e = 0.0, eFull = 0.0;
        const int end = std::min (n, (b + 1) * blk);
        for (int i = b * blk; i < end; ++i)
        {
            const double xi = x[i];
            const double y = hb0 * xi + hs1;
            hs1 = hb1 * xi - ha1 * y + hs2;
            hs2 = hb2 * xi - ha2 * y;
            e += y * y;
            eFull += xi * xi;
        }
        const int len = end - b * blk;
        hold = std::max (powerDb (e / len), hold - holdDecayDbPerBlock);
        scratch[static_cast<std::size_t> (2 * b)] = hold;                      // HP envelope w/ hold (dB)
        scratch[static_cast<std::size_t> (2 * b + 1)] = powerDb (eFull / len); // full envelope (dB)
        ++numBlocks;
    }

    // Attack time: block of maximum full-band energy (centre of the block).
    int maxBlock = 0;
    float maxHp = -200.0f;
    for (int b = 0; b < numBlocks; ++b)
    {
        if (scratch[static_cast<std::size_t> (2 * b + 1)] > scratch[static_cast<std::size_t> (2 * maxBlock + 1)])
            maxBlock = b;
        maxHp = std::max (maxHp, scratch[static_cast<std::size_t> (2 * b)]);
    }
    f[Feature::AttackMs] = static_cast<float> ((maxBlock + 0.5) * blk * 1000.0 / sr);

    // Sub-onsets: rises of >= 8 dB above the running minimum of the HP
    // envelope, >= 4 ms apart, within 30 dB of the loudest block.
    constexpr float riseDb = 8.0f;
    const int minGap = std::max (1, static_cast<int> (std::lround (0.004 * sr / blk)));
    int count = 0, last = -minGap;
    float runMin = -120.0f;
    for (int b = 0; b < numBlocks; ++b)
    {
        const float lv = scratch[static_cast<std::size_t> (2 * b)];
        if (lv > runMin + riseDb && lv > maxHp - 30.0f && b - last >= minGap)
        {
            ++count;
            last = b;
            runMin = lv;
        }
        else
        {
            runMin = std::min (runMin, lv);
        }
    }
    f[Feature::SubOnsets] = static_cast<float> (count);
}

} // namespace m2m
