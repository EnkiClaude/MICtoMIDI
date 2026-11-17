#include "m2m/beatbox/CalibratedClassifier.h"
#include "m2m/beatbox/RuleClassifier.h"

#include "ClassifierTestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <vector>

using namespace m2m;
using namespace m2m::beatboxtest;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int hitsPerClass = 120;

    Confusion runRules (float windowMs, std::uint64_t seed)
    {
        FeatureExtractor fx;
        fx.prepare (sr, 40.0f);
        RuleClassifier rc;
        Rng rng (seed);
        Confusion conf;
        for (int r = 0; r < 5; ++r)
            for (int i = 0; i < hitsPerClass; ++i)
                conf.add (r, rc.classify (hitFeatures (fiveSounds[r], sr, rng, fx, windowMs)));
        return conf;
    }

    CalibratedClassifier calibrate (float windowMs, int examplesPerClass, std::uint64_t seed)
    {
        FeatureExtractor fx;
        fx.prepare (sr, 40.0f);
        CalibratedClassifier cc;
        Rng rng (seed);
        for (auto s : fiveSounds)
            for (int i = 0; i < examplesPerClass; ++i)
                REQUIRE (cc.addExample (expectedClass (s), hitFeatures (s, sr, rng, fx, windowMs)));
        cc.finalize();
        return cc;
    }

    Confusion runCalibrated (const CalibratedClassifier& cc, float windowMs, std::uint64_t seed)
    {
        FeatureExtractor fx;
        fx.prepare (sr, 40.0f);
        Rng rng (seed);
        Confusion conf;
        for (int r = 0; r < 5; ++r)
            for (int i = 0; i < hitsPerClass; ++i)
                conf.add (r, cc.classify (hitFeatures (fiveSounds[r], sr, rng, fx, windowMs)));
        return conf;
    }
}

TEST_CASE ("acceptanceThreshold maps sensitivity around minScore", "[beatbox][classifier]")
{
    CHECK (acceptanceThreshold (0.4f, 0.5f) == 0.4f);
    CHECK (acceptanceThreshold (0.4f, 1.0f) == 0.0f);
    CHECK (acceptanceThreshold (0.4f, 0.0f) > 1.0f);
    CHECK (acceptanceThreshold (0.4f, 0.75f) < 0.4f);
    CHECK (acceptanceThreshold (0.4f, 0.25f) > 0.4f);
}

TEST_CASE ("FeatureExtractor: basic feature sanity", "[beatbox][features]")
{
    FeatureExtractor fx;
    fx.prepare (sr, 40.0f);
    REQUIRE (fx.windowSamples (20.0f) == 960);
    REQUIRE (fx.maxWindowSamples() == 1920);

    // 1 kHz sine: all power in 150 Hz..5 kHz bands, centroid ~0 octaves, ZCR 2/ms
    std::vector<float> s (960);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 1000.0 * static_cast<double> (i) / sr));
    const auto f = fx.extract (s.data(), 960);
    CHECK (f[Feature::BandLowMid] + f[Feature::BandHighMid] > 0.99f);
    CHECK (std::abs (f[Feature::CentroidOct]) < 0.1f);
    CHECK (std::abs (f[Feature::ZeroCrossingRate] - 2.0f) < 0.05f);
    CHECK (f[Feature::Flatness] < 0.05f);
    CHECK (std::abs (f[Feature::LevelDb] - (-9.03f)) < 0.2f);
    CHECK (f[Feature::SubOnsets] == 1.0f);

    // White noise: flat spectrum, high flatness
    Rng rng (1);
    for (auto& v : s)
        v = 0.3f * rng.noise();
    CHECK (fx.extract (s.data(), 960)[Feature::Flatness] > 0.4f);

    // Clap: several bursts are counted
    Rng rng2 (2);
    int multi = 0;
    for (int i = 0; i < 50; ++i)
        if (hitFeatures (SynthDrum::Clap, sr, rng2, fx, 20.0f)[Feature::SubOnsets] >= 2.0f)
            ++multi;
    CHECK (multi >= 45);

    // Deterministic
    CHECK (fx.extract (s.data(), 960) == fx.extract (s.data(), 960));
}

TEST_CASE ("RuleClassifier: synthetic kick / snare / closed hat >= 95 %", "[beatbox][classifier]")
{
    const auto conf = runRules (20.0f, 1001);
    conf.print ("[rules] 20 ms window, 120 hits per class:");

    CHECK (conf.accuracy (0) >= 0.95); // kick
    CHECK (conf.accuracy (1) >= 0.95); // snare
    CHECK (conf.accuracy (3) >= 0.95); // closed hat

    // reported, softer expectations for the harder classes
    CHECK (conf.accuracy (2) >= 0.8);  // clap
    CHECK (conf.accuracy (4) >= 0.8);  // open hat

    const auto conf10 = runRules (10.0f, 1001);
    conf10.print ("[rules] 10 ms window, 120 hits per class:");
}

TEST_CASE ("RuleClassifier: toms disabled by default, per-class sensitivity", "[beatbox][classifier]")
{
    FeatureExtractor fx;
    fx.prepare (sr, 40.0f);
    Rng rng (5);
    const auto kick = hitFeatures (SynthDrum::Kick, sr, rng, fx, 20.0f);

    RuleClassifier rc;
    auto c = rc.classify (kick);
    REQUIRE (c.accepted);
    REQUIRE (c.cls == DrumClass::Kick);

    ClassifierParams p;
    p.sensitivity[static_cast<std::size_t> (toIndex (DrumClass::Kick))] = 0.0f; // never accept kicks
    rc.setParams (p);
    CHECK_FALSE (rc.classify (kick).accepted);

    p = {};
    p.enabled[static_cast<std::size_t> (toIndex (DrumClass::Kick))] = false; // kick takes no part
    rc.setParams (p);
    CHECK (rc.classify (kick).cls != DrumClass::Kick);

    // a steady 440 Hz tone is not a drum
    rc.setParams ({});
    int accepted = 0;
    for (int i = 0; i < 20; ++i)
        accepted += rc.classify (hitFeatures (SynthDrum::Tone440, sr, rng, fx, 20.0f)).accepted ? 1 : 0;
    CHECK (accepted == 0);
}

TEST_CASE ("CalibratedClassifier: 6 examples per class, held-out hits", "[beatbox][classifier]")
{
    const auto cc = calibrate (20.0f, 6, 7);
    REQUIRE (cc.isUsable());

    const auto conf = runCalibrated (cc, 20.0f, 2002);
    conf.print ("[calibrated] 20 ms window, 6 examples/class, 120 held-out hits per class:");
    CHECK (conf.accuracy (0) >= 0.95);
    CHECK (conf.accuracy (1) >= 0.95);
    CHECK (conf.accuracy (3) >= 0.95);
    CHECK (conf.accuracy (2) >= 0.8);
    CHECK (conf.accuracy (4) >= 0.8);

    const auto cc10 = calibrate (10.0f, 6, 7);
    runCalibrated (cc10, 10.0f, 2002).print ("[calibrated] 10 ms window, 6 examples/class, 120 held-out hits per class:");

    // Different calibration takes: the result depends on how representative
    // the 6 examples are, so check several.
    for (std::uint64_t seed : { 11u, 12u, 13u, 14u, 15u })
    {
        const auto c = runCalibrated (calibrate (20.0f, 6, seed), 20.0f, 3000 + seed);
        std::printf ("[calibrated] calibration seed %d: kick %.1f%% snare %.1f%% clap %.1f%% closedhat %.1f%% openhat %.1f%%\n",
                     static_cast<int> (seed), 100 * c.accuracy (0), 100 * c.accuracy (1), 100 * c.accuracy (2),
                     100 * c.accuracy (3), 100 * c.accuracy (4));
        CHECK (c.accuracy (0) >= 0.95);
        CHECK (c.accuracy (1) >= 0.95);
        CHECK (c.accuracy (3) >= 0.95);
    }
}

TEST_CASE ("CalibratedClassifier: rejects sounds unlike any class", "[beatbox][classifier]")
{
    FeatureExtractor fx;
    fx.prepare (sr, 40.0f);
    for (std::uint64_t seed : { 7u, 11u, 12u, 13u, 14u, 15u })
    {
        const auto cc = calibrate (20.0f, 6, seed);
        Rng rng (31 + seed);
        int accepted = 0;
        float maxScore = 0.0f;
        for (int i = 0; i < 50; ++i)
        {
            const auto c = cc.classify (hitFeatures (SynthDrum::Tone440, sr, rng, fx, 20.0f));
            accepted += c.accepted ? 1 : 0;
            maxScore = std::max (maxScore, c.score);
        }
        std::printf ("[calibrated] seed %d: 440 Hz tone onsets %d / 50 accepted (max score %.3f, threshold %.2f)\n",
                     static_cast<int> (seed), accepted, maxScore, ClassifierParams {}.minScore);
        CHECK (accepted == 0);
    }
}

TEST_CASE ("CalibratedClassifier: classes without examples are never returned", "[beatbox][classifier]")
{
    FeatureExtractor fx;
    fx.prepare (sr, 40.0f);
    Rng rng (8);
    CalibratedClassifier cc;
    CHECK_FALSE (cc.isUsable());
    CHECK_FALSE (cc.classify (hitFeatures (SynthDrum::Kick, sr, rng, fx, 20.0f)).accepted);

    for (int i = 0; i < 3; ++i)
    {
        cc.addExample (DrumClass::Kick, hitFeatures (SynthDrum::Kick, sr, rng, fx, 20.0f));
        cc.addExample (DrumClass::ClosedHat, hitFeatures (SynthDrum::ClosedHat, sr, rng, fx, 20.0f));
    }
    cc.finalize();
    CHECK (cc.isUsable());

    for (int i = 0; i < 30; ++i)
    {
        const auto c = cc.classify (hitFeatures (fiveSounds[i % 5], sr, rng, fx, 20.0f));
        CHECK ((c.cls == DrumClass::Kick || c.cls == DrumClass::ClosedHat));
    }

    // capacity
    CalibratedClassifier full;
    for (int i = 0; i < CalibratedClassifier::maxExamplesPerClass; ++i)
        CHECK (full.addExample (DrumClass::Snare, {}));
    CHECK_FALSE (full.addExample (DrumClass::Snare, {}));
    full.clear (DrumClass::Snare);
    CHECK (full.exampleCount (DrumClass::Snare) == 0);
}

TEST_CASE ("CalibratedClassifier: serialisation round trip is exact", "[beatbox][classifier]")
{
    const auto cc = calibrate (20.0f, 6, 7);
    const auto text = cc.toString();
    INFO (text);
    REQUIRE (text.rfind ("m2mcal 1 ", 0) == 0);

    CalibratedClassifier loaded;
    REQUIRE (loaded.fromString (text));
    CHECK (loaded.toString() == text);
    for (auto s : fiveSounds)
        CHECK (loaded.exampleCount (expectedClass (s)) == 6);

    FeatureExtractor fx;
    fx.prepare (sr, 40.0f);
    Rng rng (444);
    for (int i = 0; i < 100; ++i)
    {
        const auto f = hitFeatures (fiveSounds[i % 5], sr, rng, fx, 20.0f);
        const auto a = cc.classify (f), b = loaded.classify (f);
        CHECK (a.cls == b.cls);
        CHECK (a.score == b.score);
        CHECK (a.accepted == b.accepted);
    }

    // invalid input leaves the object unchanged
    CalibratedClassifier other = loaded;
    CHECK_FALSE (other.fromString (""));
    CHECK_FALSE (other.fromString ("m2mcal 2 14\nend\n"));
    CHECK_FALSE (other.fromString ("m2mcal 1 13\nend\n"));
    CHECK_FALSE (other.fromString ("m2mcal 1 14\nkick 1\n00000000\nend\n"));
    CHECK_FALSE (other.fromString ("m2mcal 1 14\nbanjo 0\nend\n"));
    CHECK_FALSE (other.fromString (text.substr (0, text.size() - 4))); // no "end"
    CHECK (other.toString() == text);

    // empty calibration round-trips too
    CalibratedClassifier empty;
    CHECK (other.fromString (empty.toString()));
    CHECK (other.exampleCount (DrumClass::Kick) == 0);
}
