#pragma once

#include "m2m/audio/EnvelopeFollower.h"
#include "m2m/midi/MidiEvent.h"

#include <cstdint>

namespace m2m
{

/** Phase 1 test engine: input level above a threshold -> one fixed MIDI note.

    Exists to prove the audio-in -> MIDI-out route end to end (plugin and
    standalone) before real pitch detection. Uses hysteresis and a minimum note
    length so a level hovering around the threshold does not chatter.

    Output is sample-accurate and independent of the host block size. */
class LevelTrigger
{
public:
    struct Params
    {
        float onThresholdDb   = -30.0f;
        float hysteresisDb    = 6.0f;   // note-off happens at (on - hysteresis)
        float minNoteMs       = 40.0f;  // note stays on at least this long
        float attackMs        = 1.0f;
        float releaseMs       = 60.0f;
        int   note            = 60;     // 0..127
        int   channel         = 1;      // 1..16
        int   velocity        = 100;    // 1..127
    };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Cheap; safe to call every block from the audio thread. If the note or
        channel changes while a note is held, the old note is released at the
        start of the next process() call. */
    void setParams (const Params& p) noexcept;
    const Params& getParams() const noexcept { return params; }

    /** Analyse a mono block and append events (offsets relative to block start). */
    void process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept;

    /** Append a note-off for any held note (e.g. on bypass or mode change). */
    void flush (MidiEventBuffer& out, int sampleOffset = 0) noexcept;

    bool isNoteOn() const noexcept   { return active; }
    float currentLevel() const noexcept { return follower.current(); }

private:
    void updateDerived() noexcept;

    Params params;
    EnvelopeFollower follower;
    double sr = 48000.0;

    float onLevel = 0.0f, offLevel = 0.0f;
    std::int64_t minNoteSamples = 0;

    bool active = false;
    std::uint8_t activeNote = 60, activeChannel = 1;
    std::int64_t samplesSinceOn = 0;
    bool pendingRelease = false;
};

} // namespace m2m
