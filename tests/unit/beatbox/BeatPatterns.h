#pragma once

#include "DrumSynth.h"

#include <cstdio>
#include <cstdlib>

namespace m2m::beatboxtest
{

/** A rendered pattern plus its ground truth. */
struct Pattern
{
    std::vector<float> audio;
    std::vector<TruthHit> truth;
};

/** Monophonic beatbox-like pattern on a 16th grid: kick on beats 1/3,
    snare on 2/4, closed hats on most other 16ths, occasional open hat / clap.
    Like a mouth, each sound is cut (2 ms fade) when the next one starts.
    'hatDensity' 0..1 = probability that a free 16th gets a hat. */
inline Pattern makePattern (double sr, double bpm, int bars, Rng& rng, double hatDensity = 1.0,
                            float minDb = -24.0f, float maxDb = -6.0f, bool allowOpenAndClap = true)
{
    Pattern p;
    const double step = 60.0 / bpm / 4.0;
    const double lead = 0.25;
    const int steps = bars * 16;

    std::vector<std::pair<std::int64_t, SynthDrum>> events;
    for (int i = 0; i < steps; ++i)
    {
        const int inBar = i % 16;
        SynthDrum d;
        bool use = true;
        if (inBar == 0 || inBar == 8 || (inBar == 10 && rng.uniform() < 0.5))
            d = SynthDrum::Kick;
        else if (inBar == 4 || inBar == 12)
            d = (allowOpenAndClap && rng.uniform() < 0.25) ? SynthDrum::Clap : SynthDrum::Snare;
        else
        {
            use = rng.uniform() < hatDensity;
            d = (allowOpenAndClap && (inBar % 4 == 2) && rng.uniform() < 0.3) ? SynthDrum::OpenHat
                                                                               : SynthDrum::ClosedHat;
        }
        if (! use)
            continue;

        // up to +-3 ms of human timing jitter
        const double t = lead + i * step + rng.range (-0.003, 0.003);
        events.emplace_back (static_cast<std::int64_t> (t * sr), d);
    }

    const auto total = static_cast<std::size_t> ((lead + steps * step + 0.5) * sr);
    p.audio.assign (total, 0.0f);

    for (std::size_t e = 0; e < events.size(); ++e)
    {
        const float db = static_cast<float> (rng.range (minDb, maxDb));
        auto hit = renderDrum (events[e].second, sr, rng, db);
        const auto start = static_cast<std::size_t> (events[e].first);

        if (e + 1 < events.size())
        {
            const auto next = static_cast<std::size_t> (events[e + 1].first);
            const std::size_t maxLen = next - start;
            const auto fade = static_cast<std::size_t> (0.002 * sr);
            if (hit.size() > maxLen)
            {
                hit.resize (maxLen);
                for (std::size_t k = 0; k < fade && k < hit.size(); ++k)
                    hit[hit.size() - 1 - k] *= static_cast<float> (k) / static_cast<float> (fade);
            }
        }

        mixInto (p.audio, hit, start);
        p.truth.push_back ({ events[e].first, events[e].second, db });
    }

    return p;
}

struct MatchStats
{
    int truePositives = 0, falsePositives = 0, falseNegatives = 0;
    double sumAbsErrMs = 0.0, maxAbsErrMs = 0.0, sumErrMs = 0.0;

    double precision() const { return truePositives + falsePositives == 0 ? 1.0 : double (truePositives) / (truePositives + falsePositives); }
    double recall() const    { return truePositives + falseNegatives == 0 ? 1.0 : double (truePositives) / (truePositives + falseNegatives); }
    double f1() const        { const double p = precision(), r = recall(); return p + r > 0 ? 2 * p * r / (p + r) : 0.0; }
    double meanAbsErrMs() const { return truePositives > 0 ? sumAbsErrMs / truePositives : 0.0; }
    double meanErrMs() const    { return truePositives > 0 ? sumErrMs / truePositives : 0.0; }

    void add (const MatchStats& o)
    {
        truePositives += o.truePositives; falsePositives += o.falsePositives; falseNegatives += o.falseNegatives;
        sumAbsErrMs += o.sumAbsErrMs; sumErrMs += o.sumErrMs; maxAbsErrMs = std::max (maxAbsErrMs, o.maxAbsErrMs);
    }
};

/** Greedy in-order matching of detections to truth within +-tolMs.
    matchedTruth (optional) receives, per detection, the index of the matched
    truth hit or -1. */
inline MatchStats matchOnsets (const std::vector<std::int64_t>& truth, const std::vector<std::int64_t>& det,
                               double sr, double tolMs, std::vector<int>* matchedTruth = nullptr)
{
    MatchStats s;
    std::vector<bool> used (truth.size(), false);
    if (matchedTruth)
        matchedTruth->assign (det.size(), -1);

    const auto tol = static_cast<std::int64_t> (tolMs * 0.001 * sr);
    for (std::size_t d = 0; d < det.size(); ++d)
    {
        std::int64_t bestDist = tol + 1;
        int best = -1;
        for (std::size_t t = 0; t < truth.size(); ++t)
        {
            if (used[t])
                continue;
            const std::int64_t dist = std::llabs (det[d] - truth[t]);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = static_cast<int> (t);
            }
        }

        if (best >= 0)
        {
            used[static_cast<std::size_t> (best)] = true;
            ++s.truePositives;
            const double err = double (det[d] - truth[static_cast<std::size_t> (best)]) * 1000.0 / sr;
            s.sumErrMs += err;
            s.sumAbsErrMs += std::abs (err);
            s.maxAbsErrMs = std::max (s.maxAbsErrMs, std::abs (err));
            if (matchedTruth)
                (*matchedTruth)[d] = best;
        }
        else
        {
            ++s.falsePositives;
        }
    }

    for (bool u : used)
        if (! u)
            ++s.falseNegatives;

    return s;
}

} // namespace m2m::beatboxtest
