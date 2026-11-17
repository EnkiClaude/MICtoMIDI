// m2m-cli: offline WAV -> MIDI + metrics, synthetic scenario generation and
// the metric regression gate. See docs/m2m-cli.md.

#include "EngineFactory.h"

#include "m2m/test/EngineRunner.h"
#include "m2m/test/Metrics.h"
#include "m2m/test/MidiFile.h"
#include "m2m/test/Report.h"
#include "m2m/test/Scenarios.h"
#include "m2m/test/WavIO.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace m2m::test;

namespace
{
    constexpr const char* kVersion = "0.1.0";

    enum ExitCode
    {
        kOk = 0,
        kIoError = 1,           // file could not be read/written/parsed
        kUsage = 2,             // bad command line
        kNoEngine = 3,          // mode's engine not in this build
        kRegression = 4,        // baseline gate failed
    };

    //==========================================================================
    struct Args
    {
        std::vector<std::string> positional;
        std::map<std::string, std::string> options;
        std::set<std::string> flags;

        std::string get (const std::string& key, const std::string& fallback = {}) const
        {
            const auto it = options.find (key);
            return it == options.end() ? fallback : it->second;
        }
        bool has (const std::string& key) const { return options.count (key) > 0; }
    };

    const std::set<std::string> kValueOptions {
        "--mode", "--truth", "--out", "--report", "--block", "--tolerance-ms",
        "--scenarios", "--baseline", "--write-baseline", "--sample-rate", "--format",
    };
    const std::set<std::string> kFlags { "--help", "-h", "--quiet" };

    bool parseArgs (int argc, char** argv, int first, Args& out, std::string& error)
    {
        for (int i = first; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (kFlags.count (a) > 0)
            {
                out.flags.insert (a);
            }
            else if (kValueOptions.count (a) > 0)
            {
                if (i + 1 >= argc)
                {
                    error = "option " + a + " needs a value";
                    return false;
                }
                out.options[a] = argv[++i];
            }
            else if (a.size() > 1 && a[0] == '-')
            {
                error = "unknown option " + a;
                return false;
            }
            else
            {
                out.positional.push_back (a);
            }
        }
        return true;
    }

    bool parseInt (const std::string& s, int& out)
    {
        char* end = nullptr;
        const long v = std::strtol (s.c_str(), &end, 10);
        if (s.empty() || *end != '\0' || v <= 0 || v > (1 << 24))
            return false;
        out = static_cast<int> (v);
        return true;
    }

    bool parsePositive (const std::string& s, double& out)
    {
        char* end = nullptr;
        const double v = std::strtod (s.c_str(), &end);
        if (s.empty() || *end != '\0' || ! (v > 0.0) || ! std::isfinite (v))
            return false;
        out = v;
        return true;
    }

    std::vector<std::string> split (const std::string& s, char sep)
    {
        std::vector<std::string> parts;
        std::string cur;
        for (const char c : s)
        {
            if (c == sep)
            {
                if (! cur.empty())
                    parts.push_back (cur);
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        if (! cur.empty())
            parts.push_back (cur);
        return parts;
    }

    void printUsage()
    {
        std::printf (
            "m2m-cli %s - MICtoMIDI offline test tool\n"
            "\n"
            "Usage:\n"
            "  m2m-cli run --mode <trigger|melody|beatbox> <in.wav> [--truth t.json] [--out out.mid]\n"
            "              [--report r.json] [--block N] [--tolerance-ms T]\n"
            "  m2m-cli gen <scenario|all|all-melody|all-beatbox|all-trigger> --out <dir>\n"
            "              [--sample-rate SR] [--format float32|pcm16]\n"
            "  m2m-cli scenarios\n"
            "  m2m-cli eval --mode <mode> [--scenarios a,b,c|all|all-melody|all-beatbox|all-trigger]\n"
            "              [--report r.json] [--baseline b.json] [--write-baseline b.json]\n"
            "              [--block N] [--tolerance-ms T] [--sample-rate SR]\n"
            "\n"
            "Exit codes: 0 ok, 1 I/O error, 2 usage error, 3 engine not available,\n"
            "            4 metric regression against --baseline.\n",
            kVersion);
    }

    //==========================================================================
    struct Common
    {
        std::string mode;
        int blockSize = 256;
        double toleranceMs = 50.0;
        double sampleRate = 48000.0;
    };

    bool parseCommon (const Args& args, Common& c, bool needMode)
    {
        c.mode = args.get ("--mode");
        if (needMode && c.mode.empty())
        {
            std::fprintf (stderr, "error: --mode is required\n");
            return false;
        }
        if (! c.mode.empty() && ! m2m::cli::isKnownMode (c.mode))
        {
            std::fprintf (stderr, "error: unknown mode '%s' (expected trigger, melody or beatbox)\n", c.mode.c_str());
            return false;
        }
        if (args.has ("--block") && ! parseInt (args.get ("--block"), c.blockSize))
        {
            std::fprintf (stderr, "error: --block needs a positive integer\n");
            return false;
        }
        if (args.has ("--tolerance-ms") && ! parsePositive (args.get ("--tolerance-ms"), c.toleranceMs))
        {
            std::fprintf (stderr, "error: --tolerance-ms needs a positive number\n");
            return false;
        }
        if (args.has ("--sample-rate") && ! parsePositive (args.get ("--sample-rate"), c.sampleRate))
        {
            std::fprintf (stderr, "error: --sample-rate needs a positive number\n");
            return false;
        }
        return true;
    }

    std::unique_ptr<m2m::IEngine> engineOrComplain (const std::string& mode)
    {
        auto e = m2m::cli::makeEngine (mode);
        if (e == nullptr)
            std::fprintf (stderr, "error: '%s' engine not available in this build\n", mode.c_str());
        return e;
    }

    std::string fmtMs (double v)
    {
        if (! std::isfinite (v))
            return "-";
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.1f", v);
        return buf;
    }

    Json onsetOnlyJson (const Metrics& m)
    {
        Json j = Json::object();
        j["precision"] = m.precision;
        j["recall"] = m.recall;
        j["f1"] = m.f1;
        j["meanAbsOnsetErrorMs"] = m.meanAbsOnsetErrorMs;
        return j;
    }

    void printMetrics (const Metrics& m, const Metrics& onsetOnly)
    {
        std::printf ("  truth %d, detected %d, matched %d (tolerance %.0f ms)\n",
                     m.numTruth, m.numDetected, m.truePositives, m.onsetToleranceMs);
        std::printf ("  precision %.3f  recall %.3f  F1 %.3f   (onset-only F1 %.3f)\n",
                     m.precision, m.recall, m.f1, onsetOnly.f1);
        std::printf ("  onset error: mean |e| %s ms, max |e| %s ms, mean signed (latency) %s ms\n",
                     fmtMs (m.meanAbsOnsetErrorMs).c_str(), fmtMs (m.maxAbsOnsetErrorMs).c_str(),
                     fmtMs (m.meanSignedOnsetErrorMs).c_str());
        std::printf ("  offset error: mean |e| %s ms over %d notes\n", fmtMs (m.meanAbsOffsetErrorMs).c_str(), m.offsetPairs);
        std::printf ("  false triggers %.2f/min (%d), octave errors %d, wrong pitch %d\n",
                     m.falseTriggersPerMinute, m.spuriousDetections, m.octaveErrors, m.wrongPitch);
    }

    void printRunInfo (const RunResult& r)
    {
        std::printf ("  block %d: %d blocks, mean %.2f us, max %.2f us, realtime factor %.5f, latency %d samples\n",
                     r.blockSize, r.numBlocks, r.meanBlockMicros, r.maxBlockMicros, r.realtimeFactor, r.latencySamples);
        if (r.fullBufferBlocks > 0)
            std::printf ("  warning: event buffer full in %d blocks (events may be dropped)\n", r.fullBufferBlocks);
    }

    Json reportHeader (const char* command, const Common& c)
    {
        Json j = Json::object();
        j["tool"] = "m2m-cli";
        j["version"] = kVersion;
        j["command"] = command;
        j["mode"] = c.mode;
        j["blockSize"] = c.blockSize;
        j["toleranceMs"] = c.toleranceMs;
        return j;
    }

    //==========================================================================
    int cmdRun (const Args& args)
    {
        Common c;
        if (! parseCommon (args, c, true))
            return kUsage;
        if (args.positional.size() != 1)
        {
            std::fprintf (stderr, "error: run needs exactly one input WAV file\n");
            return kUsage;
        }

        auto engine = engineOrComplain (c.mode);
        if (engine == nullptr)
            return kNoEngine;

        const std::string inPath = args.positional[0];
        std::string err;
        WavData wav;
        if (! readWav (inPath, wav, err))
        {
            std::fprintf (stderr, "error: %s\n", err.c_str());
            return kIoError;
        }

        std::optional<TruthFile> truth;
        if (args.has ("--truth"))
        {
            TruthFile t;
            if (! readTruthFile (args.get ("--truth"), t, err))
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                return kIoError;
            }
            truth = std::move (t);
        }

        const auto mono = mixToMono (wav);
        RunOptions ro;
        ro.blockSize = c.blockSize;
        const auto result = runEngine (*engine, mono, wav.sampleRate, ro);

        std::printf ("m2m-cli run: mode %s, '%s' (%.0f Hz, %d ch, %d-bit%s, %.2f s)\n",
                     c.mode.c_str(), inPath.c_str(), wav.sampleRate, wav.numChannels, wav.bitsPerSample,
                     wav.isFloat ? " float" : "", result.durationSec());
        printRunInfo (result);
        std::printf ("  detected %zu notes\n", result.notes.size());
        const std::size_t maxListed = 20;
        for (std::size_t i = 0; i < result.notes.size() && i < maxListed; ++i)
        {
            const auto& n = result.notes[i];
            std::printf ("    %8.3f - %8.3f s  %-4s (%3d) vel %3d ch %d\n", n.onsetSec, n.offsetSec,
                         m2m::noteName (n.midiNote).c_str(), n.midiNote, n.velocity, n.channel);
        }
        if (result.notes.size() > maxListed)
            std::printf ("    ... (%zu more)\n", result.notes.size() - maxListed);

        Json report = reportHeader ("run", c);
        report["input"] = inPath;
        report["run"] = runInfoToJson (result);
        report["notes"] = notesToJson (result.notes);

        if (truth)
        {
            const double tol = c.toleranceMs / 1000.0;
            const auto m = evaluate (truth->notes, result.notes, result.durationSec(), { tol, true });
            const auto mo = evaluateOnsets (truth->notes, result.notes, result.durationSec(), tol);
            printMetrics (m, mo);
            report["truth"] = args.get ("--truth");
            report["metrics"] = metricsToJson (m);
            report["onsetOnly"] = onsetOnlyJson (mo);
        }

        int rc = kOk;
        if (args.has ("--out"))
        {
            if (writeMidiFile (args.get ("--out"), result.events, wav.sampleRate, err))
                std::printf ("  wrote %s\n", args.get ("--out").c_str());
            else
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                rc = kIoError;
            }
        }
        if (args.has ("--report"))
        {
            if (writeJsonFile (args.get ("--report"), report, err))
                std::printf ("  wrote %s\n", args.get ("--report").c_str());
            else
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                rc = kIoError;
            }
        }
        return rc;
    }

    //==========================================================================
    /** Expand a comma-separated list of names and groups. */
    bool resolveScenarios (const std::string& spec, std::vector<std::string>& out)
    {
        for (const auto& item : split (spec, ','))
        {
            std::vector<std::string> names;
            if (item == "all")
                names = scenarioNames();
            else if (item.rfind ("all-", 0) == 0)
            {
                const auto cat = categoryFromName (item.substr (4));
                if (! cat)
                {
                    std::fprintf (stderr, "error: unknown scenario group '%s'\n", item.c_str());
                    return false;
                }
                names = scenarioNames (*cat);
            }
            else if (hasScenario (item))
                names = { item };
            else
            {
                std::fprintf (stderr, "error: unknown scenario '%s' (see 'm2m-cli scenarios')\n", item.c_str());
                return false;
            }

            for (auto& n : names)
                if (std::find (out.begin(), out.end(), n) == out.end())
                    out.push_back (n);
        }
        if (out.empty())
        {
            std::fprintf (stderr, "error: no scenarios selected\n");
            return false;
        }
        return true;
    }

    int cmdScenarios()
    {
        for (const auto& info : scenarioCatalogue())
            std::printf ("%-22s %-8s %s\n", info.name.c_str(), categoryName (info.category), info.description.c_str());
        return kOk;
    }

    int cmdGen (const Args& args)
    {
        Common c;
        if (! parseCommon (args, c, false))
            return kUsage;
        if (args.positional.size() != 1 || ! args.has ("--out"))
        {
            std::fprintf (stderr, "error: usage: m2m-cli gen <scenario|all> --out <dir>\n");
            return kUsage;
        }

        const std::string fmtName = args.get ("--format", "float32");
        if (fmtName != "float32" && fmtName != "pcm16")
        {
            std::fprintf (stderr, "error: --format must be float32 or pcm16\n");
            return kUsage;
        }
        const auto format = fmtName == "pcm16" ? WavFormat::Pcm16 : WavFormat::Float32;

        std::vector<std::string> names;
        if (! resolveScenarios (args.positional[0], names))
            return kUsage;

        const fs::path dir (args.get ("--out"));
        std::error_code ec;
        fs::create_directories (dir, ec);
        if (ec)
        {
            std::fprintf (stderr, "error: cannot create '%s': %s\n", dir.string().c_str(), ec.message().c_str());
            return kIoError;
        }

        for (const auto& name : names)
        {
            const auto sc = makeScenario (name, c.sampleRate);
            std::string err;
            const auto wavPath = (dir / (name + ".wav")).string();
            const auto truthPath = (dir / (name + ".truth.json")).string();
            if (! writeWavMono (wavPath, sc->audio, sc->sampleRate, format, err)
                || ! writeJsonFile (truthPath, truthToJson (*sc), err))
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                return kIoError;
            }
            std::printf ("wrote %s (%.2f s, %zu notes) + %s\n", wavPath.c_str(), sc->durationSec(),
                         sc->notes.size(), truthPath.c_str());
        }
        return kOk;
    }

    //==========================================================================
    int cmdEval (const Args& args)
    {
        Common c;
        if (! parseCommon (args, c, true))
            return kUsage;
        if (! args.positional.empty())
        {
            std::fprintf (stderr, "error: eval takes no positional arguments\n");
            return kUsage;
        }

        std::vector<std::string> names;
        if (! resolveScenarios (args.get ("--scenarios", "all-" + c.mode), names))
            return kUsage;

        if (m2m::cli::makeEngine (c.mode) == nullptr)
        {
            std::fprintf (stderr, "error: '%s' engine not available in this build\n", c.mode.c_str());
            return kNoEngine;
        }

        std::optional<Baseline> baseline;
        std::string err;
        if (args.has ("--baseline"))
        {
            Baseline b;
            if (! readBaselineFile (args.get ("--baseline"), b, err))
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                return kIoError;
            }
            if (! b.mode.empty() && b.mode != c.mode)
            {
                std::fprintf (stderr, "error: baseline is for mode '%s', not '%s'\n", b.mode.c_str(), c.mode.c_str());
                return kUsage;
            }
            if (b.blockSize != c.blockSize)
                std::printf ("note: baseline was written with block size %d, running with %d\n", b.blockSize, c.blockSize);
            if (std::abs (b.toleranceMs - c.toleranceMs) > 1e-9)
                std::printf ("note: baseline was written with tolerance %.1f ms, running with %.1f ms\n", b.toleranceMs, c.toleranceMs);
            baseline = std::move (b);
        }

        const double tol = c.toleranceMs / 1000.0;
        Json report = reportHeader ("eval", c);
        report["sampleRate"] = c.sampleRate;
        Json scenariosJson = Json::array();

        Baseline newBaseline;
        newBaseline.mode = c.mode;
        newBaseline.blockSize = c.blockSize;
        newBaseline.toleranceMs = c.toleranceMs;
        if (baseline)
            newBaseline.tolerance = baseline->tolerance;

        std::vector<std::string> failures;
        double sumF1 = 0.0, sumOnsetF1 = 0.0, worstF1 = 1.0;

        std::printf ("%-22s %6s %6s %6s %8s %9s %9s %8s %5s %5s\n", "scenario", "P", "R", "F1", "onsetF1",
                     "onset ms", "latency", "false/m", "oct", "wrong");
        for (const auto& name : names)
        {
            const auto sc = makeScenario (name, c.sampleRate);
            auto engine = m2m::cli::makeEngine (c.mode);
            RunOptions ro;
            ro.blockSize = c.blockSize;
            const auto r = runEngine (*engine, sc->audio, sc->sampleRate, ro);
            const auto m = evaluate (sc->notes, r.notes, sc->durationSec(), { tol, true });
            const auto mo = evaluateOnsets (sc->notes, r.notes, sc->durationSec(), tol);

            std::printf ("%-22s %6.3f %6.3f %6.3f %8.3f %9s %9s %8.2f %5d %5d\n", name.c_str(), m.precision, m.recall,
                         m.f1, mo.f1, fmtMs (m.meanAbsOnsetErrorMs).c_str(), fmtMs (m.meanSignedOnsetErrorMs).c_str(),
                         m.falseTriggersPerMinute, m.octaveErrors, m.wrongPitch);

            sumF1 += m.f1;
            sumOnsetF1 += mo.f1;
            worstF1 = std::min (worstF1, m.f1);

            Json s = Json::object();
            s["name"] = name;
            s["metrics"] = metricsToJson (m);
            s["onsetOnly"] = onsetOnlyJson (mo);
            s["run"] = runInfoToJson (r);
            scenariosJson.push (std::move (s));

            newBaseline.scenarios[name] = makeBaselineEntry (m, newBaseline.tolerance);

            if (baseline)
            {
                const auto it = baseline->scenarios.find (name);
                if (it == baseline->scenarios.end())
                    std::printf ("  note: no baseline entry for '%s'\n", name.c_str());
                else
                    for (auto& f : checkBaseline (name, m, it->second))
                        failures.push_back (std::move (f));
            }
        }

        const double n = static_cast<double> (names.size());
        Json summary = Json::object();
        summary["scenarios"] = names.size();
        summary["meanF1"] = sumF1 / n;
        summary["meanOnsetOnlyF1"] = sumOnsetF1 / n;
        summary["worstF1"] = worstF1;
        report["scenarios"] = std::move (scenariosJson);
        report["summary"] = std::move (summary);
        std::printf ("mean F1 %.3f, mean onset-only F1 %.3f, worst F1 %.3f\n", sumF1 / n, sumOnsetF1 / n, worstF1);

        if (baseline)
        {
            Json b = Json::object();
            b["path"] = args.get ("--baseline");
            b["passed"] = failures.empty();
            Json f = Json::array();
            for (const auto& s : failures)
                f.push (s);
            b["failures"] = std::move (f);
            report["baseline"] = std::move (b);

            if (failures.empty())
                std::printf ("baseline: PASS (%s)\n", args.get ("--baseline").c_str());
            else
            {
                std::printf ("baseline: FAIL (%s)\n", args.get ("--baseline").c_str());
                for (const auto& s : failures)
                    std::printf ("  REGRESSION %s\n", s.c_str());
            }
        }

        int rc = kOk;
        if (args.has ("--report"))
        {
            if (writeJsonFile (args.get ("--report"), report, err))
                std::printf ("wrote %s\n", args.get ("--report").c_str());
            else
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                rc = kIoError;
            }
        }
        if (args.has ("--write-baseline"))
        {
            if (writeBaselineFile (args.get ("--write-baseline"), newBaseline, err))
                std::printf ("wrote baseline %s\n", args.get ("--write-baseline").c_str());
            else
            {
                std::fprintf (stderr, "error: %s\n", err.c_str());
                rc = kIoError;
            }
        }

        if (rc != kOk)
            return rc;
        return failures.empty() ? kOk : kRegression;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return kUsage;
    }

    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "help")
    {
        printUsage();
        return kOk;
    }
    if (cmd == "--version")
    {
        std::printf ("m2m-cli %s\n", kVersion);
        return kOk;
    }

    Args args;
    std::string err;
    if (! parseArgs (argc, argv, 2, args, err))
    {
        std::fprintf (stderr, "error: %s\n", err.c_str());
        return kUsage;
    }
    if (args.flags.count ("--help") > 0 || args.flags.count ("-h") > 0)
    {
        printUsage();
        return kOk;
    }

    if (cmd == "run")       return cmdRun (args);
    if (cmd == "gen")       return cmdGen (args);
    if (cmd == "scenarios") return cmdScenarios();
    if (cmd == "eval")      return cmdEval (args);

    std::fprintf (stderr, "error: unknown command '%s'\n", cmd.c_str());
    printUsage();
    return kUsage;
}
