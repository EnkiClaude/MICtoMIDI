#include "m2m/audio/Fft.h"
#include "m2m/audio/Window.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
std::size_t sz (int n) { return static_cast<std::size_t> (n); }
}

TEST_CASE ("Fft: prepare rounds up to a power of two")
{
    m2m::Fft fft;
    fft.prepare (1000);
    REQUIRE (fft.size() == 1024);
    fft.prepare (1);
    REQUIRE (fft.size() == 2);
    fft.prepare (512);
    REQUIRE (fft.size() == 512);
}

TEST_CASE ("Fft: impulse has a flat spectrum")
{
    const int n = GENERATE (2, 4, 64, 1024);
    CAPTURE (n);
    m2m::Fft fft;
    fft.prepare (n);

    std::vector<float> x (sz (n), 0.0f), re (sz (n / 2 + 1)), im (sz (n / 2 + 1));
    x[0] = 1.0f;
    fft.forward (x.data(), re.data(), im.data());
    for (int k = 0; k <= n / 2; ++k)
    {
        REQUIRE_THAT (re[sz (k)], WithinAbs (1.0, 1e-5));
        REQUIRE_THAT (im[sz (k)], WithinAbs (0.0, 1e-5));
    }

    // Delayed impulse: unit magnitude, linear phase.
    std::fill (x.begin(), x.end(), 0.0f);
    x[1] = 1.0f;
    fft.forward (x.data(), re.data(), im.data());
    for (int k = 0; k <= n / 2; ++k)
    {
        const double a = -2.0 * std::numbers::pi * k / n;
        REQUIRE_THAT (re[sz (k)], WithinAbs (std::cos (a), 1e-5));
        REQUIRE_THAT (im[sz (k)], WithinAbs (std::sin (a), 1e-5));
    }
}

TEST_CASE ("Fft: bin-centred sine lands in a single bin")
{
    const int n = 512;
    const int bin = GENERATE (1, 7, 100, 255);
    CAPTURE (bin);
    m2m::Fft fft;
    fft.prepare (n);

    std::vector<float> x (sz (n)), mag (sz (n / 2 + 1));
    for (int i = 0; i < n; ++i)
        x[sz (i)] = static_cast<float> (std::sin (2.0 * std::numbers::pi * bin * i / n));

    fft.magnitudes (x.data(), mag.data());
    for (int k = 0; k <= n / 2; ++k)
    {
        if (k == bin)
            REQUIRE_THAT (mag[sz (k)], WithinRel (n / 2.0, 1e-4));
        else
            REQUIRE (mag[sz (k)] < 1e-2f);
    }

    // Cosine phase: purely real bin.
    for (int i = 0; i < n; ++i)
        x[sz (i)] = static_cast<float> (std::cos (2.0 * std::numbers::pi * bin * i / n));
    std::vector<float> re (sz (n / 2 + 1)), im (sz (n / 2 + 1));
    fft.forward (x.data(), re.data(), im.data());
    REQUIRE_THAT (re[sz (bin)], WithinRel (n / 2.0, 1e-4));
    REQUIRE_THAT (im[sz (bin)], WithinAbs (0.0, 1e-2));
}

TEST_CASE ("Fft: DC and Nyquist bins")
{
    const int n = 64;
    m2m::Fft fft;
    fft.prepare (n);
    std::vector<float> x (sz (n)), re (sz (n / 2 + 1)), im (sz (n / 2 + 1));
    for (int i = 0; i < n; ++i)
        x[sz (i)] = 0.5f + ((i % 2) == 0 ? 0.25f : -0.25f);
    fft.forward (x.data(), re.data(), im.data());
    REQUIRE_THAT (re[0], WithinAbs (0.5 * n, 1e-4));
    REQUIRE_THAT (re[sz (n / 2)], WithinAbs (0.25 * n, 1e-4));
    for (int k = 1; k < n / 2; ++k)
        REQUIRE_THAT (re[sz (k)], WithinAbs (0.0, 1e-4));
}

TEST_CASE ("Fft: inverse (forward (x)) == x and Parseval holds")
{
    const int n = GENERATE (64, 128, 256, 512, 1024, 2048, 4096);
    CAPTURE (n);
    m2m::Fft fft;
    fft.prepare (n);

    std::mt19937 rng (1234u + static_cast<unsigned> (n));
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    std::vector<float> x (sz (n)), y (sz (n)), re (sz (n / 2 + 1)), im (sz (n / 2 + 1));
    for (auto& v : x)
        v = dist (rng);

    fft.forward (x.data(), re.data(), im.data());
    fft.inverse (re.data(), im.data(), y.data());

    float maxErr = 0.0f;
    for (int i = 0; i < n; ++i)
        maxErr = std::max (maxErr, std::abs (x[sz (i)] - y[sz (i)]));
    REQUIRE (maxErr < 1e-5f);

    // Parseval for a real signal using only the half spectrum.
    double timeEnergy = 0.0;
    for (float v : x)
        timeEnergy += static_cast<double> (v) * v;

    double freqEnergy = 0.0;
    for (int k = 0; k <= n / 2; ++k)
    {
        const double p = static_cast<double> (re[sz (k)]) * re[sz (k)]
                       + static_cast<double> (im[sz (k)]) * im[sz (k)];
        freqEnergy += (k == 0 || k == n / 2) ? p : 2.0 * p;
    }
    freqEnergy /= n;
    REQUIRE_THAT (freqEnergy, WithinRel (timeEnergy, 1e-4));
}

TEST_CASE ("makeHann: periodic Hann window")
{
    std::vector<float> w (8);
    m2m::makeHann (w.data(), 8);
    REQUIRE_THAT (w[0], WithinAbs (0.0, 1e-7));
    REQUIRE_THAT (w[4], WithinAbs (1.0, 1e-7));
    REQUIRE_THAT (w[2], WithinAbs (0.5, 1e-6));
    REQUIRE_THAT (w[1], WithinAbs (w[7], 1e-6)); // periodic symmetry
    REQUIRE_THAT (w[3], WithinAbs (w[5], 1e-6));
}
