#include "m2m/midi/MidiEvent.h"

namespace m2m
{

std::string noteName (int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
    if (midiNote < 0 || midiNote > 127)
        return "?";

    const int octave = midiNote / 12 - 1;
    return std::string (names[midiNote % 12]) + std::to_string (octave);
}

} // namespace m2m
