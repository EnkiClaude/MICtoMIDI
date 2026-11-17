#include "m2m/pitch/PitchSmoother.h"
#include "m2m/pitch/ScaleSnap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace m2m;

TEST_CASE ("PitchSmoother: median removes single-frame glitches", "[pitch][smoother]")
{
    PitchSmoother s;
    s.setMedianLength (3);
    CHECK (s.process (60.0f) == Catch::Approx (60.0f));
    CHECK (s.process (60.1f) == Catch::Approx (60.05f)); // two frames: mean of the middle pair
    CHECK (s.process (72.0f) == Catch::Approx (60.1f));  // octave glitch rejected
    CHECK (s.process (60.0f) == Catch::Approx (60.1f));
    CHECK (s.process (60.0f) == Catch::Approx (60.0f));
}

TEST_CASE ("PitchSmoother: a clean step is delayed by at most one frame", "[pitch][smoother]")
{
    PitchSmoother s;
    s.setMedianLength (3);
    for (int i = 0; i < 5; ++i)
        s.process (60.0f);
    CHECK (s.process (64.0f) == Catch::Approx (60.0f));
    CHECK (s.process (64.0f) == Catch::Approx (64.0f));
}

TEST_CASE ("PitchSmoother: reset forgets history; one-pole smooths", "[pitch][smoother]")
{
    PitchSmoother s;
    s.setMedianLength (3);
    s.process (60.0f);
    s.process (60.0f);
    s.reset();
    CHECK (s.process (70.0f) == Catch::Approx (70.0f));

    s.reset();
    s.setMedianLength (1);
    s.setOnePoleCoefficient (0.5f);
    CHECK (s.process (60.0f) == Catch::Approx (60.0f));
    CHECK (s.process (62.0f) == Catch::Approx (61.0f));
    CHECK (s.process (62.0f) == Catch::Approx (61.5f));

    CHECK (PitchSmoother::coefficientFor (0.0f, 375.0) == 0.0f);
    CHECK (PitchSmoother::coefficientFor (10.0f, 375.0) > 0.0f);
}

TEST_CASE ("ScaleSnap: chromatic rounds to nearest, ties go down", "[pitch][snap]")
{
    ScaleSnap s;
    s.setScale (Scale::Off, 0);
    CHECK (s.snap (60.0f) == 60);
    CHECK (s.snap (60.49f) == 60);
    CHECK (s.snap (60.5f) == 60);
    CHECK (s.snap (60.51f) == 61);
    CHECK (s.snap (59.6f) == 60);
    CHECK (s.snap (-0.7f) == -1);
}

TEST_CASE ("ScaleSnap: C major snaps C#-ish pitches", "[pitch][snap]")
{
    ScaleSnap s;
    s.setScale (Scale::Major, 0);
    CHECK (s.snap (61.0f) == 60);  // exact C#: tie between C and D -> lower
    CHECK (s.snap (61.2f) == 62);  // sharp C# -> D
    CHECK (s.snap (60.8f) == 60);  // flat C# -> C
    CHECK (s.snap (63.0f) == 62);  // D#: tie D/E -> D
    CHECK (s.snap (64.4f) == 64);  // E
    CHECK (s.snap (64.6f) == 65);  // between E and F: nearest F
    CHECK (s.isAllowed (72));
    CHECK_FALSE (s.isAllowed (73));
}

TEST_CASE ("ScaleSnap: every scale and root yields allowed notes", "[pitch][snap]")
{
    ScaleSnap s;
    for (int sc = 0; sc < kNumScales; ++sc)
    {
        for (int root = 0; root < 12; ++root)
        {
            s.setScale (static_cast<Scale> (sc), root);
            CHECK (s.isAllowed (root + 60));
            for (float x = 40.0f; x < 90.0f; x += 0.13f)
            {
                const int n = s.snap (x);
                REQUIRE (s.isAllowed (n));
                REQUIRE (std::abs (static_cast<float> (n) - x) <= 2.0f); // widest gap is 3 semitones
            }
        }
    }

    s.setScale (Scale::MinorPentatonic, 9); // A minor pentatonic: A C D E G
    CHECK (s.snap (70.0f) == 69);           // A#  -> A
    CHECK (s.snap (71.0f) == 72);           // B   -> C
    s.setScale (Scale::Blues, 0);
    CHECK (s.isAllowed (66));               // blue note F#
    s.setScale (Scale::HarmonicMinor, 2);   // D harmonic minor has C#
    CHECK (s.isAllowed (61));
    CHECK_FALSE (s.isAllowed (60));
    s.setScale (Scale::Major, 14);          // root wraps to D
    CHECK (s.getRoot() == 2);
}
