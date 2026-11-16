#pragma once

namespace m2m
{

struct VelocityParams
{
    float floorDb       = -40.0f; // peak at/below this -> minVelocity
    float ceilingDb     = -6.0f;  // peak at/above this -> maxVelocity
    float curve         = 1.0f;   // exponent: 1 linear, <1 louder/log-like, >1 softer/exp-like
    float sensitivity   = 1.0f;   // 0 => always fixedVelocity, 1 => fully level-derived
    int   fixedVelocity = 100;
    int   minVelocity   = 1;
    int   maxVelocity   = 127;
};

/** Maps an attack peak level (dBFS) to MIDI velocity (PLAN.md section 5).
    Real-time safe; shared by all engines. */
class VelocityMapper
{
public:
    void setParams (const VelocityParams& p) noexcept { params = p; }
    const VelocityParams& getParams() const noexcept { return params; }

    /** Always returns 1..127. */
    int map (float peakDb) const noexcept;

private:
    VelocityParams params;
};

} // namespace m2m
