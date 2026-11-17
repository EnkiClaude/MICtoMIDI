#pragma once

// Internal helpers shared by the pitch detectors (not part of the public API).

#include "m2m/pitch/PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace m2m::pitchdetail
{

inline std::size_t idx (int i) noexcept { return static_cast<std::size_t> (i); }

/** Mean-square level below which a window is treated as silence (-100 dBFS RMS). */
inline constexpr double kSilenceMeanSquare = 1.0e-10;

struct Range
{
    float minHz, maxHz;
    int minLag, maxLag, window;
};

inline Range computeRange (double sr, float lowestHz, float minHzIn, float maxHzIn) noexcept
{
    Range r {};
    const float nyqLimit = static_cast<float> (sr * 0.25);
    r.minHz = std::isfinite (minHzIn) ? std::clamp (minHzIn, lowestHz, nyqLimit / 1.5f) : lowestHz;
    r.maxHz = std::isfinite (maxHzIn) ? std::clamp (maxHzIn, r.minHz * 1.5f, nyqLimit) : nyqLimit;
    r.maxLag = pitchMaxLag (sr, r.minHz);
    r.window = 2 * r.maxLag;
    r.minLag = std::max (2, static_cast<int> (std::floor (sr / static_cast<double> (r.maxHz))));
    return r;
}

/** Parabolic interpolation offset (-0.5..0.5) of the extremum of y around the
    middle of three equally spaced samples. */
inline double parabolicOffset (double ym1, double y0, double yp1) noexcept
{
    const double denom = ym1 - 2.0 * y0 + yp1;
    if (std::abs (denom) < 1.0e-20)
        return 0.0;
    return std::clamp (0.5 * (ym1 - yp1) / denom, -0.5, 0.5);
}

} // namespace m2m::pitchdetail
