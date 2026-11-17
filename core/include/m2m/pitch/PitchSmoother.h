#pragma once

#include <array>

namespace m2m
{

/** Smooths a frame-rate pitch track in MIDI (semitone) units.

    Stage 1: running median over the last N voiced frames (default 3). A median
    of 3 removes single-frame octave glitches and delays a clean step by one
    frame only.
    Stage 2: optional one-pole low-pass (coefficient 0 = off, the default).

    reset() is called on unvoiced frames so a new note never inherits the old
    pitch. Real-time safe, no allocation. */
class PitchSmoother
{
public:
    static constexpr int maxMedianLength = 15;

    /** Clamped to 1..maxMedianLength. */
    void setMedianLength (int frames) noexcept;
    int getMedianLength() const noexcept { return length; }

    /** One-pole feedback coefficient in [0, 1): y += (1 - a) (x - y). 0 = off. */
    void setOnePoleCoefficient (float a) noexcept;

    /** Coefficient for a time constant in ms at a given frame rate (frames/s). */
    static float coefficientFor (float timeMs, double frameRate) noexcept;

    void reset() noexcept;

    /** Feed one voiced frame (fractional MIDI); returns the smoothed value. */
    float process (float midi) noexcept;

private:
    std::array<float, maxMedianLength> history {};
    std::array<float, maxMedianLength> sorted {};
    int length = 3;
    int count = 0;     // valid entries (<= length)
    int writePos = 0;
    float coeff = 0.0f;
    float state = 0.0f;
    bool hasState = false;
};

} // namespace m2m
