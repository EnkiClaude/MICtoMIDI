#include "m2m/beatbox/CalibratedClassifier.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace m2m
{

namespace
{
    constexpr int formatVersion = 1;
    constexpr float distanceScale = 2.5f; // score = exp (-d^2 / (2 * scale^2))
    constexpr float maxZFull = 8.0f, maxZZero = 12.0f; // single-feature outlier penalty

    bool validClass (DrumClass c) noexcept
    {
        return toIndex (c) >= 0 && toIndex (c) < numDrumClasses;
    }

    std::size_t idx (DrumClass c) noexcept { return static_cast<std::size_t> (toIndex (c)); }
}

float CalibratedClassifier::featureWeight (Feature f) noexcept
{
    switch (f)
    {
        case Feature::LevelDb:  return 0.0f;  // loudness must not decide the class
        case Feature::AttackMs: return 0.5f;  // noisy on noise-like sounds
        default:                return 1.0f;
    }
}

float CalibratedClassifier::spreadFloor (Feature f) noexcept
{
    // Minimum spread per feature: roughly the natural variation of one
    // person's repeated sound, which 5-8 examples rarely capture.
    switch (f)
    {
        case Feature::BandLow:
        case Feature::BandLowMid:
        case Feature::BandHighMid:
        case Feature::BandHigh:         return 0.08f;
        case Feature::CentroidOct:      return 0.3f;
        case Feature::SpreadOct:        return 0.15f;
        case Feature::RolloffOct:       return 0.3f;
        case Feature::Flatness:         return 0.04f;
        case Feature::ZeroCrossingRate: return 1.5f;
        case Feature::CrestDb:          return 1.5f;
        case Feature::AttackMs:         return 3.0f;
        case Feature::EarlyDecayDb:     return 2.0f;
        case Feature::SubOnsets:        return 0.5f;
        case Feature::LevelDb:          return 6.0f;
        case Feature::Count:            break;
    }
    return 1.0f;
}

bool CalibratedClassifier::addExample (DrumClass c, const FeatureVector& f) noexcept
{
    if (! validClass (c))
        return false;
    auto& n = counts[idx (c)];
    if (n >= maxExamplesPerClass)
        return false;
    examples[idx (c)][static_cast<std::size_t> (n)] = f;
    ++n;
    return true;
}

void CalibratedClassifier::clear (DrumClass c) noexcept
{
    if (validClass (c))
        counts[idx (c)] = 0;
}

void CalibratedClassifier::clearAll() noexcept
{
    counts.fill (0);
}

int CalibratedClassifier::exampleCount (DrumClass c) const noexcept
{
    return validClass (c) ? counts[idx (c)] : 0;
}

const FeatureVector& CalibratedClassifier::example (DrumClass c, int i) const noexcept
{
    const auto ci = validClass (c) ? idx (c) : 0;
    return examples[ci][static_cast<std::size_t> (std::clamp (i, 0, maxExamplesPerClass - 1))];
}

void CalibratedClassifier::finalize() noexcept
{
    // Pass 1: per-class mean and sum of squared deviations.
    std::array<std::array<double, numFeatures>, numDrumClasses> ss {};
    std::array<double, numFeatures> pooledSs {};
    int pooledDof = 0;

    for (int c = 0; c < numDrumClasses; ++c)
    {
        const auto cs = static_cast<std::size_t> (c);
        const int n = counts[cs];
        for (int fi = 0; fi < numFeatures; ++fi)
        {
            const auto fs = static_cast<std::size_t> (fi);
            double sum = 0.0;
            for (int i = 0; i < n; ++i)
                sum += examples[cs][static_cast<std::size_t> (i)][fi];
            const double m = n > 0 ? sum / n : 0.0;

            double dev = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double dlt = examples[cs][static_cast<std::size_t> (i)][fi] - m;
                dev += dlt * dlt;
            }
            mean[cs][fi] = static_cast<float> (m);
            ss[cs][fs] = dev;
            pooledSs[fs] += dev;
        }
        if (n > 1)
            pooledDof += n - 1;
    }

    // Pass 2: shrink each class variance toward the pooled within-class
    // variance (a handful of examples badly underestimates the spread), then
    // apply the per-feature floor.
    constexpr double shrinkDof = 4.0;
    for (int c = 0; c < numDrumClasses; ++c)
    {
        const auto cs = static_cast<std::size_t> (c);
        const double dof = std::max (0, counts[cs] - 1);
        double logSum = 0.0, wSum = 0.0;
        for (int fi = 0; fi < numFeatures; ++fi)
        {
            const auto fs = static_cast<std::size_t> (fi);
            const double pooledVar = pooledDof > 0 ? pooledSs[fs] / pooledDof : 0.0;
            const double var = (ss[cs][fs] + shrinkDof * pooledVar) / (dof + shrinkDof);
            const double sigma = std::max (std::sqrt (var), static_cast<double> (spreadFloor (static_cast<Feature> (fi))));
            invSigma[cs][fi] = static_cast<float> (1.0 / sigma);
            const double w = featureWeight (static_cast<Feature> (fi));
            logSum += w * std::log (sigma / spreadFloor (static_cast<Feature> (fi)));
            wSum += w;
        }
        logSigma[cs] = static_cast<float> (logSum / wSum);
    }
}

bool CalibratedClassifier::isUsable() const noexcept
{
    int usable = 0;
    for (int n : counts)
        if (n >= 3)
            ++usable;
    return usable >= 2;
}

float CalibratedClassifier::distance (const FeatureVector& f, DrumClass c) const noexcept
{
    if (! validClass (c) || counts[idx (c)] == 0)
        return std::numeric_limits<float>::infinity();

    const auto cs = idx (c);
    double sum = 0.0, wsum = 0.0;
    for (int fi = 0; fi < numFeatures; ++fi)
    {
        const double w = featureWeight (static_cast<Feature> (fi));
        if (w <= 0.0)
            continue;
        const double z = (static_cast<double> (f[fi]) - mean[cs][fi]) * invSigma[cs][fi];
        sum += w * z * z;
        wsum += w;
    }

    const double d = std::sqrt (sum / wsum);
    return std::isfinite (d) ? static_cast<float> (d) : std::numeric_limits<float>::infinity();
}

float CalibratedClassifier::maxAbsZ (const FeatureVector& f, DrumClass c) const noexcept
{
    if (! validClass (c) || counts[idx (c)] == 0)
        return std::numeric_limits<float>::infinity();

    const auto cs = idx (c);
    float m = 0.0f;
    for (int fi = 0; fi < numFeatures; ++fi)
        if (featureWeight (static_cast<Feature> (fi)) > 0.0f)
            m = std::max (m, std::abs ((f[fi] - mean[cs][fi]) * invSigma[cs][fi]));
    return m;
}

Classification CalibratedClassifier::classify (const FeatureVector& f) const noexcept
{
    Classification result;
    float bestD = std::numeric_limits<float>::infinity();
    float bestCost = bestD;
    bool any = false;

    for (int c = 0; c < numDrumClasses; ++c)
    {
        const auto cls = static_cast<DrumClass> (c);
        if (! params.enabled[static_cast<std::size_t> (c)] || counts[static_cast<std::size_t> (c)] == 0)
            continue;
        // Selection by diagonal-Gaussian negative log-likelihood: the plain
        // normalised distance plus the mean log spread, so a class with a wide
        // spread does not swallow outliers of a tight class.
        const float d = distance (f, cls);
        const float cost = d * d + 2.0f * logSigma[static_cast<std::size_t> (c)];
        if (! any || cost < bestCost)
        {
            bestCost = cost;
            bestD = d;
            result.cls = cls;
            any = true;
        }
    }

    if (! any || ! std::isfinite (bestD))
        return {};

    // Score from the overall distance, reduced further if any single feature
    // is a gross outlier (a sound that matches "on average" but is clearly
    // different in one respect, e.g. a tone vs. a kick's low band).
    const float zPenalty = std::clamp ((maxZZero - maxAbsZ (f, result.cls)) / (maxZZero - maxZFull), 0.0f, 1.0f);
    result.score = std::exp (-0.5f * (bestD / distanceScale) * (bestD / distanceScale)) * zPenalty;
    result.accepted = result.score >= acceptanceThreshold (params.minScore, params.sensitivity[idx (result.cls)]);
    return result;
}

std::string CalibratedClassifier::toString() const
{
    std::string s = "m2mcal " + std::to_string (formatVersion) + " " + std::to_string (numFeatures) + "\n";
    char hex[16];

    for (int c = 0; c < numDrumClasses; ++c)
    {
        const auto cs = static_cast<std::size_t> (c);
        if (counts[cs] == 0)
            continue;
        s += drumClassName (static_cast<DrumClass> (c));
        s += " " + std::to_string (counts[cs]) + "\n";
        for (int i = 0; i < counts[cs]; ++i)
        {
            for (int fi = 0; fi < numFeatures; ++fi)
            {
                const auto bits = std::bit_cast<std::uint32_t> (examples[cs][static_cast<std::size_t> (i)][fi]);
                std::snprintf (hex, sizeof (hex), "%08x", static_cast<unsigned int> (bits));
                if (fi > 0)
                    s += ' ';
                s += hex;
            }
            s += '\n';
        }
    }

    s += "end\n";
    return s;
}

bool CalibratedClassifier::fromString (const std::string& text)
{
    std::istringstream in (text);
    std::string magic;
    int version = 0, nf = 0;
    if (! (in >> magic >> version >> nf) || magic != "m2mcal" || version != formatVersion || nf != numFeatures)
        return false;

    CalibratedClassifier parsed;
    parsed.params = params;

    std::string name;
    while (in >> name)
    {
        if (name == "end")
        {
            parsed.finalize();
            *this = parsed;
            return true;
        }

        int cls = -1;
        for (int c = 0; c < numDrumClasses; ++c)
            if (name == drumClassName (static_cast<DrumClass> (c)))
                cls = c;

        int count = 0;
        if (cls < 0 || ! (in >> count) || count < 0 || count > maxExamplesPerClass
            || parsed.counts[static_cast<std::size_t> (cls)] != 0)
            return false;

        for (int i = 0; i < count; ++i)
        {
            FeatureVector fv;
            for (int fi = 0; fi < numFeatures; ++fi)
            {
                std::string tok;
                if (! (in >> tok) || tok.size() != 8)
                    return false;
                char* endp = nullptr;
                const unsigned long bits = std::strtoul (tok.c_str(), &endp, 16);
                if (endp != tok.c_str() + tok.size())
                    return false;
                const float v = std::bit_cast<float> (static_cast<std::uint32_t> (bits));
                if (! std::isfinite (v))
                    return false;
                fv[fi] = v;
            }
            parsed.addExample (static_cast<DrumClass> (cls), fv);
        }
    }

    return false; // missing "end"
}

} // namespace m2m
