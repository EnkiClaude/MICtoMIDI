#include "m2m/pitch/MpmDetector.h"

#include "PitchCommon.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

using pitchdetail::idx;

void MpmDetector::prepare (double sampleRate, float lowestMinHz)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    lowestHz = lowestMinHz > 1.0f ? lowestMinHz : kLowestPitchHz;

    const int worstLag = pitchMaxLag (sr, lowestHz);
    const int worstWindow = 2 * worstLag;

    // Linear autocorrelation up to maxLag needs a transform >= window + maxLag.
    ffts.prepare (worstWindow + worstLag);
    const int fftMax = ffts.maxSize();

    padX.assign (idx (fftMax), 0.0f);
    acf.assign (idx (fftMax), 0.0f);
    xRe.assign (idx (fftMax / 2 + 1), 0.0f);
    xIm.assign (idx (fftMax / 2 + 1), 0.0f);
    prefix.assign (idx (worstWindow + 1), 0.0);
    nsdf.assign (idx (worstLag + 2), 0.0f);

    setRange (minHz, maxHz);
}

void MpmDetector::setRange (float newMinHz, float newMaxHz) noexcept
{
    const auto r = pitchdetail::computeRange (sr, lowestHz, newMinHz, newMaxHz);
    minHz = r.minHz;
    maxHz = r.maxHz;
    maxLag = r.maxLag;
    minLag = r.minLag;
    n = r.window;
}

PitchEstimate MpmDetector::detect (const float* window, int numSamples) noexcept
{
    if (numSamples < n || n <= 0 || prefix.size() < idx (n + 1))
        return {};

    const float* x = window + (numSamples - n);

    prefix[0] = 0.0;
    for (int i = 0; i < n; ++i)
        prefix[idx (i + 1)] = prefix[idx (i)] + static_cast<double> (x[i]) * static_cast<double> (x[i]);

    if (prefix[idx (n)] / n < pitchdetail::kSilenceMeanSquare)
        return {};

    auto& fft = ffts.get (n + maxLag);
    const int L = fft.size();

    std::fill (padX.begin(), padX.begin() + L, 0.0f);
    std::copy (x, x + n, padX.begin());
    fft.forward (padX.data(), xRe.data(), xIm.data());

    for (int k = 0; k <= L / 2; ++k)
    {
        const auto i = idx (k);
        xRe[i] = xRe[i] * xRe[i] + xIm[i] * xIm[i];
        xIm[i] = 0.0f;
    }

    fft.inverse (xRe.data(), xIm.data(), acf.data());

    // n(tau) = 2 r(tau) / m(tau), m(tau) = sum_{j < n - tau} (x[j]^2 + x[j+tau]^2).
    const double total = prefix[idx (n)];
    nsdf[0] = 1.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        const double m = prefix[idx (n - tau)] + (total - prefix[idx (tau)]);
        nsdf[idx (tau)] = m > 1.0e-20 ? static_cast<float> (2.0 * acf[idx (tau)] / m) : 0.0f;
    }

    // Key maxima: highest point of each positive lobe after the first
    // negative-going zero crossing.
    constexpr int maxKeys = 128;
    int keyTau[maxKeys];
    int numKeys = 0;

    int tau = 1;
    while (tau <= maxLag && nsdf[idx (tau)] > 0.0f)
        ++tau;

    while (tau <= maxLag && numKeys < maxKeys)
    {
        // Skip the negative part.
        while (tau <= maxLag && nsdf[idx (tau)] <= 0.0f)
            ++tau;
        if (tau > maxLag)
            break;

        int peak = tau;
        while (tau <= maxLag && nsdf[idx (tau)] > 0.0f)
        {
            if (nsdf[idx (tau)] > nsdf[idx (peak)])
                peak = tau;
            ++tau;
        }

        // Ignore peaks outside the range or at the truncated edge.
        if (peak >= minLag && peak < maxLag)
            keyTau[numKeys++] = peak;
    }

    if (numKeys == 0)
        return {};

    float highest = 0.0f;
    for (int i = 0; i < numKeys; ++i)
        highest = std::max (highest, nsdf[idx (keyTau[i])]);

    const float cutoff = kConst * highest;
    int chosen = keyTau[0];
    for (int i = 0; i < numKeys; ++i)
    {
        if (nsdf[idx (keyTau[i])] >= cutoff)
        {
            chosen = keyTau[i];
            break;
        }
    }

    double tauExact = chosen;
    double peakValue = nsdf[idx (chosen)];
    if (chosen > 1)
    {
        const double ym1 = nsdf[idx (chosen - 1)], y0 = nsdf[idx (chosen)], yp1 = nsdf[idx (chosen + 1)];
        const double off = pitchdetail::parabolicOffset (ym1, y0, yp1);
        tauExact += off;
        peakValue = y0 - 0.25 * (ym1 - yp1) * off;
    }

    const float clarity = static_cast<float> (std::clamp (peakValue, 0.0, 1.0));
    if (clarity <= 0.0f || tauExact <= 0.0)
        return {};

    return { static_cast<float> (sr / tauExact), clarity };
}

} // namespace m2m
