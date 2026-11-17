#include "m2m/beatbox/BeatboxEngine.h"

#include "m2m/audio/MathUtil.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <thread>

namespace m2m
{

namespace
{
    constexpr float maxWindowMs = 40.0f;
    constexpr float minWindowMs = 10.0f;
    constexpr float preWindowMs = 5.0f; // pre-onset audio for tail subtraction

    float finiteOr (float v, float fallback) noexcept { return std::isfinite (v) ? v : fallback; }

    std::uint64_t packClassification (const Classification& c) noexcept
    {
        return static_cast<std::uint64_t> (std::bit_cast<std::uint32_t> (c.score))
             | (static_cast<std::uint64_t> (static_cast<std::uint32_t> (toIndex (c.cls)) & 0xffu) << 32)
             | (static_cast<std::uint64_t> (c.accepted ? 1u : 0u) << 40);
    }

    Classification unpackClassification (std::uint64_t v) noexcept
    {
        Classification c;
        c.score = std::bit_cast<float> (static_cast<std::uint32_t> (v & 0xffffffffu));
        c.cls = static_cast<DrumClass> (static_cast<int> ((v >> 32) & 0xffu));
        c.accepted = ((v >> 40) & 1u) != 0;
        return c;
    }
}

BeatboxEngine::BeatboxEngine()
{
    for (auto& a : countAtomics)
        a.store (0, std::memory_order_relaxed);
}

//==============================================================================
void BeatboxEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = std::max (1, maxBlockSize);

    pre.prepare (sr);
    onsets.prepare (sr);
    features.prepare (sr, maxWindowMs);
    hop = onsets.hopSize();

    scratch.assign (static_cast<std::size_t> (std::max (maxBlock, hop)), 0.0f);
    windowBuf.assign (static_cast<std::size_t> (features.maxWindowSamples()), 0.0f);
    preN = std::max (16, static_cast<int> (std::lround (preWindowMs * 0.001 * sr)));
    preBuf.assign (static_cast<std::size_t> (preN), 0.0f);

    // Must hold the onset look-back plus the longest window plus a hop.
    const int needed = features.maxWindowSamples() + onsets.detectionDelaySamples() + 4 * hop + preN;
    const int ringSize = nextPowerOfTwo (std::max (needed, static_cast<int> (0.1 * sr)));
    ring.assign (static_cast<std::size_t> (ringSize), 0.0f);
    ringMask = ringSize - 1;

    setParams (params);
    reset();

    // Audio is not running during prepare(): apply queued messages now.
    applyMessages();
    publishCounts();
    publishCalibration();
}

void BeatboxEngine::reset() noexcept
{
    pre.reset();
    onsets.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    pos = 0;
    numPending = 0;
    numOffs = 0;
}

int BeatboxEngine::latencySamples() const noexcept
{
    return std::max (windowN, onsets.detectionDelaySamples()) + hop;
}

void BeatboxEngine::setParams (const BeatboxParams& p) noexcept
{
    params = p;
    updateDerived();
}

void BeatboxEngine::updateDerived() noexcept
{
    pre.setHighPassHz (finiteOr (params.highPassHz, 30.0f));
    onsets.setParams (params.onset);

    const float winMs = std::clamp (finiteOr (params.analysisWindowMs, 20.0f), minWindowMs, maxWindowMs);
    windowN = features.windowSamples (winMs);
    const float velMs = std::clamp (finiteOr (params.velocityWindowMs, 10.0f), 1.0f, winMs);
    velocityN = std::clamp (static_cast<int> (std::lround (velMs * 0.001 * sr)), 1, windowN);
    const float gate = std::clamp (finiteOr (params.gateMs, 60.0f), 1.0f, 2000.0f);
    gateSamples = std::max<std::int64_t> (1, static_cast<std::int64_t> (std::lround (gate * 0.001 * sr)));

    rules.setParams (params.classifier);
    calibrated.setParams (params.classifier);
    velocityMapper.setParams (params.velocity);
    mapping.setParams (params.mapping);
}

//==============================================================================
// Message thread -> audio thread

bool BeatboxEngine::pushCommand (CmdType t, int cls) noexcept
{
    const auto h = cmdHead.load (std::memory_order_relaxed);
    const auto tl = cmdTail.load (std::memory_order_acquire);
    if (h - tl >= commands.size())
        return false;
    commands[h % commands.size()] = { t, cls };
    cmdHead.store (h + 1, std::memory_order_release);
    return true;
}

void BeatboxEngine::beginCalibration (DrumClass c) noexcept
{
    if (toIndex (c) >= 0 && toIndex (c) < numDrumClasses)
        pushCommand (CmdType::Begin, toIndex (c));
}

void BeatboxEngine::endCalibration() noexcept     { pushCommand (CmdType::End, 0); }
void BeatboxEngine::cancelCalibration() noexcept  { pushCommand (CmdType::Cancel, 0); }
void BeatboxEngine::clearAllCalibration() noexcept { pushCommand (CmdType::ClearAll, 0); }

void BeatboxEngine::clearCalibration (DrumClass c) noexcept
{
    if (toIndex (c) >= 0 && toIndex (c) < numDrumClasses)
        pushCommand (CmdType::Clear, toIndex (c));
}

int BeatboxEngine::calibrationExampleCount (DrumClass c) const noexcept
{
    const int i = toIndex (c);
    return i >= 0 && i < numDrumClasses ? countAtomics[static_cast<std::size_t> (i)].load (std::memory_order_acquire) : 0;
}

bool BeatboxEngine::setCalibrationString (const std::string& text)
{
    CalibratedClassifier parsed;
    if (! parsed.fromString (text))
        return false;

    // Mailbox to the audio thread: take the slot (free, or ready = not yet
    // consumed, which we overwrite). Wait only while the audio thread copies.
    for (;;)
    {
        int expected = slotFree;
        if (incomingState.compare_exchange_weak (expected, slotWriting, std::memory_order_acquire))
            break;
        expected = slotReady;
        if (incomingState.compare_exchange_weak (expected, slotWriting, std::memory_order_acquire))
            break;
        std::this_thread::yield();
    }
    incoming = parsed;
    incomingState.store (slotReady, std::memory_order_release);

    // Also update the snapshot so getCalibrationString() reflects it at once.
    for (;;)
    {
        int expected = slotFree;
        if (snapshotState.compare_exchange_weak (expected, slotWriting, std::memory_order_acquire))
            break;
        expected = slotReady;
        if (snapshotState.compare_exchange_weak (expected, slotWriting, std::memory_order_acquire))
            break;
        std::this_thread::yield();
    }
    snapshot = parsed;
    snapshotState.store (slotReady, std::memory_order_release);
    return true;
}

std::string BeatboxEngine::getCalibrationString() const
{
    CalibratedClassifier copy;
    for (;;)
    {
        int expected = slotReady;
        if (snapshotState.compare_exchange_weak (expected, slotReading, std::memory_order_acquire))
        {
            copy = snapshot;
            snapshotState.store (slotReady, std::memory_order_release);
            break;
        }
        if (expected == slotFree)
            break; // nothing published yet: empty calibration
        std::this_thread::yield();
    }
    return copy.toString();
}

//==============================================================================
// Audio thread side of the hand-over

void BeatboxEngine::applyMessages() noexcept
{
    // 1. a complete calibration loaded from the message thread
    int expected = slotReady;
    if (incomingState.compare_exchange_strong (expected, slotReading, std::memory_order_acquire))
    {
        calibrated = incoming;
        incomingState.store (slotFree, std::memory_order_release);
        calibrated.setParams (params.classifier);
        calibrated.finalize();
        calibActive = false;
        snapshotDirty = true;
        publishCounts();
    }

    // 2. queued calibration commands
    auto t = cmdTail.load (std::memory_order_relaxed);
    const auto h = cmdHead.load (std::memory_order_acquire);
    while (t != h)
    {
        applyCommand (commands[t % commands.size()]);
        ++t;
    }
    cmdTail.store (t, std::memory_order_release);

    if (snapshotDirty)
        publishCalibration();
}

void BeatboxEngine::applyCommand (const Command& c) noexcept
{
    const auto cls = static_cast<DrumClass> (std::clamp (c.cls, 0, numDrumClasses - 1));

    switch (c.type)
    {
        case CmdType::Begin:
        {
            calibCls = cls;
            backupCount = calibrated.exampleCount (cls);
            for (int i = 0; i < backupCount; ++i)
                backup[static_cast<std::size_t> (i)] = calibrated.example (cls, i);
            calibrated.clear (cls);
            calibActive = true;
            break;
        }
        case CmdType::End:
            calibActive = false;
            break;
        case CmdType::Cancel:
            if (calibActive)
            {
                calibrated.clear (calibCls);
                for (int i = 0; i < backupCount; ++i)
                    calibrated.addExample (calibCls, backup[static_cast<std::size_t> (i)]);
            }
            calibActive = false;
            break;
        case CmdType::Clear:
            calibrated.clear (cls);
            if (calibActive && calibCls == cls)
                backupCount = 0;
            break;
        case CmdType::ClearAll:
            calibrated.clearAll();
            calibActive = false;
            break;
    }

    calibrated.finalize();
    snapshotDirty = true;
    publishCounts();
}

void BeatboxEngine::publishCounts() noexcept
{
    for (int c = 0; c < numDrumClasses; ++c)
        countAtomics[static_cast<std::size_t> (c)].store (calibrated.exampleCount (static_cast<DrumClass> (c)),
                                                          std::memory_order_release);
    usableAtomic.store (calibrated.isUsable(), std::memory_order_release);
    calibClassAtomic.store (toIndex (calibCls), std::memory_order_release);
    calibratingAtomic.store (calibActive, std::memory_order_release);
}

void BeatboxEngine::publishCalibration() noexcept
{
    // Try-only: never wait on the audio thread. If the message thread is
    // reading or writing the snapshot, retry on the next block.
    int expected = snapshotState.load (std::memory_order_relaxed);
    if ((expected != slotFree && expected != slotReady)
        || ! snapshotState.compare_exchange_strong (expected, slotWriting, std::memory_order_acquire))
        return;

    snapshot = calibrated;
    snapshotState.store (slotReady, std::memory_order_release);
    snapshotDirty = false;
}

//==============================================================================
Classification BeatboxEngine::lastClassification() const noexcept
{
    return unpackClassification (lastClassAtomic.load (std::memory_order_relaxed));
}

void BeatboxEngine::pushHit (const HitInfo& hInfo) noexcept
{
    const auto h = hitHead.load (std::memory_order_relaxed);
    const auto t = hitTail.load (std::memory_order_acquire);
    if (h - t >= hits.size())
        return; // full: drop
    hits[h % hits.size()] = hInfo;
    hitHead.store (h + 1, std::memory_order_release);
}

int BeatboxEngine::getRecentHits (HitInfo* out, int max) noexcept
{
    auto t = hitTail.load (std::memory_order_relaxed);
    const auto h = hitHead.load (std::memory_order_acquire);
    int n = 0;
    while (t != h && n < max)
    {
        out[n++] = hits[t % hits.size()];
        ++t;
    }
    hitTail.store (t, std::memory_order_release);
    return n;
}

//==============================================================================
void BeatboxEngine::flush (MidiEventBuffer& out, int sampleOffset) noexcept
{
    for (int i = 0; i < numOffs; ++i)
        out.push ({ MidiEvent::Type::NoteOff, offs[static_cast<std::size_t> (i)].channel,
                    offs[static_cast<std::size_t> (i)].note, 0, sampleOffset });
    numOffs = 0;
}

void BeatboxEngine::emitNoteOffsUpTo (std::int64_t t, std::int64_t blockStart, MidiEventBuffer& out) noexcept
{
    // Emit due note-offs in time order (the list is tiny).
    for (;;)
    {
        int best = -1;
        for (int i = 0; i < numOffs; ++i)
            if (offs[static_cast<std::size_t> (i)].time <= t
                && (best < 0 || offs[static_cast<std::size_t> (i)].time < offs[static_cast<std::size_t> (best)].time))
                best = i;
        if (best < 0)
            return;

        const auto& o = offs[static_cast<std::size_t> (best)];
        out.push ({ MidiEvent::Type::NoteOff, o.channel, o.note, 0, static_cast<std::int32_t> (o.time - blockStart) });
        offs[static_cast<std::size_t> (best)] = offs[static_cast<std::size_t> (numOffs - 1)];
        --numOffs;
    }
}

void BeatboxEngine::process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept
{
    applyMessages();

    const std::int64_t blockStart = pos;

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (mono[i]));
    inputLevelAtomic.store (gainToDb (peak), std::memory_order_relaxed);

    const int chunkMax = static_cast<int> (scratch.size());
    int done = 0;
    while (done < numSamples)
    {
        // Chunks never cross a hop boundary, so each completed hop is handled
        // at exactly the same absolute sample whatever the block size.
        const int toHop = hop - static_cast<int> (pos % hop);
        const int n = std::min ({ numSamples - done, chunkMax, toHop });

        float* y = scratch.data();
        pre.process (mono + done, y, n);
        for (int i = 0; i < n; ++i)
            ring[static_cast<std::size_t> ((pos + i) & ringMask)] = y[i];

        OnsetEvent ev[2];
        const int got = onsets.process (y, n, ev, 2);
        pos += n;
        done += n;

        for (int k = 0; k < got; ++k)
        {
            if (numPending < static_cast<int> (pending.size()))
                pending[static_cast<std::size_t> (numPending++)] = { ev[k].samplePosition, ev[k].strength };
        }

        if (pos % hop == 0)
            handleHop (pos, blockStart, out);
    }

    emitNoteOffsUpTo (blockStart + numSamples - 1, blockStart, out);
}

void BeatboxEngine::handleHop (std::int64_t hopEnd, std::int64_t blockStart, MidiEventBuffer& out) noexcept
{
    const std::int64_t emitSample = hopEnd - 1; // last sample of this hop
    emitNoteOffsUpTo (emitSample, blockStart, out);

    int i = 0;
    while (i < numPending)
    {
        const auto p = pending[static_cast<std::size_t> (i)];
        if (p.onset + windowN <= hopEnd)
        {
            classifyOnset (p, emitSample, blockStart, out);
            for (int k = i + 1; k < numPending; ++k)
                pending[static_cast<std::size_t> (k - 1)] = pending[static_cast<std::size_t> (k)];
            --numPending;
        }
        else
        {
            ++i;
        }
    }
}

void BeatboxEngine::classifyOnset (const PendingOnset& p, std::int64_t emitSample, std::int64_t blockStart,
                                   MidiEventBuffer& out) noexcept
{
    float velPeak = 0.0f;
    for (int i = 0; i < windowN; ++i)
    {
        const float v = ring[static_cast<std::size_t> ((p.onset + i) & ringMask)];
        windowBuf[static_cast<std::size_t> (i)] = v;
        if (i < velocityN)
            velPeak = std::max (velPeak, std::abs (v));
    }

    HitInfo hit;
    hit.onsetSample = p.onset;
    hit.emitSample = emitSample;
    for (int i = 0; i < preN; ++i)
        preBuf[static_cast<std::size_t> (i)] = ring[static_cast<std::size_t> ((p.onset - preN + i) & ringMask)];
    hit.features = features.extract (windowBuf.data(), windowN, preBuf.data(), p.onset >= preN ? preN : 0);
    hit.velocity = velocityMapper.map (gainToDb (velPeak));

    if (calibActive)
    {
        calibrated.addExample (calibCls, hit.features);
        calibrated.finalize();
        snapshotDirty = true;
        publishCounts();

        hit.cls = calibCls;
        hit.score = 1.0f;
        hit.accepted = true;
        hit.calibration = true;
        lastClassAtomic.store (packClassification ({ hit.cls, hit.score, true }), std::memory_order_relaxed);
        pushHit (hit);
        return;
    }

    const bool useCal = params.classifierMode == ClassifierMode::Calibrated && calibrated.isUsable();
    usingCalibratedAtomic.store (useCal, std::memory_order_relaxed);
    const Classification c = useCal ? calibrated.classify (hit.features) : rules.classify (hit.features);

    hit.cls = c.cls;
    hit.score = c.score;
    hit.accepted = c.accepted;
    hit.calibrated = useCal;
    lastClassAtomic.store (packClassification (c), std::memory_order_relaxed);

    if (c.accepted && mapping.isEnabled (c.cls))
    {
        const std::uint8_t note = mapping.noteFor (c.cls);
        const std::uint8_t ch = mapping.channel();
        const auto offset = static_cast<std::int32_t> (emitSample - blockStart);

        // Retrigger: close a still-held instance of the same note first.
        for (int i = 0; i < numOffs; ++i)
        {
            auto& o = offs[static_cast<std::size_t> (i)];
            if (o.note == note && o.channel == ch)
            {
                out.push ({ MidiEvent::Type::NoteOff, ch, note, 0, offset });
                o = offs[static_cast<std::size_t> (numOffs - 1)];
                --numOffs;
                break;
            }
        }

        if (numOffs >= static_cast<int> (offs.size()))
        {
            // Too many held notes (only with very long gates): release one now.
            const auto& o = offs[0];
            out.push ({ MidiEvent::Type::NoteOff, o.channel, o.note, 0, offset });
            offs[0] = offs[static_cast<std::size_t> (numOffs - 1)];
            --numOffs;
        }

        out.push ({ MidiEvent::Type::NoteOn, ch, note, static_cast<std::uint8_t> (hit.velocity), offset });
        offs[static_cast<std::size_t> (numOffs++)] = { emitSample + gateSamples, note, ch };
        hit.emitted = true;
    }

    pushHit (hit);
}

} // namespace m2m
