#pragma once

#include "m2m/beatbox/DrumClass.h"

namespace m2m
{

/** Setup-free classifier (PLAN.md section 6): each class gets a 0..1 score
    from soft ramps on the features, the highest enabled score wins, and the
    winner is accepted if it reaches acceptanceThreshold (minScore, sensitivity).

    Conceptual rules:
      Kick       most power < 1 kHz (mainly < 150 Hz), low centroid
      Snare      broadband: tonal body (150 Hz - 1 kHz) plus noise above 1 kHz, wide spread
      Clap       noise concentrated in 1-5 kHz, narrow spread, several bursts
      Closed hat power mostly > 5 kHz, decays fast inside the window
      Open hat   power mostly > 5 kHz, still sustained at the end of the window
      Toms       tonal (not flat), 150 Hz - 1 kHz; low/high split at ~200 Hz centroid

    Tuned on synthetic sounds; real voices vary a lot more, which is why the
    calibrated classifier is the recommended mode. Real-time safe. */
class RuleClassifier final : public IDrumClassifier
{
public:
    void setParams (const ClassifierParams& p) noexcept override { params = p; }
    const ClassifierParams& getParams() const noexcept { return params; }

    Classification classify (const FeatureVector& f) const noexcept override;

    /** Raw per-class scores (0..1) before enable/acceptance (diagnostics). */
    static void scores (const FeatureVector& f, float* out) noexcept;

private:
    ClassifierParams params;
};

} // namespace m2m
