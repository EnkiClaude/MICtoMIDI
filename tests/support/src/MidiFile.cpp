#include "m2m/test/MidiFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace m2m::test
{

namespace
{
    void putBE (std::vector<unsigned char>& b, std::uint32_t v, int bytes)
    {
        for (int i = bytes - 1; i >= 0; --i)
            b.push_back (static_cast<unsigned char> ((v >> (8 * i)) & 0xff));
    }

    void putVarLen (std::vector<unsigned char>& b, std::uint32_t v)
    {
        unsigned char tmp[5];
        int n = 0;
        tmp[n++] = static_cast<unsigned char> (v & 0x7f);
        while ((v >>= 7) != 0)
            tmp[n++] = static_cast<unsigned char> ((v & 0x7f) | 0x80);
        while (n > 0)
            b.push_back (tmp[--n]);
    }

    std::uint32_t usPerQuarter (double bpm)
    {
        return static_cast<std::uint32_t> (std::lround (60'000'000.0 / (bpm > 0.0 ? bpm : 120.0)));
    }
}

std::vector<unsigned char> encodeMidiFile (const std::vector<TimedEvent>& events, double sampleRate,
                                           const MidiFileOptions& options)
{
    const int ppq = std::clamp (options.ppq, 1, 0x7fff);
    const std::uint32_t tempo = usPerQuarter (options.bpm);
    const double ticksPerSec = static_cast<double> (ppq) * 1e6 / static_cast<double> (tempo);

    std::vector<TimedEvent> sorted (events);
    std::stable_sort (sorted.begin(), sorted.end(),
                      [] (const TimedEvent& a, const TimedEvent& b) { return a.samplePos < b.samplePos; });

    std::vector<unsigned char> track;
    putVarLen (track, 0);
    track.insert (track.end(), { 0xFF, 0x51, 0x03 });
    putBE (track, tempo, 3);

    std::int64_t lastTick = 0;
    for (const auto& te : sorted)
    {
        const double sec = static_cast<double> (std::max<std::int64_t> (0, te.samplePos)) / sampleRate;
        const auto tick = std::max (lastTick, static_cast<std::int64_t> (std::llround (sec * ticksPerSec)));
        putVarLen (track, static_cast<std::uint32_t> (tick - lastTick));
        lastTick = tick;

        const auto ch = static_cast<unsigned char> ((std::clamp<int> (te.event.channel, 1, 16) - 1) & 0x0f);
        const bool on = te.event.type == MidiEvent::Type::NoteOn;
        track.push_back (static_cast<unsigned char> ((on ? 0x90 : 0x80) | ch));
        track.push_back (static_cast<unsigned char> (te.event.note & 0x7f));
        track.push_back (static_cast<unsigned char> (on ? std::clamp<int> (te.event.velocity, 1, 127) : 0));
    }

    putVarLen (track, 0);
    track.insert (track.end(), { 0xFF, 0x2F, 0x00 });

    std::vector<unsigned char> file;
    file.insert (file.end(), { 'M', 'T', 'h', 'd' });
    putBE (file, 6, 4);
    putBE (file, 0, 2);   // format 0
    putBE (file, 1, 2);   // one track
    putBE (file, static_cast<std::uint32_t> (ppq), 2);
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    putBE (file, static_cast<std::uint32_t> (track.size()), 4);
    file.insert (file.end(), track.begin(), track.end());
    return file;
}

bool writeMidiFile (const std::string& path, const std::vector<TimedEvent>& events, double sampleRate,
                    std::string& error, const MidiFileOptions& options)
{
    if (sampleRate <= 0.0)
    {
        error = "invalid sample rate";
        return false;
    }
    const auto bytes = encodeMidiFile (events, sampleRate, options);
    std::ofstream o (path, std::ios::binary | std::ios::trunc);
    if (! o)
    {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    o.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    if (! o)
    {
        error = "error writing '" + path + "'";
        return false;
    }
    return true;
}

bool writeMidiFile (const std::string& path, const std::vector<DetectedNote>& notes,
                    std::string& error, const MidiFileOptions& options)
{
    // Microsecond "samples" keep full precision for the tick conversion.
    constexpr double rate = 1e6;
    std::vector<TimedEvent> ev;
    for (const auto& n : notes)
    {
        const auto ch = static_cast<std::uint8_t> (std::clamp (n.channel, 1, 16));
        const auto note = static_cast<std::uint8_t> (std::clamp (n.midiNote, 0, 127));
        ev.push_back ({ std::llround (n.onsetSec * rate),
                        { MidiEvent::Type::NoteOn, ch, note, static_cast<std::uint8_t> (std::clamp (n.velocity, 1, 127)), 0 } });
        ev.push_back ({ std::llround (n.offsetSec * rate), { MidiEvent::Type::NoteOff, ch, note, 0, 0 } });
    }
    // NoteOffs before NoteOns at the same time, so back-to-back notes don't overlap.
    std::stable_sort (ev.begin(), ev.end(), [] (const TimedEvent& a, const TimedEvent& b)
    {
        if (a.samplePos != b.samplePos)
            return a.samplePos < b.samplePos;
        return a.event.type == MidiEvent::Type::NoteOff && b.event.type == MidiEvent::Type::NoteOn;
    });
    return writeMidiFile (path, ev, rate, error, options);
}

//==============================================================================
bool parseMidiFile (const std::vector<unsigned char>& bytes, MidiFileContents& out, std::string& error)
{
    out = {};
    const std::size_t size = bytes.size();
    const unsigned char* d = bytes.data();

    const auto be = [d] (std::size_t pos, int n)
    {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v = (v << 8) | d[pos + static_cast<std::size_t> (i)];
        return v;
    };

    if (size < 14 || std::memcmp (d, "MThd", 4) != 0)
    {
        error = "not a Standard MIDI File";
        return false;
    }
    const std::size_t hdrLen = be (4, 4);
    if (hdrLen < 6 || 8 + hdrLen > size)
    {
        error = "bad MThd header";
        return false;
    }
    out.format = static_cast<int> (be (8, 2));
    const auto numTracks = be (10, 2);
    const auto division = be (12, 2);
    if (division & 0x8000)
    {
        error = "SMPTE time division not supported";
        return false;
    }
    out.ppq = static_cast<int> (division);
    if (out.ppq <= 0)
    {
        error = "invalid ticks per quarter";
        return false;
    }

    struct Raw { std::int64_t tick; int order; MidiEvent ev; };
    struct Tempo { std::int64_t tick; std::uint32_t us; };
    std::vector<Raw> raw;
    std::vector<Tempo> tempos;
    int order = 0;

    std::size_t pos = 8 + hdrLen;
    for (std::uint32_t t = 0; t < numTracks && pos + 8 <= size; ++t)
    {
        if (std::memcmp (d + pos, "MTrk", 4) != 0)
        {
            // Unknown chunk: skip it.
            pos += 8 + be (pos + 4, 4);
            --t;
            continue;
        }
        const std::size_t len = be (pos + 4, 4);
        std::size_t p = pos + 8;
        const std::size_t end = std::min (size, p + len);
        pos = p + len;

        std::int64_t tick = 0;
        unsigned char status = 0;
        const auto varLen = [&] (std::uint32_t& v)
        {
            v = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (p >= end)
                    return false;
                const unsigned char c = d[p++];
                v = (v << 7) | (c & 0x7fu);
                if ((c & 0x80) == 0)
                    return true;
            }
            return false;
        };

        while (p < end)
        {
            std::uint32_t delta = 0;
            if (! varLen (delta))
            {
                error = "truncated delta time";
                return false;
            }
            tick += delta;
            if (p >= end)
                break;

            unsigned char c = d[p];
            if (c & 0x80)
            {
                ++p;
                if (c < 0xF0)
                    status = c;
            }
            else if (status == 0)
            {
                error = "running status without a status byte";
                return false;
            }
            else
            {
                c = status;
            }

            if (c == 0xFF)
            {
                if (p >= end) break;
                const unsigned char type = d[p++];
                std::uint32_t mlen = 0;
                if (! varLen (mlen) || p + mlen > end)
                {
                    error = "truncated meta event";
                    return false;
                }
                if (type == 0x51 && mlen == 3)
                    tempos.push_back ({ tick, be (p, 3) });
                p += mlen;
                if (type == 0x2F)
                    break;
                continue;
            }
            if (c == 0xF0 || c == 0xF7)
            {
                std::uint32_t slen = 0;
                if (! varLen (slen) || p + slen > end)
                {
                    error = "truncated sysex";
                    return false;
                }
                p += slen;
                continue;
            }

            const unsigned char hi = c & 0xF0;
            const int dataBytes = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (p + static_cast<std::size_t> (dataBytes) > end)
            {
                error = "truncated channel event";
                return false;
            }
            const unsigned char d1 = d[p];
            const unsigned char d2 = dataBytes == 2 ? d[p + 1] : 0;
            p += static_cast<std::size_t> (dataBytes);

            if (hi == 0x80 || hi == 0x90)
            {
                MidiEvent ev;
                const bool on = hi == 0x90 && d2 > 0;
                ev.type = on ? MidiEvent::Type::NoteOn : MidiEvent::Type::NoteOff;
                ev.channel = static_cast<std::uint8_t> ((c & 0x0f) + 1);
                ev.note = d1;
                ev.velocity = on ? d2 : 0;
                raw.push_back ({ tick, order++, ev });
            }
        }
    }

    std::stable_sort (tempos.begin(), tempos.end(), [] (const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
    std::stable_sort (raw.begin(), raw.end(), [] (const Raw& a, const Raw& b)
    {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    out.initialBpm = (! tempos.empty() && tempos.front().tick == 0) ? 60e6 / tempos.front().us : 120.0;

    // Tick -> seconds through the tempo map.
    const double ppq = out.ppq;
    const auto toSec = [&] (std::int64_t tick)
    {
        double sec = 0.0;
        std::int64_t lastTick = 0;
        double us = 500000.0;
        for (const auto& tp : tempos)
        {
            if (tp.tick >= tick)
                break;
            sec += static_cast<double> (tp.tick - lastTick) * us / (ppq * 1e6);
            lastTick = tp.tick;
            us = tp.us;
        }
        return sec + static_cast<double> (tick - lastTick) * us / (ppq * 1e6);
    };

    for (const auto& r : raw)
        out.events.push_back ({ r.tick, toSec (r.tick), r.ev });
    return true;
}

bool readMidiFile (const std::string& path, MidiFileContents& out, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "cannot open '" + path + "' for reading";
        return false;
    }
    std::vector<unsigned char> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (! parseMidiFile (bytes, out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

std::vector<DetectedNote> notesFromMidiFile (const MidiFileContents& contents)
{
    constexpr double rate = 1e9; // nanosecond positions: no precision loss
    std::vector<TimedEvent> ev;
    std::int64_t last = 0;
    for (const auto& e : contents.events)
    {
        last = std::llround (e.timeSec * rate);
        ev.push_back ({ last, e.event });
    }
    return pairNotes (ev, rate, last);
}

} // namespace m2m::test
