#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace m2m
{

/** A MIDI event produced by the analysis engines.
    sampleOffset is relative to the start of the audio block that produced it. */
struct MidiEvent
{
    enum class Type : std::uint8_t { NoteOn, NoteOff };

    Type          type         = Type::NoteOn;
    std::uint8_t  channel      = 1;   // 1..16
    std::uint8_t  note         = 60;  // 0..127
    std::uint8_t  velocity     = 100; // 1..127 for NoteOn, 0 for NoteOff
    std::int32_t  sampleOffset = 0;

    bool operator== (const MidiEvent&) const = default;
};

/** Fixed-capacity event list. Storage is allocated in reserve() (call from
    prepare, never from the audio thread); push() never allocates. */
class MidiEventBuffer
{
public:
    void reserve (std::size_t capacity) { storage.resize (capacity); used = 0; }

    /** Returns false (and drops the event) if the buffer is full. */
    bool push (const MidiEvent& e) noexcept
    {
        if (used >= storage.size())
            return false;
        storage[used++] = e;
        return true;
    }

    void clear() noexcept { used = 0; }

    std::size_t size() const noexcept     { return used; }
    std::size_t capacity() const noexcept { return storage.size(); }
    bool empty() const noexcept           { return used == 0; }

    const MidiEvent& operator[] (std::size_t i) const noexcept { return storage[i]; }
    const MidiEvent* begin() const noexcept { return storage.data(); }
    const MidiEvent* end() const noexcept   { return storage.data() + used; }

private:
    std::vector<MidiEvent> storage;
    std::size_t used = 0;
};

/** "C4" style note name (MIDI 60 = C4). UI/diagnostics only: allocates. */
std::string noteName (int midiNote);

} // namespace m2m
