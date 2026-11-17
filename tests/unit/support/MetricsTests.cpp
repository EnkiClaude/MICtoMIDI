#include "m2m/test/Metrics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace m2m::test;
using Catch::Matchers::WithinAbs;

namespace
{
    DetectedNote det (double on, double off, int note) { return { on, off, note, 1, 100 }; }
    using CM = ConfusionMatrix;
}

TEST_CASE ("support/Metrics: perfect detection")
{
    const std::vector<TruthNote> truth { { 0.5, 1.0, 60 }, { 1.5, 2.0, 62 }, { 2.5, 3.0, 64 } };
    const std::vector<DetectedNote> d { det (0.5, 1.0, 60), det (1.5, 2.0, 62), det (2.5, 3.0, 64) };
    const auto m = evaluate (truth, d, 60.0);
    REQUIRE (m.truePositives == 3);
    REQUIRE (m.falsePositives == 0);
    REQUIRE (m.falseNegatives == 0);
    REQUIRE (m.precision == 1.0);
    REQUIRE (m.recall == 1.0);
    REQUIRE (m.f1 == 1.0);
    REQUIRE_THAT (m.meanAbsOnsetErrorMs, WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (m.meanAbsOffsetErrorMs, WithinAbs (0.0, 1e-9));
    REQUIRE (m.offsetPairs == 3);
    REQUIRE (m.falseTriggersPerMinute == 0.0);
    REQUIRE (m.confusion.get (60, 60) == 1);
    REQUIRE (m.confusion.get (64, 64) == 1);
    REQUIRE (m.matches.size() == 3);
}

TEST_CASE ("support/Metrics: missed and spurious notes")
{
    const std::vector<TruthNote> truth { { 0.5, 1.0, 60 }, { 1.5, 2.0, 62 } };
    const std::vector<DetectedNote> d { det (0.5, 1.0, 60), det (5.0, 5.5, 70), det (6.0, 6.5, 71) };
    const auto m = evaluate (truth, d, 30.0);
    REQUIRE (m.truePositives == 1);
    REQUIRE (m.falseNegatives == 1);
    REQUIRE (m.falsePositives == 2);
    REQUIRE_THAT (m.precision, WithinAbs (1.0 / 3.0, 1e-12));
    REQUIRE_THAT (m.recall, WithinAbs (0.5, 1e-12));
    REQUIRE_THAT (m.f1, WithinAbs (0.4, 1e-12));
    REQUIRE (m.spuriousDetections == 2);
    REQUIRE_THAT (m.falseTriggersPerMinute, WithinAbs (4.0, 1e-12)); // 2 in 30 s
    REQUIRE (m.confusion.get (62, CM::kMissed) == 1);
    REQUIRE (m.confusion.get (CM::kSpurious, 70) == 1);
    REQUIRE (m.confusion.get (CM::kSpurious, 71) == 1);
}

TEST_CASE ("support/Metrics: late onsets give signed latency")
{
    const std::vector<TruthNote> truth { { 1.0, 2.0, 60 }, { 3.0, 4.0, 60 } };
    const std::vector<DetectedNote> d { det (1.010, 2.1, 60), det (3.030, 3.9, 60) };
    const auto m = evaluate (truth, d, 10.0);
    REQUIRE (m.f1 == 1.0);
    REQUIRE_THAT (m.meanAbsOnsetErrorMs, WithinAbs (20.0, 1e-6));
    REQUIRE_THAT (m.maxAbsOnsetErrorMs, WithinAbs (30.0, 1e-6));
    REQUIRE_THAT (m.meanSignedOnsetErrorMs, WithinAbs (20.0, 1e-6));
    REQUIRE_THAT (m.meanAbsOffsetErrorMs, WithinAbs (100.0, 1e-6));

    // Early detections give negative signed error.
    const auto e = evaluate (truth, { det (0.990, 2.0, 60), det (2.990, 4.0, 60) }, 10.0);
    REQUIRE_THAT (e.meanSignedOnsetErrorMs, WithinAbs (-10.0, 1e-6));
    REQUIRE_THAT (e.meanAbsOnsetErrorMs, WithinAbs (10.0, 1e-6));
}

TEST_CASE ("support/Metrics: octave and wrong-pitch errors")
{
    const std::vector<TruthNote> truth { { 1.0, 2.0, 48 }, { 3.0, 4.0, 60 }, { 5.0, 6.0, 64 }, { 7.0, 8.0, 67 } };
    const std::vector<DetectedNote> d { det (1.0, 2.0, 60),   // +12
                                        det (3.0, 4.0, 36),   // -24
                                        det (5.0, 6.0, 65),   // wrong pitch
                                        det (7.0, 8.0, 67) }; // correct
    const auto m = evaluate (truth, d, 10.0);
    REQUIRE (m.truePositives == 1);
    REQUIRE (m.octaveErrors == 2);
    REQUIRE (m.wrongPitch == 1);
    REQUIRE (m.spuriousDetections == 0);   // all detections coincide with a truth onset
    REQUIRE (m.falseTriggersPerMinute == 0.0);
    REQUIRE (m.falsePositives == 3);
    REQUIRE (m.falseNegatives == 3);
    REQUIRE (m.confusion.get (48, 60) == 1);
    REQUIRE (m.confusion.get (60, 36) == 1);
    REQUIRE (m.confusion.get (64, 65) == 1);
    REQUIRE (m.confusion.get (67, 67) == 1);

    // Onset-only mode counts them all as hits.
    const auto o = evaluateOnsets (truth, d, 10.0);
    REQUIRE (o.f1 == 1.0);
    REQUIRE (o.octaveErrors == 0);
    REQUIRE_FALSE (o.pitchMatched);
    REQUIRE (o.confusion.get (48, 60) == 1); // onset-only confusion still records the detected pitch
}

TEST_CASE ("support/Metrics: tolerance edges")
{
    const std::vector<TruthNote> truth { { 1.0, 1.0, 36 } };
    REQUIRE (evaluate (truth, { det (1.050, 1.05, 36) }, 10.0).f1 == 1.0);
    REQUIRE (evaluate (truth, { det (0.950, 0.95, 36) }, 10.0).f1 == 1.0);
    REQUIRE (evaluate (truth, { det (1.0501, 1.06, 36) }, 10.0).f1 == 0.0);
    REQUIRE (evaluate (truth, { det (0.9499, 0.95, 36) }, 10.0).f1 == 0.0);

    // Custom tolerance.
    REQUIRE (evaluate (truth, { det (1.020, 1.02, 36) }, 10.0, { 0.010, true }).f1 == 0.0);
    REQUIRE (evaluate (truth, { det (1.010, 1.01, 36) }, 10.0, { 0.010, true }).f1 == 1.0);

    // Drum hits (offset == onset) contribute no offset error.
    const auto m = evaluate (truth, { det (1.0, 1.2, 36) }, 10.0);
    REQUIRE (m.offsetPairs == 0);
    REQUIRE (std::isnan (m.meanAbsOffsetErrorMs));
}

TEST_CASE ("support/Metrics: one-to-one, maximum matching")
{
    // Two detections near one truth note: one hit; the duplicate matches
    // nothing, so it is a false positive and a spurious detection.
    const std::vector<TruthNote> one { { 1.0, 1.5, 60 } };
    const auto dup = evaluate (one, { det (1.0, 1.2, 60), det (1.02, 1.5, 60) }, 60.0);
    REQUIRE (dup.truePositives == 1);
    REQUIRE (dup.falsePositives == 1);
    REQUIRE (dup.spuriousDetections == 1);
    REQUIRE_THAT (dup.meanAbsOnsetErrorMs, WithinAbs (0.0, 1e-9)); // the closer one was chosen

    // d = 0.04 is closest to the second note, d' = -0.03 only reaches the
    // first: maximum-cardinality matching must pair both.
    const std::vector<TruthNote> two { { 0.0, 0.05, 60 }, { 0.06, 0.2, 60 } };
    const auto m = evaluate (two, { det (0.04, 0.05, 60), det (-0.03, 0.0, 60) }, 1.0);
    REQUIRE (m.truePositives == 2);

    // Classic greedy trap: t1 = 0.00, t2 = 0.08; d1 = 0.04 (equidistant),
    // d2 = 0.11 (only near t2). Optimal = 2.
    const std::vector<TruthNote> trap { { 0.00, 0.0, 38 }, { 0.08, 0.08, 38 } };
    const auto t = evaluate (trap, { det (0.04, 0.04, 38), det (0.11, 0.11, 38) }, 1.0);
    REQUIRE (t.truePositives == 2);
}

TEST_CASE ("support/Metrics: empty inputs")
{
    const auto none = evaluate ({}, {}, 10.0);
    REQUIRE (none.f1 == 1.0);
    REQUIRE (none.precision == 1.0);
    REQUIRE (none.recall == 1.0);
    REQUIRE (std::isnan (none.meanAbsOnsetErrorMs));

    const auto falseOnly = evaluate ({}, { det (1.0, 1.1, 60) }, 30.0);
    REQUIRE (falseOnly.f1 == 0.0);
    REQUIRE_THAT (falseOnly.falseTriggersPerMinute, WithinAbs (2.0, 1e-12));

    const auto missedOnly = evaluate ({ { 1.0, 2.0, 60 } }, {}, 30.0);
    REQUIRE (missedOnly.f1 == 0.0);
    REQUIRE (missedOnly.recall == 0.0);
    REQUIRE (missedOnly.confusion.get (60, ConfusionMatrix::kMissed) == 1);

    // Duration falls back to the latest time seen.
    const auto implied = evaluate ({}, { det (29.0, 30.0, 60) }, 0.0);
    REQUIRE_THAT (implied.durationSec, WithinAbs (30.0, 1e-12));
}

TEST_CASE ("support/Metrics: glide check")
{
    // 60 -> 64 glide from 1.0 to 1.2 s; truth: 60 until 1.0, 64 from 1.2.
    const std::vector<TruthNote> truth { { 0.5, 1.0, 60 }, { 1.2, 2.0, 64 } };
    const std::vector<GlideSegment> glides { { 1.0, 1.2, 60, 64 } };
    const MatchOptions opt { 0.05, true };

    // Legato chromatic steps: passing notes are neither hits nor false triggers.
    const std::vector<DetectedNote> steps { det (0.52, 1.06, 60), det (1.06, 1.12, 62), det (1.12, 1.23, 63),
                                            det (1.23, 2.0, 64) };
    auto m = evaluate (truth, steps, 3.0, opt, glides);
    CHECK (m.f1 == 1.0);
    CHECK (m.spuriousDetections == 0);
    CHECK (m.glideChecks == 1);
    CHECK (m.glideFailures == 0);
    CHECK (m.glidePassingNotes == 2);

    // Direct jump (no passing notes) is fine too.
    m = evaluate (truth, { det (0.52, 1.2, 60), det (1.2, 2.0, 64) }, 3.0, opt, glides);
    CHECK (m.f1 == 1.0);
    CHECK (m.glideFailures == 0);

    // Not monotonic.
    m = evaluate (truth, { det (0.52, 1.06, 60), det (1.06, 1.1, 62), det (1.1, 1.15, 61), det (1.15, 2.0, 64) },
                  3.0, opt, glides);
    CHECK (m.glideFailures == 1);

    // Gap between notes.
    m = evaluate (truth, { det (0.52, 1.06, 60), det (1.1, 2.0, 64) }, 3.0, opt, glides);
    CHECK (m.glideFailures == 1);

    // Two notes at once.
    m = evaluate (truth, { det (0.52, 1.15, 60), det (1.1, 2.0, 64) }, 3.0, opt, glides);
    CHECK (m.glideFailures == 1);

    // Wrong end note (and a missed truth note).
    m = evaluate (truth, { det (0.52, 1.2, 60), det (1.2, 2.0, 65) }, 3.0, opt, glides);
    CHECK (m.glideFailures == 1);
    CHECK (m.f1 < 1.0);

    // Without glides the plain evaluation is used.
    CHECK (evaluate (truth, steps, 3.0, opt, {}).f1 < 1.0);
}
