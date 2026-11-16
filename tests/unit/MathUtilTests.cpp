#include "m2m/audio/MathUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("MathUtil: dB conversions")
{
    REQUIRE_THAT (m2m::dbToGain (0.0f), WithinAbs (1.0, 1e-6));
    REQUIRE_THAT (m2m::dbToGain (-20.0f), WithinAbs (0.1, 1e-6));
    REQUIRE_THAT (m2m::dbToGain (-6.0206f), WithinAbs (0.5, 1e-4));
    REQUIRE_THAT (m2m::gainToDb (1.0f), WithinAbs (0.0, 1e-5));
    REQUIRE_THAT (m2m::gainToDb (0.01f), WithinAbs (-40.0, 1e-4));
    REQUIRE_THAT (m2m::gainToDb (m2m::dbToGain (-37.5f)), WithinAbs (-37.5, 1e-4));
    REQUIRE (m2m::gainToDb (0.0f) == -120.0f);
    REQUIRE (m2m::gainToDb (-1.0f) == -120.0f);
    REQUIRE (m2m::gainToDb (1e-9f, -90.0f) == -90.0f);
}

TEST_CASE ("MathUtil: pitch conversions")
{
    REQUIRE_THAT (m2m::hzToMidi (440.0), WithinAbs (69.0, 1e-9));
    REQUIRE_THAT (m2m::hzToMidi (880.0), WithinAbs (81.0, 1e-9));
    REQUIRE_THAT (m2m::hzToMidi (261.6255653), WithinAbs (60.0, 1e-6));
    REQUIRE_THAT (m2m::hzToMidi (452.893), WithinAbs (69.5, 1e-3));
    REQUIRE_THAT (m2m::midiToHz (69.0), WithinAbs (440.0, 1e-9));
    REQUIRE_THAT (m2m::midiToHz (57.0), WithinAbs (220.0, 1e-9));
    REQUIRE_THAT (m2m::midiToHz (m2m::hzToMidi (123.4)), WithinAbs (123.4, 1e-9));
    REQUIRE (m2m::hzToMidi (0.0) == 0.0);
}

TEST_CASE ("MathUtil: powers of two")
{
    STATIC_REQUIRE (m2m::isPowerOfTwo (1));
    STATIC_REQUIRE (m2m::isPowerOfTwo (1024));
    STATIC_REQUIRE_FALSE (m2m::isPowerOfTwo (0));
    STATIC_REQUIRE_FALSE (m2m::isPowerOfTwo (-4));
    STATIC_REQUIRE_FALSE (m2m::isPowerOfTwo (1000));
    STATIC_REQUIRE (m2m::nextPowerOfTwo (0) == 1);
    STATIC_REQUIRE (m2m::nextPowerOfTwo (1) == 1);
    STATIC_REQUIRE (m2m::nextPowerOfTwo (3) == 4);
    STATIC_REQUIRE (m2m::nextPowerOfTwo (1024) == 1024);
    STATIC_REQUIRE (m2m::nextPowerOfTwo (1025) == 2048);
}
