#include "m2m/trigger/LevelTrigger.h"

#include <algorithm>
#include <cmath>

namespace m2m
{

namespace
{
    float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    std::uint8_t clampByte (int v, int lo, int hi)
    {
        return static_cast<std::uint8_t> (std::clamp (v, lo, hi));
    }
}

void LevelTrigger::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    follower.prepare (sr);
    updateDerived();
    reset();
}

void LevelTrigger::reset() noexcept
{
    follower.reset();
    active = false;
    pendingRelease = false;
    samplesSinceOn = 0;
}

void LevelTrigger::setParams (const Params& p) noexcept
{
    const bool noteChanged = clampByte (p.note, 0, 127) != activeNote
                          || clampByte (p.channel, 1, 16) != activeChannel;
    if (active && noteChanged)
        pendingRelease = true;

    params = p;
    updateDerived();
}

void LevelTrigger::updateDerived() noexcept
{
    onLevel  = dbToGain (params.onThresholdDb);
    offLevel = dbToGain (params.onThresholdDb - std::max (0.0f, params.hysteresisDb));
    minNoteSamples = static_cast<std::int64_t> (std::max (0.0f, params.minNoteMs) * 0.001 * sr);
    follower.setTimes (params.attackMs, params.releaseMs);
}

void LevelTrigger::flush (MidiEventBuffer& out, int sampleOffset) noexcept
{
    if (! active)
        return;

    out.push ({ MidiEvent::Type::NoteOff, activeChannel, activeNote, 0, sampleOffset });
    active = false;
    pendingRelease = false;
}

void LevelTrigger::process (const float* mono, int numSamples, MidiEventBuffer& out) noexcept
{
    if (pendingRelease)
        flush (out, 0);

    for (int i = 0; i < numSamples; ++i)
    {
        const float env = follower.process (mono[i]);

        if (! active)
        {
            if (env >= onLevel)
            {
                activeNote    = clampByte (params.note, 0, 127);
                activeChannel = clampByte (params.channel, 1, 16);
                out.push ({ MidiEvent::Type::NoteOn, activeChannel, activeNote,
                            clampByte (params.velocity, 1, 127), i });
                active = true;
                samplesSinceOn = 0;
            }
        }
        else
        {
            ++samplesSinceOn;
            if (env < offLevel && samplesSinceOn >= minNoteSamples)
                flush (out, i);
        }
    }
}

} // namespace m2m
