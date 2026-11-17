#pragma once

#include "m2m/beatbox/DrumClass.h"

#include <array>
#include <cstdint>

namespace m2m
{

/** Drum class -> MIDI note (PLAN.md section 6). Defaults are General MIDI:
    Kick 36, Snare 38, Clap 39, Closed Hat 42, Open Hat 46, Low Tom 45,
    High Tom 50, channel 10. */
struct DrumMappingParams
{
    std::array<int, numDrumClasses> notes { 36, 38, 39, 42, 46, 45, 50 };
    std::array<bool, numDrumClasses> enabled { true, true, true, true, true, true, true };
    int channel = 10; // 1..16
};

/** Plain value type; every member is real-time safe. A disabled class is
    still classified but produces no MIDI. */
class DrumMapping
{
public:
    void setParams (const DrumMappingParams& p) noexcept { params = p; }
    const DrumMappingParams& getParams() const noexcept { return params; }

    void setNote (DrumClass c, int note) noexcept;
    void setEnabled (DrumClass c, bool on) noexcept;
    void setChannel (int channel) noexcept { params.channel = channel; }

    /** Clamped to 0..127; 0 for an invalid class. */
    std::uint8_t noteFor (DrumClass c) const noexcept;
    bool isEnabled (DrumClass c) const noexcept;
    /** Clamped to 1..16. */
    std::uint8_t channel() const noexcept;

private:
    DrumMappingParams params;
};

} // namespace m2m
