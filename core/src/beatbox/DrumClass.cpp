#include "m2m/beatbox/DrumClass.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

const char* drumClassName (DrumClass c) noexcept
{
    switch (c)
    {
        case DrumClass::Kick:      return "kick";
        case DrumClass::Snare:     return "snare";
        case DrumClass::Clap:      return "clap";
        case DrumClass::ClosedHat: return "closedhat";
        case DrumClass::OpenHat:   return "openhat";
        case DrumClass::TomLow:    return "tomlow";
        case DrumClass::TomHigh:   return "tomhigh";
        case DrumClass::Count:     break;
    }
    return "?";
}

const char* drumClassDisplayName (DrumClass c) noexcept
{
    switch (c)
    {
        case DrumClass::Kick:      return "Kick";
        case DrumClass::Snare:     return "Snare";
        case DrumClass::Clap:      return "Clap";
        case DrumClass::ClosedHat: return "Closed Hat";
        case DrumClass::OpenHat:   return "Open Hat";
        case DrumClass::TomLow:    return "Low Tom";
        case DrumClass::TomHigh:   return "High Tom";
        case DrumClass::Count:     break;
    }
    return "?";
}

float acceptanceThreshold (float minScore, float sensitivity) noexcept
{
    const float m = std::isfinite (minScore) ? std::clamp (minScore, 0.0f, 1.0f) : 0.35f;
    const float s = std::isfinite (sensitivity) ? std::clamp (sensitivity, 0.0f, 1.0f) : 0.5f;

    if (s >= 0.5f)
        return m * (1.0f - s) * 2.0f;            // 0.5 -> m, 1 -> 0

    // 0.5 -> m, 0 -> 1.01 (never accepted, scores are <= 1)
    const float t = (0.5f - s) * 2.0f;
    return m + t * (1.01f - m);
}

} // namespace m2m
