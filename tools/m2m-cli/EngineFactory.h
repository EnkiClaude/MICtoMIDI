#pragma once

#include "m2m/Engine.h"

#include <memory>
#include <string>
#include <vector>

namespace m2m::cli
{

/** Modes the CLI accepts (whether or not an engine is built in). */
std::vector<std::string> engineModes();

bool isKnownMode (const std::string& mode);

/** Fresh engine for a mode, or nullptr if the mode is unknown or its engine
    is not available in this build. The caller prepares it. */
std::unique_ptr<m2m::IEngine> makeEngine (const std::string& mode);

} // namespace m2m::cli
