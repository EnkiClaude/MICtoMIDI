#include "m2m/pitch/YinDetector.h"

#include "PitchCommon.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

using pitchdetail::idx;

void YinDetector::prepare (double sampleRate, float lowestMinHz)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    lowestHz = lowestMinHz > 1.0f ? lowestMinHz : kLowestPitchHz;

    const int worstLag = pitchMaxLag (sr, lowestHz);
    const int worstWindow = 2 * worstLag;

    ffts.prepare (worstWindow);
    const int fftMax = ffts.maxSize();

    padA.assign (idx (fftMax), 0.0f);
    padX.assign (idx (fftMax), 0.0f);
    corr.assign (idx (fftMax), 0.0f);
    aRe.assign (idx (fftMax / 2 + 1), 0.0f);
    aIm.assign (idx (fftMax / 2 + 1), 0.0f);
    xRe.assign (idx (fftMax / 2 + 1), 0.0f);
    xIm.assign (idx (fftMax / 2 + 1), 0.0f);
    prefix.assign (idx (worstWindow + 1), 0.0);
    dPrime.assign (idx (worstLag + 2), 1.0f);

    setRange (minHz, maxHz);
}

void YinDetector::setRange (float newMinHz, float newMaxHz) noexcept
{
    const auto r = pitchdetail::computeRange (sr, lowestHz, newMinHz, newMaxHz);
    minHz = r.minHz;
    maxHz = r.maxHz;
    maxLag = r.maxLag;
    minLag = r.minLag;
    n = r.window;
}

PitchEstimate YinDetector::detect (const float* window, int numSamples) noexcept
{
    if (numSamples < n || n <= 0 || prefix.size() < idx (n + 1))
        return {};

    const float* x = window + (numSamples - n);
    const int w = maxLag; // integration window

    prefix[0] = 0.0;
    for (int i = 0; i < n; ++i)
        prefix[idx (i + 1)] = prefix[idx (i)] + static_cast<double> (x[i]) * static_cast<double> (x[i]);

    if (prefix[idx (n)] / n < pitchdetail::kSilenceMeanSquare)
        return {};

    // r(tau) = sum_{j<W} x[j] x[j+tau] for tau <= maxLag. Since j + tau <= n - 1
    // the circular correlation of size >= n has no wrap-around.
    auto& fft = ffts.get (n);
    const int L = fft.size();

    std::fill (padA.begin(), padA.begin() + L, 0.0f);
    std::fill (padX.begin(), padX.begin() + L, 0.0f);
    std::copy (x, x + w, padA.begin());
    std::copy (x, x + n, padX.begin());

    fft.forward (padA.data(), aRe.data(), aIm.data());
    fft.forward (padX.data(), xRe.data(), xIm.data());

    for (int k = 0; k <= L / 2; ++k)
    {
        const auto i = idx (k);
        const float re = aRe[i] * xRe[i] + aIm[i] * xIm[i];
        const float im = aRe[i] * xIm[i] - aIm[i] * xRe[i];
        aRe[i] = re;
        aIm[i] = im;
    }

    fft.inverse (aRe.data(), aIm.data(), corr.data());

    // Cumulative-mean-normalised difference.
    const double e0 = prefix[idx (w)];
    dPrime[0] = 1.0f;
    double runningSum = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        const double eTau = prefix[idx (tau + w)] - prefix[idx (tau)];
        const double d = std::max (0.0, e0 + eTau - 2.0 * static_cast<double> (corr[idx (tau)]));
        runningSum += d;
        dPrime[idx (tau)] = runningSum > 0.0 ? static_cast<float> (d * tau / runningSum) : 1.0f;
    }

    // Absolute threshold: first dip below threshold, followed to its minimum.
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (dPrime[idx (tau)] < threshold)
        {
            while (tau + 1 <= maxLag && dPrime[idx (tau + 1)] < dPrime[idx (tau)])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        // No dip below threshold: global minimum (low confidence).
        best = minLag;
        for (int tau = minLag + 1; tau <= maxLag; ++tau)
            if (dPrime[idx (tau)] < dPrime[idx (best)])
                best = tau;
    }

    double tauExact = best;
    double dBest = dPrime[idx (best)];
    if (best > minLag && best < maxLag)
    {
        const double ym1 = dPrime[idx (best - 1)], y0 = dPrime[idx (best)], yp1 = dPrime[idx (best + 1)];
        const double off = pitchdetail::parabolicOffset (ym1, y0, yp1);
        tauExact += off;
        dBest = y0 - 0.25 * (ym1 - yp1) * off;
    }
    else
    {
        // A minimum at the edge of the search range is not a real dip.
        return {};
    }

    const float confidence = static_cast<float> (std::clamp (1.0 - dBest, 0.0, 1.0));
    if (confidence <= 0.0f || tauExact <= 0.0)
        return {};

    return { static_cast<float> (sr / tauExact), confidence };
}

} // namespace m2m
