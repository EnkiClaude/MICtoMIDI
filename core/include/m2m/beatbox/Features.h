#pragma once

#include "m2m/audio/Fft.h"

#include <array>
#include <vector>

namespace m2m
{

/** Named indices into FeatureVector (PLAN.md section 6). */
enum class Feature : int
{
    BandLow,          // fraction of power  < 150 Hz            (0..1)
    BandLowMid,       // fraction of power  150 Hz .. 1 kHz     (0..1)
    BandHighMid,      // fraction of power  1 kHz .. 5 kHz      (0..1)
    BandHigh,         // fraction of power  > 5 kHz             (0..1)
    CentroidOct,      // spectral centroid, log2 (Hz / 1 kHz)
    SpreadOct,        // power-weighted std. dev. of log2 frequency (octaves)
    RolloffOct,       // 85 % rolloff frequency, log2 (Hz / 1 kHz)
    Flatness,         // geometric / arithmetic mean of the power spectrum (0..1)
    ZeroCrossingRate, // zero crossings per millisecond
    CrestDb,          // peak / RMS in dB
    AttackMs,         // time from window start to the envelope maximum
    EarlyDecayDb,     // energy of 2nd half vs 1st half of the window in dB, scaled
                      // to a 20 ms window (0 = sustained, -10 = fast decay)
    SubOnsets,        // number of separate bursts in the window (claps: >= 2)
    LevelDb,          // RMS level of the window, dBFS
    Count
};

constexpr int numFeatures = static_cast<int> (Feature::Count);

/** Short identifier of a feature ("bandLow", ...). */
const char* featureName (Feature f) noexcept;

struct FeatureVector
{
    std::array<float, numFeatures> v {};

    float& operator[] (Feature f) noexcept             { return v[static_cast<std::size_t> (f)]; }
    float operator[] (Feature f) const noexcept        { return v[static_cast<std::size_t> (f)]; }
    float& operator[] (int i) noexcept                 { return v[static_cast<std::size_t> (i)]; }
    float operator[] (int i) const noexcept            { return v[static_cast<std::size_t> (i)]; }

    bool operator== (const FeatureVector&) const = default;
};

/** Computes a FeatureVector from the window that follows an onset.

    The window is Hann-weighted and zero-padded to a power-of-two FFT that
    fits the longest window (2048 for 20-40 ms at 48 kHz). prepare() allocates;
    extract() is real-time safe. */
class FeatureExtractor
{
public:
    /** maxWindowMs: the longest window extract() will be given (10..40 ms). */
    void prepare (double sampleRate, float maxWindowMs = 40.0f);

    /** Number of samples for a window of 'ms' at the prepared rate. */
    int windowSamples (float ms) const noexcept;
    int maxWindowSamples() const noexcept { return maxN; }

    /** window: n samples starting at the onset; n is clamped to
        maxWindowSamples(). Optional pre: the preN samples just before the
        onset; their power spectrum (scaled to the window length) is
        subtracted before the spectral features are computed, so the decaying
        tail of a previous sound (e.g. a kick under a hat) does not dominate.
        Temporal features always use the raw window. */
    FeatureVector extract (const float* window, int n, const float* pre = nullptr, int preN = 0) noexcept;

private:
    void computeSpectral (int n, FeatureVector& f) noexcept;
    void computeTemporal (const float* x, int n, FeatureVector& f) noexcept;

    double sr = 48000.0;
    int maxN = 1920, fftN = 4096, numBins = 2049;
    int hannLen = -1, preHannLen = -1;
    double preScale = 0.0;

    Fft fft;
    std::vector<float> hann, preHann, frame, mags, preMags, logFreq, scratch;
};

} // namespace m2m
