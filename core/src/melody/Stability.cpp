#include "m2m/melody/Stability.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

namespace
{
    struct Anchor
    {
        float hysteresisCents, minStableMs, medianFrames, smoothingMs, minNoteMs, lockSettleMs;
    };

    // Keep in sync with the table in Stability.h. The middle anchor must
    // equal the MelodyParams defaults (checked by the unit tests).
    constexpr Anchor kLoose   { 45.0f,  8.0f, 1.0f,  0.0f, 30.0f, 10.0f };
    constexpr Anchor kDefault { 60.0f, 10.0f, 3.0f,  0.0f, 40.0f, 15.0f };
    constexpr Anchor kTight   { 90.0f, 20.0f, 5.0f, 0.0f, 60.0f, 30.0f };

    float lerp (float a, float b, float x) noexcept { return a + (b - a) * x; }
}

MelodyParams applyStability (MelodyParams p, float amount01) noexcept
{
    const float a = std::isfinite (amount01) ? std::clamp (amount01, 0.0f, 1.0f) : kStabilityDefault;

    const bool lower = a <= kStabilityDefault;
    const Anchor& from = lower ? kLoose : kDefault;
    const Anchor& to = lower ? kDefault : kTight;
    const float x = lower ? a / kStabilityDefault : (a - kStabilityDefault) / (1.0f - kStabilityDefault);

    p.changeHysteresisCents = lerp (from.hysteresisCents, to.hysteresisCents, x);
    p.minStableMs           = lerp (from.minStableMs, to.minStableMs, x);
    p.medianFrames          = static_cast<int> (std::lround (lerp (from.medianFrames, to.medianFrames, x)));
    p.smoothingMs           = lerp (from.smoothingMs, to.smoothingMs, x);
    p.minNoteMs             = lerp (from.minNoteMs, to.minNoteMs, x);
    p.lockSettleMs          = lerp (from.lockSettleMs, to.lockSettleMs, x);
    return p;
}

} // namespace m2m
