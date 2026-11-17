#pragma once

namespace m2m
{

/** Input conditioning: DC blocker followed by a 2nd-order Butterworth
    high-pass (default 60 Hz) to remove rumble and handling noise.
    Non-finite input samples (NaN, Inf) are replaced by 0 and the input is
    limited to +-64 (+36 dBFS), so a corrupt host buffer cannot poison the
    filter state; tiny state values are flushed to 0 (no denormals).
    Real-time safe; process() works in place (in may equal out). */
class Preprocessor
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** Cheap, real-time safe; keeps filter state. hz <= 0 disables the
        high-pass (the DC blocker stays active). */
    void setHighPassHz (float hz) noexcept;
    float getHighPassHz() const noexcept { return hpHz; }

    void process (const float* in, float* out, int numSamples) noexcept;

private:
    void updateCoefficients() noexcept;

    double sr = 48000.0;
    float hpHz = 60.0f;

    // DC blocker: y = x - x1 + r * y1
    double dcR = 0.0, dcX1 = 0.0, dcY1 = 0.0;

    // Biquad (transposed direct form II), normalised a0 = 1.
    bool hpEnabled = true;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;
};

} // namespace m2m
