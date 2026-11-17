#pragma once

#include "m2m/pitch/ScaleSnap.h"

#include <array>
#include <cstdint>

namespace m2m
{

/** One analysis frame as seen by the NoteTracker. */
struct TrackerFrame
{
    std::int64_t time = 0;     // absolute sample position of the decision
    float pitch = 0.0f;        // smoothed fractional MIDI pitch (ignored if !voiced)
    float confidence = 0.0f;   // 0..1
    bool voiced = false;       // detector found a pitch (before the confidence test)
    float levelDb = -120.0f;   // envelope level, dBFS
    float peakDb = -120.0f;    // peak |x| over this hop, dBFS (for velocity)
    bool onset = false;        // amplitude onset detected in this hop
};

/** Note event produced by the tracker (before transpose/channel/velocity mapping). */
struct TrackerEvent
{
    bool noteOn = false;
    int note = 0;              // snapped MIDI note (untransposed)
    float peakDb = -120.0f;    // attack peak level for velocity (note-on only)
    std::int64_t time = 0;
};

struct NoteTrackerParams
{
    float confidenceThreshold   = 0.6f;
    float onThresholdDb         = -45.0f;
    float offThresholdDb        = -55.0f;
    float minNoteMs             = 40.0f;
    float changeHysteresisCents = 60.0f;
    float minStableMs           = 10.0f;
    float releaseMs             = 30.0f;
    float attackWindowMs        = 10.0f;
    bool  retriggerEnabled      = true;
};

/** Monophonic note state machine (PLAN.md section 4): Idle -> Pending -> Active.

    - Idle -> Pending: voiced, confidence >= threshold, level >= on-threshold.
    - Pending -> Active (Note On): the snapped candidate note is held for
      minStableMs (pitch never leaves +/- changeHysteresis of it) AND the attack
      window (attackWindowMs from the onset) has elapsed. Velocity peak = max
      hop peak inside the attack window.
    - Pending -> Idle: unvoiced/low confidence or level < off-threshold.
    - Active, legato change: pitch further than changeHysteresis from the note
      centre, snapping to another note, held there for minStableMs ->
      Note Off(old) + Note On(new) at the same time, off first.
    - Active, retrigger: amplitude onset (if enabled) after the level has
      dipped >= 3 dB below the note's maximum so far -> Note Off now, back to
      Pending; the same note restarts after the stability/attack wait. (An
      onset during the note's own rising attack is not a retrigger.)
    - Active -> Idle: level < off-threshold or confidence lost, continuously for
      releaseMs.
    - Notes are never shorter than minNoteMs (off/change/retrigger wait).

    Invariants: at most one note sounding; every On is followed by an Off
    (flush() ends a held note); event times never decrease. Real-time safe. */
class NoteTracker
{
public:
    enum class State : std::uint8_t { Idle, Pending, Active };

    static constexpr int maxEventsPerFrame = 2;

    void prepare (double sampleRate) noexcept;
    void setParams (const NoteTrackerParams& p) noexcept;
    const NoteTrackerParams& getParams() const noexcept { return params; }

    void setScale (Scale s, int root) noexcept { snapper.setScale (s, root); }
    const ScaleSnap& scaleSnap() const noexcept { return snapper; }

    void reset() noexcept;

    /** Process one frame; writes up to maxEventsPerFrame events, returns count.
        Frame times must not decrease. */
    int process (const TrackerFrame& f, TrackerEvent* out) noexcept;

    /** Ends a sounding note at `time` (0 or 1 events) and returns to Idle. */
    int flush (std::int64_t time, TrackerEvent* out) noexcept;

    State state() const noexcept { return st; }
    int activeNote() const noexcept { return st == State::Active ? note : -1; }

private:
    std::int64_t msToSamples (float ms) const noexcept;
    void updateDerived() noexcept;
    void startAttack (std::int64_t t) noexcept;
    float recentPeakDb (std::int64_t t) const noexcept;
    float peakSince (std::int64_t start) const noexcept;
    bool withinBand (float pitch, int centre) const noexcept;

    NoteTrackerParams params;
    ScaleSnap snapper;
    double sr = 48000.0;

    std::int64_t minNoteSamples = 0, minStableSamples = 0, releaseSamples = 0, attackSamples = 0;
    std::int64_t onsetLatchSamples = 0;
    std::int64_t latchedOnset = -1;  // onset seen while Active, waiting for a usable frame
    float activeMaxDb = -120.0f;     // highest level since the note-on
    float activeDipDb = -120.0f;     // lowest level since that maximum
    float hysteresisSemis = 0.6f;

    State st = State::Idle;
    int note = -1;               // active note
    int candidate = -1;          // pending note / legato change candidate
    std::int64_t candidateSince = 0;
    std::int64_t noteOnTime = 0;
    std::int64_t releaseSince = -1;

    std::int64_t attackStart = -1;
    float attackPeakDb = -120.0f;
    bool levelArmed = true;      // level has been below the off-threshold since the last attack

    // Recent hop peaks for the velocity of legato changes.
    static constexpr int peakHistory = 64;
    std::array<float, peakHistory> peakDbs {};
    std::array<std::int64_t, peakHistory> peakTimes {};
    int peakWrite = 0, peakCount = 0;
};

} // namespace m2m
