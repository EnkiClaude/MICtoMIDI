// The ONLY place the CLI creates engines. To wire in a new engine, include
// its header and return it from makeEngine() below.

#include "EngineFactory.h"

#include "m2m/trigger/LevelTriggerEngine.h"
// #include "m2m/melody/MelodyEngine.h"     // TODO (integration): enable
// #include "m2m/beatbox/BeatboxEngine.h"   // TODO (integration): enable

namespace m2m::cli
{

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

std::unique_ptr<m2m::IEngine> makeEngine (const std::string& mode)
{
    if (mode == "trigger")
        return std::make_unique<m2m::LevelTriggerEngine>(); // default params: note 60, -30 dBFS

    if (mode == "melody")
        return nullptr; // TODO (integration): return std::make_unique<m2m::MelodyEngine>();

    if (mode == "beatbox")
        return nullptr; // TODO (integration): return std::make_unique<m2m::BeatboxEngine>();

    return nullptr;
}

} // namespace m2m::cli
