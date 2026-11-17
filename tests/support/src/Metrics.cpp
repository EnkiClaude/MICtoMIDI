#include "m2m/test/Metrics.h"

#include <cstdio>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace m2m::test
{

int ConfusionMatrix::get (int truthNote, int detectedNote) const
{
    const auto row = counts.find (truthNote);
    if (row == counts.end())
        return 0;
    const auto cell = row->second.find (detectedNote);
    return cell == row->second.end() ? 0 : cell->second;
}

namespace
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    /** Maximum-cardinality bipartite matching (Kuhn's augmenting paths).
        Truth notes are tried in the given order; each one's candidates are
        tried closest-onset first, so the result prefers small errors. */
    class Matcher
    {
    public:
        Matcher (const std::vector<TruthNote>& t, const std::vector<DetectedNote>& d,
                 const std::vector<std::size_t>& truthIdx, const std::vector<std::size_t>& detIdx,
                 double tol, bool pitch)
            : truth (t), det (d), tIdx (truthIdx)
        {
            adj.resize (tIdx.size());
            for (std::size_t a = 0; a < tIdx.size(); ++a)
            {
                const auto& tn = truth[tIdx[a]];
                for (const auto j : detIdx)
                {
                    const auto& dn = det[j];
                    if (pitch && dn.midiNote != tn.midiNote)
                        continue;
                    if (std::abs (dn.onsetSec - tn.onsetSec) <= tol + 1e-9)
                        adj[a].push_back (j);
                }
                std::stable_sort (adj[a].begin(), adj[a].end(), [&] (std::size_t x, std::size_t y)
                {
                    return std::abs (det[x].onsetSec - tn.onsetSec) < std::abs (det[y].onsetSec - tn.onsetSec);
                });
            }
        }

        /** Returns (truth index, detected index) pairs. */
        std::vector<std::pair<std::size_t, std::size_t>> run()
        {
            owner.assign (det.size(), npos);
            for (std::size_t a = 0; a < tIdx.size(); ++a)
            {
                visited.assign (det.size(), false);
                augment (a);
            }

            std::vector<std::pair<std::size_t, std::size_t>> pairs;
            for (std::size_t j = 0; j < det.size(); ++j)
                if (owner[j] != npos)
                    pairs.emplace_back (tIdx[owner[j]], j);
            std::sort (pairs.begin(), pairs.end());
            return pairs;
        }

    private:
        static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

        bool augment (std::size_t a)
        {
            for (const auto j : adj[a])
            {
                if (visited[j])
                    continue;
                visited[j] = true;
                if (owner[j] == npos || augment (owner[j]))
                {
                    owner[j] = a;
                    return true;
                }
            }
            return false;
        }

        const std::vector<TruthNote>& truth;
        const std::vector<DetectedNote>& det;
        const std::vector<std::size_t>& tIdx;
        std::vector<std::vector<std::size_t>> adj;
        std::vector<std::size_t> owner;
        std::vector<bool> visited;
    };

    std::vector<std::size_t> indicesSortedByOnset (std::size_t n, const auto& onsetOf)
    {
        std::vector<std::size_t> idx (n);
        std::iota (idx.begin(), idx.end(), std::size_t { 0 });
        std::stable_sort (idx.begin(), idx.end(), [&] (std::size_t a, std::size_t b) { return onsetOf (a) < onsetOf (b); });
        return idx;
    }
}

Metrics evaluate (const std::vector<TruthNote>& truth, const std::vector<DetectedNote>& detected,
                  double durationSec, const MatchOptions& options)
{
    Metrics m;
    m.numTruth = static_cast<int> (truth.size());
    m.numDetected = static_cast<int> (detected.size());
    m.onsetToleranceMs = 1000.0 * options.onsetToleranceSec;
    m.pitchMatched = options.matchPitch;

    if (durationSec <= 0.0)
    {
        durationSec = 0.0;
        for (const auto& t : truth)    durationSec = std::max ({ durationSec, t.onsetSec, t.offsetSec });
        for (const auto& d : detected) durationSec = std::max ({ durationSec, d.onsetSec, d.offsetSec });
    }
    m.durationSec = durationSec;

    const auto allTruth = indicesSortedByOnset (truth.size(), [&] (std::size_t i) { return truth[i].onsetSec; });
    const auto allDet   = indicesSortedByOnset (detected.size(), [&] (std::size_t i) { return detected[i].onsetSec; });

    // Pass 1: the real matching (pitch-exact unless disabled).
    const auto pairs = Matcher (truth, detected, allTruth, allDet, options.onsetToleranceSec, options.matchPitch).run();

    std::vector<bool> truthUsed (truth.size(), false), detUsed (detected.size(), false);
    double sumAbs = 0.0, sumSigned = 0.0, maxAbs = 0.0, sumOff = 0.0;
    for (const auto& [ti, di] : pairs)
    {
        truthUsed[ti] = detUsed[di] = true;
        const double err = detected[di].onsetSec - truth[ti].onsetSec;
        m.matches.push_back ({ ti, di, err });
        sumAbs += std::abs (err);
        sumSigned += err;
        maxAbs = std::max (maxAbs, std::abs (err));
        m.confusion.add (truth[ti].midiNote, options.matchPitch ? truth[ti].midiNote : detected[di].midiNote);

        if (truth[ti].offsetSec > truth[ti].onsetSec)
        {
            sumOff += std::abs (detected[di].offsetSec - truth[ti].offsetSec);
            ++m.offsetPairs;
        }
    }

    m.truePositives = static_cast<int> (pairs.size());
    m.falsePositives = m.numDetected - m.truePositives;
    m.falseNegatives = m.numTruth - m.truePositives;

    const double tp = m.truePositives;
    m.precision = m.numDetected > 0 ? tp / m.numDetected : (m.numTruth == 0 ? 1.0 : 0.0);
    m.recall    = m.numTruth > 0    ? tp / m.numTruth    : (m.numDetected == 0 ? 1.0 : 0.0);
    m.f1 = (m.precision + m.recall) > 0.0 ? 2.0 * m.precision * m.recall / (m.precision + m.recall) : 0.0;

    if (m.truePositives > 0)
    {
        m.meanAbsOnsetErrorMs    = 1000.0 * sumAbs / tp;
        m.maxAbsOnsetErrorMs     = 1000.0 * maxAbs;
        m.meanSignedOnsetErrorMs = 1000.0 * sumSigned / tp;
    }
    else
    {
        m.meanAbsOnsetErrorMs = m.maxAbsOnsetErrorMs = m.meanSignedOnsetErrorMs = nan;
    }
    m.meanAbsOffsetErrorMs = m.offsetPairs > 0 ? 1000.0 * sumOff / m.offsetPairs : nan;

    // Pass 2: leftover pairs that coincide in time but differ in pitch.
    std::vector<std::size_t> restTruth, restDet;
    for (const auto i : allTruth) if (! truthUsed[i]) restTruth.push_back (i);
    for (const auto j : allDet)   if (! detUsed[j])   restDet.push_back (j);

    if (options.matchPitch)
    {
        const auto pitchPairs = Matcher (truth, detected, restTruth, restDet, options.onsetToleranceSec, false).run();
        for (const auto& [ti, di] : pitchPairs)
        {
            truthUsed[ti] = detUsed[di] = true;
            const int diff = std::abs (detected[di].midiNote - truth[ti].midiNote);
            if (diff == 12 || diff == 24)
                ++m.octaveErrors;
            else
                ++m.wrongPitch;
            m.confusion.add (truth[ti].midiNote, detected[di].midiNote);
        }
    }

    for (std::size_t i = 0; i < truth.size(); ++i)
        if (! truthUsed[i])
            m.confusion.add (truth[i].midiNote, ConfusionMatrix::kMissed);

    for (std::size_t j = 0; j < detected.size(); ++j)
        if (! detUsed[j])
        {
            ++m.spuriousDetections;
            m.confusion.add (ConfusionMatrix::kSpurious, detected[j].midiNote);
        }

    m.falseTriggersPerMinute = m.durationSec > 0.0 ? m.spuriousDetections * 60.0 / m.durationSec : 0.0;
    return m;
}

Metrics evaluate (const std::vector<TruthNote>& truth, const std::vector<DetectedNote>& detected,
                  double durationSec, const MatchOptions& options, const std::vector<GlideSegment>& glides)
{
    if (glides.empty())
        return evaluate (truth, detected, durationSec, options);

    const double tol = options.onsetToleranceSec;
    const auto between = [] (int n, int a, int b) { return n > std::min (a, b) && n < std::max (a, b); };

    std::vector<DetectedNote> kept;
    int passing = 0;
    for (const auto& d : detected)
    {
        bool isPassing = false;
        for (const auto& g : glides)
            if (d.onsetSec > g.startSec && d.onsetSec <= g.endSec + tol && between (d.midiNote, g.fromNote, g.toNote))
                isPassing = true;
        if (isPassing)
            ++passing;
        else
            kept.push_back (d);
    }

    auto m = evaluate (truth, kept, durationSec, options);
    m.numDetected = static_cast<int> (detected.size()); // informational: all detections
    m.glidePassingNotes = passing;

    std::vector<DetectedNote> sorted = detected;
    std::stable_sort (sorted.begin(), sorted.end(),
                      [] (const DetectedNote& a, const DetectedNote& b) { return a.onsetSec < b.onsetSec; });

    for (const auto& g : glides)
    {
        ++m.glideChecks;
        const double from = g.startSec, to = g.endSec + tol;
        std::vector<const DetectedNote*> seq;
        for (const auto& d : sorted)
            if (d.onsetSec < to && d.offsetSec > from)
                seq.push_back (&d);

        char where[64];
        std::snprintf (where, sizeof (where), "glide %d->%d at %.3f s: ", g.fromNote, g.toNote, g.startSec);
        std::vector<std::string> problems;
        const int dir = g.toNote > g.fromNote ? 1 : -1;
        const int span = std::abs (g.toNote - g.fromNote);

        if (seq.empty())
            problems.push_back ("no note sounding");
        else
        {
            if (seq.front()->midiNote != g.fromNote)
                problems.push_back ("first note " + std::to_string (seq.front()->midiNote) + " is not the start note");
            if (seq.back()->midiNote != g.toNote)
                problems.push_back ("last note " + std::to_string (seq.back()->midiNote) + " is not the end note");
            if (static_cast<int> (seq.size()) > span + 1)
                problems.push_back (std::to_string (seq.size()) + " notes for a " + std::to_string (span) + "-semitone glide");
            for (std::size_t i = 1; i < seq.size(); ++i)
            {
                const auto& a = *seq[i - 1];
                const auto& b = *seq[i];
                if ((b.midiNote - a.midiNote) * dir <= 0)
                    problems.push_back ("not monotonic (" + std::to_string (a.midiNote) + " then " + std::to_string (b.midiNote) + ")");
                if (b.onsetSec < a.offsetSec - 1.0e-6)
                    problems.push_back ("two notes at once at " + std::to_string (b.onsetSec) + " s");
                else if (b.onsetSec - a.offsetSec > 0.001)
                    problems.push_back ("gap of " + std::to_string (1000.0 * (b.onsetSec - a.offsetSec)) + " ms");
            }
        }

        if (! problems.empty())
        {
            ++m.glideFailures;
            for (const auto& p : problems)
                m.glideProblems.push_back (where + p);
        }
    }
    return m;
}

Metrics evaluateOnsets (const std::vector<TruthNote>& truth, const std::vector<DetectedNote>& detected,
                        double durationSec, double onsetToleranceSec)
{
    return evaluate (truth, detected, durationSec, { onsetToleranceSec, false });
}

} // namespace m2m::test
