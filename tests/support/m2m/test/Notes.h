#pragma once

#include "m2m/midi/MidiEvent.h"

#include <cstdint>
#include <vector>

namespace m2m::test
{

/** One ground-truth note or drum hit. Drum hits use the GM drum note
    (kick 36, snare 38, clap 39, closed hat 42, open hat 46) and have
    offsetSec == onsetSec. */
struct TruthNote
{
    double onsetSec  = 0.0;
    double offsetSec = 0.0;
    int    midiNote  = 0;

    bool operator== (const TruthNote&) const = default;
};

/** A note reconstructed from an engine's NoteOn/NoteOff stream. */
struct DetectedNote
{
    double onsetSec  = 0.0;
    double offsetSec = 0.0;
    int    midiNote  = 0;
    int    channel   = 1;
    int    velocity  = 0;

    bool operator== (const DetectedNote&) const = default;
};

/** A MIDI event at an absolute sample position in the analysed buffer. */
struct TimedEvent
{
    std::int64_t samplePos = 0;
    MidiEvent    event;

    bool operator== (const TimedEvent&) const = default;
};

/** Pair NoteOn/NoteOff events into notes (result sorted by onset).
    - A NoteOff closes the oldest open note with the same channel and pitch.
    - A NoteOn for a pitch that is already open closes the open one first.
    - NoteOn with velocity 0 counts as NoteOff; unmatched NoteOffs are ignored.
    - Notes still open at the end get offset endSample (if >= 0), else their onset. */
std::vector<DetectedNote> pairNotes (const std::vector<TimedEvent>& events, double sampleRate,
                                     std::int64_t endSample = -1);

} // namespace m2m::test
