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
      changeHysteresisCents   45            60                90
      minStableMs              8            10                20
      medianFrames             1             3                 5
      smoothingMs              0             0                 0
      minNoteMs               30            40                60
      lockSettleMs            10            15                30

    At kStabilityDefault every mapped value equals the MelodyParams default,
    so the default macro position changes nothing. Tighter = steadier notes
    but more note-on latency (MelodyEngine::latencySamples() grows with
    minStableMs / lockSettleMs and the median length). The anchors come
    from m2m-cli sweeps over all melody scenarios (docs/m2m-cli.md,
    "Stability macro"): beyond 90 cents / 20 ms Free mode loses legato
    semitone steps and goes past the latency budget, and any one-pole
    smoothing only made results worse (it delays every pitch change), so
    smoothingMs stays 0 at every position. Every other field of `p` is
    returned unchanged. NaN counts as the default amount. */
MelodyParams applyStability (MelodyParams p, float amount01) noexcept;

/** The macro position whose mapped values equal the MelodyParams defaults. */
inline constexpr float kStabilityDefault = 0.4f;

} // namespace m2m
