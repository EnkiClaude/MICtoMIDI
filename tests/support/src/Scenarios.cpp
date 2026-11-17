#include "m2m/test/Scenarios.h"
#include "m2m/test/Drums.h"
#include "m2m/test/SignalGen.h"

#include "m2m/audio/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

    //==========================================================================
    // Harder melody scenarios: voice-like spectra, extreme ranges, fast
    // passages, soft re-articulations, consonants, scoops, hum.

    /** Formants (centre Hz, bandwidth Hz, gain dB) of a few vowels. */
    struct Vowel { double f[3]; double bw[3]; double gainDb[3]; };
    const Vowel vowelA { { 700, 1220, 2600 }, { 110, 120, 160 }, { 0, -6, -18 } };
    const Vowel vowelO { { 450, 800, 2830 },  { 80, 90, 120 },   { 0, -8, -28 } };
    const Vowel vowelI { { 300, 2300, 3000 }, { 60, 100, 120 },  { 0, -14, -18 } };
    const Vowel vowelE { { 530, 1840, 2480 }, { 60, 100, 120 },  { 0, -10, -15 } };

    /** Harmonic amplitudes of a voiced vowel at f0: a -6 dB/octave source
        shaped by three resonances plus a small floor (the fundamental of a
        low voice ends up well below the harmonics near F1). */
    std::vector<float> vowelPartials (double f0, const Vowel& v, double sr)
    {
        std::vector<float> amps;
        const double fMax = std::min (8000.0, 0.45 * sr);
        for (int k = 1; k * f0 < fMax; ++k)
        {
            const double f = k * f0;
            double env = 0.05;
            for (int i = 0; i < 3; ++i)
            {
                const double x = (f - v.f[i]) / v.bw[i];
                env += std::pow (10.0, v.gainDb[i] / 20.0) / (1.0 + x * x);
            }
            amps.push_back (static_cast<float> (env / k));
        }
        return amps;
    }

    /** Breath noise (band-passed around 2.5 kHz) under a tone, at relDb
        relative to the tone's RMS, following the tone's own envelope. */
    void addBreath (std::vector<float>& tone, double sr, double relDb, std::uint64_t seed)
    {
        auto n = whiteNoise (sr, static_cast<double> (tone.size()) / sr, -20.0, seed);
        auto bp = Biquad::bandPass (sr, std::min (2500.0, 0.4 * sr), 0.7);
        bp.processInPlace (n);
        const double gain = rms (tone) * std::pow (10.0, relDb / 20.0) / std::max (1.0e-12, rms (n));
        // envelope of the tone: running peak with ~10 ms release
        double env = 0.0;
        const double rel = std::exp (-1.0 / (0.01 * sr));
        double peakAll = peak (tone);
        for (std::size_t i = 0; i < tone.size(); ++i)
        {
            env = std::max (static_cast<double> (std::abs (tone[i])), env * rel);
            tone[i] += static_cast<float> (gain * n[i] * env / std::max (1.0e-12, peakAll));
        }
    }

    /** A sung vowel on a pitch track (Hz per sample), with the standard
        voice envelope and breath noise. */
    std::vector<float> sungVowel (double sr, const std::vector<double>& f0, double refHz, const Vowel& v,
                                  double levelDb, double breathDb, std::uint64_t seed)
    {
        auto t = renderTrack (sr, f0, vowelPartials (refHz, v, sr), levelDb);
        applyEnvelope (t, sr, voiceEnv);
        if (breathDb > -100.0)
            addBreath (t, sr, breathDb, seed);
        return t;
    }

    std::vector<double> steadyTrack (double sr, double seconds, double hzValue)
    {
        return std::vector<double> (secondsToSamples (seconds, sr), hzValue);
    }

    std::vector<double> vibratoTrack (double sr, double seconds, double hzValue, double cents, double rate)
    {
        std::vector<double> f (secondsToSamples (seconds, sr));
        for (std::size_t i = 0; i < f.size(); ++i)
            f[i] = hzValue * std::exp2 (cents / 1200.0 * std::sin (2.0 * 3.141592653589793 * rate * static_cast<double> (i) / sr));
        return f;
    }

    Scenario voiceFormants (double sr)
    {
        Timeline tl ("voice_formants", sr);
        tl.rest (0.25);
        const int notes[] = { 57, 60, 64, 62, 67, 55 };
        const Vowel* vowels[] = { &vowelA, &vowelO, &vowelI, &vowelE, &vowelA, &vowelO };
        for (int i = 0; i < 6; ++i)
        {
            const double f = hz (notes[i]);
            tl.addNote (notes[i], sungVowel (sr, vibratoTrack (sr, 0.6, f, 20.0, 5.5), f, *vowels[i], -12.0, -22.0,
                                             40u + static_cast<std::uint64_t> (i)));
            tl.rest (0.25);
        }
        return tl.finish();
    }

    Scenario lowMaleRange (double sr)
    {
        Timeline tl ("low_male_range", sr);
        tl.rest (0.25);
        const int notes[] = { 40, 43, 45, 47, 48, 41 }; // 82 .. 131 Hz
        const Vowel* vowels[] = { &vowelA, &vowelO, &vowelA, &vowelE, &vowelO, &vowelI };
        for (int i = 0; i < 6; ++i)
        {
            const double f = hz (notes[i]);
            tl.addNote (notes[i], sungVowel (sr, steadyTrack (sr, 0.7, f), f, *vowels[i], -12.0, -26.0,
                                             50u + static_cast<std::uint64_t> (i)));
            tl.rest (0.25);
        }
        return tl.finish();
    }

    Scenario highRange (double sr)
    {
        Timeline tl ("high_range", sr);
        tl.rest (0.25);
        const int notes[] = { 74, 77, 79, 81, 83, 76 }; // 587 .. 988 Hz
        const Vowel* vowels[] = { &vowelI, &vowelA, &vowelE, &vowelA, &vowelO, &vowelI };
        for (int i = 0; i < 6; ++i)
        {
            const double f = hz (notes[i]);
            tl.addNote (notes[i], sungVowel (sr, steadyTrack (sr, 0.5, f), f, *vowels[i], -12.0, -26.0,
                                             60u + static_cast<std::uint64_t> (i)));
            tl.rest (0.2);
        }
        return tl.finish();
    }

    Scenario fast16thsLegato (double sr)
    {
        // 16ths at 120 bpm (125 ms per note), one breath, pitch steps only.
        Timeline tl ("fast_16ths_legato", sr);
        tl.rest (0.25);
        const int notes[] = { 60, 62, 64, 65, 67, 65, 64, 62, 60, 64, 67, 72, 67, 64, 60, 55 };
        const auto per = secondsToSamples (0.125, sr);
        std::vector<double> f0;
        std::vector<TruthNote> truth;
        for (int n : notes)
        {
            const double start = static_cast<double> (f0.size()) / sr;
            f0.insert (f0.end(), per, hz (n));
            truth.push_back ({ start, static_cast<double> (f0.size()) / sr, n });
        }
        auto sound = renderTrack (sr, f0, partials::voiceLike(), -12.0);
        applyEnvelope (sound, sr, voiceEnv);
        tl.addSound (sound, truth);
        tl.rest (0.3);
        return tl.finish();
    }

    Scenario fast16thsDetached (double sr)
    {
        // 16ths at 120 bpm: 100 ms tones with 25 ms gaps ("da-da-da" runs).
        Timeline tl ("fast_16ths_detached", sr);
        tl.rest (0.25);
        const int notes[] = { 60, 62, 64, 65, 67, 67, 65, 64, 62, 62, 60, 64, 67, 67, 72, 60 };
        for (int n : notes)
        {
            auto t = harmonicTone (sr, 0.1, hz (n), partials::voiceLike(), -12.0);
            applyEnvelope (t, sr, { 0.01, 0.0, 1.0, 0.015 });
            tl.addNote (n, t);
            tl.rest (0.025);
        }
        tl.rest (0.3);
        return tl.finish();
    }

    Scenario softRearticulation (double sr)
    {
        // One continuous tone per pitch with short level dips of only 6..10 dB
        // between syllables ("la-la-la" sung legato): each syllable is a note.
        Timeline tl ("soft_rearticulation", sr);
        tl.rest (0.25);
        const double dips[] = { 6.0, 7.0, 8.0, 9.0, 10.0 };
        for (int pitch : { 62, 57 })
        {
            const double syll = 0.3;
            const int count = 6;
            const double len = syll * count;
            auto t = harmonicTone (sr, len, hz (pitch), partials::voiceLike(), -12.0);
            std::vector<TruthNote> truth;
            for (int k = 0; k < count; ++k)
                truth.push_back ({ k * syll, (k + 1) * syll, pitch });
            // dips centred on the syllable boundaries, 40 ms wide (raised cosine)
            for (std::size_t i = 0; i < t.size(); ++i)
            {
                const double time = static_cast<double> (i) / sr;
                double gainDb = 0.0;
                for (int k = 1; k < count; ++k)
                {
                    const double d = (time - k * syll) / 0.02;
                    if (std::abs (d) < 1.0)
                        gainDb = -dips[(k - 1) % 5] * 0.5 * (1.0 + std::cos (3.141592653589793 * d));
                }
                t[i] = static_cast<float> (t[i] * std::pow (10.0, gainDb / 20.0));
            }
            applyEnvelope (t, sr, voiceEnv);
            tl.addSound (t, truth);
            tl.rest (0.3);
        }
        return tl.finish();
    }

    Scenario consonantOnsets (double sr)
    {
        // "ta": 20-40 ms of consonant noise (burst + aspiration, 2-7 kHz)
        // before each vowel. Truth onset = vowel start. The consonant must
        // not produce a (wrong) note of its own.
        Timeline tl ("consonant_onsets", sr);
        tl.rest (0.25);
        const int notes[] = { 60, 64, 67, 62, 57, 65 };
        const double consMs[] = { 20, 30, 40, 25, 35, 30 };
        for (int i = 0; i < 6; ++i)
        {
            auto cons = whiteNoise (sr, consMs[i] / 1000.0, -20.0, 80u + static_cast<std::uint64_t> (i));
            auto hp = Biquad::highPass (sr, 2000.0);
            auto lp = Biquad::lowPass (sr, std::min (7000.0, 0.45 * sr));
            hp.processInPlace (cons);
            lp.processInPlace (cons);
            normalisePeak (cons, -18.0);
            for (std::size_t k = 0; k < cons.size(); ++k) // burst then decaying aspiration
                cons[k] *= static_cast<float> (std::exp (-static_cast<double> (k) / (0.012 * sr)) * 0.7 + 0.3);
            append (tl.audio(), cons);
            const double f = hz (notes[i]);
            tl.addNote (notes[i], sungVowel (sr, steadyTrack (sr, 0.45, f), f, vowelA, -12.0, -26.0,
                                             90u + static_cast<std::uint64_t> (i)));
            tl.rest (0.25);
        }
        return tl.finish();
    }

    Scenario pitchDrift (double sr)
    {
        // Sustained notes whose pitch wanders slowly by up to +-30 cents.
        Timeline tl ("pitch_drift", sr);
        tl.rest (0.25);
        const int notes[] = { 60, 64, 67, 62, 65, 69 };
        for (int i = 0; i < 6; ++i)
        {
            const double target = hz (notes[i]);
            std::vector<double> f (secondsToSamples (0.8, sr));
            for (std::size_t k = 0; k < f.size(); ++k)
            {
                const double t = static_cast<double> (k) / sr;
                f[k] = target * std::exp2 (30.0 * std::sin (2.0 * 3.141592653589793 * 0.8 * t + 0.9 * i) / 1200.0);
            }
            tl.addNote (notes[i], sungVowel (sr, f, target, i % 2 == 0 ? vowelA : vowelO, -12.0, -26.0,
                                             110u + static_cast<std::uint64_t> (i)));
            tl.rest (0.25);
        }
        return tl.finish();
    }

    Scenario tremoloVibrato (double sr)
    {
        // Vibrato (+-40 cents, 5.5 Hz) with the amplitude modulation real
        // voices add to it (+-3 dB, i.e. 6 dB peak to peak): one note each.
        Timeline tl ("tremolo_vibrato", sr);
        tl.rest (0.25);
        int i = 0;
        for (int n : { 60, 64, 57 })
        {
            auto t = vibratoTone (sr, 1.5, hz (n), 40.0, 5.5, partials::voiceLike(), -12.0);
            for (std::size_t k = 0; k < t.size(); ++k)
            {
                const double time = static_cast<double> (k) / sr;
                const double db = 3.0 * std::sin (2.0 * 3.141592653589793 * 5.5 * time + 0.7 * i);
                t[k] = static_cast<float> (t[k] * std::pow (10.0, db / 20.0));
            }
            applyEnvelope (t, sr, voiceEnv);
            tl.addNote (n, t);
            tl.rest (0.3);
            ++i;
        }
        return tl.finish();
    }

    Scenario scoopsAndDrift (double sr)
    {
        // Each note starts 150 cents flat and slides up to the target in
        // 60 ms (a "scoop"), then drifts slowly by up to +-30 cents. One note
        // at the target pitch is expected per sung note.
        Timeline tl ("scoop_ups", sr);
        tl.rest (0.25);
        const int notes[] = { 60, 64, 67, 62, 65, 69 };
        for (int i = 0; i < 6; ++i)
        {
            const double target = hz (notes[i]);
            const double seconds = 0.7;
            std::vector<double> f (secondsToSamples (seconds, sr));
            const double phase = 0.9 * i;
            for (std::size_t k = 0; k < f.size(); ++k)
            {
                const double t = static_cast<double> (k) / sr;
                const double scoop = t < 0.06 ? -150.0 * (1.0 - t / 0.06) : 0.0;
                const double drift = 30.0 * std::sin (2.0 * 3.141592653589793 * 0.8 * t + phase);
                f[k] = target * std::exp2 ((scoop + drift) / 1200.0);
            }
            tl.addNote (notes[i], sungVowel (sr, f, target, i % 2 == 0 ? vowelA : vowelO, -12.0, -26.0,
                                             100u + static_cast<std::uint64_t> (i)));
            tl.rest (0.25);
        }
        return tl.finish();
    }

    Scenario humBackground (double sr, double humHz, const char* name)
    {
        // Mains hum (fundamental + 2nd/3rd harmonic) at -40 dBFS peak under
        // the whole take, silences included.
        Timeline tl (name, sr);
        tl.rest (0.4);
        for (int n : { 57, 60, 64, 67, 62 })
        {
            tl.addNote (n, sungNote (sr, n, 0.6, -12.0));
            tl.rest (0.35);
        }
        auto hum = harmonicTone (sr, tl.now(), humHz, { 1.0f, 0.5f, 0.3f }, -40.0);
        mixInto (tl.audio(), hum, 0);
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
            { { "voice_formants", C::Melody, "vowels a/o/i/e with formants, breath noise, light vibrato" }, voiceFormants },
            { { "low_male_range", C::Melody, "low voice 82-131 Hz (E2..C3), formant vowels, weak fundamentals" }, lowMaleRange },
            { { "high_range", C::Melody, "high voice 587-988 Hz (D5..B5), formant vowels" }, highRange },
            { { "fast_16ths_legato", C::Melody, "16 legato notes of 125 ms (16ths at 120 bpm), one breath" }, fast16thsLegato },
            { { "fast_16ths_detached", C::Melody, "16 notes of 100 ms with 25 ms gaps, repeated pitches included" }, fast16thsDetached },
            { { "soft_rearticulation", C::Melody, "legato syllables on one pitch separated by 6-10 dB dips only" }, softRearticulation },
            { { "consonant_onsets", C::Melody, "'ta': 20-40 ms consonant noise before each vowel; truth = vowel" }, consonantOnsets },
            { { "pitch_drift", C::Melody, "sustained notes drifting slowly by +-30 cents" }, pitchDrift },
            { { "tremolo_vibrato", C::Melody, "+-40 cent vibrato with +-3 dB amplitude modulation; one note each" }, tremoloVibrato },
            { { "scoop_ups", C::Melody, "notes scooping up 150 cents in 60 ms, then +-30 cent drift; one note each" }, scoopsAndDrift },
            { { "hum_50hz", C::Melody, "sustained notes over 50 Hz mains hum at -40 dBFS (also in the gaps)" }, [] (double sr) { return humBackground (sr, 50.0, "hum_50hz"); } },
            { { "hum_60hz", C::Melody, "sustained notes over 60 Hz mains hum at -40 dBFS (also in the gaps)" }, [] (double sr) { return humBackground (sr, 60.0, "hum_60hz"); } },
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
