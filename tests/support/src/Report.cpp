#include "m2m/test/Report.h"
#include "m2m/test/Scenarios.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace m2m::test
{

namespace
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    std::string noteKey (int n, const char* negativeName)
    {
        return n < 0 ? std::string (negativeName) : std::to_string (n);
    }

    /** Number or null -> double (null/missing -> fallback). */
    double numOr (const Json& obj, const std::string& key, double fallback)
    {
        const auto* v = obj.find (key);
        return (v != nullptr && v->isNumber()) ? v->asNumber() : fallback;
    }

    std::string fmt (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.3f", v);
        return buf;
    }
}

//==============================================================================
Json truthToJson (const TruthFile& truth)
{
    Json j = Json::object();
    j["version"] = 1;
    if (! truth.name.empty())
        j["name"] = truth.name;
    if (truth.sampleRate > 0.0)
        j["sampleRate"] = truth.sampleRate;
    if (truth.durationSec > 0.0)
        j["durationSec"] = truth.durationSec;

    Json notes = Json::array();
    for (const auto& n : truth.notes)
    {
        Json o = Json::object();
        o["onsetSec"] = n.onsetSec;
        o["offsetSec"] = n.offsetSec;
        o["note"] = n.midiNote;
        notes.push (std::move (o));
    }
    j["notes"] = std::move (notes);

    if (! truth.glides.empty())
    {
        Json gl = Json::array();
        for (const auto& g : truth.glides)
        {
            Json o = Json::object();
            o["startSec"] = g.startSec;
            o["endSec"] = g.endSec;
            o["fromNote"] = g.fromNote;
            o["toNote"] = g.toNote;
            gl.push (std::move (o));
        }
        j["glides"] = std::move (gl);
    }
    return j;
}

Json truthToJson (const Scenario& scenario)
{
    return truthToJson (TruthFile { scenario.name, scenario.sampleRate, scenario.durationSec(), scenario.notes,
                                    scenario.glides });
}

bool truthFromJson (const Json& j, TruthFile& out, std::string& error)
{
    out = {};
    if (! j.isObject())
    {
        error = "truth JSON must be an object";
        return false;
    }
    const auto* notes = j.find ("notes");
    if (notes == nullptr || ! notes->isArray())
    {
        error = "truth JSON needs a \"notes\" array";
        return false;
    }

    out.name = j.getString ("name");
    out.sampleRate = j.getNumber ("sampleRate");
    out.durationSec = j.getNumber ("durationSec");

    for (std::size_t i = 0; i < notes->size(); ++i)
    {
        const auto& n = notes->at (i);
        const auto* onset = n.find ("onsetSec");
        const auto* note = n.find ("note");
        if (onset == nullptr || ! onset->isNumber() || note == nullptr || ! note->isNumber())
        {
            error = "truth note " + std::to_string (i) + " needs numeric \"onsetSec\" and \"note\"";
            return false;
        }
        const double on = onset->asNumber();
        out.notes.push_back ({ on, numOr (n, "offsetSec", on), note->asInt() });
    }

    std::stable_sort (out.notes.begin(), out.notes.end(),
                      [] (const TruthNote& a, const TruthNote& b) { return a.onsetSec < b.onsetSec; });

    if (const auto* gl = j.find ("glides"); gl != nullptr && gl->isArray())
        for (std::size_t i = 0; i < gl->size(); ++i)
        {
            const auto& g = gl->at (i);
            if (! g.isObject() || g.find ("startSec") == nullptr || g.find ("endSec") == nullptr
                || g.find ("fromNote") == nullptr || g.find ("toNote") == nullptr)
            {
                error = "truth glide " + std::to_string (i) + " needs startSec, endSec, fromNote, toNote";
                return false;
            }
            out.glides.push_back ({ g.getNumber ("startSec"), g.getNumber ("endSec"),
                                    static_cast<int> (g.getNumber ("fromNote")), static_cast<int> (g.getNumber ("toNote")) });
        }
    return true;
}

bool readJsonFile (const std::string& path, Json& out, std::string& error)
{
    std::string text;
    if (! readTextFile (path, text, error))
        return false;
    std::string perr;
    auto parsed = Json::parse (text, &perr);
    if (! parsed)
    {
        error = path + ": " + perr;
        return false;
    }
    out = std::move (*parsed);
    return true;
}

bool writeJsonFile (const std::string& path, const Json& j, std::string& error)
{
    return writeTextFile (path, j.dump (2), error);
}

bool readTruthFile (const std::string& path, TruthFile& out, std::string& error)
{
    Json j;
    if (! readJsonFile (path, j, error))
        return false;
    if (! truthFromJson (j, out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool writeTruthFile (const std::string& path, const TruthFile& truth, std::string& error)
{
    return writeJsonFile (path, truthToJson (truth), error);
}

//==============================================================================
Json metricsToJson (const Metrics& m)
{
    Json j = Json::object();
    j["numTruth"] = m.numTruth;
    j["numDetected"] = m.numDetected;
    j["truePositives"] = m.truePositives;
    j["falsePositives"] = m.falsePositives;
    j["falseNegatives"] = m.falseNegatives;
    j["precision"] = m.precision;
    j["recall"] = m.recall;
    j["f1"] = m.f1;
    j["meanAbsOnsetErrorMs"] = m.meanAbsOnsetErrorMs;
    j["maxAbsOnsetErrorMs"] = m.maxAbsOnsetErrorMs;
    j["meanSignedOnsetErrorMs"] = m.meanSignedOnsetErrorMs;
    j["meanAbsOffsetErrorMs"] = m.meanAbsOffsetErrorMs;
    j["offsetPairs"] = m.offsetPairs;
    j["spuriousDetections"] = m.spuriousDetections;
    j["falseTriggersPerMinute"] = m.falseTriggersPerMinute;
    j["octaveErrors"] = m.octaveErrors;
    j["wrongPitch"] = m.wrongPitch;
    j["durationSec"] = m.durationSec;
    j["onsetToleranceMs"] = m.onsetToleranceMs;
    j["pitchMatched"] = m.pitchMatched;
    if (m.glideChecks > 0)
    {
        j["glideChecks"] = m.glideChecks;
        j["glideFailures"] = m.glideFailures;
        j["glidePassingNotes"] = m.glidePassingNotes;
        Json probs = Json::array();
        for (const auto& p : m.glideProblems)
            probs.push (p);
        j["glideProblems"] = std::move (probs);
    }

    Json conf = Json::object();
    for (const auto& [truthNote, row] : m.confusion.counts)
    {
        Json r = Json::object();
        for (const auto& [detNote, count] : row)
            r[noteKey (detNote, "missed")] = count;
        conf[noteKey (truthNote, "spurious")] = std::move (r);
    }
    j["confusion"] = std::move (conf);
    return j;
}

Json runInfoToJson (const RunResult& r)
{
    Json j = Json::object();
    j["sampleRate"] = r.sampleRate;
    j["numSamples"] = r.numSamples;
    j["durationSec"] = r.durationSec();
    j["blockSize"] = r.blockSize;
    j["numBlocks"] = r.numBlocks;
    j["latencySamples"] = r.latencySamples;
    j["numEvents"] = r.events.size();
    j["numNotes"] = r.notes.size();
    j["fullBufferBlocks"] = r.fullBufferBlocks;
    j["meanBlockMicros"] = r.meanBlockMicros;
    j["maxBlockMicros"] = r.maxBlockMicros;
    j["realtimeFactor"] = r.realtimeFactor;
    return j;
}

Json notesToJson (const std::vector<DetectedNote>& notes)
{
    Json arr = Json::array();
    for (const auto& n : notes)
    {
        Json o = Json::object();
        o["onsetSec"] = n.onsetSec;
        o["offsetSec"] = n.offsetSec;
        o["note"] = n.midiNote;
        o["channel"] = n.channel;
        o["velocity"] = n.velocity;
        arr.push (std::move (o));
    }
    return arr;
}

//==============================================================================
BaselineEntry makeBaselineEntry (const Metrics& m, const BaselineTolerance& tol)
{
    BaselineEntry e;
    e.f1 = m.f1;
    e.meanAbsOnsetErrorMs = m.meanAbsOnsetErrorMs;
    e.falseTriggersPerMinute = m.falseTriggersPerMinute;
    e.minF1 = std::max (0.0, m.f1 - tol.f1);
    e.maxMeanAbsOnsetErrorMs = std::isfinite (m.meanAbsOnsetErrorMs) ? m.meanAbsOnsetErrorMs + tol.onsetErrorMs : nan;
    e.maxFalseTriggersPerMinute = m.falseTriggersPerMinute + tol.falseTriggersPerMinute;
    if (m.glideChecks > 0)
        e.maxGlideFailures = m.glideFailures;
    return e;
}

void applyLimits (BaselineEntry& e, const BaselineLimits& limits)
{
    if (! e.knownLimitation.empty())
        return;
    if (std::isfinite (limits.minF1) && ! (e.minF1 >= limits.minF1))
        e.minF1 = limits.minF1;
    if (std::isfinite (limits.maxMeanAbsOnsetErrorMs) && std::isfinite (e.maxMeanAbsOnsetErrorMs))
        e.maxMeanAbsOnsetErrorMs = std::min (e.maxMeanAbsOnsetErrorMs, limits.maxMeanAbsOnsetErrorMs);
    if (std::isfinite (limits.maxFalseTriggersPerMinute) && std::isfinite (e.maxFalseTriggersPerMinute))
        e.maxFalseTriggersPerMinute = std::min (e.maxFalseTriggersPerMinute, limits.maxFalseTriggersPerMinute);
}

std::vector<std::string> checkBaseline (const std::string& scenario, const Metrics& m, const BaselineEntry& e)
{
    constexpr double eps = 1e-9;
    std::vector<std::string> fails;
    if (std::isfinite (e.minF1) && ! (m.f1 >= e.minF1 - eps))
        fails.push_back (scenario + ": F1 " + fmt (m.f1) + " < min " + fmt (e.minF1));
    if (std::isfinite (e.maxMeanAbsOnsetErrorMs) && std::isfinite (m.meanAbsOnsetErrorMs)
        && m.meanAbsOnsetErrorMs > e.maxMeanAbsOnsetErrorMs + eps)
        fails.push_back (scenario + ": onset error " + fmt (m.meanAbsOnsetErrorMs) + " ms > max "
                         + fmt (e.maxMeanAbsOnsetErrorMs) + " ms");
    if (std::isfinite (e.maxFalseTriggersPerMinute) && m.falseTriggersPerMinute > e.maxFalseTriggersPerMinute + eps)
        fails.push_back (scenario + ": false triggers " + fmt (m.falseTriggersPerMinute) + "/min > max "
                         + fmt (e.maxFalseTriggersPerMinute) + "/min");
    if (std::isfinite (e.maxGlideFailures) && m.glideFailures > e.maxGlideFailures + eps)
        fails.push_back (scenario + ": " + std::to_string (m.glideFailures) + " glide check failures > max "
                         + fmt (e.maxGlideFailures));
    return fails;
}

Json baselineToJson (const Baseline& b)
{
    Json j = Json::object();
    j["version"] = 1;
    j["mode"] = b.mode;
    j["blockSize"] = b.blockSize;
    j["toleranceMs"] = b.toleranceMs;

    Json tol = Json::object();
    tol["f1"] = b.tolerance.f1;
    tol["onsetErrorMs"] = b.tolerance.onsetErrorMs;
    tol["falseTriggersPerMinute"] = b.tolerance.falseTriggersPerMinute;
    j["tolerance"] = std::move (tol);

    Json lim = Json::object();
    if (std::isfinite (b.limits.minF1))
        lim["minF1"] = b.limits.minF1;
    if (std::isfinite (b.limits.maxMeanAbsOnsetErrorMs))
        lim["maxMeanAbsOnsetErrorMs"] = b.limits.maxMeanAbsOnsetErrorMs;
    if (std::isfinite (b.limits.maxFalseTriggersPerMinute))
        lim["maxFalseTriggersPerMinute"] = b.limits.maxFalseTriggersPerMinute;
    if (! lim.keys().empty())
        j["limits"] = std::move (lim);

    Json sc = Json::object();
    for (const auto& [name, e] : b.scenarios)
    {
        Json o = Json::object();
        o["f1"] = e.f1;
        o["meanAbsOnsetErrorMs"] = e.meanAbsOnsetErrorMs;
        o["falseTriggersPerMinute"] = e.falseTriggersPerMinute;
        o["minF1"] = e.minF1;
        o["maxMeanAbsOnsetErrorMs"] = e.maxMeanAbsOnsetErrorMs;
        o["maxFalseTriggersPerMinute"] = e.maxFalseTriggersPerMinute;
        if (std::isfinite (e.maxGlideFailures))
            o["maxGlideFailures"] = e.maxGlideFailures;
        if (! e.knownLimitation.empty())
            o["knownLimitation"] = e.knownLimitation;
        sc[name] = std::move (o);
    }
    j["scenarios"] = std::move (sc);
    return j;
}

bool baselineFromJson (const Json& j, Baseline& out, std::string& error)
{
    out = {};
    if (! j.isObject())
    {
        error = "baseline JSON must be an object";
        return false;
    }
    out.mode = j.getString ("mode");
    out.blockSize = static_cast<int> (j.getNumber ("blockSize", 256));
    out.toleranceMs = j.getNumber ("toleranceMs", 50.0);
    if (const auto* tol = j.find ("tolerance"); tol != nullptr && tol->isObject())
    {
        out.tolerance.f1 = numOr (*tol, "f1", out.tolerance.f1);
        out.tolerance.onsetErrorMs = numOr (*tol, "onsetErrorMs", out.tolerance.onsetErrorMs);
        out.tolerance.falseTriggersPerMinute = numOr (*tol, "falseTriggersPerMinute", out.tolerance.falseTriggersPerMinute);
    }

    if (const auto* lim = j.find ("limits"); lim != nullptr && lim->isObject())
    {
        out.limits.minF1 = numOr (*lim, "minF1", nan);
        out.limits.maxMeanAbsOnsetErrorMs = numOr (*lim, "maxMeanAbsOnsetErrorMs", nan);
        out.limits.maxFalseTriggersPerMinute = numOr (*lim, "maxFalseTriggersPerMinute", nan);
    }

    const auto* sc = j.find ("scenarios");
    if (sc == nullptr || ! sc->isObject())
    {
        error = "baseline JSON needs a \"scenarios\" object";
        return false;
    }
    for (std::size_t i = 0; i < sc->keys().size(); ++i)
    {
        const auto& o = sc->items()[i];
        BaselineEntry e;
        e.f1 = numOr (o, "f1", nan);
        e.meanAbsOnsetErrorMs = numOr (o, "meanAbsOnsetErrorMs", nan);
        e.falseTriggersPerMinute = numOr (o, "falseTriggersPerMinute", nan);
        e.minF1 = numOr (o, "minF1", nan);
        e.maxMeanAbsOnsetErrorMs = numOr (o, "maxMeanAbsOnsetErrorMs", nan);
        e.maxFalseTriggersPerMinute = numOr (o, "maxFalseTriggersPerMinute", nan);
        e.maxGlideFailures = numOr (o, "maxGlideFailures", nan);
        e.knownLimitation = o.getString ("knownLimitation");
        out.scenarios[sc->keys()[i]] = e;
    }
    return true;
}

bool readBaselineFile (const std::string& path, Baseline& out, std::string& error)
{
    Json j;
    if (! readJsonFile (path, j, error))
        return false;
    if (! baselineFromJson (j, out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

bool writeBaselineFile (const std::string& path, const Baseline& b, std::string& error)
{
    return writeJsonFile (path, baselineToJson (b), error);
}

} // namespace m2m::test
