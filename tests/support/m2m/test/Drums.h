#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace m2m::test
{

/** Synthetic vocal-percussion stand-ins. Each hit starts at sample 0 (the
    truth onset) and is normalised to peak level levelDb.

    Rough recipes:
    - Kick:      sine sweeping ~150 -> 50 Hz (exponential) + 2 ms click, ~300 ms
    - Snare:     200 Hz tone (fast decay) + band-passed noise (~1-6 kHz), ~200 ms
    - Clap:      4 noise bursts ~10 ms apart, band-passed 1-4 kHz, short tail
    - ClosedHat: high-passed noise (> 6 kHz), ~30 ms
    - OpenHat:   high-passed noise (> 6 kHz), ~250 ms decay */
enum class DrumKind { Kick, Snare, Clap, ClosedHat, OpenHat };

struct DrumParams
{
    double levelDb = -6.0;        // peak dBFS
    double variation = 0.0;       // 0 = canonical sound, 1 = full random variation
    std::uint64_t seed = 1;       // noise + variation seed
};

/** GM drum note: kick 36, snare 38, clap 39, closed hat 42, open hat 46. */
int gmNote (DrumKind kind) noexcept;
const char* drumName (DrumKind kind) noexcept;

std::vector<float> kick      (double sr, const DrumParams& p = {});
std::vector<float> snare     (double sr, const DrumParams& p = {});
std::vector<float> clap      (double sr, const DrumParams& p = {});
std::vector<float> closedHat (double sr, const DrumParams& p = {});
std::vector<float> openHat   (double sr, const DrumParams& p = {});

std::vector<float> drum (DrumKind kind, double sr, const DrumParams& p = {});

} // namespace m2m::test
