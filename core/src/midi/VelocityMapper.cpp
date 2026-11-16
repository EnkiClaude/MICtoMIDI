#include "m2m/midi/VelocityMapper.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

int VelocityMapper::map (float peakDb) const noexcept
{
    const auto& p = params;

    // Normalised level 0..1. NaN or degenerate ranges are handled explicitly.
    float norm = 0.0f;
    if (p.ceilingDb > p.floorDb)
    {
        const float t = (peakDb - p.floorDb) / (p.ceilingDb - p.floorDb);
        norm = t >= 0.0f ? std::min (t, 1.0f) : 0.0f; // NaN -> 0
    }
    else
    {
        norm = peakDb >= p.ceilingDb ? 1.0f : 0.0f;
    }

    const float exponent = std::isfinite (p.curve) ? std::max (p.curve, 0.01f) : 1.0f;
    const float shaped = std::pow (norm, exponent);

    const int lo = std::clamp (std::min (p.minVelocity, p.maxVelocity), 1, 127);
    const int hi = std::clamp (std::max (p.minVelocity, p.maxVelocity), 1, 127);
    const float levelVel = static_cast<float> (lo) + shaped * static_cast<float> (hi - lo);

    const float fixedVel = static_cast<float> (std::clamp (p.fixedVelocity, 1, 127));
    const float s = p.sensitivity >= 0.0f ? std::min (p.sensitivity, 1.0f) : 0.0f; // NaN -> 0
    const float v = fixedVel + s * (levelVel - fixedVel);

    return std::clamp (static_cast<int> (std::lround (v)), 1, 127);
}

} // namespace m2m
