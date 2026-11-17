#pragma once

#include <cstdint>

namespace m2m
{

enum class Scale : std::uint8_t
{
    Off,             // chromatic: nearest semitone
    Major,
    NaturalMinor,
    HarmonicMinor,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Dorian,
    Mixolydian
};

inline constexpr int kNumScales = 9;

/** Quantises a fractional MIDI pitch to the nearest note of a scale.

    Ties (pitch exactly halfway between two allowed notes) resolve to the
    LOWER note, so the result is deterministic. Real-time safe. */
class ScaleSnap
{
public:
    /** root: pitch class 0..11 (0 = C); wrapped into range. */
    void setScale (Scale s, int root) noexcept;

    Scale getScale() const noexcept { return scale; }
    int getRoot() const noexcept { return rootPc; }

    /** True if the MIDI note's pitch class is in the scale. */
    bool isAllowed (int note) const noexcept;

    /** Nearest allowed MIDI note (unclamped integer). */
    int snap (float fractionalMidi) const noexcept;

    /** 12-bit mask of allowed pitch classes relative to the root (bit 0 = root). */
    static std::uint16_t intervalMask (Scale s) noexcept;

private:
    Scale scale = Scale::Off;
    int rootPc = 0;
    std::uint16_t mask = 0x0FFF;
};

} // namespace m2m
