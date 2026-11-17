#include "m2m/test/Scenarios.h"
#include "m2m/test/Drums.h"
#include "m2m/test/SignalGen.h"

#include "m2m/audio/MathUtil.h"

#include <algorithm>
#include <functional>

namespace m2m::test
{

namespace
{
    //==========================================================================
    /** Appends sound left to right and records truth notes. */
    class Timeline
    {
    public:
        Timeline (std::string name, double sampleRate)
        {
            sc.name = std::move (name);
            sc.sampleRate = sampleRate;
        }

        double sr() const noexcept { return sc.sampleRate; }
        double now() const noexcept { return static_cast<double> (sc.audio.size()) / sc.sampleRate; }

        void rest (double seconds) { append (sc.audio, silence (sr(), seconds)); }

        /** Append a rendered sound; it is one truth note spanning the whole sound. */
        void addNote (int midiNote, const std::vector<float>& sound)
        {
            const double start = now();
            append (sc.audio, sound);
            sc.notes.push_back ({ start, now(), midiNote });
        }

        /** Append a sound and record explicit truth notes, given relative to its start. */
        void addSound (const std::vector<float>& sound, const std::vector<TruthNote>& relativeNotes)
        {
            const double start = now();
            append (sc.audio, sound);
            for (auto n : relativeNotes)
                sc.notes.push_back ({ start + n.onsetSec, start + n.offsetSec, n.midiNote });
        }

        /** Record a glide relative to a sound that starts at soundStartSec. */
        void addGlide (double soundStartSec, double relStart, double relEnd, int fromNote, int toNote)
        {
            sc.glides.push_back ({ soundStartSec + relStart, soundStartSec + relEnd, fromNote, toNote });
        }

        /** Mix a drum hit at an absolute time (the timeline grows as needed). */
        void addHit (DrumKind kind, double atSec, const DrumParams& p)
        {
            const auto offset = secondsToSamples (atSec, sr());
            mixInto (sc.audio, drum (kind, sr(), p), offset);
            const double t = static_cast<double> (offset) / sr();
            sc.notes.push_back ({ t, t, gmNote (kind) });
        }

        /** Mix a drum hit cut off ("choked") after maxSec with a 3 ms fade,
            as when a beatboxer's next sound ends the previous one. */
        void addHitChoked (DrumKind kind, double atSec, const DrumParams& p, double maxSec)
        {
            auto h = drum (kind, sr(), p);
            choke (h, maxSec);
            const auto offset = secondsToSamples (atSec, sr());
            mixInto (sc.audio, h, offset);
            const double t = static_cast<double> (offset) / sr();
            sc.notes.push_back ({ t, t, gmNote (kind) });
        }

        void choke (std::vector<float>& h, double maxSec) const
        {
            const auto n = std::min (h.size(), secondsToSamples (maxSec, sr()));
            h.resize (n);
            const auto fade = std::min (n, secondsToSamples (0.003, sr()));
            for (std::size_t k = 0; k < fade; ++k)
                h[n - 1 - k] *= static_cast<float> (k) / static_cast<float> (fade);
        }

        /** Mix a voice-like hit (see voiceDrum) whose truth onset is at atSec. */
        void addVoiceHit (DrumKind kind, double atSec, const DrumParams& p)
        {
            const auto onset = secondsToSamples (atSec, sr());
            const auto v = voiceDrum (kind, sr(), p);
            const auto start = onset >= v.onsetSample ? onset - v.onsetSample : 0;
            mixInto (sc.audio, v.audio, start);
            const double t = static_cast<double> (start + v.onsetSample) / sr();
            sc.notes.push_back ({ t, t, gmNote (kind) });
        }

        /** Pad with silence up to an absolute length. */
        void padTo (double seconds)
        {
            const auto n = secondsToSamples (seconds, sr());
            if (sc.audio.size() < n)
                sc.audio.resize (n, 0.0f);
        }

        Scenario finish()
        {
            std::stable_sort (sc.notes.begin(), sc.notes.end(),
                              [] (const TruthNote& a, const TruthNote& b) { return a.onsetSec < b.onsetSec; });
            return std::move (sc);
        }

        std::vector<float>& audio() noexcept { return sc.audio; }

    private:
        Scenario sc;
    };

    double hz (int midi) { return midiToHz (static_cast<double> (midi)); }

    const Adsr voiceEnv { 0.015, 0.0, 1.0, 0.03 };

    std::vector<float> sungNote (double sr, int midi, double seconds, double levelDb,
                                 const std::vector<float>& parts = partials::voiceLike())
    {
        auto t = harmonicTone (sr, seconds, hz (midi), parts, levelDb);
        applyEnvelope (t, sr, voiceEnv);
        return t;
    }

    //==========================================================================
    // Trigger

    Scenario levelBursts (double sr)
    {
        Timeline tl ("level_bursts", sr);
        tl.rest (0.25);
        const double lengths[] = { 0.2, 0.3, 0.4, 0.25, 0.35 };
        const double levels[]  = { -12.0, -6.0, -18.0, -12.0, -9.0 };
        for (int i = 0; i < 5; ++i)
        {
            auto b = sine (sr, lengths[i], 440.0, levels[i]);
            applyEnvelope (b, sr, { 0.005, 0.0, 1.0, 0.005 });
            tl.addNote (60, b);
            tl.rest (0.3);
        }
        tl.rest (0.1);
        return tl.finish();
    }

    //==========================================================================
    // Melody

    Scenario sustainedNotes (double sr)
    {
        Timeline tl ("sustained_notes", sr);
        tl.rest (0.25);
        for (int n : { 57, 60, 64, 67, 62 })
        {
            tl.addNote (n, sungNote (sr, n, 0.8, -12.0));
            tl.rest (0.3);
        }
        return tl.finish();
    }

    Scenario chromaticScale (double sr)
    {
        Timeline tl ("chromatic_scale", sr);
        tl.rest (0.25);
        for (int n = 48; n <= 72; ++n)
        {
            tl.addNote (n, sungNote (sr, n, 0.25, -12.0));
            tl.rest (0.1);
        }
        tl.rest (0.2);
        return tl.finish();
    }

    Scenario majorScaleLegato (double sr)
    {
        Timeline tl ("major_scale_legato", sr);
        tl.rest (0.25);

        const int notes[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
        const double noteSec = 0.35;
        const auto perNote = secondsToSamples (noteSec, sr);

        std::vector<double> f0;
        std::vector<TruthNote> truth;
        for (int n : notes)
        {
            const double start = static_cast<double> (f0.size()) / sr;
            f0.insert (f0.end(), perNote, hz (n));
            truth.push_back ({ start, static_cast<double> (f0.size()) / sr, n });
        }

        auto sound = renderTrack (sr, f0, partials::voiceLike(), -12.0);
        applyEnvelope (sound, sr, voiceEnv);
        tl.addSound (sound, truth);
        tl.rest (0.3);
        return tl.finish();
    }

    Scenario vibrato (double sr)
    {
        Timeline tl ("vibrato", sr);
        tl.rest (0.25);
        const double rates[] = { 5.0, 6.0, 7.0 };
        int i = 0;
        for (int n : { 60, 64, 67 })
        {
            auto t = vibratoTone (sr, 1.2, hz (n), 50.0, rates[i++], partials::voiceLike(), -12.0);
            applyEnvelope (t, sr, voiceEnv);
            tl.addNote (n, t);
            tl.rest (0.3);
        }
        return tl.finish();
    }

    Scenario glide (double sr)
    {
        // Truth: the start note until the glide begins and the end note from
        // the moment the glide arrives; the glide itself is scored by the
        // glide check (Metrics.h), which allows chromatic passing notes.
        Timeline tl ("glide", sr);
        tl.rest (0.25);

        struct G { int from, to; };
        for (const G g : { G { 60, 67 }, G { 69, 64 } })
        {
            const double before = 0.5, glideSec = 0.15, after = 0.5;
            auto t = glideTone (sr, hz (g.from), hz (g.to), before, glideSec, after, partials::voiceLike(), -12.0);
            applyEnvelope (t, sr, voiceEnv);
            const double len = static_cast<double> (t.size()) / sr;
            const double start = tl.now();
            tl.addSound (t, { { 0.0, before, g.from }, { before + glideSec, len, g.to } });
            tl.addGlide (start, before, before + glideSec, g.from, g.to);
            tl.rest (0.3);
        }
        return tl.finish();
    }

    Scenario repeatedSamePitch (double sr)
    {
        Timeline tl ("repeated_same_pitch", sr);
        tl.rest (0.25);
        const double gaps[] = { 0.06, 0.07, 0.08, 0.06, 0.07 };
        for (int i = 0; i < 6; ++i)
        {
            tl.addNote (62, sungNote (sr, 62, 0.3, -12.0));
            tl.rest (i < 5 ? gaps[i] : 0.3);
        }
        return tl.finish();
    }

    Scenario weakFundamental (double sr)
    {
        Timeline tl ("weak_fundamental", sr);
        tl.rest (0.25);
        for (int n : { 48, 52, 55, 60, 64 })
        {
            tl.addNote (n, sungNote (sr, n, 0.6, -12.0, partials::weakFundamental()));
            tl.rest (0.2);
        }
        tl.rest (0.1);
        return tl.finish();
    }

    Scenario dynamics (double sr)
    {
        Timeline tl ("dynamics", sr);
        tl.rest (0.25);
        const int notes[]     = { 60, 62, 64, 65, 67, 67, 65, 64, 62, 60 };
        const double levels[] = { -30, -24, -18, -12, -6, -6, -12, -18, -24, -30 };
        for (int i = 0; i < 10; ++i)
        {
            tl.addNote (notes[i], sungNote (sr, notes[i], 0.4, levels[i]));
            tl.rest (0.2);
        }
        tl.rest (0.1);
        return tl.finish();
    }

    Scenario noisy20dbSnr (double sr)
    {
        Timeline tl ("noisy_20db_snr", sr);
        tl.rest (0.25);
        for (int n : { 57, 60, 64, 67, 62 })
        {
            tl.addNote (n, sungNote (sr, n, 0.6, -12.0));
            tl.rest (0.25);
        }

        // SNR is relative to the RMS of a sustained note (not of the whole
        // file, which includes the gaps).
        const double noteRms = rms (harmonicTone (sr, 0.5, hz (60), partials::voiceLike(), -12.0));
        auto mixed = mixWithSnr (tl.audio(), pinkNoise (sr, 1.0, -20.0, 20), 20.0, noteRms);
        tl.audio() = std::move (mixed);
        return tl.finish();
    }

    Scenario silenceScenario (double sr)
    {
        Timeline tl ("silence", sr);
        tl.rest (3.0);
        return tl.finish();
    }

    Scenario noiseOnly (double sr)
    {
        Timeline tl ("noise_only", sr);
        tl.audio() = pinkNoise (sr, 4.0, -45.0, 7);
        return tl.finish();
    }

    //==========================================================================
    // Beatbox

    constexpr double drumVariation = 0.3;

    Scenario singleDrum (const char* name, DrumKind kind, double sr)
    {
        Timeline tl (name, sr);
        for (int i = 0; i < 8; ++i)
            tl.addHit (kind, 0.25 + 0.5 * i, { -6.0 - (i % 3) * 3.0, drumVariation, 100u + static_cast<unsigned> (i) });
        tl.padTo (4.5);
        return tl.finish();
    }

    /** Monophonic (one sound at a time, like a real beatboxer) 8th-note groove. */
    void basicPattern (Timeline& tl, double startSec, int bars, const std::function<double (int)>& levelFor)
    {
        const DrumKind pattern[] = { DrumKind::Kick, DrumKind::ClosedHat, DrumKind::Snare, DrumKind::ClosedHat,
                                     DrumKind::Kick, DrumKind::Kick,      DrumKind::Snare, DrumKind::ClosedHat };
        const double eighth = 0.25; // 120 bpm
        int idx = 0;
        for (int bar = 0; bar < bars; ++bar)
            for (const auto k : pattern)
            {
                tl.addHit (k, startSec + eighth * idx, { levelFor (idx), drumVariation, 200u + static_cast<unsigned> (idx) });
                ++idx;
            }
    }

    Scenario basicBeat120 (double sr)
    {
        Timeline tl ("basic_beat_120bpm", sr);
        basicPattern (tl, 0.25, 2, [] (int i) { return (i % 2 == 0) ? -6.0 : -10.0; });
        tl.padTo (0.25 + 16 * 0.25 + 0.5);
        return tl.finish();
    }

    Scenario fastHats16ths (double sr)
    {
        Timeline tl ("fast_hats_16ths", sr);
        const double sixteenth = 0.125; // 120 bpm
        for (int i = 0; i < 32; ++i)
            tl.addHit (DrumKind::ClosedHat, 0.25 + sixteenth * i,
                       { (i % 4 == 0) ? -6.0 : -12.0, drumVariation, 300u + static_cast<unsigned> (i) });
        tl.padTo (0.25 + 32 * 0.125 + 0.5);
        return tl.finish();
    }

    Scenario voiceSingle (const char* name, DrumKind kind, double sr)
    {
        Timeline tl (name, sr);
        for (int i = 0; i < 12; ++i)
            tl.addVoiceHit (kind, 0.3 + 0.5 * i, { -6.0 - (i % 4) * 5.0, 1.0, 400u + static_cast<unsigned> (i) });
        tl.padTo (0.3 + 12 * 0.5 + 0.2);
        return tl.finish();
    }

    Scenario voiceBeat (double sr)
    {
        Timeline tl ("voice_beat_100bpm", sr);
        const DrumKind pattern[] = { DrumKind::Kick, DrumKind::ClosedHat, DrumKind::Snare, DrumKind::ClosedHat,
                                     DrumKind::Kick, DrumKind::Kick,      DrumKind::Snare, DrumKind::ClosedHat };
        const double eighth = 0.3; // 100 bpm
        for (int i = 0; i < 24; ++i)
            tl.addVoiceHit (pattern[i % 8], 0.3 + eighth * i,
                            { (i % 2 == 0) ? -8.0 : -14.0, 1.0, 500u + static_cast<unsigned> (i) });
        tl.padTo (0.3 + 24 * eighth + 0.4);
        return tl.finish();
    }

    Scenario beatNoiseFloor (double sr)
    {
        Timeline tl ("beat_noise_floor", sr);
        const double levels[] = { -14, -22, -16, -24, -14, -18, -16, -26 };
        basicPattern (tl, 0.3, 2, [&levels] (int i) { return levels[i % 8]; });
        tl.padTo (0.3 + 16 * 0.25 + 0.5);
        // steady pink noise floor at -48 dBFS RMS (room / preamp hiss)
        const auto noise = pinkNoise (sr, tl.now(), -48.0, 31);
        mixInto (tl.audio(), noise, 0);
        return tl.finish();
    }

    Scenario fast16ths200 (double sr)
    {
        // 16ths at 200 bpm = 75 ms apart; each sound is cut off by the next.
        Timeline tl ("fast_16ths_200bpm", sr);
        const DrumKind pattern[] = { DrumKind::Kick, DrumKind::ClosedHat, DrumKind::ClosedHat, DrumKind::ClosedHat,
                                     DrumKind::Snare, DrumKind::ClosedHat, DrumKind::Kick, DrumKind::ClosedHat };
        const double step = 0.075;
        for (int i = 0; i < 32; ++i)
            tl.addHitChoked (pattern[i % 8], 0.25 + step * i,
                             { (i % 4 == 0) ? -8.0 : -14.0, drumVariation, 600u + static_cast<unsigned> (i) }, step);
        tl.padTo (0.25 + 32 * step + 0.4);
        return tl.finish();
    }

    Scenario flams50ms (double sr)
    {
        // Pairs of hits 50 ms apart (grace note + main note); the first sound
        // is cut off by the second.
        Timeline tl ("flams_50ms", sr);
        struct Pair { DrumKind a, b; };
        const Pair pairs[] = { { DrumKind::Snare, DrumKind::Snare }, { DrumKind::Kick, DrumKind::ClosedHat },
                               { DrumKind::ClosedHat, DrumKind::Snare }, { DrumKind::Kick, DrumKind::Snare },
                               { DrumKind::ClosedHat, DrumKind::ClosedHat }, { DrumKind::Snare, DrumKind::Kick },
                               { DrumKind::Kick, DrumKind::Kick }, { DrumKind::ClosedHat, DrumKind::Kick } };
        for (int i = 0; i < 8; ++i)
        {
            const double t = 0.3 + 0.6 * i;
            tl.addHitChoked (pairs[i].a, t, { -14.0, drumVariation, 700u + static_cast<unsigned> (i) }, 0.05);
            tl.addHitChoked (pairs[i].b, t + 0.05, { -8.0, drumVariation, 750u + static_cast<unsigned> (i) }, 0.4);
        }
        tl.padTo (0.3 + 8 * 0.6 + 0.3);
        return tl.finish();
    }

    Scenario beatDynamics (double sr)
    {
        Timeline tl ("beat_dynamics", sr);
        const double levels[] = { -30, -6, -24, -12, -18, -9, -27, -15 };
        basicPattern (tl, 0.25, 2, [&levels] (int i) { return levels[i % 8]; });
        tl.padTo (0.25 + 16 * 0.25 + 0.5);
        return tl.finish();
    }

    //==========================================================================
    struct Entry
    {
        ScenarioInfo info;
        std::function<Scenario (double)> make;
    };

    const std::vector<Entry>& registry()
    {
        using C = ScenarioCategory;
        static const std::vector<Entry> entries {
            { { "level_bursts", C::Trigger, "5 sine bursts (440 Hz, -18..-6 dBFS) for the level trigger; truth note 60" }, levelBursts },

            { { "sustained_notes", C::Melody, "5 sustained voice-like notes (0.8 s) with gaps" }, sustainedNotes },
            { { "chromatic_scale", C::Melody, "chromatic scale C3..C5, 0.25 s notes, 0.1 s gaps" }, chromaticScale },
            { { "major_scale_legato", C::Melody, "C major scale, 0.35 s notes, no gaps (pitch steps only)" }, majorScaleLegato },
            { { "vibrato", C::Melody, "3 notes with +-50 cent vibrato at 5/6/7 Hz; one note each" }, vibrato },
            { { "glide", C::Melody, "60->67 and 69->64 glides over 150 ms; scored with the glide check" }, glide },
            { { "repeated_same_pitch", C::Melody, "note 62 six times with 60-80 ms gaps" }, repeatedSamePitch },
            { { "weak_fundamental", C::Melody, "low notes whose fundamental is 20 dB below the 2nd harmonic" }, weakFundamental },
            { { "dynamics", C::Melody, "notes at -30..-6 dBFS peak" }, dynamics },
            { { "noisy_20db_snr", C::Melody, "sustained notes plus pink noise at 20 dB SNR (re note RMS)" }, noisy20dbSnr },
            { { "silence", C::Melody, "3 s of digital silence; expect no notes" }, silenceScenario },
            { { "noise_only", C::Melody, "4 s of pink noise at -45 dBFS RMS; expect no notes" }, noiseOnly },

            { { "kick_only", C::Beatbox, "8 kicks, 0.5 s apart" }, [] (double sr) { return singleDrum ("kick_only", DrumKind::Kick, sr); } },
            { { "snare_only", C::Beatbox, "8 snares, 0.5 s apart" }, [] (double sr) { return singleDrum ("snare_only", DrumKind::Snare, sr); } },
            { { "closed_hat_only", C::Beatbox, "8 closed hats, 0.5 s apart" }, [] (double sr) { return singleDrum ("closed_hat_only", DrumKind::ClosedHat, sr); } },
            { { "open_hat_only", C::Beatbox, "8 open hats, 0.5 s apart" }, [] (double sr) { return singleDrum ("open_hat_only", DrumKind::OpenHat, sr); } },
            { { "clap_only", C::Beatbox, "8 claps, 0.5 s apart" }, [] (double sr) { return singleDrum ("clap_only", DrumKind::Clap, sr); } },
            { { "basic_beat_120bpm", C::Beatbox, "2 bars of K H S H K K S H 8ths at 120 bpm" }, basicBeat120 },
            { { "fast_hats_16ths", C::Beatbox, "32 closed hats, 16ths at 120 bpm (125 ms apart), accented" }, fastHats16ths },
            { { "beat_dynamics", C::Beatbox, "basic beat with hit levels from -30 to -6 dBFS" }, beatDynamics },
            { { "beat_noise_floor", C::Beatbox, "basic beat at -26..-14 dBFS over pink noise at -48 dBFS RMS" }, beatNoiseFloor },
            { { "fast_16ths_200bpm", C::Beatbox, "K H H H S H K H 16ths at 200 bpm (75 ms apart), each sound choked by the next" }, fast16ths200 },
            { { "flams_50ms", C::Beatbox, "8 pairs of hits 50 ms apart (grace + main), mixed classes" }, flams50ms },
            { { "voice_kick_only", C::Beatbox, "12 voice-like 'b/p' kicks (plosive + 75-125 Hz body), -6..-21 dBFS" }, [] (double sr) { return voiceSingle ("voice_kick_only", DrumKind::Kick, sr); } },
            { { "voice_snare_only", C::Beatbox, "12 voice-like 'pf/k' snares (1-6 kHz noise, weak tone)" }, [] (double sr) { return voiceSingle ("voice_snare_only", DrumKind::Snare, sr); } },
            { { "voice_closed_hat_only", C::Beatbox, "12 voice-like 'ts' closed hats (4-10 kHz fricative)" }, [] (double sr) { return voiceSingle ("voice_closed_hat_only", DrumKind::ClosedHat, sr); } },
            { { "voice_open_hat_only", C::Beatbox, "12 voice-like 'tsss' open hats" }, [] (double sr) { return voiceSingle ("voice_open_hat_only", DrumKind::OpenHat, sr); } },
            { { "voice_clap_only", C::Beatbox, "12 voice-like claps (2-4 short 1.5-4 kHz bursts)" }, [] (double sr) { return voiceSingle ("voice_clap_only", DrumKind::Clap, sr); } },
            { { "voice_beat_100bpm", C::Beatbox, "3 bars of K H S H K K S H 8ths at 100 bpm, voice-like sounds" }, voiceBeat },
        };
        return entries;
    }
}

//==============================================================================
const char* categoryName (ScenarioCategory c) noexcept
{
    switch (c)
    {
        case ScenarioCategory::Trigger: return "trigger";
        case ScenarioCategory::Melody:  return "melody";
        case ScenarioCategory::Beatbox: return "beatbox";
    }
    return "?";
}

std::optional<ScenarioCategory> categoryFromName (const std::string& name)
{
    for (auto c : { ScenarioCategory::Trigger, ScenarioCategory::Melody, ScenarioCategory::Beatbox })
        if (name == categoryName (c))
            return c;
    return std::nullopt;
}

const std::vector<ScenarioInfo>& scenarioCatalogue()
{
    static const std::vector<ScenarioInfo> infos = []
    {
        std::vector<ScenarioInfo> v;
        for (const auto& e : registry())
            v.push_back (e.info);
        return v;
    }();
    return infos;
}

std::vector<std::string> scenarioNames()
{
    std::vector<std::string> names;
    for (const auto& e : registry())
        names.push_back (e.info.name);
    return names;
}

std::vector<std::string> scenarioNames (ScenarioCategory category)
{
    std::vector<std::string> names;
    for (const auto& e : registry())
        if (e.info.category == category)
            names.push_back (e.info.name);
    return names;
}

bool hasScenario (const std::string& name)
{
    return std::any_of (registry().begin(), registry().end(),
                        [&name] (const Entry& e) { return e.info.name == name; });
}

std::optional<Scenario> makeScenario (const std::string& name, double sampleRate)
{
    if (sampleRate <= 0.0)
        return std::nullopt;
    for (const auto& e : registry())
        if (e.info.name == name)
            return e.make (sampleRate);
    return std::nullopt;
}

} // namespace m2m::test
