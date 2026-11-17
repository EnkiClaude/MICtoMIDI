#pragma once

#include "m2m/pitch/PitchDetector.h"

#include <vector>

namespace m2m
{

/** McLeod Pitch Method (McLeod & Wyvill, 2005).

    - Normalised square difference function n(tau) = 2 r(tau) / m(tau) over the
      whole window, with the autocorrelation r(tau) computed by FFT.
    - Key maxima: the highest point of every positive lobe after the first
      negative-going zero crossing, restricted to [tau_min, tau_max].
    - Pick the first key maximum >= k * (highest key maximum), k = 0.9 default.
    - Parabolic interpolation; confidence = the interpolated peak ("clarity").

    Window: pitchWindowSize (sampleRate, minHz) = 2 * maxLag samples. */
class MpmDetector final : public IPitchDetector
{
public:
    void prepare (double sampleRate, float lowestMinHz = kLowestPitchHz) override;
    void setRange (float minHz, float maxHz) noexcept override;
    int windowSize() const noexcept override { return n; }
    PitchEstimate detect (const float* window, int numSamples) noexcept override;

    /** Key-maximum selection constant (default 0.9). */
    void setK (float k) noexcept { kConst = k; }
    float getK() const noexcept { return kConst; }

private:
    double sr = 48000.0;
    float lowestHz = kLowestPitchHz;
    float minHz = 80.0f, maxHz = 1000.0f;
    float kConst = 0.9f;

    int maxLag = 0, minLag = 2, n = 0;

    FftBank ffts;
    std::vector<float> padX, acf, xRe, xIm;
    std::vector<double> prefix;
    std::vector<float> nsdf;
};

} // namespace m2m
