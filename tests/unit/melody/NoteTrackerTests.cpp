#include "m2m/melody/NoteTracker.h"
#include "m2m/melody/RetriggerDetector.h"
#include "MelodySignals.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace m2m;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int hop = 128;

    struct Recorder
    {
        NoteTracker tracker;
        std::vector<TrackerEvent> events;
        std::int64_t t = 0;

        Recorder (NoteTrackerParams p = {})
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

    int framesFor (float ms) { return static_cast<int> (ms * 0.001 * sr / hop) + 1; }
}

TEST_CASE ("NoteTracker: Idle -> Pending -> Active after stability and attack window", "[melody][tracker]")
{
    Recorder r;
    r.frame (60.1f);
    CHECK (r.tracker.state() == NoteTracker::State::Pending);
    r.frames (framesFor (10.0f), 60.1f);
    CHECK (r.tracker.state() == NoteTracker::State::Active);
    CHECK (r.tracker.activeNote() == 60);
    REQUIRE (r.ons() == std::vector<int> { 60 });

    // Low confidence never starts a note.
    Recorder q;
    for (int i = 0; i < 50; ++i)
        q.frame (60.0f, -12.0f, false, 0.3f);
    CHECK (q.events.empty());

    // Level below the on-threshold never starts a note.
    Recorder z;
    z.frames (50, 60.0f, -50.0f);
    CHECK (z.events.empty());
}

TEST_CASE ("NoteTracker: drift inside the hysteresis band keeps the note", "[melody][tracker]")
{
    Recorder r;
    r.frames (20, 64.0f);
    for (int i = 0; i < 200; ++i)
        r.frame (64.0f + static_cast<float> ((i % 7) - 3) * 0.19f); // +/- 57 cents
    r.flush();
    CHECK (r.ons() == std::vector<int> { 64 });
    CHECK (r.invariants());
}

TEST_CASE ("NoteTracker: legato change is off+on at the same time, off first", "[melody][tracker]")
{
    Recorder r;
    r.frames (30, 60.0f);
    r.frames (30, 62.0f);
    r.flush();
    REQUIRE (r.events.size() == 4);
    CHECK_FALSE (r.events[1].noteOn);
    CHECK (r.events[1].note == 60);
    CHECK (r.events[2].noteOn);
    CHECK (r.events[2].note == 62);
    CHECK (r.events[1].time == r.events[2].time);

    // A blip shorter than minStableMs does not change the note.
    Recorder b;
    b.frames (30, 60.0f);
    b.frames (2, 63.0f);
    b.frames (30, 60.0f);
    b.flush();
    CHECK (b.ons() == std::vector<int> { 60 });
}

TEST_CASE ("NoteTracker: release after releaseMs; min note duration honoured", "[melody][tracker]")
{
    NoteTrackerParams p;
    p.releaseMs = 30.0f;
    p.minNoteMs = 100.0f;
    Recorder r (p);
    r.frames (6, 60.0f);  // note on after ~5 frames
    REQUIRE (r.tracker.state() == NoteTracker::State::Active);
    const auto onTime = r.events[0].time;
    r.silence (100);
    REQUIRE (r.events.size() == 2);
    CHECK (r.events[1].time - onTime >= static_cast<std::int64_t> (0.1 * sr));

    Recorder s;
    s.frames (40, 60.0f);
    const auto lostAt = s.t + hop;
    s.silence (40);
    REQUIRE (s.events.size() == 2);
    const auto releaseDelay = s.events[1].time - lostAt;
    CHECK (releaseDelay >= static_cast<std::int64_t> (0.03 * sr) - hop);
    CHECK (releaseDelay <= static_cast<std::int64_t> (0.03 * sr) + hop);
}

TEST_CASE ("NoteTracker: onset retriggers the same note; can be disabled", "[melody][tracker]")
{
    for (const bool enabled : { true, false })
    {
        NoteTrackerParams p;
        p.retriggerEnabled = enabled;
        Recorder r (p);
        r.frames (40, 57.0f);
        r.frames (3, 57.0f, -18.0f); // the level dips between the syllables ...
        r.frame (57.0f, -12.0f, true); // ... and rises again
        r.frames (40, 57.0f);
        r.flush();
        CHECK (r.invariants());
        CHECK (r.ons() == (enabled ? std::vector<int> { 57, 57 } : std::vector<int> { 57 }));
    }
}

TEST_CASE ("NoteTracker: an onset during the note's own rising attack is no retrigger", "[melody][tracker]")
{
    // e.g. a vowel swelling after a consonant: the level only ever rises.
    Recorder r;
    for (int i = 0; i < 30; ++i)
        r.frame (57.0f, -40.0f + static_cast<float> (i), i == 12);
    r.frames (40, 57.0f, -10.0f);
    r.flush();
    CHECK (r.invariants());
    CHECK (r.ons() == std::vector<int> { 57 });
}

TEST_CASE ("NoteTracker: scale snap picks allowed notes", "[melody][tracker]")
{
    Recorder r;
    r.tracker.setScale (Scale::Major, 0);
    r.frames (30, 61.3f);
    r.flush();
    CHECK (r.ons() == std::vector<int> { 62 });
}

TEST_CASE ("NoteTracker: invariants hold for random input", "[melody][tracker]")
{
    melodytest::Rng rng (2024);
    for (int trial = 0; trial < 20; ++trial)
    {
        NoteTrackerParams p;
        p.minStableMs = static_cast<float> (5 + trial);
        p.minNoteMs = static_cast<float> (trial * 5);
        p.releaseMs = static_cast<float> (10 + trial * 3);
        Recorder r (p);
        if (trial % 3 == 0)
            r.tracker.setScale (Scale::MinorPentatonic, trial % 12);

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

TEST_CASE ("RetriggerDetector: rise, refractory and level floor", "[melody][tracker]")
{
    RetriggerDetector d;
    d.prepare (hop / sr);
    d.setParams (12.0f, 40.0f, 60.0f, -45.0f);

    for (int i = 0; i < 20; ++i)
        CHECK_FALSE (d.process (-12.0f));          // steady
    for (int i = 0; i < 10; ++i)
        d.process (-30.0f);                         // dip
    CHECK (d.process (-12.0f));                     // 18 dB rise
    for (int i = 0; i < 3; ++i)
        d.process (-30.0f);
    CHECK_FALSE (d.process (-12.0f));               // inside refractory

    d.reset();
    for (int i = 0; i < 10; ++i)
        d.process (-90.0f);
    CHECK_FALSE (d.process (-50.0f));               // below the level floor

    CHECK (RetriggerDetector::riseDbForSensitivity (0.0f) > RetriggerDetector::riseDbForSensitivity (1.0f));
}
