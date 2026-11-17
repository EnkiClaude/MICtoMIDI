// Classifier robustness across three independent families of synthetic
// vocal-percussion sounds, run through the complete BeatboxEngine (onset
// detection, window placement and features exactly as in use):
//
//   unit    - the beatbox tests' own synthesiser (DrumSynth.h)
//   harness - the m2m_testsupport drum synths used by the m2m-cli scenarios
//   voice   - the voice-like family (m2m::test::voiceDrum): plosive kicks,
//             "pf/k" snares, "ts" hats, wider random variation, pre-roll
//
// The rule classifier must not be tuned to one synthesiser, so it is
// checked on all three. The calibrated classifier is checked the way it is
// used: calibrated on 6 examples per class of ONE family (one user's sounds)
// and tested on held-out hits of that same family.

#include "m2m/beatbox/BeatboxEngine.h"
#include "m2m/test/Drums.h"

#include "ClassifierTestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace m2m;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int hitsPerClass = 40;
    constexpr int calibrationExamples = 6;

    // Rows: kick, snare, clap, closed hat, open hat.
    constexpr DrumClass rowClass[5] = { DrumClass::Kick, DrumClass::Snare, DrumClass::Clap, DrumClass::ClosedHat,
                                        DrumClass::OpenHat };
    constexpr test::DrumKind rowKind[5] = { test::DrumKind::Kick, test::DrumKind::Snare, test::DrumKind::Clap,
                                            test::DrumKind::ClosedHat, test::DrumKind::OpenHat };
    constexpr beatboxtest::SynthDrum rowSynth[5] = { beatboxtest::SynthDrum::Kick, beatboxtest::SynthDrum::Snare,
                                                     beatboxtest::SynthDrum::Clap, beatboxtest::SynthDrum::ClosedHat,
                                                     beatboxtest::SynthDrum::OpenHat };

    struct Rendered
    {
        std::vector<float> audio;
        std::size_t onset = 0;
    };

    using Renderer = std::function<Rendered (int row, std::uint64_t seed, double levelDb)>;

    Rendered renderUnit (int row, std::uint64_t seed, double levelDb)
    {
        beatboxtest::Rng rng (seed);
        return { beatboxtest::renderDrum (rowSynth[row], sr, rng, static_cast<float> (levelDb)), 0 };
    }

    Rendered renderHarness (int row, std::uint64_t seed, double levelDb)
    {
        // full variation (the scenarios use 0.3)
        return { test::drum (rowKind[row], sr, { levelDb, 1.0, seed }), 0 };
    }

    Rendered renderVoice (int row, std::uint64_t seed, double levelDb)
    {
        auto v = test::voiceDrum (rowKind[row], sr, { levelDb, 1.0, seed });
        return { std::move (v.audio), v.onsetSample };
    }

    struct Confusion
    {
        // columns: 7 classes, rejected, no onset
        int m[5][numDrumClasses + 2] {};
        int total[5] {};

        void add (int row, int col) { ++m[row][col]; ++total[row]; }

        double accuracy (int row) const
        {
            return total[row] > 0 ? static_cast<double> (m[row][toIndex (rowClass[row])]) / total[row] : 0.0;
        }

        void print (const std::string& title) const
        {
            std::printf ("%s\n  %-10s", title.c_str(), "truth\\out");
            for (int c = 0; c < numDrumClasses; ++c)
                std::printf ("%9.9s", drumClassName (static_cast<DrumClass> (c)));
            std::printf ("%9s%9s\n", "rejected", "noOnset");
            for (int r = 0; r < 5; ++r)
            {
                if (total[r] == 0)
                    continue;
                std::printf ("  %-10s", drumClassName (rowClass[r]));
                for (int c = 0; c < numDrumClasses + 2; ++c)
                    std::printf ("%9d", m[r][c]);
                std::printf ("   acc %5.1f%%\n", 100.0 * accuracy (r));
            }
        }
    };

    struct LabelledHit
    {
        int row = 0;
        bool detected = false;
        HitInfo hit;
    };

    /** Renders hitsPerClass hits of each class in a shuffled order, 450 ms
        apart, levels -30..-6 dBFS, runs a BeatboxEngine (rules) over them
        and pairs every truth hit with the onset found within 30 ms. */
    std::vector<LabelledHit> runFamily (const Renderer& render, std::uint64_t seed, int numRows)
    {
        beatboxtest::Rng rng (seed);
        std::vector<int> order;
        for (int r = 0; r < numRows; ++r)
            for (int i = 0; i < hitsPerClass; ++i)
                order.push_back (r);
        for (std::size_t i = order.size() - 1; i > 0; --i) // Fisher-Yates
            std::swap (order[i], order[static_cast<std::size_t> (rng.uniform() * static_cast<double> (i + 1))]);

        std::vector<float> audio (static_cast<std::size_t> (0.3 * sr), 0.0f);
        std::vector<std::pair<std::int64_t, int>> truth;
        const auto spacing = static_cast<std::size_t> (0.45 * sr);
        std::size_t t = audio.size();
        for (std::size_t k = 0; k < order.size(); ++k)
        {
            const double level = rng.range (-30.0, -6.0);
            const auto s = render (order[k], seed * 1000 + k, level);
            const std::size_t start = t - s.onset;
            beatboxtest::mixInto (audio, s.audio, start);
            truth.emplace_back (static_cast<std::int64_t> (t), order[k]);
            t += spacing;
        }
        audio.resize (t + static_cast<std::size_t> (0.3 * sr), 0.0f);

        BeatboxEngine engine;
        engine.prepare (sr, 256);
        MidiEventBuffer buf;
        buf.reserve (256);
        std::vector<HitInfo> hits;
        HitInfo tmp[64];
        for (std::size_t i = 0; i < audio.size(); i += 256)
        {
            buf.clear();
            engine.process (audio.data() + i, static_cast<int> (std::min<std::size_t> (256, audio.size() - i)), buf);
            int n = 0;
            while ((n = engine.getRecentHits (tmp, 64)) > 0)
                hits.insert (hits.end(), tmp, tmp + n);
        }

        std::vector<LabelledHit> out;
        const auto tol = static_cast<std::int64_t> (0.03 * sr);
        for (const auto& [ts, row] : truth)
        {
            LabelledHit lh;
            lh.row = row;
            for (const auto& h : hits)
                if (std::abs (h.onsetSample - ts) <= tol && (! lh.detected || h.score > lh.hit.score))
                {
                    lh.hit = h;
                    lh.detected = true;
                }
            out.push_back (lh);
        }
        return out;
    }

    Confusion rulesConfusion (const std::vector<LabelledHit>& hits)
    {
        Confusion c;
        for (const auto& h : hits)
            c.add (h.row, ! h.detected ? numDrumClasses + 1 : h.hit.accepted ? toIndex (h.hit.cls) : numDrumClasses);
        return c;
    }

    /** Calibrate on the first 6 detected hits of each class (in time order,
        as a user would record them), classify the rest. */
    Confusion calibratedConfusion (const std::vector<LabelledHit>& hits, int numRows)
    {
        CalibratedClassifier cc;
        int used[5] {};
        std::vector<bool> isCal (hits.size(), false);
        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            const auto& h = hits[i];
            if (h.detected && used[h.row] < calibrationExamples)
            {
                cc.addExample (rowClass[h.row], h.hit.features);
                ++used[h.row];
                isCal[i] = true;
            }
        }
        cc.finalize();
        REQUIRE (cc.isUsable());

        Confusion c;
        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            if (isCal[i])
                continue;
            const auto& h = hits[i];
            if (! h.detected)
            {
                c.add (h.row, numDrumClasses + 1);
                continue;
            }
            const auto r = cc.classify (h.hit.features);
            c.add (h.row, r.accepted ? toIndex (r.cls) : numDrumClasses);
        }
        (void) numRows;
        return c;
    }

    struct Family
    {
        const char* name;
        Renderer render;
    };

    const Family families[] = {
        { "unit", renderUnit },
        { "harness", renderHarness },
        { "voice", renderVoice },
    };
}

TEST_CASE ("Beatbox classifiers across three sound families (engine end to end)", "[beatbox][classifier][families]")
{
    // Several independent takes per family (different hits, different
    // calibration examples). M2M_FAMILY_SEEDS overrides the count.
    const char* ns = std::getenv ("M2M_FAMILY_SEEDS");
    const int numTakes = ns != nullptr ? std::max (1, std::atoi (ns)) : 6;
    constexpr int gated[] = { 0, 1, 3 }; // kick, snare, closed hat

    for (const auto& fam : families)
    {
        double calSum[5] {}, rulesSum[5] {};
        for (int take = 1; take <= numTakes; ++take)
        {
            INFO ("family " << fam.name << " take " << take);
            const auto hits = runFamily (fam.render, 100 * static_cast<std::uint64_t> (take) + 7, 5);

            const auto rules = rulesConfusion (hits);
            rules.print (std::string ("[families] rules, family ") + fam.name + ", take " + std::to_string (take)
                         + ", " + std::to_string (hitsPerClass) + " hits/class:");
            // Every take: rules >= 90 % on kick, snare, closed hat. Clap and
            // open hat are reported, not gated.
            for (int r : gated)
                CHECK (rules.accuracy (r) >= 0.90);

            const auto cal = calibratedConfusion (hits, 5);
            cal.print (std::string ("[families] calibrated (6 examples/class, same family), family ") + fam.name
                       + ", take " + std::to_string (take) + ", held-out hits:");
            // A single take depends on how representative its 6 examples
            // are; an unlucky take may be worse than the average.
            for (int r : gated)
                CHECK (cal.accuracy (r) >= 0.80);

            for (int r = 0; r < 5; ++r)
            {
                calSum[r] += cal.accuracy (r);
                rulesSum[r] += rules.accuracy (r);
            }
        }

        std::printf ("[families] %s, mean over %d takes:  rules", fam.name, numTakes);
        for (int r = 0; r < 5; ++r)
            std::printf (" %s %.1f%%", drumClassName (rowClass[r]), 100.0 * rulesSum[r] / numTakes);
        std::printf ("  |  calibrated");
        for (int r = 0; r < 5; ++r)
            std::printf (" %s %.1f%%", drumClassName (rowClass[r]), 100.0 * calSum[r] / numTakes);
        std::printf ("\n");

        INFO ("family " << fam.name);
        for (int r : gated)
            CHECK (calSum[r] / numTakes >= 0.95); // calibrated, mean over takes
    }
}
