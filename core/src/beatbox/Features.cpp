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
    preMags.assign (static_cast<std::size_t> (numBins), 0.0f);
    preHann.assign (static_cast<std::size_t> (maxN), 0.0f);
    preHannLen = -1;
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

namespace
{
    void makeSymmetricHann (std::vector<float>& w, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            w[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (i + 0.5) / n));
    }

    double sumSquares (const std::vector<float>& w, int n) noexcept
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i)
            s += static_cast<double> (w[static_cast<std::size_t> (i)]) * w[static_cast<std::size_t> (i)];
        return s;
    }
}

FeatureVector FeatureExtractor::extract (const float* window, int n, const float* pre, int preN) noexcept
{
    FeatureVector f;
    n = std::clamp (n, 0, maxN);
    if (n < 16 || fftN == 0)
        return f;

    if (n != hannLen)
    {
        makeSymmetricHann (hann, n); // symmetric Hann over exactly n samples
        hannLen = n;
    }

    // Spectrum of what was already sounding just before the onset, scaled to
    // the window's length, so it can be subtracted (a hat on a kick's tail).
    preScale = 0.0;
    preEnergy = 0.0;
    preN = pre != nullptr ? std::min (preN, maxN) : 0;
    if (preN >= 16)
    {
        if (preN != preHannLen)
        {
            makeSymmetricHann (preHann, preN);
            preHannLen = preN;
        }
        std::fill (frame.begin(), frame.end(), 0.0f);
        float prePeak = 0.0f;
        double preSum = 0.0;
        for (int i = 0; i < preN; ++i)
        {
            preSum += static_cast<double> (pre[i]) * pre[i];
            frame[static_cast<std::size_t> (i)] = pre[i] * preHann[static_cast<std::size_t> (i)];
            prePeak = std::max (prePeak, std::abs (pre[i]));
        }
        if (prePeak > 0.0f)
        {
            preEnergy = preSum / preN;
            fft.magnitudes (frame.data(), preMags.data());
            preScale = sumSquares (hann, n) / sumSquares (preHann, preN);
        }
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
        double p = static_cast<double> (mags[ks]) * mags[ks];
        if (preScale > 0.0)
            p = std::max (0.0, p - preScale * preMags[ks] * preMags[ks]);
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
    double energy = 0.0;
    float peak = 0.0f;
    int crossings = 0;

    for (int i = 0; i < n; ++i)
    {
        const double v = x[i];
        energy += v * v;
        peak = std::max (peak, std::abs (x[i]));
        if (i > 0 && ((x[i - 1] < 0.0f) != (x[i] < 0.0f)))
            ++crossings;
    }

    const double rms = std::sqrt (energy / n);
    const double ms = n * 1000.0 / sr;
    f[Feature::LevelDb] = powerDb (energy / n);
    f[Feature::CrestDb] = rms > 0.0 ? static_cast<float> (20.0 * std::log10 (std::max (1.0, peak / rms))) : 0.0f;
    f[Feature::ZeroCrossingRate] = static_cast<float> (crossings / ms);

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

    // Early decay: energy of the last quarter of the window against the
    // middle (35-60 %), in dB per 10 ms between the segment centres. Skipping
    // the first third keeps the attack out of it: a "t" click in front of a
    // "sss", or a fricative that takes a few ms to build up, would otherwise
    // read as decay (or hide it).
    {
        const int m0 = (n * 35) / 100, m1 = (n * 60) / 100, t0 = (n * 75) / 100;
        double em = 0.0, et = 0.0;
        for (int i = m0; i < m1; ++i)
            em += static_cast<double> (x[i]) * x[i];
        for (int i = t0; i < n; ++i)
            et += static_cast<double> (x[i]) * x[i];
        if (m1 > m0 && n > t0)
        {
            // Remove what was already sounding before the onset (a kick's
            // tail under a hat, a noise floor), at most 10 dB of each.
            const auto minusPre = [this] (double e) { return std::max (e - preEnergy, 0.1 * e); };
            const double distMs = (0.5 * (t0 + n) - 0.5 * (m0 + m1)) * 1000.0 / sr;
            const float d = powerDb (minusPre (et / (n - t0)) + tiny) - powerDb (minusPre (em / (m1 - m0)) + tiny);
            f[Feature::EarlyDecayDb] = std::clamp (d * static_cast<float> (10.0 / distMs), -60.0f, 20.0f);
        }
    }

    // Sub-onsets: the first block within 30 dB of the loudest one starts the
    // first burst. A further burst needs a genuine gap: the HP envelope must
    // have fallen >= 8 dB below the peak of the current burst and then risen
    // >= 8 dB out of that dip, >= 4 ms after the previous burst. (A slow
    // attack or the 1 ms fluctuations of band-limited noise are not bursts.)
    constexpr float riseDb = 8.0f;
    const int minGap = std::max (1, static_cast<int> (std::lround (0.004 * sr / blk)));
    int count = 0, last = -minGap;
    float burstPeak = -120.0f, dipMin = -120.0f;
    for (int b = 0; b < numBlocks; ++b)
    {
        const float lv = scratch[static_cast<std::size_t> (2 * b)];
        if (count == 0)
        {
            if (lv > maxHp - 30.0f)
            {
                count = 1;
                last = b;
                burstPeak = dipMin = lv;
            }
        }
        else if (burstPeak - dipMin >= riseDb && lv >= dipMin + riseDb && lv > maxHp - 30.0f && b - last >= minGap)
        {
            ++count;
            last = b;
            burstPeak = dipMin = lv;
        }
        else if (lv > burstPeak && burstPeak - dipMin < riseDb)
        {
            burstPeak = dipMin = lv; // still rising within the same burst
        }
        else
        {
            dipMin = std::min (dipMin, lv);
        }
    }
    f[Feature::SubOnsets] = static_cast<float> (count);
}

} // namespace m2m
