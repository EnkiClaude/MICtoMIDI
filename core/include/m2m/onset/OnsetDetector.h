#pragma once

#include "m2m/audio/Fft.h"

#include <cstdint>
#include <vector>

namespace m2m
{

/** One detected onset. samplePosition is absolute (counted from the last
    reset()/prepare()) and is the estimated *true* start of the sound, which
    lies in the past by up to detectionDelaySamples() when it is reported. */
struct OnsetEvent
{
    std::int64_t samplePosition = 0;
    float strength = 0.0f; // detection-function value at the peak (>= 0)
};

struct OnsetParams
{
    float sensitivity  = 0.5f;   // 0..1; higher = lower adaptive margin
    float refractoryMs = 40.0f;  // min distance between two onsets
    float minLevelDb   = -45.0f; // absolute floor: quieter hits are ignored (dBFS peak)
};

/** Streaming percussive onset detector (PLAN.md section 6).

    Detection function per hop (hop 64 @ 48 kHz, FFT 256 @ 48 kHz, both scaled
    with the sample rate):
        d = mean positive difference of log-compressed magnitudes (spectral flux)
          + broadband rise of the hop energy (dB / 20) against the previous hops.
    Adaptive threshold: moving median of d over ~100 ms + margin (from
    sensitivity) + absolute floor; a candidate must also exceed minLevelDb.
    Peak picking uses one hop of look-ahead. The onset time is then refined
    with a fine (sub-hop) envelope: the sub-block with the largest level rise
    in the frame that triggered.

    Output does not depend on how the input is split into blocks.
    prepare() allocates; everything else is real-time safe. */
class OnsetDetector
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    void setParams (const OnsetParams& p) noexcept;
    const OnsetParams& getParams() const noexcept { return params; }

    /** Feed n samples; writes at most maxOut onsets to out, returns the count.
        (With n <= hopSize() at most one onset can be produced.) */
    int process (const float* mono, int n, OnsetEvent* out, int maxOut) noexcept;

    int hopSize() const noexcept { return hop; }
    int fftSize() const noexcept { return fftN; }

    /** Upper bound of (report time - estimated onset): the peak frame plus one
        hop of look-ahead. The typical delay is about 2-3 hops. */
    int detectionDelaySamples() const noexcept { return fftN + 2 * hop; }

    /** Samples consumed since reset. */
    std::int64_t position() const noexcept { return pos; }

    /** Last detection-function value and threshold (diagnostics). */
    float lastDetection() const noexcept { return lastD; }
    float lastThreshold() const noexcept { return lastThr; }

private:
    bool analyseHop (OnsetEvent& ev) noexcept; // called when a hop completes
    std::int64_t refineOnset (std::int64_t searchStart, std::int64_t searchEnd) const noexcept;
    float median() noexcept;

    OnsetParams params;
    double sr = 48000.0;
    int hop = 64, fftN = 256, numBins = 129;
    int subBlock = 16;           // fine envelope resolution
    std::int64_t refractory = 0;
    float margin = 0.5f;
    float minPeak = 0.0f;        // linear minLevel

    Fft fft;
    std::vector<float> window, frame, mags, prevLog, curLog;
    std::vector<float> history;  // ring of the last fftN input samples
    int histWrite = 0;
    int hopFill = 0;
    float hopEnergy = 0.0f, hopPeak = 0.0f;

    // detection-function history (for median and peak picking)
    std::vector<float> dHist, medScratch;
    int dWrite = 0, dCount = 0;
    float dPrev1 = 0.0f, dPrev2 = 0.0f; // d[n-1], d[n-2]
    float thrPrev1 = 0.0f;
    float peakPrev1 = 0.0f, peakCur = 0.0f; // frame peak levels
    std::vector<float> eHist;           // ring of recent hop energies (dB), ~20 ms
    int eWrite = 0;

    // fine envelope: per sub-block peak (dB), ring indexed by absolute sub-block
    std::vector<float> fineDb;
    int fineMask = 0;
    float subPeak = 0.0f;
    int subFill = 0;

    std::int64_t pos = 0;           // samples consumed
    std::int64_t lastOnset = -1;    // last reported onset sample
    bool haveLast = false;
    float postMask = 0.0f, postMaskDecay = 0.95f;
    int framesSeen = 0;
    float lastD = 0.0f, lastThr = 0.0f;
};

} // namespace m2m
