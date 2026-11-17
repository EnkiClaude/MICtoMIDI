#include "m2m/midi/DrumMapping.h"

#include <algorithm>

namespace m2m
{

namespace
{
    bool valid (DrumClass c) noexcept { return toIndex (c) >= 0 && toIndex (c) < numDrumClasses; }
    std::size_t idx (DrumClass c) noexcept { return static_cast<std::size_t> (toIndex (c)); }
}

void DrumMapping::setNote (DrumClass c, int note) noexcept
{
    if (valid (c))
        params.notes[idx (c)] = note;
}

void DrumMapping::setEnabled (DrumClass c, bool on) noexcept
{
    if (valid (c))
        params.enabled[idx (c)] = on;
}

std::uint8_t DrumMapping::noteFor (DrumClass c) const noexcept
{
    return valid (c) ? static_cast<std::uint8_t> (std::clamp (params.notes[idx (c)], 0, 127)) : 0;
}

bool DrumMapping::isEnabled (DrumClass c) const noexcept
{
    return valid (c) && params.enabled[idx (c)];
}

std::uint8_t DrumMapping::channel() const noexcept
{
    return static_cast<std::uint8_t> (std::clamp (params.channel, 1, 16));
}

} // namespace m2m
