#include "m2m/audio/EnvelopeFollower.h"

#include <cmath>

namespace m2m
{

float EnvelopeFollower::coeffFor (double sampleRate, float ms)
{
    if (ms <= 0.0f)
        return 0.0f; // instantaneous
    // Time to reach ~63% of a step.
    return static_cast<float> (std::exp (-1.0 / (sampleRate * static_cast<double> (ms) * 0.001)));
}

void EnvelopeFollower::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    setTimes (attackMsValue, releaseMsValue);
    reset();
}

void EnvelopeFollower::setTimes (float attackMs, float releaseMs)
{
    attackMsValue = attackMs;
    releaseMsValue = releaseMs;
    attackCoeff = coeffFor (sr, attackMs);
    releaseCoeff = coeffFor (sr, releaseMs);
}

} // namespace m2m
