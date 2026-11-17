#pragma once

#include "m2m/test/Notes.h"

#include <map>
#include <string>
#include <vector>

namespace m2m::test
{

struct MatchOptions
{
    double onsetToleranceSec = 0.050;  // |detected - truth| onset must be <= this
    bool   matchPitch = true;          // false = onset-only matching
};

/** truth note (or kSpurious) -> detected note (or kMissed) -> count. */
struct ConfusionMatrix
{
    static constexpr int kMissed    = -1;  // column: truth note with no detection
    static constexpr int kSpurious  = -1;  // row: detection with no truth note

    std::map<int, std::map<int, int>> counts;

    void add (int truthNote, int detectedNote) { ++counts[truthNote][detectedNote]; }
    int get (int truthNote, int detectedNote) const;
};

/** One matched truth/detection pair (indices into the evaluated vectors). */
struct NoteMatch
{
    std::size_t truthIndex = 0;
    std::size_t detectedIndex = 0;
    double onsetErrorSec = 0.0;   // detected - truth
};

/** Note-level evaluation.

    Matching: maximum-cardinality one-to-one matching of detections to truth
    notes whose onsets are within the tolerance (and, in pitch mode, whose
    MIDI notes are equal); among candidates the closest onset is preferred.

    Pitch errors: after pitch matching, the remaining truth notes and
    detections are matched again on onset only. Those pairs are counted as
    octaveErrors (|diff| == 12 or 24) or wrongPitch (any other diff) and land
    off the diagonal in the confusion matrix. They still count as a false
    negative + false positive for precision/recall.

    False triggers: detections that match no truth note even ignoring pitch,
    per minute of audio.

    Empty cases: no truth and no detections -> P = R = F1 = 1. */
struct Metrics
{
    int numTruth = 0;
    int numDetected = 0;
    int truePositives = 0;
    int falsePositives = 0;
    int falseNegatives = 0;

    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;

    // Over matched pairs; NaN when there are no matches.
    double meanAbsOnsetErrorMs = 0.0;
    double maxAbsOnsetErrorMs = 0.0;
    double meanSignedOnsetErrorMs = 0.0;   // > 0 = detections late (latency)
    // Over matched pairs whose truth note has offset > onset; NaN if none.
    double meanAbsOffsetErrorMs = 0.0;
    int offsetPairs = 0;

    int spuriousDetections = 0;            // matched nothing even ignoring pitch
    double falseTriggersPerMinute = 0.0;
    int octaveErrors = 0;
    int wrongPitch = 0;

    double durationSec = 0.0;
    double onsetToleranceMs = 0.0;
    bool pitchMatched = true;

    ConfusionMatrix confusion;
    std::vector<NoteMatch> matches;

    // Glide check (only with glide segments, see evaluate()).
    int glideChecks = 0;          // glide segments checked
    int glideFailures = 0;        // segments that broke a glide rule
    int glidePassingNotes = 0;    // in-between notes accepted as passing notes
    std::vector<std::string> glideProblems; // one line per broken rule
};

/** durationSec is used for false triggers per minute; if <= 0 the latest
    onset/offset seen is used. */
Metrics evaluate (const std::vector<TruthNote>& truth, const std::vector<DetectedNote>& detected,
                  double durationSec, const MatchOptions& options = {});

/** Evaluation with pitch glides.

    Truth for a glide is its start note (until the glide) and its end note
    with onset = glide end (when the target pitch is reached). Detections
    that start inside a glide (after startSec, up to endSec + tolerance)
    with a pitch strictly between fromNote and toNote are "passing notes":
    they are removed before the normal matching (neither hits nor false
    triggers). Each glide is then checked on the detections sounding during
    it (from startSec to endSec + tolerance):
      - the first is fromNote and the last is toNote;
      - pitches move monotonically in the glide's direction;
      - at most |toNote - fromNote| + 1 notes;
      - never two notes at once, and no gaps (each note ends where the next
        starts, within 1 ms).
    Failures are counted in glideFailures (described in glideProblems). */
Metrics evaluate (const std::vector<TruthNote>& truth, const std::vector<DetectedNote>& detected,
                  double durationSec, const MatchOptions& options, const std::vector<GlideSegment>& glides);

/** Convenience: onset-only evaluation (ignores pitch). */
Metrics evaluateOnsets (const std::vector<TruthNote>& truth, const std::vector<DetectedNote>& detected,
                        double durationSec, double onsetToleranceSec = 0.050);

} // namespace m2m::test
