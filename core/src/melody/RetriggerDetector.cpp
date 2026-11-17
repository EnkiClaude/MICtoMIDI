#include "m2m/melody/RetriggerDetector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace m2m
{

void RetriggerDetector::prepare (double hopSeconds) noexcept
{
    hopSec = hopSeconds > 0.0 ? hopSeconds : 128.0 / 48000.0;
    updateDerived();
    reset();
}

void RetriggerDetector::setParams (float riseDb, float lookbackMs, float refractoryMs, float minLevelDb) noexcept
{
    rise = std::isfinite (riseDb) ? std::max (0.5f, riseDb) : 15.0f;
    lookback = lookbackMs;
    refractory = refractoryMs;
    minLevel = minLevelDb;
    updateDerived();
}

float RetriggerDetector::riseDbForSensitivity (float sensitivity) noexcept
{
    const float s = sensitivity >= 0.0f ? std::min (sensitivity, 1.0f) : 0.0f; // NaN -> 0
    return 20.0f - 16.0f * s;
}

void RetriggerDetector::updateDerived() noexcept
{
    const auto hops = [this] (float ms)
    {
        const double h = std::isfinite (ms) && ms > 0.0f ? static_cast<double> (ms) * 0.001 / hopSec : 0.0;
        return static_cast<int> (std::ceil (h));
    };
    lookbackHops = std::clamp (hops (lookback), 1, maxHistory);
    refractoryHops = std::max (0, hops (refractory));
}

void RetriggerDetector::reset() noexcept
{
    writePos = 0;
    count = 0;
    hopsSinceOnset = 1 << 20;
}

bool RetriggerDetector::process (float levelDb) noexcept
{
    float minimum = levelDb;
    const int n = std::min (count, lookbackHops);
    for (int i = 1; i <= n; ++i)
    {
        const int k = (writePos - i + maxHistory) % maxHistory;
        minimum = std::min (minimum, history[static_cast<std::size_t> (k)]);
    }

    history[static_cast<std::size_t> (writePos)] = levelDb;
    writePos = (writePos + 1) % maxHistory;
    count = std::min (count + 1, maxHistory);

    if (hopsSinceOnset < (1 << 20))
        ++hopsSinceOnset;

    const bool onset = n > 0 && levelDb - minimum >= rise && levelDb >= minLevel
                    && hopsSinceOnset >= refractoryHops;
    if (onset)
        hopsSinceOnset = 0;
    return onset;
}

} // namespace m2m
