#include "m2m/audio/Preprocessor.h"
#include "m2m/audio/MathUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace
{
constexpr double sr = 48000.0;

std::vector<float> sine (double hz, double seconds, float offset = 0.0f)
{
    std::vector<float> v (static_cast<std::size_t> (seconds * sr));
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = offset + static_cast<float> (std::sin (2.0 * std::numbers::pi * hz
                                                      * static_cast<double> (i) / sr));
    return v;
}

/** RMS over the second half (after the filter has settled). */
double tailRms (const std::vector<float>& v)
{
    double sum = 0.0;
    const std::size_t start = v.size() / 2;
    for (std::size_t i = start; i < v.size(); ++i)
        sum += static_cast<double> (v[i]) * v[i];
    return std::sqrt (sum / static_cast<double> (v.size() - start));
}

double tailMean (const std::vector<float>& v)
{
    double sum = 0.0;
    const std::size_t start = v.size() / 2;
    for (std::size_t i = start; i < v.size(); ++i)
        sum += v[i];
    return sum / static_cast<double> (v.size() - start);
}

double gainDb (double hz)
{
    m2m::Preprocessor pre;
    pre.prepare (sr);
    auto x = sine (hz, 2.0);
    const double inRms = tailRms (x);
    pre.process (x.data(), x.data(), static_cast<int> (x.size())); // in place
    return 20.0 * std::log10 (tailRms (x) / inRms);
}
} // namespace

TEST_CASE ("Preprocessor: removes DC offset")
{
    m2m::Preprocessor pre;
    pre.prepare (sr);
    const auto in = sine (440.0, 2.0, 0.5f);
    std::vector<float> out (in.size());
    pre.process (in.data(), out.data(), static_cast<int> (in.size()));
    REQUIRE (std::abs (tailMean (out)) < 1e-3);
}

TEST_CASE ("Preprocessor: 1 kHz passes within 0.5 dB")
{
    REQUIRE (std::abs (gainDb (1000.0)) < 0.5);
}

TEST_CASE ("Preprocessor: 20 Hz is attenuated by more than 12 dB")
{
    REQUIRE (gainDb (20.0) < -12.0);
}

TEST_CASE ("Preprocessor: block size does not change the output")
{
    const auto in = sine (90.0, 0.5, 0.1f);

    m2m::Preprocessor a;
    a.prepare (sr);
    std::vector<float> ref (in.size());
    a.process (in.data(), ref.data(), static_cast<int> (in.size()));

    const int block = GENERATE (1, 64, 441);
    m2m::Preprocessor b;
    b.prepare (sr);
    std::vector<float> out (in.size());
    for (std::size_t pos = 0; pos < in.size(); pos += static_cast<std::size_t> (block))
    {
        const auto n = std::min<std::size_t> (static_cast<std::size_t> (block), in.size() - pos);
        b.process (in.data() + pos, out.data() + pos, static_cast<int> (n));
    }
    REQUIRE (out == ref);
}

TEST_CASE ("Preprocessor: reset clears state and high-pass can be disabled")
{
    m2m::Preprocessor pre;
    pre.prepare (sr);
    REQUIRE (pre.getHighPassHz() == 60.0f);

    auto x = sine (30.0, 1.0);
    pre.process (x.data(), x.data(), static_cast<int> (x.size()));
    pre.reset();

    std::vector<float> zeros (256, 0.0f);
    pre.process (zeros.data(), zeros.data(), 256);
    for (float v : zeros)
        REQUIRE (v == 0.0f);

    pre.setHighPassHz (0.0f);
    pre.reset();
    auto y = sine (30.0, 2.0);
    const double inRms = tailRms (y);
    pre.process (y.data(), y.data(), static_cast<int> (y.size()));
    REQUIRE (std::abs (m2m::gainToDb (static_cast<float> (tailRms (y) / inRms))) < 1.0f);
}
