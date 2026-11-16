#include "m2m/midi/VelocityMapper.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE ("VelocityMapper: defaults map floor..ceiling to 1..127 monotonically")
{
    m2m::VelocityMapper vm;
    REQUIRE (vm.map (-40.0f) == 1);
    REQUIRE (vm.map (-6.0f) == 127);
    REQUIRE (vm.map (-23.0f) == 64);

    int prev = 0;
    for (float db = -60.0f; db <= 6.0f; db += 0.25f)
    {
        const int v = vm.map (db);
        REQUIRE (v >= prev);
        prev = v;
    }
}

TEST_CASE ("VelocityMapper: output is clamped to 1..127")
{
    m2m::VelocityMapper vm;
    m2m::VelocityParams p;
    p.minVelocity = -50;
    p.maxVelocity = 500;
    vm.setParams (p);
    REQUIRE (vm.map (-200.0f) == 1);
    REQUIRE (vm.map (40.0f) == 127);
    REQUIRE (vm.map (std::numeric_limits<float>::quiet_NaN()) == 1);
    REQUIRE (vm.map (-std::numeric_limits<float>::infinity()) == 1);
    REQUIRE (vm.map (std::numeric_limits<float>::infinity()) == 127);

    p = {};
    p.minVelocity = 30;
    p.maxVelocity = 90;
    vm.setParams (p);
    REQUIRE (vm.map (-100.0f) == 30);
    REQUIRE (vm.map (0.0f) == 90);

    p = {};
    p.sensitivity = 0.0f;
    p.fixedVelocity = 300;
    vm.setParams (p);
    REQUIRE (vm.map (-20.0f) == 127);
}

TEST_CASE ("VelocityMapper: sensitivity blends fixed and level-derived velocity")
{
    m2m::VelocityMapper vm;
    m2m::VelocityParams p;
    p.fixedVelocity = 100;

    p.sensitivity = 0.0f;
    vm.setParams (p);
    REQUIRE (vm.map (-80.0f) == 100);
    REQUIRE (vm.map (-23.0f) == 100);
    REQUIRE (vm.map (0.0f) == 100);

    p.sensitivity = 1.0f;
    vm.setParams (p);
    REQUIRE (vm.map (-40.0f) == 1);
    REQUIRE (vm.map (-6.0f) == 127);

    p.sensitivity = 0.5f;
    vm.setParams (p);
    REQUIRE (vm.map (-40.0f) == 51);  // (100 + 1) / 2 rounded
    REQUIRE (vm.map (-6.0f) == 114);  // (100 + 127) / 2 rounded
}

TEST_CASE ("VelocityMapper: curve shapes the response")
{
    m2m::VelocityMapper vm;
    m2m::VelocityParams p;

    vm.setParams (p);
    const int linear = vm.map (-23.0f);

    p.curve = 0.5f;
    vm.setParams (p);
    const int louder = vm.map (-23.0f);

    p.curve = 2.0f;
    vm.setParams (p);
    const int softer = vm.map (-23.0f);

    REQUIRE (louder > linear);
    REQUIRE (softer < linear);

    // Endpoints are unaffected by the curve.
    REQUIRE (vm.map (-40.0f) == 1);
    REQUIRE (vm.map (-6.0f) == 127);
}
