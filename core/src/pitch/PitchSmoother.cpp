#include "m2m/pitch/PitchSmoother.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace m2m
{

void PitchSmoother::setMedianLength (int frames) noexcept
{
    const int newLength = std::clamp (frames, 1, maxMedianLength);
    if (newLength != length)
    {
        length = newLength;
        reset();
    }
}

void PitchSmoother::setOnePoleCoefficient (float a) noexcept
{
    coeff = std::isfinite (a) ? std::clamp (a, 0.0f, 0.999f) : 0.0f;
}

float PitchSmoother::coefficientFor (float timeMs, double frameRate) noexcept
{
    if (! (timeMs > 0.0f) || ! (frameRate > 0.0))
        return 0.0f;
    return static_cast<float> (std::exp (-1000.0 / (static_cast<double> (timeMs) * frameRate)));
}

void PitchSmoother::reset() noexcept
{
    count = 0;
    writePos = 0;
    hasState = false;
}

float PitchSmoother::process (float midi) noexcept
{
    history[static_cast<std::size_t> (writePos)] = midi;
    writePos = (writePos + 1) % length;
    count = std::min (count + 1, length);

    std::copy (history.begin(), history.begin() + count, sorted.begin());
    std::sort (sorted.begin(), sorted.begin() + count);

    const auto mid = static_cast<std::size_t> (count / 2);
    const float median = (count % 2 == 1) ? sorted[mid] : 0.5f * (sorted[mid - 1] + sorted[mid]);

    if (coeff <= 0.0f || ! hasState)
    {
        state = median;
        hasState = true;
        return median;
    }

    state = median + coeff * (state - median);
    return state;
}

} // namespace m2m
