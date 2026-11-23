// Note lock ("one note per syllable"): NoteTracker rules and MelodyEngine
// behaviour with PitchFollow::Lock.

#include "m2m/melody/MelodyEngine.h"
#include "m2m/melody/NoteTracker.h"
#include "m2m/test/Scenarios.h"
#include "MelodySignals.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <vector>

using namespace m2m;
using namespace m2m::melodytest;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int hop = 128;
    constexpr double hopMs = 1000.0 * hop / sr;

    NoteTrackerParams lockParams()
    {
        NoteTrackerParams p;
        p.lock = true;
        p.lockSettleMs = 15.0f;
        p.lockLeapSemitones = 1.2f;
        p.lockLeapMs = 20.0f;
        return p;
    }

    struct Recorder
    {
        NoteTracker tracker;
        std::vector<TrackerEvent> events;
        std::int64_t t = 0;

        explicit Recorder (NoteTrackerParams p = lockParams())
        {
            tracker.prepare (sr);
            tracker.setParams (p);
        }

        void frame (float pitch, float levelDb = -12.0f, bool onset = false, float conf = 0.95f, bool voiced = true)
        {
            t += hop;
            TrackerFrame f;
            f.time = t;
            f.pitch = pitch;
            f.voiced = voiced;
            f.confidence = conf;
            f.levelDb = levelDb;
            f.peakDb = levelDb;
            f.onset = onset;
            TrackerEvent ev[NoteTracker::maxEventsPerFrame];
            const int n = tracker.process (f, ev);
            REQUIRE (n <= NoteTracker::maxEventsPerFrame);
            events.insert (events.end(), ev, ev + n);
        }

        void frames (int count, float pitch, float levelDb = -12.0f)
        {
            for (int i = 0; i < count; ++i)
                frame (pitch, levelDb);
        }

        /** Frames whose pitch is f(ms since this call). */
        template <typename Fn>
        void track (double ms, Fn&& pitchAt)
        {
            const int n = static_cast<int> (ms / hopMs);
            for (int i = 0; i < n; ++i)
                frame (static_cast<float> (pitchAt (i * hopMs)));
        }

        void silence (int count) { for (int i = 0; i < count; ++i) frame (0.0f, -120.0f, false, 0.0f, false); }

        void flush()
        {
            TrackerEvent ev[NoteTracker::maxEventsPerFrame];
            const int n = tracker.flush (t, ev);
            events.insert (events.end(), ev, ev + n);
        }

        std::vector<int> ons() const
        {
            std::vector<int> v;
            for (const auto& e : events)
                if (e.noteOn)
                    v.push_back (e.note);
            return v;
        }

        std::int64_t firstOnTime() const
        {
            for (const auto& e : events)
                if (e.noteOn)
                    return e.time;
            return -1;
        }

        bool invariants() const
        {
            int sounding = -1;
            std::int64_t last = -1;
            for (const auto& e : events)
            {
                if (e.time < last)
                    return false;
                last = e.time;
                if (e.noteOn)
                {
                    if (sounding >= 0)
                        return false;
                    sounding = e.note;
                }
                else
                {
                    if (sounding != e.note)
                        return false;
                    sounding = -1;
                }
            }
            return sounding < 0;
        }
    };

    /** A 150-cent scoop over 60 ms into `target`, then held. */
    double scoop (double ms, int target)
    {
        return target - (ms < 60.0 ? 1.5 * (1.0 - ms / 60.0) : 0.0);
    }
}

TEST_CASE ("Note lock: a steady pitch is chosen after the settle window", "[melody][lock]")
{
    Recorder r;
    r.frame (60.1f);
    CHECK (r.tracker.state() == NoteTracker::State::Pending);
    r.frames (20, 60.1f);
    r.flush();
    CHECK (r.ons() == std::vector<int> { 60 });
    // lockSettleMs (15) after the first frame, within one hop.
    const auto delay = r.firstOnTime() - hop;
    CHECK (delay >= static_cast<std::int64_t> (0.015 * sr));
    CHECK (delay <= static_cast<std::int64_t> (0.015 * sr) + hop);
}

TEST_CASE ("Note lock: a scoop does not choose the note (Free emits the scoop's note first)", "[melody][lock]")
{
    Recorder lock;
    lock.track (400.0, [] (double ms) { return scoop (ms, 60); });
    lock.flush();
    CHECK (lock.ons() == std::vector<int> { 60 });
    CHECK (lock.invariants());

    Recorder freeMode (NoteTrackerParams {});
    freeMode.track (400.0, [] (double ms) { return scoop (ms, 60); });
    freeMode.flush();
    CHECK (freeMode.ons().size() >= 2); // the known scoop_ups limitation of Free
    CHECK (freeMode.ons().front() == 59);

    // A fall from above settles the same way.
    Recorder fall;
    fall.track (400.0, [] (double ms) { return 64.0 + (ms < 50.0 ? 0.9 * (1.0 - ms / 50.0) : 0.0); });
    fall.flush();
    CHECK (fall.ons() == std::vector<int> { 64 });
}

TEST_CASE ("Note lock: pitch movement never changes a held note", "[melody][lock]")
{
    // Slow wander +-120 cents (the leap rule measures from a moving average of the held pitch) plus
    // fast jitter, for 3 s: Free would change notes, Lock keeps one.
    const auto wander = [] (double ms)
    {
        const double s = ms / 1000.0;
        const double w = 1.2 * std::min (1.0, s / 0.3) * std::sin (2.0 * std::numbers::pi * 0.7 * s);
        return 64.0 + w + 0.15 * std::sin (2.0 * std::numbers::pi * 9.0 * s);
    };
    Recorder lock;
    lock.track (3000.0, wander);
    lock.flush();
    CHECK (lock.ons() == std::vector<int> { 64 });

    Recorder freeMode (NoteTrackerParams {});
    freeMode.track (3000.0, wander);
    freeMode.flush();
    CHECK (freeMode.ons().size() > 1);

    // With the big-leap rule off, even a held 3-semitone move keeps the note.
    auto p = lockParams();
    p.lockLeapSemitones = 0.0f;
    Recorder noLeap (p);
    noLeap.frames (30, 60.0f);
    noLeap.frames (200, 63.0f);
    noLeap.flush();
    CHECK (noLeap.ons() == std::vector<int> { 60 });
}

TEST_CASE ("Note lock: a re-articulation starts a new note chosen afresh", "[melody][lock]")
{
    Recorder r;
    r.frames (40, 60.0f);
    // Level dip, then a re-attack whose pitch scoops into 62 (a semitone
    // step: below the leap threshold, so only the re-articulation can do it).
    r.frames (3, 60.5f, -20.0f);
    r.frame (61.0f, -12.0f, true);
    r.track (300.0, [] (double ms) { return 62.0 - (ms < 40.0 ? 0.8 * (1.0 - ms / 40.0) : 0.0); });
    // Same pitch again ("da-da").
    r.frames (3, 62.0f, -20.0f);
    r.frame (62.0f, -12.0f, true);
    r.frames (40, 62.0f);
    r.flush();
    CHECK (r.invariants());
    CHECK (r.ons() == std::vector<int> { 60, 62, 62 });

    // Without retrigger the note is held through everything.
    auto p = lockParams();
    p.retriggerEnabled = false;
    Recorder q (p);
    q.frames (40, 60.0f);
    q.frames (3, 60.0f, -20.0f);
    q.frame (61.0f, -12.0f, true);
    q.frames (40, 61.0f);
    q.flush();
    CHECK (q.ons() == std::vector<int> { 60 });
}

TEST_CASE ("Note lock: big-leap escape for wide legato intervals only", "[melody][lock]")
{
    // A whole tone up, a fourth up, a semitone down: only jumps beyond 1.2
    // semitones change the note, at the same time (off first).
    Recorder r;
    r.frames (60, 60.0f);
    r.track (300.0, [] (double ms) { return 62.0 - (ms < 30.0 ? 2.0 * (1.0 - ms / 30.0) : 0.0); });
    r.frames (100, 67.0f);
    r.frames (100, 66.0f);
    r.flush();
    CHECK (r.invariants());
    CHECK (r.ons() == std::vector<int> { 60, 62, 67 });
    for (std::size_t i = 1; i + 1 < r.events.size(); ++i)
        if (r.events[i].noteOn)
        {
            CHECK_FALSE (r.events[i - 1].noteOn);
            CHECK (r.events[i - 1].time == r.events[i].time);
        }

    // A short excursion (shorter than lockLeapMs) is ignored.
    Recorder b;
    b.frames (60, 60.0f);
    b.frames (5, 64.0f); // ~13 ms
    b.frames (60, 60.0f);
    b.flush();
    CHECK (b.ons() == std::vector<int> { 60 });
}

TEST_CASE ("Note lock: wide vibrato gives one note, never a ping-pong", "[melody][lock]")
{
    // +-60 cent vibrato at 6 Hz (troughs and peaks 1.2 semitones apart).
    const auto vib = [] (double ms, double delayMs)
    {
        const double depth = 0.6 * std::clamp ((ms - delayMs) / 100.0, 0.0, 1.0);
        return 67.0 + depth * std::sin (2.0 * std::numbers::pi * 6.0 * (ms - delayMs) / 1000.0);
    };

    // Vibrato that starts after the onset (the usual case): the centre note.
    Recorder r;
    r.track (1500.0, [&] (double ms) { return vib (ms, 150.0); });
    r.flush();
    CHECK (r.ons() == std::vector<int> { 67 });

    // Vibrato from the very first frame: a peak can look settled, so the
    // note may be the one next to the centre, but it is held: one note.
    Recorder v;
    v.track (1500.0, [&] (double ms) { return vib (ms, -100.0); });
    v.flush();
    REQUIRE (v.ons().size() == 1);
    CHECK (std::abs (v.ons()[0] - 67) <= 1);
    CHECK (v.firstOnTime() - hop <= static_cast<std::int64_t> (0.075 * sr) + hop);
}

TEST_CASE ("Note lock: invariants hold for random input", "[melody][lock]")
{
    melodytest::Rng rng (77);
    for (int trial = 0; trial < 20; ++trial)
    {
        auto p = lockParams();
        p.lockSettleMs = static_cast<float> (5 + trial * 4);
        p.lockLeapSemitones = static_cast<float> (trial % 4) * 0.8f;
        p.lockLeapMs = static_cast<float> (trial * 3);
        p.minNoteMs = static_cast<float> (trial * 5);
        p.retriggerEnabled = trial % 5 != 0;
        Recorder r (p);
        if (trial % 3 == 0)
            r.tracker.setScale (Scale::Blues, trial % 12);

        float pitch = 60.0f;
        for (int i = 0; i < 5000; ++i)
        {
            if (rng.bipolar() > 0.9)
                pitch = static_cast<float> (50.0 + 20.0 * (rng.bipolar() + 1.0));
            pitch += static_cast<float> (0.2 * rng.bipolar());
            const float level = static_cast<float> (-30.0 + 30.0 * rng.bipolar());
            const bool onset = rng.bipolar() > 0.95;
            const float conf = static_cast<float> (0.5 * (rng.bipolar() + 1.0));
            r.frame (pitch, level, onset, conf, rng.bipolar() > -0.8);
        }
        r.flush();
        CAPTURE (trial);
        CHECK (r.invariants());
    }
}

//==============================================================================
namespace
{
    MelodyParams lockEngineParams()
    {
        MelodyParams p;
        p.pitchFollow = PitchFollow::Lock;
        return p;
    }

    std::vector<AbsEvent> runEngineOn (const std::vector<float>& sig, const MelodyParams& p, int block)
    {
        MelodyEngine e;
        e.setParams (p);
        e.prepare (sr, 2048);
        return runEngine (e, sig, block);
    }
}

TEST_CASE ("MelodyEngine Lock: output is identical for any block size", "[melody][lock]")
{
    const auto sc = test::makeScenario ("unsteady_melody_detached", sr);
    REQUIRE (sc.has_value());
    const auto p = lockEngineParams();
    const auto reference = runEngineOn (sc->audio, p, 128);
    REQUIRE (noteOns (reference).size() >= 10);
    CHECK (monophonicInvariantsHold (reference));
    for (int block : { 1, 37, 256, 441, 4096 })
    {
        CAPTURE (block);
        CHECK (runEngineOn (sc->audio, p, block) == reference);
    }
}

TEST_CASE ("MelodyEngine Lock: unsteady singer gives exactly the intended notes", "[melody][lock]")
{
    for (const char* name : { "unsteady_sustained", "unsteady_melody_detached" })
    {
        const auto sc = test::makeScenario (name, sr);
        REQUIRE (sc.has_value());
        std::vector<int> truth;
        for (const auto& n : sc->notes)
            truth.push_back (n.midiNote);

        const auto lock = runEngineOn (sc->audio, lockEngineParams(), 256);
        const auto freeMode = runEngineOn (sc->audio, MelodyParams {}, 256);
        CAPTURE (name);
        CHECK (monophonicInvariantsHold (lock));
        CHECK (noteOns (lock) == truth);
        CHECK (noteOns (freeMode) != truth); // what the user reported
    }
}

TEST_CASE ("MelodyEngine Lock: scoops give no extra notes", "[melody][lock]")
{
    const auto sc = test::makeScenario ("scoop_ups", sr);
    REQUIRE (sc.has_value());
    const auto lock = runEngineOn (sc->audio, lockEngineParams(), 256);
    CHECK (monophonicInvariantsHold (lock));
    // One note per sung note (Free mode emits 12: the scoop's note first).
    CHECK (noteOns (lock).size() == sc->notes.size());
    int correct = 0;
    for (std::size_t i = 0; i < sc->notes.size() && i < noteOns (lock).size(); ++i)
        correct += noteOns (lock)[i] == sc->notes[i].midiNote ? 1 : 0;
    CHECK (correct >= 5);
    CHECK (noteOns (runEngineOn (sc->audio, MelodyParams {}, 256)).size() > sc->notes.size());
}

TEST_CASE ("MelodyEngine Lock: reported latency follows the settle window", "[melody][lock][latency]")
{
    MelodyEngine e;
    e.prepare (sr, 256);
    const int freeLatency = e.latencySamples();
    auto p = lockEngineParams();
    e.setParams (p);
    const int lockLatency = e.latencySamples();
    // Lock waits lockSettleMs (15) instead of minStableMs (10).
    CHECK (lockLatency - freeLatency == static_cast<int> (std::lround (0.005 * sr)));
    p.lockSettleMs = 30.0f;
    e.setParams (p);
    CHECK (e.latencySamples() - lockLatency == static_cast<int> (std::lround (0.015 * sr)));

    // Measured on clean tones the note-on comes within the reported latency (+ one hop).
    std::vector<double> delays;
    for (int n = 55; n <= 79; n += 4)
    {
        Synth s (sr);
        s.setHarmonics ({ 1.0, 0.6, 0.4, 0.25 });
        s.setRampMs (1.0);
        s.silence (0.1);
        const auto onset = static_cast<std::int64_t> (s.position());
        s.tone (0.3, midiToHz (n), -12.0f);
        s.silence (0.1);
        const auto ev = runEngineOn (s.buffer(), lockEngineParams(), 128);
        REQUIRE (! ev.empty());
        CHECK (ev[0].note == n);
        delays.push_back (static_cast<double> (ev[0].time - onset));
    }
    double mean = 0.0;
    for (double d : delays)
        mean += d / static_cast<double> (delays.size());
    CHECK (mean <= lockLatency + hop);
}

TEST_CASE ("MelodyEngine Lock: settings change mid-note keeps the invariants", "[melody][lock]")
{
    const auto sc = test::makeScenario ("unsteady_melody_legato", sr);
    REQUIRE (sc.has_value());
    MelodyEngine e;
    e.prepare (sr, 512);
    MidiEventBuffer buf;
    buf.reserve (64);
    std::vector<AbsEvent> ev;
    std::int64_t pos = 0;
    int k = 0;
    for (std::size_t i = 0; i < sc->audio.size(); i += 512, ++k)
    {
        if (k % 40 == 0)
        {
            auto p = e.getParams();
            p.pitchFollow = (k / 40) % 2 == 0 ? PitchFollow::Lock : PitchFollow::Free;
            p.lockSettleMs = 10.0f + static_cast<float> (k % 7) * 5.0f;
            e.setParams (p);
        }
        buf.clear();
        const int n = static_cast<int> (std::min<std::size_t> (512, sc->audio.size() - i));
        e.process (sc->audio.data() + i, n, buf);
        for (const auto& m : buf)
            ev.push_back ({ m.type, m.channel, m.note, m.velocity, pos + m.sampleOffset });
        pos += n;
    }
    buf.clear();
    e.flush (buf, 0);
    for (const auto& m : buf)
        ev.push_back ({ m.type, m.channel, m.note, m.velocity, pos });
    CHECK (monophonicInvariantsHold (ev));
    CHECK (noteOns (ev).size() >= 8);
}
