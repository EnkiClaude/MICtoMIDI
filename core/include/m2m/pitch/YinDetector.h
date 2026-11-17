#pragma once

#include "m2m/pitch/PitchDetector.h"

#include <vector>

namespace m2m
{

/** YIN F0 estimator (de Cheveigne & Kawahara, 2002).

    - Difference function d(tau) = e(0) + e(tau) - 2 r(tau) over an integration
      window of W = maxLag samples, with the cross term r(tau) computed by FFT.
    - Cumulative-mean-normalised difference d'(tau).
    - Absolute threshold: the first dip below `threshold` (followed down to its
      local minimum); if there is none, the global minimum in range.
    - Parabolic interpolation around the chosen lag.
    - confidence = 1 - d'(tau) clamped to 0..1.

    Window: pitchWindowSize (sampleRate, minHz) = 2 * maxLag samples. */
class YinDetector final : public IPitchDetector
{
public:
    void prepare (double sampleRate, float lowestMinHz = kLowestPitchHz) override;
    void setRange (float minHz, float maxHz) noexcept override;
    int windowSize() const noexcept override { return n; }
    PitchEstimate detect (const float* window, int numSamples) noexcept override;

    /** First-dip threshold on d' (default 0.15). */
    void setThreshold (float t) noexcept { threshold = t; }
    float getThreshold() const noexcept { return threshold; }

private:
    double sr = 48000.0;
    float lowestHz = kLowestPitchHz;
    float minHz = 80.0f, maxHz = 1000.0f;
    float threshold = 0.15f;

    int maxLag = 0;   // tau_max (also integration length W)
    int minLag = 2;   // tau_min
    int n = 0;        // window size = 2 * maxLag

    FftBank ffts;
    std::vector<float> padA, padX, corr;
    std::vector<float> aRe, aIm, xRe, xIm;
    std::vector<double> prefix;     // prefix sums of x^2
    std::vector<float> dPrime;      // d'(tau), 0..maxLag
};

} // namespace m2m
