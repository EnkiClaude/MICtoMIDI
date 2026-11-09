#include "m2m/audio/EnvelopeFollower.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("EnvelopeFollower rises with attack and falls with release")
{
    constexpr double sr = 48000.0;
    m2m::EnvelopeFollower f;
    f.prepare (sr);
    f.setTimes (1.0f, 50.0f);

    // One attack time constant of a unit step -> ~63%.
    float env = 0.0f;
    for (int i = 0; i < 48; ++i)
        env = f.process (1.0f);
    REQUIRE_THAT (env, WithinAbs (0.632, 0.02));

    // Settle fully, then one release time constant -> ~37%.
    for (int i = 0; i < 4800; ++i)
        f.process (1.0f);
    for (int i = 0; i < 2400; ++i)
        env = f.process (0.0f);
    REQUIRE_THAT (env, WithinAbs (0.368, 0.02));
}

TEST_CASE ("EnvelopeFollower rectifies negative input")
{
    m2m::EnvelopeFollower f;
    f.prepare (48000.0);
    f.setTimes (0.0f, 50.0f);
    REQUIRE_THAT (f.process (-0.5f), WithinAbs (0.5, 1e-6));
}

TEST_CASE ("EnvelopeFollower reset clears state")
{
    m2m::EnvelopeFollower f;
    f.prepare (48000.0);
    f.setTimes (0.0f, 50.0f);
    f.process (1.0f);
    f.reset();
    REQUIRE (f.current() == 0.0f);
}
