#include "m2m/test/Scenarios.h"
#include "m2m/test/SignalGen.h"
#include "SupportTestUtil.h"

#include "m2m/audio/MathUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <set>

using namespace m2m::test;
using Catch::Matchers::WithinAbs;

TEST_CASE ("support/Scenarios: catalogue")
{
    const std::vector<std::string> expected {
        "level_bursts",
        "sustained_notes", "chromatic_scale", "major_scale_legato", "vibrato", "glide", "repeated_same_pitch",
        "weak_fundamental", "dynamics", "noisy_20db_snr", "voice_formants", "low_male_range", "high_range",
        "fast_16ths_legato", "fast_16ths_detached", "soft_rearticulation", "consonant_onsets", "pitch_drift",
        "tremolo_vibrato", "scoop_ups", "unsteady_sustained", "unsteady_melody_detached", "unsteady_melody_legato",
        "hum_50hz", "hum_60hz", "silence", "noise_only",
        "kick_only", "snare_only", "closed_hat_only", "open_hat_only", "clap_only",
        "basic_beat_120bpm", "fast_hats_16ths", "beat_dynamics", "beat_noise_floor", "fast_16ths_200bpm",
        "flams_50ms", "voice_kick_only", "voice_snare_only", "voice_closed_hat_only", "voice_open_hat_only",
        "voice_clap_only", "voice_beat_100bpm",
    };
    REQUIRE (scenarioNames() == expected);
    REQUIRE (scenarioNames (ScenarioCategory::Trigger).size() == 1);
    REQUIRE (scenarioNames (ScenarioCategory::Melody).size() == 26);
    REQUIRE (scenarioNames (ScenarioCategory::Beatbox).size() == 17);
    REQUIRE (scenarioCatalogue().size() == expected.size());
    REQUIRE (hasScenario ("glide"));
    REQUIRE_FALSE (hasScenario ("nope"));
    REQUIRE_FALSE (makeScenario ("nope").has_value());
    REQUIRE (categoryFromName ("beatbox") == ScenarioCategory::Beatbox);
    REQUIRE_FALSE (categoryFromName ("x").has_value());
}

TEST_CASE ("support/Scenarios: truth sanity for every scenario")
{
    const std::set<int> gmDrums { 36, 38, 39, 42, 46 };

    for (const auto& info : scenarioCatalogue())
    {
        INFO (info.name);
        const auto sc = makeScenario (info.name);
        REQUIRE (sc.has_value());
        REQUIRE (sc->name == info.name);
        REQUIRE (sc->sampleRate == 48000.0);
        REQUIRE (sc->durationSec() > 1.0);
        REQUIRE (peak (sc->audio) < 1.0);  // no clipping

        const double dur = sc->durationSec();
        for (std::size_t i = 0; i < sc->notes.size(); ++i)
        {
            const auto& n = sc->notes[i];
            REQUIRE (n.onsetSec >= 0.0);
            REQUIRE (n.onsetSec < dur);
            REQUIRE (n.offsetSec >= n.onsetSec);
            REQUIRE (n.offsetSec <= dur + 1e-9);
            if (i > 0)
                REQUIRE (n.onsetSec >= sc->notes[i - 1].onsetSec);

            if (info.category == ScenarioCategory::Beatbox)
            {
                REQUIRE (gmDrums.count (n.midiNote) == 1);
                REQUIRE (n.offsetSec == n.onsetSec);
            }
            else
            {
                REQUIRE (n.offsetSec > n.onsetSec);
                REQUIRE (n.midiNote >= 36);
                REQUIRE (n.midiNote <= 84);
            }
            if (info.category == ScenarioCategory::Trigger)
                REQUIRE (n.midiNote == 60);
        }

        // Melody notes don't overlap (monophonic).
        if (info.category == ScenarioCategory::Melody)
            for (std::size_t i = 1; i < sc->notes.size(); ++i)
                REQUIRE (sc->notes[i].onsetSec >= sc->notes[i - 1].offsetSec - 1e-9);

        // Deterministic.
        const auto again = makeScenario (info.name);
        REQUIRE (again->audio == sc->audio);
        REQUIRE (again->notes == sc->notes);
    }
}

TEST_CASE ("support/Scenarios: content spot checks")
{
    using namespace m2m::test::util;

    SECTION ("silence and noise_only have no notes")
    {
        const auto s = makeScenario ("silence");
        REQUIRE (s->notes.empty());
        REQUIRE (peak (s->audio) == 0.0);
        const auto n = makeScenario ("noise_only");
        REQUIRE (n->notes.empty());
        REQUIRE_THAT (gainToDb (rms (n->audio)), WithinAbs (-45.0, 0.01));
    }

    SECTION ("sustained notes are at the truth pitch")
    {
        const auto sc = makeScenario ("sustained_notes");
        REQUIRE (sc->notes.size() == 5);
        const int n = 4096;
        for (const auto& note : sc->notes)
        {
            const auto mid = static_cast<std::size_t> ((note.onsetSec + 0.2) * sc->sampleRate);
            const auto p = powerSpectrum (sc->audio, n, mid, 2 * static_cast<std::size_t> (n));
            REQUIRE_THAT (m2m::hzToMidi (peakFrequency (p, sc->sampleRate, n, 60.0, 2000.0)),
                          WithinAbs (static_cast<double> (note.midiNote), 0.1));
        }
    }

    SECTION ("legato scale has no gaps; repeated notes have 60-80 ms gaps")
    {
        const auto legato = makeScenario ("major_scale_legato");
        REQUIRE (legato->notes.size() == 8);
        for (std::size_t i = 1; i < legato->notes.size(); ++i)
            REQUIRE (legato->notes[i].onsetSec == legato->notes[i - 1].offsetSec);

        const auto rep = makeScenario ("repeated_same_pitch");
        REQUIRE (rep->notes.size() == 6);
        for (std::size_t i = 1; i < rep->notes.size(); ++i)
        {
            REQUIRE (rep->notes[i].midiNote == 62);
            const double gap = rep->notes[i].onsetSec - rep->notes[i - 1].offsetSec;
            REQUIRE (gap >= 0.06 - 1e-9);
            REQUIRE (gap <= 0.08 + 1e-9);
        }
    }

    SECTION ("dynamics span -30..-6 dBFS")
    {
        const auto sc = makeScenario ("dynamics");
        double lo = 0.0, hi = -200.0;
        for (const auto& note : sc->notes)
        {
            const auto a = static_cast<std::size_t> (note.onsetSec * sc->sampleRate);
            const auto b = static_cast<std::size_t> (note.offsetSec * sc->sampleRate);
            const std::vector<float> seg (sc->audio.begin() + static_cast<long> (a), sc->audio.begin() + static_cast<long> (b));
            lo = std::min (lo, gainToDb (peak (seg)));
            hi = std::max (hi, gainToDb (peak (seg)));
        }
        REQUIRE_THAT (lo, WithinAbs (-30.0, 0.1));
        REQUIRE_THAT (hi, WithinAbs (-6.0, 0.1));
    }

    SECTION ("drum onsets are sample-exact: silence before, sound at the onset")
    {
        for (const char* name : { "kick_only", "snare_only", "closed_hat_only", "open_hat_only", "clap_only" })
        {
            INFO (name);
            const auto sc = makeScenario (name);
            REQUIRE (sc->notes.size() == 8);
            for (const auto& hit : sc->notes)
            {
                const auto s = static_cast<std::size_t> (std::llround (hit.onsetSec * sc->sampleRate));
                // The 5 ms after each onset carry energy.
                REQUIRE (rms (sc->audio.data() + s, 240) > 0.01);
            }
            const auto first = static_cast<std::size_t> (std::llround (sc->notes[0].onsetSec * sc->sampleRate));
            REQUIRE (peak (std::vector<float> (sc->audio.begin(), sc->audio.begin() + static_cast<long> (first))) == 0.0);
        }
    }

    SECTION ("basic beat pattern at 120 bpm")
    {
        const auto sc = makeScenario ("basic_beat_120bpm");
        REQUIRE (sc->notes.size() == 16);
        const int pattern[] = { 36, 42, 38, 42, 36, 36, 38, 42 };
        for (std::size_t i = 0; i < 16; ++i)
        {
            REQUIRE (sc->notes[i].midiNote == pattern[i % 8]);
            REQUIRE_THAT (sc->notes[i].onsetSec, WithinAbs (0.25 + 0.25 * static_cast<double> (i), 1e-9));
        }
        const auto hats = makeScenario ("fast_hats_16ths");
        REQUIRE (hats->notes.size() == 32);
        REQUIRE_THAT (hats->notes[1].onsetSec - hats->notes[0].onsetSec, WithinAbs (0.125, 1e-9));
    }

    SECTION ("other sample rates")
    {
        const auto sc = makeScenario ("level_bursts", 44100.0);
        REQUIRE (sc->sampleRate == 44100.0);
        REQUIRE (sc->notes == makeScenario ("level_bursts")->notes);
    }
}
