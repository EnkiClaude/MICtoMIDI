#pragma once

#include <cmath>
#include <numbers>
#include <vector>

namespace m2m::test
{

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

/** Append a sine of the given length/level to buf. */
inline void appendSine (std::vector<float>& buf, double sr, double seconds,
                        double freqHz, float levelDb)
{
    const auto n = static_cast<std::size_t> (seconds * sr);
    const float amp = dbToGain (levelDb);
    const std::size_t start = buf.size();
    buf.resize (start + n);
    for (std::size_t i = 0; i < n; ++i)
        buf[start + i] = amp * static_cast<float> (std::sin (2.0 * std::numbers::pi * freqHz
                                                              * static_cast<double> (i) / sr));
}

inline void appendSilence (std::vector<float>& buf, double sr, double seconds)
{
    buf.resize (buf.size() + static_cast<std::size_t> (seconds * sr), 0.0f);
}

} // namespace m2m::test
