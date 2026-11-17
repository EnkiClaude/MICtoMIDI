#pragma once

#include "m2m/audio/Preprocessor.h"
#include "m2m/beatbox/DrumClass.h"
#include "m2m/beatbox/Features.h"

#include "DrumSynth.h"

#include <cstdio>
#include <string>

namespace m2m::beatboxtest
{

inline DrumClass expectedClass (SynthDrum d)
{
    switch (d)
    {
        case SynthDrum::Kick:      return DrumClass::Kick;
        case SynthDrum::Snare:     return DrumClass::Snare;
        case SynthDrum::Clap:      return DrumClass::Clap;
        case SynthDrum::ClosedHat: return DrumClass::ClosedHat;
        case SynthDrum::OpenHat:   return DrumClass::OpenHat;
        case SynthDrum::Tone440:   break;
    }
    return DrumClass::Count;
}

/** Features of one rendered hit as the engine would see them: 30 Hz
    preprocessing, window starting at the onset +- a little estimate jitter
    (the onset detector is accurate to well under 1 ms on these sounds). */
inline FeatureVector hitFeatures (SynthDrum type, double sr, Rng& rng, FeatureExtractor& fx, float windowMs,
                                  float minDb = -30.0f, float maxDb = -6.0f)
{
    auto s = renderDrum (type, sr, rng, static_cast<float> (rng.range (minDb, maxDb)));
    Preprocessor pp;
    pp.prepare (sr);
    pp.setHighPassHz (30.0f);
    pp.process (s.data(), s.data(), static_cast<int> (s.size()));

    const int n = fx.windowSamples (windowMs);
    const int off = rng.intRange (-16, 16);
    std::vector<float> w (static_cast<std::size_t> (n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const int j = i + off;
        if (j >= 0 && j < static_cast<int> (s.size()))
            w[static_cast<std::size_t> (i)] = s[static_cast<std::size_t> (j)];
    }
    return fx.extract (w.data(), n);
}

constexpr SynthDrum fiveSounds[] = { SynthDrum::Kick, SynthDrum::Snare, SynthDrum::Clap,
                                     SynthDrum::ClosedHat, SynthDrum::OpenHat };

/** Rows: truth (5 sounds). Columns: the 7 classes + "rejected". */
struct Confusion
{
    int m[5][numDrumClasses + 1] {};
    int total[5] {};

    void add (int row, const Classification& c)
    {
        ++total[row];
        ++m[row][c.accepted ? toIndex (c.cls) : numDrumClasses];
    }

    double accuracy (int row) const
    {
        const int col = toIndex (expectedClass (fiveSounds[row]));
        return total[row] > 0 ? double (m[row][col]) / total[row] : 0.0;
    }

    void print (const char* title) const
    {
        std::printf ("%s\n  %-10s", title, "truth\\out");
        for (int c = 0; c < 5; ++c)
            std::printf ("%10s", drumClassName (static_cast<DrumClass> (c)));
        std::printf ("%10s%10s%10s\n", "tomlow", "tomhigh", "rejected");
        for (int r = 0; r < 5; ++r)
        {
            std::printf ("  %-10s", synthName (fiveSounds[r]));
            for (int c = 0; c <= numDrumClasses; ++c)
                std::printf ("%10d", m[r][c]);
            std::printf ("   acc %.1f%%\n", 100.0 * accuracy (r));
        }
    }
};

} // namespace m2m::beatboxtest
