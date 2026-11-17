#pragma once

#include "m2m/Engine.h"
#include "m2m/audio/Preprocessor.h"
#include "m2m/beatbox/CalibratedClassifier.h"
#include "m2m/beatbox/DrumClass.h"
#include "m2m/beatbox/Features.h"
#include "m2m/beatbox/RuleClassifier.h"
#include "m2m/midi/DrumMapping.h"
#include "m2m/midi/VelocityMapper.h"
#include "m2m/onset/OnsetDetector.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace m2m
{

enum class ClassifierMode : int { Rules, Calibrated };

struct BeatboxParams
{
    OnsetParams onset;                 // sensitivity 0..1, refractoryMs (40), minLevelDb (-45)
    float analysisWindowMs = 20.0f;    // 10..40: audio after the onset used for classification
    ClassifierMode classifierMode = ClassifierMode::Rules; // Calibrated falls back to Rules until usable
    ClassifierParams classifier;       // per-class sensitivity / enabled, minScore
    float gateMs = 60.0f;              // NoteOn -> NoteOff (1..2000)
    float velocityWindowMs = 10.0f;    // peak level window after the onset (<= analysis window)
    VelocityParams velocity;
    DrumMappingParams mapping;         // notes, per-class enable, channel (default 10)
    float highPassHz = 30.0f;          // preprocessing; low so kick lows survive
};

/** One detected and classified onset: every onset produces one HitInfo,
    whether or not MIDI was sent (see accepted / emitted / calibration). */
struct HitInfo
{
    std::int64_t onsetSample = 0;   // estimated true onset, absolute samples since reset()
    std::int64_t emitSample = 0;    // absolute sample at which the NoteOn was placed
    DrumClass cls = DrumClass::Kick;
    float score = 0.0f;
    int velocity = 0;
    bool accepted = false;          // classifier accepted the hit
    bool emitted = false;           // a NoteOn was sent (accepted, class mapped & enabled, not calibrating)
    bool calibration = false;       // captured as a calibration example (no MIDI)
    bool calibrated = false;        // the calibrated classifier was used
    FeatureVector features;
};

/** Beatbox Mode (PLAN.md section 6): vocal percussion -> MIDI drum notes.

    Pipeline: Preprocessor (30 Hz HP) -> OnsetDetector -> post-onset window
    from an internal ring buffer -> FeatureExtractor -> Rule or Calibrated
    classifier -> DrumMapping -> NoteOn (velocity = peak in the first
    velocityWindowMs) and a NoteOff gateMs later.

    Timing: analysis runs on an internal fixed hop (64 samples @ 48 kHz), so
    the output is identical for any host block size. The NoteOn is placed on
    the last sample of the first hop at which the analysis window is complete,
    i.e. analysisWindowMs plus at most one hop after the estimated onset.
    HitInfo::onsetSample gives the true onset estimate for offline timing.

    Threading contract
      Audio thread: process(), flush(), reset(), setParams(), getRecentHits()
        (single consumer), telemetry getters. No allocation/locks/I/O.
      Message thread (ONE thread only; the calls are a single-producer
      queue): beginCalibration(), endCalibration(), cancelCalibration(),
      clearCalibration(), clearAllCalibration(), getCalibrationString(),
      setCalibrationString(). The first five only enqueue a command; the
      audio thread applies it at the start of its next process() call.
      setCalibrationString() parses (allocates) into a prepared copy that the
      audio thread swaps in at the start of its next process() (lock-free
      mailbox). getCalibrationString() reads a snapshot the audio thread
      publishes after every calibration change (lock-free; may briefly spin
      if the audio thread is mid-publish). State changes therefore become
      visible after the next process() call; if audio is not running,
      prepare() also applies pending commands.
      calibrationExampleCount(), isCalibrating(), isCalibrationUsable() and
      the telemetry getters are atomics, safe from any thread.
      prepare() must not run concurrently with process(). */
class BeatboxEngine final : public IEngine
{
public:
    BeatboxEngine();

    // IEngine
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept override;
    void flush (MidiEventBuffer& out, int sampleOffset = 0) noexcept override;

    /** Upper bound of onset -> NoteOn delay: analysis window (or the onset
        detection delay, if longer) plus one hop of quantisation. */
    int latencySamples() const noexcept override;

    /** Audio thread (or while not processing). Real-time safe. */
    void setParams (const BeatboxParams& p) noexcept;
    const BeatboxParams& getParams() const noexcept { return params; }

    // --- calibration (message thread) ----------------------------------
    /** Start capturing examples for a class. Replaces that class's existing
        examples (restored by cancelCalibration()). While calibrating, every
        detected onset becomes an example and no MIDI is emitted. */
    void beginCalibration (DrumClass c) noexcept;
    /** Keep the captured examples and stop capturing. */
    void endCalibration() noexcept;
    /** Discard this take's examples (restores the class's previous ones). */
    void cancelCalibration() noexcept;
    void clearCalibration (DrumClass c) noexcept;
    void clearAllCalibration() noexcept;

    bool isCalibrating() const noexcept { return calibratingAtomic.load (std::memory_order_acquire); }
    DrumClass calibrationClass() const noexcept { return static_cast<DrumClass> (calibClassAtomic.load (std::memory_order_acquire)); }
    int calibrationExampleCount (DrumClass c) const noexcept;
    bool isCalibrationUsable() const noexcept { return usableAtomic.load (std::memory_order_acquire); }

    /** Message thread; allocates. */
    std::string getCalibrationString() const;
    /** Message thread; allocates. Returns false (and changes nothing) if the
        text does not parse. Applied at the start of the next process(). */
    bool setCalibrationString (const std::string& text);

    // --- telemetry -------------------------------------------------------
    /** Peak input level of the last block, dBFS (-120 for silence). Any thread. */
    float inputLevelDb() const noexcept { return inputLevelAtomic.load (std::memory_order_relaxed); }
    /** Last classification (calibration captures included). Any thread. */
    Classification lastClassification() const noexcept;
    /** True if the calibrated classifier is currently in use. */
    bool usingCalibratedClassifier() const noexcept { return usingCalibratedAtomic.load (std::memory_order_relaxed); }

    /** Pops up to max hits (oldest first) from a 64-entry FIFO. Single
        consumer (UI thread or offline harness). If nobody reads, new hits
        are dropped once the FIFO is full. */
    int getRecentHits (HitInfo* out, int max) noexcept;

    /** Internal hop size in samples. */
    int hopSize() const noexcept { return hop; }

private:
    enum class CmdType : int { Begin, End, Cancel, Clear, ClearAll };
    struct Command { CmdType type; int cls; };

    struct PendingOnset { std::int64_t onset; float strength; };
    struct PendingOff { std::int64_t time; std::uint8_t note, channel; };

    // states of the two lock-free mailboxes
    enum : int { slotFree = 0, slotWriting = 1, slotReady = 2, slotReading = 3 };

    void updateDerived() noexcept;
    void applyMessages() noexcept;
    void applyCommand (const Command& c) noexcept;
    void publishCalibration() noexcept;
    void publishCounts() noexcept;
    bool pushCommand (CmdType t, int cls) noexcept;

    void handleHop (std::int64_t hopEnd, std::int64_t blockStart, MidiEventBuffer& out) noexcept;
    void classifyOnset (const PendingOnset& p, std::int64_t emitSample, std::int64_t blockStart,
                        MidiEventBuffer& out) noexcept;
    void emitNoteOffsUpTo (std::int64_t t, std::int64_t blockStart, MidiEventBuffer& out) noexcept;
    void pushHit (const HitInfo& h) noexcept;

    BeatboxParams params;
    double sr = 48000.0;
    int maxBlock = 512;
    int hop = 64;
    int windowN = 960, velocityN = 480;
    std::int64_t gateSamples = 2880;

    Preprocessor pre;
    OnsetDetector onsets;
    FeatureExtractor features;
    RuleClassifier rules;
    CalibratedClassifier calibrated;  // audio-thread owned (live)
    VelocityMapper velocityMapper;
    DrumMapping mapping;

    std::vector<float> scratch, ring, windowBuf, preBuf;
    int preN = 240;
    std::int64_t ringMask = 0;
    std::int64_t pos = 0;

    std::array<PendingOnset, 16> pending {};
    int numPending = 0;
    std::array<PendingOff, 32> offs {};
    int numOffs = 0;

    // calibration state (audio thread)
    bool calibActive = false;
    DrumClass calibCls = DrumClass::Kick;
    std::array<FeatureVector, CalibratedClassifier::maxExamplesPerClass> backup {};
    int backupCount = 0;
    bool snapshotDirty = false;

    // message -> audio command queue (SPSC)
    std::array<Command, 64> commands {};
    std::atomic<std::uint32_t> cmdHead { 0 }, cmdTail { 0 };

    // message -> audio calibration mailbox
    CalibratedClassifier incoming;
    std::atomic<int> incomingState { slotFree };

    // audio -> message calibration snapshot
    CalibratedClassifier snapshot;
    mutable std::atomic<int> snapshotState { slotFree };

    std::array<std::atomic<int>, numDrumClasses> countAtomics;
    std::atomic<bool> calibratingAtomic { false }, usableAtomic { false }, usingCalibratedAtomic { false };
    std::atomic<int> calibClassAtomic { 0 };
    std::atomic<float> inputLevelAtomic { -120.0f };
    std::atomic<std::uint64_t> lastClassAtomic { 0 };

    // audio -> UI hit FIFO (SPSC)
    std::array<HitInfo, 64> hits {};
    std::atomic<std::uint32_t> hitHead { 0 }, hitTail { 0 };
};

} // namespace m2m
