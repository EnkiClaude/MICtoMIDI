#include "m2m/pitch/ScaleSnap.h"

#include <cmath>
#include <initializer_list>

namespace m2m
{

namespace
{
    constexpr std::uint16_t bits (std::initializer_list<int> steps)
    {
        std::uint16_t m = 0;
        for (int s : steps)
            m = static_cast<std::uint16_t> (m | (1u << s));
        return m;
    }

    int pitchClass (int note) noexcept { return ((note % 12) + 12) % 12; }
}

std::uint16_t ScaleSnap::intervalMask (Scale s) noexcept
{
    switch (s)
    {
        case Scale::Off:             return 0x0FFF;
        case Scale::Major:           return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case Scale::NaturalMinor:    return bits ({ 0, 2, 3, 5, 7, 8, 10 });
        case Scale::HarmonicMinor:   return bits ({ 0, 2, 3, 5, 7, 8, 11 });
        case Scale::MajorPentatonic: return bits ({ 0, 2, 4, 7, 9 });
        case Scale::MinorPentatonic: return bits ({ 0, 3, 5, 7, 10 });
        case Scale::Blues:           return bits ({ 0, 3, 5, 6, 7, 10 });
        case Scale::Dorian:          return bits ({ 0, 2, 3, 5, 7, 9, 10 });
        case Scale::Mixolydian:      return bits ({ 0, 2, 4, 5, 7, 9, 10 });
    }
    return 0x0FFF;
}

void ScaleSnap::setScale (Scale s, int root) noexcept
{
    scale = s;
    rootPc = pitchClass (root);
    mask = intervalMask (s);
}

bool ScaleSnap::isAllowed (int note) const noexcept
{
    return (mask >> pitchClass (note - rootPc)) & 1u;
}

int ScaleSnap::snap (float fractionalMidi) const noexcept
{
    if (! std::isfinite (fractionalMidi))
        return 0;

    const double x = static_cast<double> (fractionalMidi);
    const int base = static_cast<int> (std::floor (x));

    // Search outward; every scale has a note within 6 semitones of any pitch.
    int best = base;
    double bestDist = 1.0e9;
    for (int candidate = base - 6; candidate <= base + 7; ++candidate)
    {
        if (! isAllowed (candidate))
            continue;
        const double dist = std::abs (x - candidate);
        if (dist < bestDist) // strict: on a tie the lower candidate (seen first) wins
        {
            bestDist = dist;
            best = candidate;
        }
    }
    return best;
}

} // namespace m2m
