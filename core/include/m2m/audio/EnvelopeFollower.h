#pragma once

namespace m2m
{

/** Peak envelope follower with separate attack and release one-pole smoothing.
    Output is linear amplitude (0..~1). Real-time safe. */
class EnvelopeFollower
{
public:
    void prepare (double sampleRate);
    void setTimes (float attackMs, float releaseMs);
    void reset() noexcept { env = 0.0f; }

    float process (float x) noexcept
    {
        const float rectified = x < 0.0f ? -x : x;
        const float coeff = rectified > env ? attackCoeff : releaseCoeff;
        env = rectified + coeff * (env - rectified);
        return env;
    }

    float current() const noexcept { return env; }

private:
    static float coeffFor (double sampleRate, float ms);

    double sr = 48000.0;
    float attackMsValue = 1.0f, releaseMsValue = 50.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float env = 0.0f;
};

} // namespace m2m
