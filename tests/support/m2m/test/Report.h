#pragma once

#include "m2m/test/EngineRunner.h"
#include "m2m/test/Json.h"
#include "m2m/test/Metrics.h"
#include "m2m/test/Notes.h"

#include <map>
#include <string>
#include <vector>

namespace m2m::test
{

struct Scenario;

//==============================================================================
// Truth files
//
// {
//   "version": 1,
//   "name": "sustained_notes",          (optional)
//   "sampleRate": 48000,                (optional, informational)
//   "durationSec": 5.25,                (optional)
//   "notes": [ { "onsetSec": 0.25, "offsetSec": 1.05, "note": 57 }, ... ]
// }
// "offsetSec" may be omitted (drum hits) and then equals "onsetSec".

struct TruthFile
{
    std::string name;
    double sampleRate = 0.0;
    double durationSec = 0.0;
    std::vector<TruthNote> notes;   // sorted by onset after reading
};

Json truthToJson (const TruthFile& truth);
Json truthToJson (const Scenario& scenario);
bool truthFromJson (const Json& j, TruthFile& out, std::string& error);

bool readTruthFile (const std::string& path, TruthFile& out, std::string& error);
bool writeTruthFile (const std::string& path, const TruthFile& truth, std::string& error);

//==============================================================================
// Reports

/** All Metrics fields (NaN -> null) incl. the confusion matrix as
    { "<truth>": { "<detected>": count } } with "missed" / "spurious" keys. */
Json metricsToJson (const Metrics& m);

/** Timing / run info: block size, blocks, latency, mean/max block us, realtime factor. */
Json runInfoToJson (const RunResult& r);

Json notesToJson (const std::vector<DetectedNote>& notes);

//==============================================================================
// Baselines (regression gate)
//
// {
//   "version": 1,
//   "mode": "melody",
//   "blockSize": 256,
//   "toleranceMs": 50,
//   "tolerance": { "f1": 0.02, "onsetErrorMs": 2.0, "falseTriggersPerMinute": 1.0 },
//   "scenarios": {
//     "sustained_notes": {
//       "f1": 1.0, "meanAbsOnsetErrorMs": 3.1, "falseTriggersPerMinute": 0.0,   (measured, informational)
//       "minF1": 0.98, "maxMeanAbsOnsetErrorMs": 5.1, "maxFalseTriggersPerMinute": 1.0   (the gate)
//     }, ...
//   }
// }
// Gate fields can be hand-edited. A null/missing gate field is not checked.

struct BaselineTolerance
{
    double f1 = 0.02;
    double onsetErrorMs = 2.0;
    double falseTriggersPerMinute = 1.0;
};

struct BaselineEntry
{
    // Measured when the baseline was written (informational).
    double f1 = 0.0;
    double meanAbsOnsetErrorMs = 0.0;
    double falseTriggersPerMinute = 0.0;

    // Gates (NaN = not checked).
    double minF1 = 0.0;
    double maxMeanAbsOnsetErrorMs = 0.0;
    double maxFalseTriggersPerMinute = 0.0;
};

struct Baseline
{
    std::string mode;
    int blockSize = 256;
    double toleranceMs = 50.0;
    BaselineTolerance tolerance;
    std::map<std::string, BaselineEntry> scenarios;
};

/** Entry for measured metrics: gates = measured -/+ tolerance (F1 floor at 0). */
BaselineEntry makeBaselineEntry (const Metrics& m, const BaselineTolerance& tol);

/** Human-readable failures (empty = pass). */
std::vector<std::string> checkBaseline (const std::string& scenario, const Metrics& m, const BaselineEntry& e);

Json baselineToJson (const Baseline& b);
bool baselineFromJson (const Json& j, Baseline& out, std::string& error);
bool readBaselineFile (const std::string& path, Baseline& out, std::string& error);
bool writeBaselineFile (const std::string& path, const Baseline& b, std::string& error);

/** Read + parse a JSON file. */
bool readJsonFile (const std::string& path, Json& out, std::string& error);
bool writeJsonFile (const std::string& path, const Json& j, std::string& error);

} // namespace m2m::test
