#include "m2m/melody/Stability.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace m2m;

TEST_CASE ("Stability: the default amount gives exactly the MelodyParams defaults", "[melody][stability]")
{
    const MelodyParams d;
    const auto m = applyStability (d, kStabilityDefault);
    CHECK (m.changeHysteresisCents == d.changeHysteresisCents);
    CHECK (m.minStableMs == d.minStableMs);
    CHECK (m.medianFrames == d.medianFrames);
    CHECK (m.smoothingMs == d.smoothingMs);
    CHECK (m.minNoteMs == d.minNoteMs);
    CHECK (m.lockSettleMs == d.lockSettleMs);

    MelodyEngine a, b;
    a.prepare (48000.0, 256);
    b.prepare (48000.0, 256);
    b.setParams (m);
    CHECK (a.latencySamples() == b.latencySamples());
}

TEST_CASE ("Stability: every mapped value and the latency are monotonic in the amount", "[melody][stability]")
{
    MelodyParams prev = applyStability ({}, 0.0f);
    MelodyEngine e;
    e.prepare (48000.0, 256);
    e.setParams (prev);
    int prevFree = e.latencySamples();
    auto lockOf = [] (MelodyParams p) { p.pitchFollow = PitchFollow::Lock; return p; };
    e.setParams (lockOf (prev));
    int prevLock = e.latencySamples();

    for (int i = 1; i <= 100; ++i)
    {
        const float a = static_cast<float> (i) / 100.0f;
        const auto m = applyStability ({}, a);
        CAPTURE (a);
        CHECK (m.changeHysteresisCents >= prev.changeHysteresisCents);
        CHECK (m.minStableMs >= prev.minStableMs);
        CHECK (m.medianFrames >= prev.medianFrames);
        CHECK (m.smoothingMs >= prev.smoothingMs);
        CHECK (m.minNoteMs >= prev.minNoteMs);
        CHECK (m.lockSettleMs >= prev.lockSettleMs);

        e.setParams (m);
        CHECK (e.latencySamples() >= prevFree);
        prevFree = e.latencySamples();
        e.setParams (lockOf (m));
        CHECK (e.latencySamples() >= prevLock);
        prevLock = e.latencySamples();
        prev = m;
    }

    // Tight really is tighter than Loose (and costs latency).
    const auto loose = applyStability ({}, 0.0f), tight = applyStability ({}, 1.0f);
    CHECK (tight.changeHysteresisCents > loose.changeHysteresisCents);
    CHECK (tight.minStableMs > loose.minStableMs);
    CHECK (tight.medianFrames > loose.medianFrames);
    MelodyEngine l, t;
    l.prepare (48000.0, 256);
    t.prepare (48000.0, 256);
    l.setParams (loose);
    t.setParams (tight);
    CHECK (t.latencySamples() > l.latencySamples());
}

TEST_CASE ("Stability: clamps, NaN and untouched fields", "[melody][stability]")
{
    MelodyParams p;
    p.scale = Scale::Dorian;
    p.transpose = -5;
    p.pitchFollow = PitchFollow::Lock;
    p.onThresholdDb = -33.0f;
    p.releaseMs = 77.0f;

    const auto m = applyStability (p, 0.9f);
    CHECK (m.scale == Scale::Dorian);
    CHECK (m.transpose == -5);
    CHECK (m.pitchFollow == PitchFollow::Lock);
    CHECK (m.onThresholdDb == -33.0f);
    CHECK (m.releaseMs == 77.0f);

    CHECK (applyStability (p, -3.0f).changeHysteresisCents == applyStability (p, 0.0f).changeHysteresisCents);
    CHECK (applyStability (p, 7.0f).minStableMs == applyStability (p, 1.0f).minStableMs);
    const auto n = applyStability (p, std::numeric_limits<float>::quiet_NaN());
    CHECK (n.minStableMs == applyStability (p, kStabilityDefault).minStableMs);
}
