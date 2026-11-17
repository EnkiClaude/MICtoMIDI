#include "m2m/beatbox/RuleClassifier.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

namespace
{
    /** Linear ramp: 0 at x = from, 1 at x = to (works for from > to as well). */
    float ramp (float x, float from, float to) noexcept
    {
        const float t = (x - from) / (to - from);
        return t >= 0.0f ? std::min (t, 1.0f) : 0.0f; // NaN -> 0
    }
}

void RuleClassifier::scores (const FeatureVector& f, float* out) noexcept
{
    const float low      = f[Feature::BandLow];
    const float lowMid   = f[Feature::BandLowMid];
    const float highMid  = f[Feature::BandHighMid];
    const float high     = f[Feature::BandHigh];
    const float centroid = f[Feature::CentroidOct];
    const float spread   = f[Feature::SpreadOct];
    const float flat     = f[Feature::Flatness];
    const float decay    = f[Feature::EarlyDecayDb];
    const float bursts   = f[Feature::SubOnsets];

    const float noisy = highMid + high;

    // Kick: low-frequency dominated.
    const float kick = ramp (low + 0.3f * lowMid, 0.25f, 0.6f)
                     * ramp (centroid, 0.0f, -1.5f)
                     * ramp (noisy, 0.5f, 0.2f);

    // Snare: body + noise, wide spread.
    const float snare = ramp (spread, 1.3f, 1.9f)
                      * ramp (noisy, 0.15f, 0.4f)
                      * ramp (lowMid + low, 0.05f, 0.2f)
                      * ramp (low, 0.5f, 0.25f);

    // Clap: mid-band noise, narrower than a snare, multiple bursts boost it.
    const float clap = ramp (highMid + 0.4f * high, 0.4f, 0.7f)
                     * ramp (spread, 1.9f, 1.4f)
                     * ramp (lowMid + low, 0.35f, 0.15f)
                     * ramp (high, 0.8f, 0.6f)
                     * (0.6f + 0.4f * ramp (bursts, 1.0f, 2.0f));

    // Hats: high-band dominated; open vs closed by the decay inside the window.
    const float hat = ramp (high, 0.5f, 0.85f) * ramp (centroid, 1.5f, 2.8f);
    const float closedHat = hat * ramp (decay, -2.5f, -6.0f);
    const float openHat   = hat * ramp (decay, -6.0f, -2.5f);

    // Toms: tonal, low-mid band.
    const float tom = ramp (lowMid + low, 0.5f, 0.8f) * ramp (flat, 0.05f, 0.01f) * ramp (noisy, 0.4f, 0.15f);
    const float tomLow  = tom * ramp (centroid, -1.6f, -2.6f) * ramp (low, 0.7f, 0.4f);
    const float tomHigh = tom * ramp (centroid, -2.6f, -1.6f) * ramp (centroid, 0.0f, -0.7f);

    out[toIndex (DrumClass::Kick)]      = kick;
    out[toIndex (DrumClass::Snare)]     = snare;
    out[toIndex (DrumClass::Clap)]      = clap;
    out[toIndex (DrumClass::ClosedHat)] = closedHat;
    out[toIndex (DrumClass::OpenHat)]   = openHat;
    out[toIndex (DrumClass::TomLow)]    = tomLow;
    out[toIndex (DrumClass::TomHigh)]   = tomHigh;
}

Classification RuleClassifier::classify (const FeatureVector& f) const noexcept
{
    float s[numDrumClasses] {};
    scores (f, s);

    Classification result;
    float best = -1.0f;
    for (int c = 0; c < numDrumClasses; ++c)
    {
        if (! params.enabled[static_cast<std::size_t> (c)])
            continue;
        if (s[c] > best)
        {
            best = s[c];
            result.cls = static_cast<DrumClass> (c);
        }
    }

    if (best < 0.0f)
        return {};

    result.score = best;
    result.accepted = best > 0.0f
                   && best >= acceptanceThreshold (params.minScore,
                                                   params.sensitivity[static_cast<std::size_t> (toIndex (result.cls))]);
    return result;
}

} // namespace m2m
