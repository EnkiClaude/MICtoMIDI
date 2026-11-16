#pragma once

#include <cmath>

namespace m2m
{

/** Decibels -> linear gain. */
inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

/** Linear gain -> decibels; gains at or below the floor (including 0 and
    negative values) return floorDb. */
inline float gainToDb (float g, float floorDb = -120.0f) noexcept
{
    const float floorGain = dbToGain (floorDb);
    return g > floorGain ? 20.0f * std::log10 (g) : floorDb;
}

/** Frequency -> fractional MIDI note (A4 = 440 Hz = 69). Non-positive hz returns 0. */
inline double hzToMidi (double hz) noexcept
{
    return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : 0.0;
}

/** Fractional MIDI note -> frequency (69 = 440 Hz). */
inline double midiToHz (double midi) noexcept
{
    return 440.0 * std::exp2 ((midi - 69.0) / 12.0);
}

constexpr bool isPowerOfTwo (int n) noexcept
{
    return n > 0 && (n & (n - 1)) == 0;
}

/** Smallest power of two >= n (1 for n <= 1). */
constexpr int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n && p < (1 << 30))
        p <<= 1;
    return p;
}

} // namespace m2m
