#pragma once

#include <cmath>
#include <numbers>

namespace m2m
{

/** Periodic Hann window (suited to STFT analysis): w[i] = 0.5 - 0.5 cos (2 pi i / n). */
inline void makeHann (float* w, int n)
{
    if (n == 1)
    {
        w[0] = 1.0f;
        return;
    }

    for (int i = 0; i < n; ++i)
        w[i] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n));
}

} // namespace m2m
