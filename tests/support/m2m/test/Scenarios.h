#pragma once

#include "m2m/test/Notes.h"

#include <optional>
#include <string>
#include <vector>

namespace m2m::test
{

/** A synthetic test case: mono audio plus the notes/hits it contains. */
struct Scenario
{
    std::string name;
    double sampleRate = 48000.0;
    std::vector<float> audio;
    std::vector<TruthNote> notes;   // sorted by onset
    std::vector<GlideSegment> glides; // pitch glides (melody), scored by the glide check

    double durationSec() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double> (audio.size()) / sampleRate : 0.0;
    }
};

enum class ScenarioCategory { Trigger, Melody, Beatbox };

struct ScenarioInfo
{
    std::string name;
    ScenarioCategory category;
    std::string description;
};

const char* categoryName (ScenarioCategory c) noexcept;                    // "trigger" / "melody" / "beatbox"
std::optional<ScenarioCategory> categoryFromName (const std::string& name);

/** All scenarios in a fixed order (trigger, melody, beatbox). */
const std::vector<ScenarioInfo>& scenarioCatalogue();

std::vector<std::string> scenarioNames();
std::vector<std::string> scenarioNames (ScenarioCategory category);
bool hasScenario (const std::string& name);

/** Build a scenario by name (deterministic: same name + rate -> same samples).
    Returns nullopt for unknown names. */
std::optional<Scenario> makeScenario (const std::string& name, double sampleRate = 48000.0);

} // namespace m2m::test
