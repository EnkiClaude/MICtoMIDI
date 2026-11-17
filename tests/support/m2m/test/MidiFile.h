#pragma once

#include "m2m/test/Notes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace m2m::test
{

struct MidiFileOptions
{
    double bpm = 120.0;   // fixed tempo written as a tempo meta event
    int ppq = 960;        // ticks per quarter note (960 @ 120 bpm ~ 0.52 ms/tick)
};

/** An event read back from a MIDI file. */
struct MidiFileEvent
{
    std::int64_t tick = 0;
    double timeSec = 0.0;
    MidiEvent event;       // sampleOffset unused (0)
};

struct MidiFileContents
{
    int format = 0;
    int ppq = 0;
    double initialBpm = 120.0;
    std::vector<MidiFileEvent> events;   // note on/off only, all tracks merged, time-ordered
};

/** Write a format-0 Standard MIDI File. Event times = samplePos / sampleRate.
    Simultaneous events keep their order. */
bool writeMidiFile (const std::string& path, const std::vector<TimedEvent>& events, double sampleRate,
                    std::string& error, const MidiFileOptions& options = {});

/** Same, from paired notes (NoteOn at onset, NoteOff at offset). */
bool writeMidiFile (const std::string& path, const std::vector<DetectedNote>& notes,
                    std::string& error, const MidiFileOptions& options = {});

/** In-memory encoder used by writeMidiFile. */
std::vector<unsigned char> encodeMidiFile (const std::vector<TimedEvent>& events, double sampleRate,
                                           const MidiFileOptions& options = {});

/** Read SMF format 0 or 1 (tracks merged). Handles running status, tempo
    changes (tempo map), NoteOn velocity 0 as NoteOff, and skips meta/sysex.
    SMPTE time division is not supported. */
bool readMidiFile (const std::string& path, MidiFileContents& out, std::string& error);
bool parseMidiFile (const std::vector<unsigned char>& bytes, MidiFileContents& out, std::string& error);

/** Convenience: read a file and pair its notes. */
std::vector<DetectedNote> notesFromMidiFile (const MidiFileContents& contents);

} // namespace m2m::test
