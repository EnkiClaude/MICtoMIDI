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

    // Note lock.
    constexpr float kLockSpreadCents = 60.0f;  // settle window "stable": trimmed spread at most this
    constexpr float kLockMaxExtraMs = 60.0f;   // ... else choose anyway this long after the window filled
    constexpr float kLockSettleMinMs = 5.0f, kLockSettleMaxMs = 150.0f;
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

    const float settle = std::isfinite (params.lockSettleMs) ? params.lockSettleMs : 40.0f;
    lockSettleSamples  = msToSamples (std::clamp (settle, kLockSettleMinMs, kLockSettleMaxMs));
    lockMaxWaitSamples = lockSettleSamples + msToSamples (kLockMaxExtraMs);
    lockLeapSamples    = msToSamples (std::isfinite (params.lockLeapMs) ? std::clamp (params.lockLeapMs, 0.0f, 1000.0f) : 80.0f);
    lockLeapSemis      = std::isfinite (params.lockLeapSemitones) ? std::clamp (params.lockLeapSemitones, 0.0f, 48.0f) : 0.0f;
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
    lockClear();
    lockSince = 0;
    leapSince = -1;
    leapSide = 0;
}

void NoteTracker::lockPush (std::int64_t t, float pitch) noexcept
{
    lockPitches[static_cast<std::size_t> (lockWrite)] = pitch;
    lockTimes[static_cast<std::size_t> (lockWrite)] = t;
    lockWrite = (lockWrite + 1) % lockHistory;
    lockCount = std::min (lockCount + 1, lockHistory);
}

float NoteTracker::lockMedian (std::int64_t since, float& spread) noexcept
{
    int n = 0;
    for (int i = 0; i < lockCount; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        if (lockTimes[k] > since)
            lockSorted[static_cast<std::size_t> (n++)] = lockPitches[k];
    }
    if (n == 0)
    {
        spread = 0.0f;
        return 0.0f;
    }
    std::sort (lockSorted.begin(), lockSorted.begin() + n);
    // Trimmed spread (10 % each side): one stray frame does not count.
    const int trim = n / 10;
    spread = lockSorted[static_cast<std::size_t> (n - 1 - trim)] - lockSorted[static_cast<std::size_t> (trim)];
    const auto mid = static_cast<std::size_t> (n / 2);
    return (n % 2 == 1) ? lockSorted[mid] : 0.5f * (lockSorted[mid - 1] + lockSorted[mid]);
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
    lockClear();
    leapSince = -1;
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

    if (params.lock)
        return processLock (f, usable, levelOn, levelLost, out);

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

int NoteTracker::processLock (const TrackerFrame& f, bool usable, bool levelOn, bool levelLost,
                              TrackerEvent* out) noexcept
{
    const std::int64_t t = f.time;
    int count = 0;

    // Pending: wait until the pitch has settled, then choose the note once.
    auto settle = [&]
    {
        if (t - lockSince < lockSettleSamples || t - attackStart < attackSamples)
            return;
        float spread = 0.0f;
        const float median = lockMedian (t - lockSettleSamples, spread);
        if (spread * 100.0f > kLockSpreadCents && t - lockSince < lockMaxWaitSamples)
            return;

        note = snapper.snap (median);
        out[count++] = { true, note, attackPeakDb, t };
        candidate = -1;
        noteOnTime = t;
        releaseSince = -1;
        latchedOnset = -1;
        leapSince = -1;
        activeMaxDb = activeDipDb = f.levelDb;
        st = State::Active;
    };

    auto beginPending = [&]
    {
        st = State::Pending;
        lockClear();
        lockSince = t;
        lockPush (t, f.pitch);
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
            beginPending();
            settle();
            break;
        }

        case State::Pending:
        {
            if (! usable || levelLost)
            {
                st = State::Idle;
                lockClear();
                break;
            }
            lockPush (t, f.pitch);
            settle();
            break;
        }

        case State::Active:
        {
            const std::int64_t age = t - noteOnTime;

            if (! usable || levelLost)
            {
                leapSince = -1;
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
            lockPush (t, f.pitch);

            if (latchedOnset >= 0 && t - latchedOnset > onsetLatchSamples)
                latchedOnset = -1;

            if (latchedOnset >= 0 && params.retriggerEnabled && age >= minNoteSamples)
            {
                // Re-articulation: end the note now; the next one is chosen
                // afresh from the settled pitch after this onset.
                out[count++] = { false, note, kSilentDb, t };
                note = -1;
                startAttack (latchedOnset);
                attackPeakDb = peakSince (latchedOnset);
                latchedOnset = -1;
                beginPending();
                settle();
                break;
            }

            // Big-leap escape; otherwise the pitch never changes the note.
            if (lockLeapSemis <= 0.0f)
                break;

            const float d = f.pitch - static_cast<float> (note);
            const int side = d > lockLeapSemis ? 1 : (d < -lockLeapSemis ? -1 : 0);
            if (side == 0)
            {
                leapSince = -1;
                break;
            }
            if (leapSince < 0 || side != leapSide)
            {
                leapSince = t;
                leapSide = side;
            }
            if (t - leapSince >= lockLeapSamples && age >= minNoteSamples)
            {
                float spread = 0.0f;
                const int c = snapper.snap (lockMedian (std::max (leapSince - 1, t - lockSettleSamples), spread));
                leapSince = -1;
                if (c != note)
                {
                    out[count++] = { false, note, kSilentDb, t };
                    out[count++] = { true, c, recentPeakDb (t), t };
                    note = c;
                    noteOnTime = t;
                    activeMaxDb = activeDipDb = f.levelDb;
                }
            }
            break;
        }
    }

    return count;
}

} // namespace m2m
