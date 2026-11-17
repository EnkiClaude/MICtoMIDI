#pragma once

// Synthetic test signals for the melody/pitch tests (deterministic, no I/O).

#include "m2m/Engine.h"
#include "m2m/audio/MathUtil.h"
#include "m2m/midi/MidiEvent.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace m2m::melodytest
{

/** Small deterministic PRNG (xorshift64*), identical on every platform. */
class Rng
{
public:
    explicit Rng (std::uint64_t seed) : state (seed ? seed : 0x9E3779B97F4A7C15ull) {}

    std::uint64_t next() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ull;
    }

    /** Uniform in [-1, 1). */
    double bipolar() noexcept { return static_cast<double> (next() >> 11) * (2.0 / 9007199254740992.0) - 1.0; }

    /** Approximately standard normal (sum of 12 uniforms). */
    double gauss() noexcept
    {
        double s = 0.0;
        for (int i = 0; i < 12; ++i)
            s += 0.5 * (bipolar() + 1.0);
        return s - 6.0;
    }

private:
    std::uint64_t state;
};

/** Phase-continuous additive synth. Amplitude changes are ramped over
    rampMs to avoid clicks; frequency changes are glitch-free. Levels are the
    peak amplitude of the summed waveform's fundamental-normalised harmonic set
    (harmonic amplitudes are normalised so their sum is 1, so levelDb is an
    upper bound on the peak). */
class Synth
{
public:
    explicit Synth (double sampleRate) : sr (sampleRate) { harmonics = { 1.0 }; }

    void setHarmonics (std::vector<double> amps)
    {
        double sum = 0.0;
        for (double a : amps)
            sum += std::abs (a);
        for (double& a : amps)
            a /= sum;
        harmonics = std::move (amps);
        phases.assign (harmonics.size(), 0.0);
    }

    void setRampMs (double ms) { rampMs = ms; }

    /** Constant tone (optionally with vibrato of +/- cents at rateHz). */
    void tone (double seconds, double hz, float levelDb, double vibCents = 0.0, double vibHz = 0.0)
    {
        segment (seconds, hz, hz, dbToGain (levelDb), vibCents, vibHz);
    }

    /** Exponential (log-frequency) glide. */
    void glide (double seconds, double hzFrom, double hzTo, float levelDb)
    {
        segment (seconds, hzFrom, hzTo, dbToGain (levelDb), 0.0, 0.0);
    }

    void silence (double seconds) { segment (seconds, lastHz, lastHz, 0.0, 0.0, 0.0); }

    /** Sample index where the next segment starts. */
    std::size_t position() const noexcept { return out.size(); }

    std::vector<float>& buffer() noexcept { return out; }

private:
    void segment (double seconds, double hz0, double hz1, double targetAmp, double vibCents, double vibHz)
    {
        if (phases.size() != harmonics.size())
            phases.assign (harmonics.size(), 0.0);

        const auto count = static_cast<std::size_t> (std::llround (seconds * sr));
        const double rampStep = 1.0 / std::max (1.0, rampMs * 0.001 * sr);
        const double startAmp = amp;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double frac = count > 1 ? static_cast<double> (i) / static_cast<double> (count) : 0.0;
            double hz = hz0 * std::pow (hz1 / hz0, frac);
            if (vibCents != 0.0)
                hz *= std::exp2 (vibCents / 1200.0 * std::sin (2.0 * std::numbers::pi * vibHz * t));

            const double rampPos = std::min (1.0, static_cast<double> (i + 1) * rampStep);
            amp = startAmp + (targetAmp - startAmp) * rampPos;

            double v = 0.0;
            for (std::size_t h = 0; h < harmonics.size(); ++h)
            {
                const double fh = hz * static_cast<double> (h + 1);
                phases[h] += 2.0 * std::numbers::pi * fh / sr;
                if (phases[h] > 2.0 * std::numbers::pi)
                    phases[h] -= 2.0 * std::numbers::pi;
                if (fh < 0.45 * sr)
                    v += harmonics[h] * std::sin (phases[h]);
            }
            out.push_back (static_cast<float> (amp * v));
        }
        lastHz = hz1;
    }

    double sr;
    double rampMs = 2.0;
    double amp = 0.0;
    double lastHz = 440.0;
    std::vector<double> harmonics;
    std::vector<double> phases;
    std::vector<float> out;
};

/** Adds white noise with the given RMS level (dBFS). */
inline void addNoise (std::vector<float>& buf, float rmsDb, std::uint64_t seed)
{
    Rng rng (seed);
    const double g = dbToGain (rmsDb);
    for (auto& s : buf)
        s += static_cast<float> (g * rng.gauss());
}

/** A MIDI event with an absolute sample position. */
struct AbsEvent
{
    MidiEvent::Type type;
    int channel, note, velocity;
    std::int64_t time;

    bool operator== (const AbsEvent&) const = default;
};

/** Runs an engine over a signal in blocks of blockSize and collects events
    with absolute sample positions; flushes at the end. */
inline std::vector<AbsEvent> runEngine (IEngine& engine, const std::vector<float>& signal,
                                        int blockSize, bool flushAtEnd = true)
{
    std::vector<AbsEvent> events;
    MidiEventBuffer buf;
    buf.reserve (256);

    std::int64_t pos = 0;
    const auto total = static_cast<std::int64_t> (signal.size());
    while (pos < total)
    {
        const int len = static_cast<int> (std::min<std::int64_t> (blockSize, total - pos));
        buf.clear();
        engine.process (signal.data() + pos, len, buf);
        for (const auto& e : buf)
            events.push_back ({ e.type, e.channel, e.note, e.velocity, pos + e.sampleOffset });
        pos += len;
    }

    if (flushAtEnd)
    {
        buf.clear();
        engine.flush (buf, 0);
        for (const auto& e : buf)
            events.push_back ({ e.type, e.channel, e.note, e.velocity, total });
    }
    return events;
}

inline std::vector<int> noteOns (const std::vector<AbsEvent>& ev)
{
    std::vector<int> notes;
    for (const auto& e : ev)
        if (e.type == MidiEvent::Type::NoteOn)
            notes.push_back (e.note);
    return notes;
}

/** Checks the monophonic invariants: non-decreasing time, at most one note
    sounding, every on matched by an off of the same note/channel. */
inline bool monophonicInvariantsHold (const std::vector<AbsEvent>& ev)
{
    int sounding = -1, channel = -1;
    std::int64_t last = -1;
    for (const auto& e : ev)
    {
        if (e.time < last)
            return false;
        last = e.time;

        if (e.type == MidiEvent::Type::NoteOn)
        {
            if (sounding >= 0 || e.velocity < 1)
                return false;
            sounding = e.note;
            channel = e.channel;
        }
        else
        {
            if (sounding != e.note || channel != e.channel)
                return false;
            sounding = -1;
        }
    }
    return sounding < 0;
}

inline int midiToHzInt (int note) { return static_cast<int> (midiToHz (note)); }

} // namespace m2m::melodytest
