#pragma once

#include "m2m/Engine.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace m2m::cli
{

/** Modes the CLI accepts (whether or not an engine is built in). */
std::vector<std::string> engineModes();

bool isKnownMode (const std::string& mode);

/** Engine parameter overrides, applied in order: key=value pairs naming
    MelodyParams / BeatboxParams fields (see settingKeys()). */
struct EngineSettings
{
    std::vector<std::pair<std::string, std::string>> values;

    bool empty() const noexcept { return values.empty(); }
};

/** Settable keys for a mode, with a short description each. */
std::vector<std::pair<std::string, std::string>> settingKeys (const std::string& mode);

/** Fresh engine for a mode, or nullptr if the mode is unknown or its engine
    is not available in this build. The caller prepares it. */
std::unique_ptr<m2m::IEngine> makeEngine (const std::string& mode);

/** As above, with parameter overrides. Returns nullptr and sets error if a
    key is unknown for the mode or a value does not parse (error is empty if
    the engine simply is not available). */
std::unique_ptr<m2m::IEngine> makeEngine (const std::string& mode, const EngineSettings& settings,
                                          std::string& error);

/** One line describing the effective engine parameters (for reports). */
std::string describeEngine (const std::string& mode, const m2m::IEngine& engine);

} // namespace m2m::cli
