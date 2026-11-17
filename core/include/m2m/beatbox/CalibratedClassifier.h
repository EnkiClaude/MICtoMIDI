#pragma once

#include "m2m/beatbox/DrumClass.h"

#include <array>
#include <string>

namespace m2m
{

/** Per-user nearest-centroid classifier (PLAN.md sections 1 and 6).

    The user performs a few examples of each sound. finalize() computes the
    per-class mean and per-feature spread (with a variance floor per feature,
    because 5-8 examples underestimate the spread). classify() returns the
    class with the smallest normalised distance
        d = sqrt (sum_f w_f ((x_f - mean_cf) / sigma_cf)^2 / sum_f w_f)
    and score = exp (-d^2 / (2 * 2.5^2)), multiplied by a penalty that goes
    from 1 to 0 as the largest single-feature |z| goes from 8 to 12. The hit
    is accepted if the score reaches acceptanceThreshold (minScore,
    sensitivity). With the default minScore 0.35 that rejects d > ~3.6 (a
    sound unlike every class).
    The winning class is chosen by diagonal-Gaussian log-likelihood
    (d^2 + 2 * mean log sigma) so wide classes do not absorb outliers. The
    two best candidates are then compared again with per-pair feature
    weights that favour the features separating THOSE two classes (e.g. the
    decay for closed vs. open hat), so features irrelevant to that decision
    cannot outvote the relevant one by chance.
    The level feature has weight 0 (loudness must not decide the class).

    Classes without examples are never returned. No heap memory: the
    example store is a fixed array, so the whole object can be copied
    (e.g. a prepared copy handed to the audio thread).

    Real-time safe: addExample, clear, finalize, classify.
    Not real-time safe (allocate): toString, fromString. */
class CalibratedClassifier final : public IDrumClassifier
{
public:
    static constexpr int maxExamplesPerClass = 16;

    void setParams (const ClassifierParams& p) noexcept override { params = p; }
    const ClassifierParams& getParams() const noexcept { return params; }

    /** Adds an example; returns false if that class is full (16). Call
        finalize() before classifying. */
    bool addExample (DrumClass c, const FeatureVector& f) noexcept;
    void clear (DrumClass c) noexcept;
    void clearAll() noexcept;

    int exampleCount (DrumClass c) const noexcept;
    const FeatureVector& example (DrumClass c, int i) const noexcept;

    /** Recomputes class statistics from the stored examples. */
    void finalize() noexcept;

    /** At least two classes with at least three examples each. */
    bool isUsable() const noexcept;

    Classification classify (const FeatureVector& f) const noexcept override;

    /** Normalised distance to a class (infinity if it has no examples). */
    float distance (const FeatureVector& f, DrumClass c) const noexcept;

    /** Largest single-feature |z| against a class (infinity if empty). */
    float maxAbsZ (const FeatureVector& f, DrumClass c) const noexcept;

    /** Compact, versioned text form (stores the raw examples as exact hex
        float bit patterns, so a round trip reproduces identical results). */
    std::string toString() const;

    /** Parses toString() output; on any error returns false and leaves this
        object unchanged. Finalizes on success. */
    bool fromString (const std::string& text);

    /** Default per-feature weights and spread floors. */
    static float featureWeight (Feature f) noexcept;
    static float spreadFloor (Feature f) noexcept;

private:
    float pairCost (const FeatureVector& f, DrumClass c, DrumClass other) const noexcept;

    ClassifierParams params;
    std::array<std::array<FeatureVector, maxExamplesPerClass>, numDrumClasses> examples {};
    std::array<int, numDrumClasses> counts {};
    std::array<FeatureVector, numDrumClasses> mean {}, invSigma {};
    std::array<float, numDrumClasses> logSigma {}; // weighted mean log (sigma / floor)
    // Per class pair, per feature: how well the feature separates the two
    // classes (s / (1 + s), s = squared mean difference over mean variance).
    std::array<std::array<FeatureVector, numDrumClasses>, numDrumClasses> pairWeight {};
};

} // namespace m2m
