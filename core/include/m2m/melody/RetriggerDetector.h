#pragma once

#include <array>

namespace m2m
{

/** Lightweight amplitude-onset detector used by Melody Mode for retriggers
    ("da-da-da" on one pitch). Runs once per analysis hop on the envelope level.

    Fires when the level has risen by at least riseDb above its minimum over
    the last lookbackMs, the level is at least minLevelDb, and refractoryMs have
    passed since the previous onset. Real-time safe, no allocation. */
class RetriggerDetector
{
public:
    static constexpr int maxHistory = 64;

    /** hopSeconds: time between process() calls. */
    void prepare (double hopSeconds) noexcept;

    void setParams (float riseDb, float lookbackMs, float refractoryMs, float minLevelDb) noexcept;

    /** Sensitivity 0..1 -> required rise: 20 dB (0) .. 4 dB (1); 0.5 -> 12 dB. */
    static float riseDbForSensitivity (float sensitivity) noexcept;

    void reset() noexcept;

    /** Feed the level (dBFS) at the end of a hop; returns true on an onset. */
    bool process (float levelDb) noexcept;

private:
    void updateDerived() noexcept;

    double hopSec = 128.0 / 48000.0;
    float rise = 15.0f, lookback = 40.0f, refractory = 60.0f, minLevel = -45.0f;
    int lookbackHops = 15, refractoryHops = 22;

    std::array<float, maxHistory> history {};
    int writePos = 0, count = 0;
    int hopsSinceOnset = 1 << 20;
};

} // namespace m2m
