#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace m2m::test
{

/** Synthetic vocal-percussion stand-ins. Each hit starts at sample 0 (the
    truth onset) and is normalised to peak level levelDb.

    Rough recipes:
    - Kick:      sine sweeping ~150 -> 50 Hz (exponential) + 2 ms click, ~300 ms
    - Snare:     200 Hz tone (fast decay) + band-passed noise (~1-6 kHz), ~200 ms
    - Clap:      4 noise bursts ~10 ms apart, band-passed 1-4 kHz, short tail
    - ClosedHat: high-passed noise (> 6 kHz), ~30 ms
    - OpenHat:   high-passed noise (> 6 kHz), ~250 ms decay */
enum class DrumKind { Kick, Snare, Clap, ClosedHat, OpenHat };

struct DrumParams
{
    double levelDb = -6.0;        // peak dBFS
    double variation = 0.0;       // 0 = canonical sound, 1 = full random variation
    std::uint64_t seed = 1;       // noise + variation seed
};

/** GM drum note: kick 36, snare 38, clap 39, closed hat 42, open hat 46. */
int gmNote (DrumKind kind) noexcept;
const char* drumName (DrumKind kind) noexcept;

std::vector<float> kick      (double sr, const DrumParams& p = {});
std::vector<float> snare     (double sr, const DrumParams& p = {});
std::vector<float> clap      (double sr, const DrumParams& p = {});
std::vector<float> closedHat (double sr, const DrumParams& p = {});
std::vector<float> openHat   (double sr, const DrumParams& p = {});

std::vector<float> drum (DrumKind kind, double sr, const DrumParams& p = {});

/** A second, deliberately different family: voice-like vocal percussion
    (closer to what a beatboxer's mouth produces than the drum-machine
    stand-ins above). Used to check that the classifiers are not tuned to
    one synthesiser. With variation 1 every hit draws its spectrum, length,
    balance and a quiet breath/lip pre-roll at random:

    - Kick  "b/p": lip plosive (2-6 ms low-passed noise burst) into a voiced
                   body at 75-125 Hz with a few glottal harmonics, low-passed
                   at 250-700 Hz, decay 30-110 ms.
    - Snare "pf/k": broadband noise burst with a 1-6 kHz emphasis (centre
                   1.2-4.5 kHz, Q 0.4-1.2), a weak (-6..-22 dB) 140-320 Hz
                   tone, some air above 6 kHz, decay 20-70 ms.
    - ClosedHat "ts": 1-4 ms "t" burst, then "s" fricative noise 4-10 kHz
                   (high-pass 3.5-6 kHz, low-pass 9-13 kHz), 1-6 ms fricative
                   attack, decay tau 6-18 ms.
    - OpenHat "tss": as the closed hat with a 150-350 ms sustained "s".
    - Clap "k-ch": 2-4 short 1.5-4 kHz noise bursts 5-11 ms apart (the
                   whole clap within ~35 ms, shorter than the onset
                   refractory period).

    The truth onset is the start of the
    main burst; the pre-roll (0-25 ms at -45..-30 dB re peak) is placed
    BEFORE it: VoiceDrum::onsetSample is where the hit itself starts.
    Peak level is levelDb. */
struct VoiceDrum
{
    std::vector<float> audio;
    std::size_t onsetSample = 0; // index of the truth onset in audio
};

VoiceDrum voiceDrum (DrumKind kind, double sr, const DrumParams& p = {});

} // namespace m2m::test
