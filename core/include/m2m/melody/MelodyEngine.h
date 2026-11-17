#pragma once

#include "m2m/Engine.h"
#include "m2m/audio/EnvelopeFollower.h"
#include "m2m/audio/Preprocessor.h"
#include "m2m/melody/NoteTracker.h"
#include "m2m/melody/RetriggerDetector.h"
#include "m2m/midi/VelocityMapper.h"
#include "m2m/pitch/MpmDetector.h"
#include "m2m/pitch/PitchSmoother.h"
#include "m2m/pitch/ScaleSnap.h"
#include "m2m/pitch/YinDetector.h"

#include <cstdint>
#include <vector>

namespace m2m
{

enum class PitchAlgorithm : std::uint8_t { Yin, Mpm };

/** All Melody Mode parameters (PLAN.md section 4). Defaults suit a singing voice. */
struct MelodyParams
{
    // Pitch detection
    PitchAlgorithm detector     = PitchAlgorithm::Yin;
    float minHz                 = 80.0f;   // >= 50; sets the analysis window (latency!)
    float maxHz                 = 1000.0f;
    float confidenceThreshold   = 0.6f;    // 0..1
    int   medianFrames          = 3;       // pitch smoothing: median length (1..15 frames)
    float smoothingMs           = 0.0f;    // pitch smoothing: extra one-pole time constant, 0 = off
    float highPassHz            = 60.0f;   // input high-pass (0 = DC blocker only)

    // Note tracking
    float onThresholdDb         = -45.0f;  // envelope level to start a note (dBFS)
    float offThresholdDb        = -55.0f;  // envelope level that releases a note (dBFS)
    float minNoteMs             = 40.0f;
    float changeHysteresisCents = 60.0f;   // vibrato/drift band around the note centre
    float minStableMs           = 10.0f;   // pitch must hold this long before note-on / change
    float releaseMs             = 30.0f;   // level/confidence must be gone this long for note-off
    bool  retriggerEnabled      = true;
    float retriggerSensitivity  = 0.5f;    // 0..1 (required envelope rise 20..4 dB; 0.5 -> 12 dB)

    // Output
    Scale scale                 = Scale::Off;
    int   scaleRoot             = 0;       // 0..11, 0 = C
    int   transpose             = 0;       // semitones
    int   channel               = 1;       // 1..16

    // Velocity
    float attackWindowMs        = 10.0f;   // peak level window after the onset (delays note-on)
    VelocityParams velocity     {};
    bool  useFixedVelocity      = false;   // true -> velocity.fixedVelocity always
};

/** Melody Mode: monophonic voice -> MIDI notes. Deterministic DSP only.

    Pipeline (per sample): Preprocessor (DC + high-pass) -> ring buffer and
    envelope follower. Every `hopSize()` samples (128 at 48 kHz, scaled with
    the sample rate, counted from reset) one analysis frame runs:
    YIN/MPM -> confidence gate -> PitchSmoother (median, cents domain) ->
    NoteTracker (uses ScaleSnap) ; level path: envelope dB + RetriggerDetector.
    Velocity comes from VelocityMapper on the attack-window peak.
    Transpose and channel are applied at output.

    Events are stamped at the sample that completed their analysis hop, so
    output is identical for any host block size. process()/setParams() never
    allocate, lock or do I/O. */
class MelodyEngine final : public IEngine
{
public:
    MelodyEngine();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept override;
    void flush (MidiEventBuffer& out, int sampleOffset = 0) noexcept override;

    /** Typical note-on delay for the current params:
        window/2 + max (minStable + one median frame, attack window) + one hop. */
    int latencySamples() const noexcept override;

    /** Real-time safe. A note held across a transpose/channel change is
        released with the values it was started with. */
    void setParams (const MelodyParams& p) noexcept;
    const MelodyParams& getParams() const noexcept { return params; }

    int hopSize() const noexcept { return hop; }
    int windowSize() const noexcept;
    double sampleRate() const noexcept { return sr; }

    // Telemetry (valid after process(); read on the audio thread or copy out).
    float currentFrequencyHz() const noexcept { return telemetryHz; }       // 0 if unvoiced
    float currentPitchMidi() const noexcept   { return telemetryMidi; }     // smoothed, 0 if unvoiced
    float currentConfidence() const noexcept  { return telemetryConfidence; }
    float inputLevelDb() const noexcept       { return telemetryLevelDb; }
    int   activeNote() const noexcept         { return sentNote; }         // output note, -1 if none
    int   lastVelocity() const noexcept       { return lastVel; }

private:
    void applyParams() noexcept;
    void analyseFrame (int offsetInBlock, MidiEventBuffer& out) noexcept;
    void emit (const TrackerEvent& e, int offset, MidiEventBuffer& out) noexcept;
    IPitchDetector& detector() noexcept;

    MelodyParams params;
    double sr = 48000.0;
    int hop = 128;
    bool prepared = false;

    Preprocessor pre;
    EnvelopeFollower follower;
    YinDetector yin;
    MpmDetector mpm;
    PitchSmoother smoother;
    NoteTracker tracker;
    RetriggerDetector onsets;
    VelocityMapper velocityMapper;

    std::vector<float> scratch;   // preprocessed block chunk
    std::vector<float> ring;      // power-of-two history
    std::vector<float> frame;     // linearised analysis window
    int ringMask = 0;
    int ringWrite = 0;

    std::int64_t samplesProcessed = 0;
    int hopCounter = 0;
    float hopPeak = 0.0f;

    int sentNote = -1;
    int sentChannel = 1;
    int lastVel = 0;

    float telemetryHz = 0.0f, telemetryMidi = 0.0f, telemetryConfidence = 0.0f;
    float telemetryLevelDb = -120.0f;
};

} // namespace m2m
