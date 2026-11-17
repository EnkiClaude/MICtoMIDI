#pragma once

#include "m2m/audio/Fft.h"
#include "m2m/audio/Window.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace m2m::test::util
{

/** A path in the system temp directory, unique per test name. Removed on destruction. */
struct TempFile
{
    explicit TempFile (const std::string& name)
        : path ((std::filesystem::temp_directory_path() / ("m2m_support_test_" + name)).string()) {}
    ~TempFile() { std::error_code ec; std::filesystem::remove (path, ec); }

    std::string path;
};

/** Averaged (Welch, Hann) power spectrum of buf with frames of size n (power of two). */
inline std::vector<double> powerSpectrum (const std::vector<float>& buf, int n, std::size_t start = 0,
                                          std::size_t length = 0)
{
    if (length == 0)
        length = buf.size() - start;

    m2m::Fft fft;
    fft.prepare (n);
    std::vector<float> win (static_cast<std::size_t> (n)), frame (static_cast<std::size_t> (n)),
                       mag (static_cast<std::size_t> (n / 2 + 1));
    m2m::makeHann (win.data(), n);

    std::vector<double> power (static_cast<std::size_t> (n / 2 + 1), 0.0);
    const auto un = static_cast<std::size_t> (n);
    int frames = 0;
    for (std::size_t pos = start; pos + un <= start + length; pos += un / 2)
    {
        for (std::size_t i = 0; i < un; ++i)
            frame[i] = buf[pos + i] * win[i];
        fft.magnitudes (frame.data(), mag.data());
        for (std::size_t k = 0; k < mag.size(); ++k)
            power[k] += static_cast<double> (mag[k]) * static_cast<double> (mag[k]);
        ++frames;
    }
    for (auto& p : power)
        p /= std::max (1, frames);
    return power;
}

/** Frequency of the strongest bin in [minHz, maxHz], refined by parabolic interpolation. */
inline double peakFrequency (const std::vector<double>& power, double sr, int n,
                             double minHz = 20.0, double maxHz = 20000.0)
{
    const double binHz = sr / n;
    const auto lo = static_cast<std::size_t> (std::max (1.0, std::floor (minHz / binHz)));
    const auto hi = std::min (power.size() - 2, static_cast<std::size_t> (std::ceil (maxHz / binHz)));
    std::size_t best = lo;
    for (std::size_t k = lo; k <= hi; ++k)
        if (power[k] > power[best])
            best = k;

    const double a = std::log (power[best - 1] + 1e-30);
    const double b = std::log (power[best] + 1e-30);
    const double c = std::log (power[best + 1] + 1e-30);
    const double denom = a - 2.0 * b + c;
    const double delta = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
    return (static_cast<double> (best) + delta) * binHz;
}

/** Sum of power between two frequencies. */
inline double bandPower (const std::vector<double>& power, double sr, int n, double loHz, double hiHz)
{
    const double binHz = sr / n;
    double acc = 0.0;
    for (std::size_t k = 0; k < power.size(); ++k)
    {
        const double f = static_cast<double> (k) * binHz;
        if (f >= loHz && f < hiHz)
            acc += power[k];
    }
    return acc;
}

/** Frequency estimate from rising zero crossings (pure tones only). */
inline double zeroCrossingFrequency (const std::vector<float>& buf, double sr)
{
    std::size_t first = 0, last = 0;
    int crossings = 0;
    for (std::size_t i = 1; i < buf.size(); ++i)
    {
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * sr / static_cast<double> (last - first);
}

} // namespace m2m::test::util
