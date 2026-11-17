// The ONLY place the CLI creates engines. To wire in a new engine, include
// its header and return it from makeEngine() below.

#include "EngineFactory.h"

#include "m2m/trigger/LevelTriggerEngine.h"
#include "m2m/melody/MelodyEngine.h"
#include "m2m/beatbox/BeatboxEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace m2m::cli
{

namespace
{
    bool parseFloat (const std::string& s, float& out)
    {
        char* end = nullptr;
        const double v = std::strtod (s.c_str(), &end);
        if (s.empty() || *end != '\0' || ! std::isfinite (v))
            return false;
        out = static_cast<float> (v);
        return true;
    }

    bool parseIntValue (const std::string& s, int& out)
    {
        char* end = nullptr;
        const long v = std::strtol (s.c_str(), &end, 10);
        if (s.empty() || *end != '\0' || v < -100000 || v > 100000)
            return false;
        out = static_cast<int> (v);
        return true;
    }

    bool parseBool (const std::string& s, bool& out)
    {
        if (s == "1" || s == "true" || s == "on" || s == "yes")  { out = true;  return true; }
        if (s == "0" || s == "false" || s == "off" || s == "no") { out = false; return true; }
        return false;
    }

    template <typename P>
    struct Setting
    {
        const char* key;
        const char* help;
        std::function<bool (P&, const std::string&)> set;
    };

    template <typename P>
    Setting<P> floatSetting (const char* key, const char* help, float P::* member)
    {
        return { key, help, [member] (P& p, const std::string& v) { return parseFloat (v, p.*member); } };
    }

    //==========================================================================
    const std::vector<Setting<MelodyParams>>& melodySettings()
    {
        using P = MelodyParams;
        static const std::vector<Setting<P>> s {
            { "detector", "pitch detector: yin | mpm", [] (P& p, const std::string& v)
              {
                  if (v == "yin") { p.detector = PitchAlgorithm::Yin; return true; }
                  if (v == "mpm") { p.detector = PitchAlgorithm::Mpm; return true; }
                  return false;
              } },
            floatSetting<P> ("minHz", "lowest pitch (Hz); sets the window", &P::minHz),
            floatSetting<P> ("maxHz", "highest pitch (Hz)", &P::maxHz),
            floatSetting<P> ("confidenceThreshold", "0..1", &P::confidenceThreshold),
            { "medianFrames", "pitch median length (frames)", [] (P& p, const std::string& v) { return parseIntValue (v, p.medianFrames); } },
            floatSetting<P> ("smoothingMs", "one-pole pitch smoothing (ms)", &P::smoothingMs),
            floatSetting<P> ("highPassHz", "input high-pass (Hz)", &P::highPassHz),
            floatSetting<P> ("onThresholdDb", "note-on level (dBFS)", &P::onThresholdDb),
            floatSetting<P> ("offThresholdDb", "note-off level (dBFS)", &P::offThresholdDb),
            floatSetting<P> ("minNoteMs", "minimum note length (ms)", &P::minNoteMs),
            floatSetting<P> ("changeHysteresisCents", "pitch band around the note (cents)", &P::changeHysteresisCents),
            floatSetting<P> ("minStableMs", "pitch stability wait (ms)", &P::minStableMs),
            floatSetting<P> ("releaseMs", "release wait (ms)", &P::releaseMs),
            { "retriggerEnabled", "true | false", [] (P& p, const std::string& v) { return parseBool (v, p.retriggerEnabled); } },
            floatSetting<P> ("retriggerSensitivity", "0..1", &P::retriggerSensitivity),
            floatSetting<P> ("attackWindowMs", "velocity attack window (ms)", &P::attackWindowMs),
            { "transpose", "semitones", [] (P& p, const std::string& v) { return parseIntValue (v, p.transpose); } },
        };
        return s;
    }

    const std::vector<Setting<BeatboxParams>>& beatboxSettings()
    {
        using P = BeatboxParams;
        static const std::vector<Setting<P>> s {
            { "onset.sensitivity", "0..1", [] (P& p, const std::string& v) { return parseFloat (v, p.onset.sensitivity); } },
            { "onset.refractoryMs", "ms", [] (P& p, const std::string& v) { return parseFloat (v, p.onset.refractoryMs); } },
            { "onset.minLevelDb", "dBFS", [] (P& p, const std::string& v) { return parseFloat (v, p.onset.minLevelDb); } },
            floatSetting<P> ("analysisWindowMs", "classification window (ms)", &P::analysisWindowMs),
            { "classifier", "rules | calibrated", [] (P& p, const std::string& v)
              {
                  if (v == "rules")      { p.classifierMode = ClassifierMode::Rules; return true; }
                  if (v == "calibrated") { p.classifierMode = ClassifierMode::Calibrated; return true; }
                  return false;
              } },
            { "classifier.minScore", "0..1", [] (P& p, const std::string& v) { return parseFloat (v, p.classifier.minScore); } },
            floatSetting<P> ("gateMs", "note length (ms)", &P::gateMs),
            floatSetting<P> ("velocityWindowMs", "ms", &P::velocityWindowMs),
            floatSetting<P> ("highPassHz", "input high-pass (Hz)", &P::highPassHz),
        };
        return s;
    }

    template <typename P>
    bool applyAll (const std::vector<Setting<P>>& table, const EngineSettings& settings, P& p, std::string& error)
    {
        for (const auto& [key, value] : settings.values)
        {
            bool found = false;
            for (const auto& s : table)
                if (key == s.key)
                {
                    found = true;
                    if (! s.set (p, value))
                    {
                        error = "bad value '" + value + "' for " + key + " (" + s.help + ")";
                        return false;
                    }
                }
            if (! found)
            {
                error = "unknown engine setting '" + key + "' (see 'm2m-cli settings --mode <mode>')";
                return false;
            }
        }
        return true;
    }

    template <typename P>
    std::vector<std::pair<std::string, std::string>> keysOf (const std::vector<Setting<P>>& table)
    {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& s : table)
            out.emplace_back (s.key, s.help);
        return out;
    }
}

std::vector<std::string> engineModes()
{
    return { "trigger", "melody", "beatbox" };
}

bool isKnownMode (const std::string& mode)
{
    for (const auto& m : engineModes())
        if (m == mode)
            return true;
    return false;
}

std::vector<std::pair<std::string, std::string>> settingKeys (const std::string& mode)
{
    if (mode == "melody")
        return keysOf (melodySettings());
    if (mode == "beatbox")
        return keysOf (beatboxSettings());
    return {};
}

std::unique_ptr<m2m::IEngine> makeEngine (const std::string& mode)
{
    std::string ignored;
    return makeEngine (mode, {}, ignored);
}

std::unique_ptr<m2m::IEngine> makeEngine (const std::string& mode, const EngineSettings& settings, std::string& error)
{
    error.clear();

    if (mode == "trigger")
    {
        if (! settings.empty())
        {
            error = "the trigger engine has no settable parameters";
            return nullptr;
        }
        return std::make_unique<m2m::LevelTriggerEngine>(); // default params: note 60, -30 dBFS
    }

    if (mode == "melody")
    {
        auto e = std::make_unique<m2m::MelodyEngine>();
        auto p = e->getParams();
        if (! applyAll (melodySettings(), settings, p, error))
            return nullptr;
        e->setParams (p);
        return e;
    }

    if (mode == "beatbox")
    {
        auto e = std::make_unique<m2m::BeatboxEngine>();
        auto p = e->getParams();
        if (! applyAll (beatboxSettings(), settings, p, error))
            return nullptr;
        e->setParams (p);
        return e;
    }

    return nullptr;
}

std::string describeEngine (const std::string& mode, const m2m::IEngine& engine)
{
    char buf[512];
    if (mode == "melody")
    {
        const auto& p = static_cast<const m2m::MelodyEngine&> (engine).getParams();
        std::snprintf (buf, sizeof (buf),
                       "detector=%s minHz=%g maxHz=%g confidenceThreshold=%g medianFrames=%d minStableMs=%g "
                       "attackWindowMs=%g changeHysteresisCents=%g",
                       p.detector == PitchAlgorithm::Mpm ? "mpm" : "yin", static_cast<double> (p.minHz),
                       static_cast<double> (p.maxHz), static_cast<double> (p.confidenceThreshold), p.medianFrames,
                       static_cast<double> (p.minStableMs), static_cast<double> (p.attackWindowMs),
                       static_cast<double> (p.changeHysteresisCents));
        return buf;
    }
    if (mode == "beatbox")
    {
        const auto& p = static_cast<const m2m::BeatboxEngine&> (engine).getParams();
        std::snprintf (buf, sizeof (buf),
                       "classifier=%s analysisWindowMs=%g onset.sensitivity=%g onset.minLevelDb=%g "
                       "onset.refractoryMs=%g minScore=%g",
                       p.classifierMode == ClassifierMode::Calibrated ? "calibrated" : "rules",
                       static_cast<double> (p.analysisWindowMs), static_cast<double> (p.onset.sensitivity),
                       static_cast<double> (p.onset.minLevelDb), static_cast<double> (p.onset.refractoryMs),
                       static_cast<double> (p.classifier.minScore));
        return buf;
    }
    return "defaults";
}

} // namespace m2m::cli
