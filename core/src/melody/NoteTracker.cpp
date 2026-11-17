#include "m2m/melody/NoteTracker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace m2m
{

namespace
{
    constexpr float kSilentDb = -120.0f;
    constexpr float kStaleAttackMs = 150.0f; // an older level onset no longer counts as this note's attack
    constexpr float kOnsetLatchMs = 40.0f;   // how long an onset waits for a usable frame to retrigger
    constexpr float kRetriggerDipDb = 3.0f;  // the level must have dipped this much since the note began
}

void NoteTracker::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    updateDerived();
    reset();
}

void NoteTracker::setParams (const NoteTrackerParams& p) noexcept
{
    params = p;
    updateDerived();
}

std::int64_t NoteTracker::msToSamples (float ms) const noexcept
{
    if (! (ms > 0.0f))
        return 0;
    return static_cast<std::int64_t> (std::llround (static_cast<double> (ms) * 0.001 * sr));
}

void NoteTracker::updateDerived() noexcept
{
    minNoteSamples   = msToSamples (params.minNoteMs);
    minStableSamples = msToSamples (params.minStableMs);
    releaseSamples   = msToSamples (params.releaseMs);
    attackSamples    = msToSamples (params.attackWindowMs);
    onsetLatchSamples = msToSamples (kOnsetLatchMs);
    const float cents = std::isfinite (params.changeHysteresisCents) ? params.changeHysteresisCents : 60.0f;
    hysteresisSemis = std::clamp (cents, 1.0f, 1200.0f) / 100.0f;
}

void NoteTracker::reset() noexcept
{
    st = State::Idle;
    note = -1;
    candidate = -1;
    candidateSince = 0;
    noteOnTime = 0;
    releaseSince = -1;
    attackStart = -1;
    attackPeakDb = kSilentDb;
    levelArmed = true;
    latchedOnset = -1;
    activeMaxDb = activeDipDb = kSilentDb;
    peakWrite = 0;
    peakCount = 0;
}

void NoteTracker::startAttack (std::int64_t t) noexcept
{
    attackStart = t;
    attackPeakDb = kSilentDb;
}

bool NoteTracker::withinBand (float pitch, int centre) const noexcept
{
    return std::abs (pitch - static_cast<float> (centre)) <= hysteresisSemis;
}

float NoteTracker::recentPeakDb (std::int64_t t) const noexcept
{
    float peak = kSilentDb;
    for (int i = 0; i < peakCount; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        if (t - peakTimes[k] <= attackSamples)
            peak = std::max (peak, peakDbs[k]);
    }
    return peak;
}

float NoteTracker::peakSince (std::int64_t start) const noexcept
{
    float peak = kSilentDb;
    for (int i = 0; i < peakCount; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        if (peakTimes[k] >= start && peakTimes[k] - start <= attackSamples)
            peak = std::max (peak, peakDbs[k]);
    }
    return peak;
}

int NoteTracker::flush (std::int64_t time, TrackerEvent* out) noexcept
{
    int count = 0;
    if (st == State::Active)
        out[count++] = { false, note, kSilentDb, time };

    st = State::Idle;
    note = -1;
    candidate = -1;
    releaseSince = -1;
    latchedOnset = -1;
    return count;
}

int NoteTracker::process (const TrackerFrame& f, TrackerEvent* out) noexcept
{
    const std::int64_t t = f.time;
    int count = 0;

    // Velocity bookkeeping.
    peakDbs[static_cast<std::size_t> (peakWrite)] = f.peakDb;
    peakTimes[static_cast<std::size_t> (peakWrite)] = t;
    peakWrite = (peakWrite + 1) % peakHistory;
    peakCount = std::min (peakCount + 1, peakHistory);

    const bool usable = f.voiced && f.confidence >= params.confidenceThreshold;
    const bool levelOn = f.levelDb >= params.onThresholdDb;
    const bool levelLost = f.levelDb < params.offThresholdDb;

    // Attack window start: an amplitude onset, or the level crossing the
    // on-threshold after having been below the off-threshold.
    if (levelLost)
        levelArmed = true;
    if (st != State::Active && (f.onset || (levelArmed && levelOn)))
        startAttack (t);
    if (levelOn)
        levelArmed = false;

    if (attackStart >= 0 && t - attackStart <= attackSamples)
        attackPeakDb = std::max (attackPeakDb, f.peakDb);

    // Level contour of the sounding note: its maximum so far and the lowest
    // level since that maximum. A retrigger needs a dip: an amplitude onset
    // while the note's own attack is still rising (a vowel swelling after a
    // consonant, a slow attack) is the same note, not a new one.
    if (st == State::Active)
    {
        if (f.levelDb > activeMaxDb)
            activeMaxDb = activeDipDb = f.levelDb;
        else
            activeDipDb = std::min (activeDipDb, f.levelDb);
    }

    // While a note sounds, remember an onset briefly: the detector's confidence
    // often dips during the amplitude step itself, so the retrigger is applied
    // on the next usable frame.
    if (st == State::Active && f.onset && activeMaxDb - activeDipDb >= kRetriggerDipDb)
        latchedOnset = t;

    auto tryNoteOn = [&]
    {
        if (t - candidateSince >= minStableSamples && t - attackStart >= attackSamples)
        {
            out[count++] = { true, candidate, attackPeakDb, t };
            note = candidate;
            candidate = -1;
            noteOnTime = t;
            releaseSince = -1;
            latchedOnset = -1;
            activeMaxDb = activeDipDb = f.levelDb;
            st = State::Active;
        }
    };

    switch (st)
    {
        case State::Idle:
        {
            if (! (usable && levelOn))
                break;

            if (attackStart < 0 || t - attackStart > msToSamples (kStaleAttackMs))
            {
                startAttack (t);
                attackPeakDb = f.peakDb;
            }

            st = State::Pending;
            candidate = snapper.snap (f.pitch);
            candidateSince = t;
            tryNoteOn();
            break;
        }

        case State::Pending:
        {
            if (! usable || levelLost)
            {
                st = State::Idle;
                candidate = -1;
                break;
            }

            if (! withinBand (f.pitch, candidate))
            {
                const int c = snapper.snap (f.pitch);
                if (c != candidate)
                {
                    candidate = c;
                    candidateSince = t;
                }
            }
            tryNoteOn();
            break;
        }

        case State::Active:
        {
            const std::int64_t age = t - noteOnTime;

            if (! usable || levelLost)
            {
                candidate = -1;
                if (releaseSince < 0)
                    releaseSince = t;
                if (t - releaseSince >= releaseSamples && age >= minNoteSamples)
                {
                    out[count++] = { false, note, kSilentDb, t };
                    note = -1;
                    releaseSince = -1;
                    latchedOnset = -1;
                    st = State::Idle;
                }
                break;
            }
            releaseSince = -1;

            if (latchedOnset >= 0 && t - latchedOnset > onsetLatchSamples)
                latchedOnset = -1;

            if (latchedOnset >= 0 && params.retriggerEnabled && age >= minNoteSamples)
            {
                // Retrigger: end the note now, restart after the attack window
                // (which began at the onset).
                out[count++] = { false, note, kSilentDb, t };
                candidate = withinBand (f.pitch, note) ? note : snapper.snap (f.pitch);
                candidateSince = t;
                note = -1;
                st = State::Pending;
                startAttack (latchedOnset);
                attackPeakDb = peakSince (latchedOnset);
                latchedOnset = -1;
                break;
            }

            // Legato change.
            if (withinBand (f.pitch, note))
            {
                candidate = -1;
                break;
            }

            const int c = snapper.snap (f.pitch);
            if (c == note)
            {
                candidate = -1;
                break;
            }

            if (candidate < 0 || (c != candidate && ! withinBand (f.pitch, candidate)))
            {
                candidate = c;
                candidateSince = t;
            }

            if (t - candidateSince >= minStableSamples && age >= minNoteSamples)
            {
                out[count++] = { false, note, kSilentDb, t };
                out[count++] = { true, candidate, recentPeakDb (t), t };
                note = candidate;
                candidate = -1;
                noteOnTime = t;
                activeMaxDb = activeDipDb = f.levelDb;
            }
            break;
        }
    }

    return count;
}

} // namespace m2m
