#pragma once

#include <vector>

namespace m2m
{

/** Self-contained real FFT for power-of-two sizes (no external dependencies).

    Implemented as a half-size iterative radix-2 complex FFT plus the standard
    real-signal split, so it is O(n log n). All tables and scratch buffers are
    allocated in prepare(); forward()/inverse()/magnitudes() never allocate.

    Conventions: forward is unscaled, X[k] = sum x[n] e^{-2 pi i k n / N};
    inverse is scaled by 1/N so inverse (forward (x)) == x. */
class Fft
{
public:
    /** size must be a power of two >= 2; other values are rounded up to the
        next power of two (minimum 2). Allocates. */
    void prepare (int size);

    int size() const noexcept { return n; }

    /** in: size() real samples. outRe/outIm: size()/2 + 1 bins. */
    void forward (const float* in, float* outRe, float* outIm) noexcept;

    /** Inverse of forward: size()/2 + 1 bins -> size() real samples.
        The imaginary parts of the DC and Nyquist bins are ignored. */
    void inverse (const float* inRe, const float* inIm, float* out) noexcept;

    /** Magnitude spectrum |X[k]| (unscaled): size()/2 + 1 values. */
    void magnitudes (const float* in, float* outMag) noexcept;

private:
    void complexForward (float* re, float* im) const noexcept; // in place, size m

    int n = 0;  // real size
    int m = 0;  // complex (half) size
    std::vector<int> bitRev;              // m
    std::vector<float> twRe, twIm;        // m/2: e^{-2 pi i j / m}
    std::vector<float> splitRe, splitIm;  // m+1: e^{-2 pi i k / n}
    std::vector<float> workRe, workIm;    // m
    std::vector<float> magRe, magIm;      // m+1
};

} // namespace m2m
