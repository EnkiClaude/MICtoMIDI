#include "m2m/audio/Preprocessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace m2m
{

namespace
{
    constexpr double dcCutoffHz = 10.0;
}

void Preprocessor::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    dcR = std::exp (-2.0 * std::numbers::pi * dcCutoffHz / sr);
    updateCoefficients();
    reset();
}

void Preprocessor::reset() noexcept
{
    dcX1 = dcY1 = 0.0;
    s1 = s2 = 0.0;
}

void Preprocessor::setHighPassHz (float hz) noexcept
{
    hpHz = hz;
    updateCoefficients();
}

void Preprocessor::updateCoefficients() noexcept
{
    hpEnabled = hpHz > 0.0f;
    if (! hpEnabled)
        return;

    // RBJ cookbook high-pass, Q = 1/sqrt(2) (Butterworth).
    const double f = std::min (static_cast<double> (hpHz), 0.45 * sr);
    const double w0 = 2.0 * std::numbers::pi * f / sr;
    const double cosw = std::cos (w0);
    const double alpha = std::sin (w0) / std::numbers::sqrt2; // sin(w0) / (2Q)
    const double a0 = 1.0 + alpha;

    b0 = (1.0 + cosw) * 0.5 / a0;
    b1 = -(1.0 + cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

void Preprocessor::process (const float* in, float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double x = in[i];

        const double dc = x - dcX1 + dcR * dcY1;
        dcX1 = x;
        dcY1 = dc;

        double y = dc;
        if (hpEnabled)
        {
            y = b0 * dc + s1;
            s1 = b1 * dc - a1 * y + s2;
            s2 = b2 * dc - a2 * y;
        }

        out[i] = static_cast<float> (y);
    }
}

} // namespace m2m
