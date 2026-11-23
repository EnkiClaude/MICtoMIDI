#pragma once

#include "m2m/melody/MelodyEngine.h"

namespace m2m
{

/** The "Stability" macro (Loose <-> Tight): one amount 0..1 that sets the
    note-tracking values a wavering voice needs together. Pure function,
    real-time safe (no allocation), also used by m2m-cli (--set stability=).

    Mapped fields (piecewise linear between the anchors below; medianFrames
    rounded to the nearest integer):

      amount              0 (Loose)   kStabilityDefault   1 (Tight)
      changeHysteresisCents   45            60               100
      minStableMs              8            10                40
      medianFrames             1             3                 7
      smoothingMs              0             0                10
      minNoteMs               30            40                60
      lockSettleMs            30            40                60

    At kStabilityDefault every mapped value equals the MelodyParams default,
    so the default macro position changes nothing. Tighter = steadier notes
    but more note-on latency (MelodyEngine::latencySamples() grows with
    minStableMs / lockSettleMs and the median length). Every other field of
    `p` is returned unchanged. NaN counts as the default amount. */
MelodyParams applyStability (MelodyParams p, float amount01) noexcept;

/** The macro position whose mapped values equal the MelodyParams defaults. */
inline constexpr float kStabilityDefault = 0.4f;

} // namespace m2m
