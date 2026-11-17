#include "m2m/melody/MelodyEngine.h"

#include "m2m/audio/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace m2m
{

namespace
{
    constexpr float kEnvAttackMs = 1.0f;
    constexpr float kEnvReleaseMs = 20.0f;
    constexpr float kOnsetLookbackMs = 40.0f;
    constexpr float kOnsetRefractoryMs = 60.0f;
    constexpr float kSilentDb = -120.0f;

    std::size_t idx (int i) noexcept { return static_cast<std::size_t> (i); }
}

MelodyEngine::MelodyEngine()
{
    prepare (48000.0, 512);
}

void MelodyEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    hop = std::max (16, static_cast<int> (std::lround (128.0 * sr / 48000.0)));

    pre.prepare (sr);
    follower.prepare (sr);
    follower.setTimes (kEnvAttackMs, kEnvReleaseMs);
    yin.prepare (sr, kLowestPitchHz);
    mpm.prepare (sr, kLowestPitchHz);
    tracker.prepare (sr);
    onsets.prepare (hop / sr);

    const int maxWindow = pitchWindowSize (sr, kLowestPitchHz);
    const int ringSize = nextPowerOfTwo (maxWindow + hop);
    ring.assign (idx (ringSize), 0.0f);
    ringMask = ringSize - 1;
    frame.assign (idx (maxWindow), 0.0f);
    scratch.assign (idx (std::max (64, maxBlockSize)), 0.0f);

    prepared = true;
    applyParams();
    reset();
}

void MelodyEngine::reset() noexcept
{
    pre.reset();
    follower.reset();
    smoother.reset();
    tracker.reset();
    onsets.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringWrite = 0;
    samplesProcessed = 0;
    hopCounter = 0;
    hopPeak = 0.0f;
    sentNote = -1;
    lastVel = 0;
    telemetryHz = telemetryMidi = telemetryConfidence = 0.0f;
    telemetryLevelDb = kSilentDb;
}

void MelodyEngine::setParams (const MelodyParams& p) noexcept
{
    params = p;
    applyParams();
}

void MelodyEngine::applyParams() noexcept
{
    if (! prepared)
        return;

    pre.setHighPassHz (params.highPassHz);
    yin.setRange (params.minHz, params.maxHz);
    mpm.setRange (params.minHz, params.maxHz);

    const int oldMedian = smoother.getMedianLength();
    smoother.setMedianLength (params.medianFrames);
    if (oldMedian != smoother.getMedianLength())
        smoother.reset();
    smoother.setOnePoleCoefficient (PitchSmoother::coefficientFor (params.smoothingMs, sr / hop));

    NoteTrackerParams tp;
    tp.confidenceThreshold   = params.confidenceThreshold;
    tp.onThresholdDb         = params.onThresholdDb;
    tp.offThresholdDb        = std::min (params.offThresholdDb, params.onThresholdDb);
    tp.minNoteMs             = params.minNoteMs;
    tp.changeHysteresisCents = params.changeHysteresisCents;
    tp.minStableMs           = params.minStableMs;
    tp.releaseMs             = params.releaseMs;
    tp.attackWindowMs        = params.attackWindowMs;
    tp.retriggerEnabled      = params.retriggerEnabled;
    tracker.setParams (tp);
    tracker.setScale (params.scale, params.scaleRoot);

    onsets.setParams (RetriggerDetector::riseDbForSensitivity (params.retriggerSensitivity),
                      kOnsetLookbackMs, kOnsetRefractoryMs, params.onThresholdDb);

    auto v = params.velocity;
    if (params.useFixedVelocity)
        v.sensitivity = 0.0f;
    velocityMapper.setParams (v);
}

IPitchDetector& MelodyEngine::detector() noexcept
{
    if (params.detector == PitchAlgorithm::Mpm)
        return mpm;
    return yin;
}

int MelodyEngine::windowSize() const noexcept
{
    return params.detector == PitchAlgorithm::Mpm ? mpm.windowSize() : yin.windowSize();
}

int MelodyEngine::latencySamples() const noexcept
{
    const auto ms = [this] (float v) { return v > 0.0f ? static_cast<double> (v) * 0.001 * sr : 0.0; };
    const double stable = ms (params.minStableMs) + (params.medianFrames > 1 ? hop : 0);
    const double wait = std::max (stable, ms (params.attackWindowMs));
    const double detection = windowSize() * (params.detector == PitchAlgorithm::Mpm ? 0.25 : 0.5);
    return static_cast<int> (std::lround (detection + wait + hop));
}

void MelodyEngine::process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept
{
    const int chunkMax = static_cast<int> (scratch.size());
    int done = 0;
    while (done < numSamples)
    {
        const int len = std::min (chunkMax, numSamples - done);
        pre.process (mono + done, scratch.data(), len);

        for (int i = 0; i < len; ++i)
        {
            const float s = scratch[idx (i)];
            ring[idx (ringWrite)] = s;
            ringWrite = (ringWrite + 1) & ringMask;

            follower.process (s);
            hopPeak = std::max (hopPeak, std::abs (s));
            ++samplesProcessed;

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyseFrame (done + i, out);
            }
        }
        done += len;
    }
}

void MelodyEngine::analyseFrame (int offsetInBlock, MidiEventBuffer& out) noexcept
{
    auto& det = detector();
    const int n = det.windowSize();

    // Linearise the newest n samples.
    const int start = (ringWrite - n) & ringMask;
    const int firstPart = std::min (n, static_cast<int> (ring.size()) - start);
    std::copy (ring.begin() + start, ring.begin() + start + firstPart, frame.begin());
    std::copy (ring.begin(), ring.begin() + (n - firstPart), frame.begin() + firstPart);

    const PitchEstimate est = det.detect (frame.data(), n);
    const bool usable = est.isVoiced() && est.confidence >= params.confidenceThreshold;

    TrackerFrame f;
    f.time = samplesProcessed - 1;
    f.voiced = est.isVoiced();
    f.confidence = est.confidence;
    f.levelDb = gainToDb (follower.current(), kSilentDb);
    f.peakDb = gainToDb (hopPeak, kSilentDb);
    f.onset = onsets.process (f.levelDb);
    hopPeak = 0.0f;

    if (usable)
    {
        f.pitch = smoother.process (static_cast<float> (hzToMidi (est.hz)));
    }
    else
    {
        smoother.reset();
        f.pitch = 0.0f;
    }

    telemetryHz = usable ? est.hz : 0.0f;
    telemetryMidi = usable ? f.pitch : 0.0f;
    telemetryConfidence = est.confidence;
    telemetryLevelDb = f.levelDb;

    TrackerEvent events[NoteTracker::maxEventsPerFrame];
    const int count = tracker.process (f, events);
    for (int i = 0; i < count; ++i)
        emit (events[i], offsetInBlock, out);
}

void MelodyEngine::emit (const TrackerEvent& e, int offset, MidiEventBuffer& out) noexcept
{
    if (e.noteOn)
    {
        if (sentNote >= 0) // never two notes at once (defensive; tracker guarantees it)
            out.push ({ MidiEvent::Type::NoteOff, static_cast<std::uint8_t> (sentChannel),
                        static_cast<std::uint8_t> (sentNote), 0, offset });

        sentNote = std::clamp (e.note + params.transpose, 0, 127);
        sentChannel = std::clamp (params.channel, 1, 16);
        lastVel = velocityMapper.map (e.peakDb);
        out.push ({ MidiEvent::Type::NoteOn, static_cast<std::uint8_t> (sentChannel),
                    static_cast<std::uint8_t> (sentNote), static_cast<std::uint8_t> (lastVel), offset });
    }
    else if (sentNote >= 0)
    {
        out.push ({ MidiEvent::Type::NoteOff, static_cast<std::uint8_t> (sentChannel),
                    static_cast<std::uint8_t> (sentNote), 0, offset });
        sentNote = -1;
    }
}

void MelodyEngine::flush (MidiEventBuffer& out, int sampleOffset) noexcept
{
    TrackerEvent events[NoteTracker::maxEventsPerFrame];
    const int count = tracker.flush (samplesProcessed, events);
    for (int i = 0; i < count; ++i)
        emit (events[i], sampleOffset, out);

    if (sentNote >= 0)
    {
        TrackerEvent off;
        emit (off, sampleOffset, out);
    }
    smoother.reset();
}

} // namespace m2m
