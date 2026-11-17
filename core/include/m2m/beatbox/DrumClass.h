#pragma once

#include "m2m/beatbox/Features.h"

#include <array>

namespace m2m
{

enum class DrumClass : int { Kick, Snare, Clap, ClosedHat, OpenHat, TomLow, TomHigh, Count };

constexpr int numDrumClasses = static_cast<int> (DrumClass::Count);

constexpr int toIndex (DrumClass c) noexcept { return static_cast<int> (c); }

/** Stable lowercase identifier ("kick", "closedhat", ...); "?" for invalid. */
const char* drumClassName (DrumClass c) noexcept;

/** Human-readable name ("Kick", "Closed Hat", ...). */
const char* drumClassDisplayName (DrumClass c) noexcept;

struct Classification
{
    DrumClass cls = DrumClass::Kick;
    float score = 0.0f;     // 0..1, higher = more confident
    bool accepted = false;  // false -> no MIDI event
};

/** Settings shared by both classifiers. */
struct ClassifierParams
{
    /** Per-class sensitivity 0..1. 0.5 = neutral (accept at minScore),
        1 = accept anything that wins, 0 = never accept this class. */
    std::array<float, numDrumClasses> sensitivity { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

    /** Classes that take part in classification at all. Toms are off by
        default (they are easily confused with kick/snare). */
    std::array<bool, numDrumClasses> enabled { true, true, true, true, true, false, false };

    /** Base acceptance score (at sensitivity 0.5). */
    float minScore = 0.35f;
};

/** Score a class must reach to be accepted, from minScore and the class's
    sensitivity: sens 0.5 -> minScore, sens 1 -> 0, sens 0 -> > 1 (never). */
float acceptanceThreshold (float minScore, float sensitivity) noexcept;

/** Common interface. classify() must be real-time safe. */
class IDrumClassifier
{
public:
    virtual ~IDrumClassifier() = default;
    virtual void setParams (const ClassifierParams& p) noexcept = 0;
    virtual Classification classify (const FeatureVector& f) const noexcept = 0;
};

} // namespace m2m
