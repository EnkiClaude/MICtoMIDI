#include "m2m/audio/MathUtil.h"
#include "m2m/pitch/MpmDetector.h"
#include "m2m/pitch/YinDetector.h"
#include "MelodySignals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace m2m;
using namespace m2m::melodytest;

namespace
{
    constexpr double sr = 48000.0;

    std::unique_ptr<IPitchDetector> makeDetector (bool mpm, float minHz, float maxHz)
    {
        std::unique_ptr<IPitchDetector> d;
        if (mpm)
            d = std::make_unique<MpmDetector>();
        else
            d = std::make_unique<YinDetector>();
        d->prepare (sr);
        d->setRange (minHz, maxHz);
        return d;
    }

    double centsError (double hz, double trueHz) { return 1200.0 * std::log2 (hz / trueHz); }
}

TEST_CASE ("Pitch window rule", "[pitch]")
{
    REQUIRE (pitchMaxLag (48000.0, 80.0f) == 602);
    REQUIRE (pitchWindowSize (48000.0, 80.0f) == 1204);
    REQUIRE (pitchWindowSize (48000.0, 100.0f) == 964);

    YinDetector y;
    y.prepare (sr);
    y.setRange (100.0f, 1000.0f);
    REQUIRE (y.windowSize() == 964);
    y.setRange (10.0f, 1000.0f); // clamped to the prepared lowest (50 Hz)
    REQUIRE (y.windowSize() == pitchWindowSize (sr, 50.0f));
}

TEST_CASE ("Pitch detectors: pure sines MIDI 40..84 within +/-10 cents", "[pitch]")
{
    for (const bool mpm : { false, true })
    {
        auto det = makeDetector (mpm, 80.0f, 1100.0f);
        double worst = 0.0;
        int worstNote = 0;

        for (int note = 40; note <= 84; ++note)
        {
            const double hz = midiToHz (note);
            Synth s (sr);
            s.setRampMs (0.0);
            s.tone (0.25, hz, -12.0f);
            const auto& sig = s.buffer();

            for (int f = 0; f < 8; ++f)
            {
                const int end = det->windowSize() + 1000 + f * 997;
                const auto est = det->detect (sig.data(), end);
                REQUIRE (est.isVoiced());
                const double err = std::abs (centsError (est.hz, hz));
                if (err > worst)
                {
                    worst = err;
                    worstNote = note;
                }
            }
        }

        std::printf ("[pitch] %s sine worst error %.3f cents (note %d)\n", mpm ? "MPM" : "YIN", worst, worstNote);
        CAPTURE (mpm, worst, worstNote);
        CHECK (worst <= 10.0);
    }
}

TEST_CASE ("Pitch detectors: weak fundamental -> octave error rate < 1%", "[pitch]")
{
    // Fundamental 20 dB below the 2nd/3rd harmonics, plus random harmonic sets.
    std::vector<std::vector<double>> spectra;
    spectra.push_back ({ 0.1, 1.0, 1.0, 0.5, 0.3, 0.2 });
    spectra.push_back ({ 0.1, 1.0, 1.0, 0.0, 0.0, 0.0 });
    spectra.push_back ({ 0.1, 1.0, 0.7, 0.7, 0.5, 0.4, 0.3, 0.2 });
    Rng rng (1234);
    for (int i = 0; i < 5; ++i)
    {
        std::vector<double> h { 0.1, 1.0, 1.0 };
        for (int k = 0; k < 7; ++k)
            h.push_back (0.5 * (rng.bipolar() + 1.0) * 0.8);
        spectra.push_back (h);
    }

    for (const bool mpm : { false, true })
    {
        auto det = makeDetector (mpm, 80.0f, 1000.0f);
        int voiced = 0, octaveErrors = 0, unvoiced = 0;
        double worstFine = 0.0;

        for (std::size_t si = 0; si < spectra.size(); ++si)
        {
            for (int note = 41; note <= 79; note += 2)
            {
                const double hz = midiToHz (note);
                Synth s (sr);
                s.setRampMs (0.0);
                s.setHarmonics (spectra[si]);
                s.tone (0.2, hz, -12.0f);
                const auto& sig = s.buffer();

                for (int f = 0; f < 10; ++f)
                {
                    const int end = det->windowSize() + 300 + f * 613;
                    const auto est = det->detect (sig.data(), end);
                    if (! est.isVoiced() || est.confidence < 0.5f)
                    {
                        ++unvoiced;
                        continue;
                    }
                    ++voiced;
                    const double err = std::abs (centsError (est.hz, hz));
                    if (err > 100.0)
                        ++octaveErrors;
                    else
                        worstFine = std::max (worstFine, err);
                }
            }
        }

        const double rate = voiced > 0 ? 100.0 * octaveErrors / voiced : 100.0;
        std::printf ("[pitch] %s weak-fundamental: %d voiced, %d unvoiced, %d gross errors (%.2f%%), worst fine err %.2f cents\n",
                     mpm ? "MPM" : "YIN", voiced, unvoiced, octaveErrors, rate, worstFine);
        CAPTURE (mpm, voiced, unvoiced, octaveErrors, rate);
        CHECK (rate < 1.0);
        CHECK (unvoiced * 100 < (voiced + unvoiced)); // >99% voiced
    }
}

TEST_CASE ("Pitch detectors: silence and white noise are unvoiced or low confidence", "[pitch]")
{
    for (const bool mpm : { false, true })
    {
        auto det = makeDetector (mpm, 80.0f, 1000.0f);

        std::vector<float> silence (static_cast<std::size_t> (det->windowSize()), 0.0f);
        REQUIRE_FALSE (det->detect (silence.data(), det->windowSize()).isVoiced());

        std::vector<float> noise (48000, 0.0f);
        addNoise (noise, -20.0f, 42);
        float maxConf = 0.0f;
        double meanConf = 0.0;
        int frames = 0;
        for (int end = det->windowSize(); end <= static_cast<int> (noise.size()); end += 128)
        {
            const auto est = det->detect (noise.data(), end);
            maxConf = std::max (maxConf, est.confidence);
            meanConf += est.confidence;
            ++frames;
        }
        meanConf /= frames;
        std::printf ("[pitch] %s white noise confidence: mean %.3f max %.3f\n", mpm ? "MPM" : "YIN", meanConf, static_cast<double> (maxConf));
        CAPTURE (mpm, maxConf, meanConf);
        CHECK (maxConf < 0.6f);
    }
}

TEST_CASE ("Pitch detectors: range limits", "[pitch]")
{
    for (const bool mpm : { false, true })
    {
        auto det = makeDetector (mpm, 100.0f, 500.0f);
        Synth s (sr);
        s.tone (0.1, 220.0, -12.0f);
        const auto est = det->detect (s.buffer().data(), static_cast<int> (s.buffer().size()));
        REQUIRE (est.isVoiced());
        CHECK (std::abs (centsError (est.hz, 220.0)) < 10.0);

        // Too short a buffer -> unvoiced, never reads out of bounds.
        CHECK_FALSE (det->detect (s.buffer().data(), det->windowSize() - 1).isVoiced());
    }
}
