#pragma once

#include "m2m/audio/Fft.h"

#include <cmath>
#include <vector>

namespace m2m
{

/** Result of one pitch analysis frame. hz == 0 means unvoiced (no usable
    periodicity); confidence is then also 0. */
struct PitchEstimate
{
    float hz = 0.0f;
    float confidence = 0.0f; // 0..1, higher = more periodic

    bool isVoiced() const noexcept { return hz > 0.0f; }
};

/** Lowest minimum frequency any detector is prepared for by default. Buffers
    are sized for this so later range changes never reallocate. */
inline constexpr float kLowestPitchHz = 50.0f;

/** Analysis window rule shared by all detectors:

        maxLag     = ceil (sampleRate / minHz) + 2      (one period of minHz + interpolation margin)
        windowSize = 2 * maxLag                          (~2 periods of minHz)

    The detectors compare the first half of the window against lags up to
    maxLag, so the oldest maxLag + period samples must contain the tone. */
inline int pitchMaxLag (double sampleRate, float minHz) noexcept
{
    return static_cast<int> (std::ceil (sampleRate / static_cast<double> (minHz))) + 2;
}

inline int pitchWindowSize (double sampleRate, float minHz) noexcept
{
    return 2 * pitchMaxLag (sampleRate, minHz);
}

/** Monophonic F0 estimator.

    Lifecycle: prepare() allocates for the worst case (lowestMinHz). setRange()
    and detect() are real-time safe and never allocate. */
class IPitchDetector
{
public:
    virtual ~IPitchDetector() = default;

    /** Allocates everything needed for any range with minHz >= lowestMinHz. */
    virtual void prepare (double sampleRate, float lowestMinHz = kLowestPitchHz) = 0;

    /** Search range in Hz. minHz is clamped to the prepared lowest value and
        maxHz to [minHz * 1.5, sampleRate / 4]. Real-time safe. */
    virtual void setRange (float minHz, float maxHz) noexcept = 0;

    /** Number of samples detect() wants (see pitchWindowSize()). */
    virtual int windowSize() const noexcept = 0;

    /** Analyse the most recent windowSize() samples of window[0..n). Returns
        unvoiced if n < windowSize(). Real-time safe. */
    virtual PitchEstimate detect (const float* window, int n) noexcept = 0;
};

/** Internal helper: one prepared Fft per power-of-two size so the detectors can
    use the smallest transform for the current range without reallocating. */
class FftBank
{
public:
    void prepare (int maxSize)
    {
        ffts.clear();
        int s = 64;
        while (s < maxSize)
            s *= 2;
        maxFftSize = s;
        for (int size = 64; size <= maxFftSize; size *= 2)
        {
            ffts.emplace_back();
            ffts.back().prepare (size);
        }
    }

    /** Smallest prepared transform with size >= minSize (or the largest). */
    Fft& get (int minSize) noexcept
    {
        std::size_t i = 0;
        while (i + 1 < ffts.size() && ffts[i].size() < minSize)
            ++i;
        return ffts[i];
    }

    int maxSize() const noexcept { return maxFftSize; }

private:
    std::vector<Fft> ffts;
    int maxFftSize = 64;
};

} // namespace m2m
