// Real-time safety: no heap allocation on the audio thread.
//
// This executable replaces the global operator new/delete with counting
// versions, so it is its own target (the override would otherwise affect
// every test). After prepare(), the audio-thread calls of both engines -
// process(), setParams(), flush(), reset(), getRecentHits() and, for
// beatbox, calibration capture and the hand-over of a loaded calibration -
// must not allocate or free at all.
//
// Plain main() rather than Catch2: the test framework itself allocates, so
// nothing but engine code may run while counting.

#include "m2m/beatbox/BeatboxEngine.h"
#include "m2m/melody/MelodyEngine.h"
#include "m2m/melody/Stability.h"
#include "m2m/test/Drums.h"
#include "m2m/test/Scenarios.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace
{
    std::atomic<bool> counting { false };
    std::atomic<long> allocations { 0 }, deallocations { 0 };

    void* countedAlloc (std::size_t n)
    {
        if (counting.load (std::memory_order_relaxed))
            allocations.fetch_add (1, std::memory_order_relaxed);
        if (void* p = std::malloc (n == 0 ? 1 : n))
            return p;
        throw std::bad_alloc();
    }

    void countedFree (void* p) noexcept
    {
        if (p != nullptr && counting.load (std::memory_order_relaxed))
            deallocations.fetch_add (1, std::memory_order_relaxed);
        std::free (p);
    }
}

void* operator new (std::size_t n) { return countedAlloc (n); }
void* operator new[] (std::size_t n) { return countedAlloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    try { return countedAlloc (n); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    try { return countedAlloc (n); } catch (...) { return nullptr; }
}
void operator delete (void* p) noexcept { countedFree (p); }
void operator delete[] (void* p) noexcept { countedFree (p); }
void operator delete (void* p, std::size_t) noexcept { countedFree (p); }
void operator delete[] (void* p, std::size_t) noexcept { countedFree (p); }

namespace
{
    int failures = 0;

    /** Runs fn with counting on; reports any allocation. */
    template <typename Fn>
    void expectNoAllocation (const char* what, Fn&& fn)
    {
        allocations = 0;
        deallocations = 0;
        counting = true;
        fn();
        counting = false;
        const long a = allocations.load(), d = deallocations.load();
        if (a != 0 || d != 0)
        {
            ++failures;
            std::printf ("FAIL  %-60s %ld allocations, %ld frees\n", what, a, d);
        }
        else
        {
            std::printf ("ok    %s\n", what);
        }
    }

    void feed (m2m::IEngine& e, const std::vector<float>& audio, int block, m2m::MidiEventBuffer& buf)
    {
        for (std::size_t i = 0; i < audio.size(); i += static_cast<std::size_t> (block))
        {
            buf.clear();
            const auto n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), audio.size() - i));
            e.process (audio.data() + i, n, buf);
        }
    }

    void melodyChecks (double sr)
    {
        using namespace m2m;
        // Rendered before counting (rendering allocates).
        const auto a = test::makeScenario ("chromatic_scale", sr)->audio;
        const auto b = test::makeScenario ("vibrato", sr)->audio;
        const auto c = test::makeScenario ("repeated_same_pitch", sr)->audio;
        const std::vector<float> bHalf (b.begin(), b.begin() + static_cast<std::ptrdiff_t> (b.size() / 2));
        const auto u = test::makeScenario ("unsteady_melody_detached", sr)->audio;
        const auto l = test::makeScenario ("unsteady_melody_legato", sr)->audio;
        const std::vector<float> uHalf (u.begin(), u.begin() + static_cast<std::ptrdiff_t> (u.size() / 2));

        MelodyEngine e;
        e.prepare (sr, 512);
        MidiEventBuffer buf;
        buf.reserve (256);

        MelodyParams yin = e.getParams();
        yin.detector = PitchAlgorithm::Yin;
        MelodyParams mpm = e.getParams();
        mpm.detector = PitchAlgorithm::Mpm;
        MelodyParams odd = mpm;
        odd.minHz = 120.0f;
        odd.maxHz = 700.0f;
        odd.medianFrames = 7;
        odd.smoothingMs = 15.0f;
        odd.scale = Scale::Major;
        odd.transpose = 12;
        odd.channel = 3;

        const std::string sfx = " @ " + std::to_string (static_cast<int> (sr)) + " Hz";
        expectNoAllocation (("melody: process (mpm, block 512)" + sfx).c_str(), [&] { feed (e, a, 512, buf); });
        expectNoAllocation (("melody: setParams (yin) + process (block 64)" + sfx).c_str(), [&]
        {
            e.setParams (yin);
            feed (e, b, 64, buf);
        });
        expectNoAllocation (("melody: setParams mid-note (range/median/scale) + flush" + sfx).c_str(), [&]
        {
            feed (e, bHalf, 128, buf);
            e.setParams (odd);
            feed (e, c, 1, buf);
            buf.clear();
            e.flush (buf, 0);
        });
        MelodyParams lock = mpm;
        lock.pitchFollow = PitchFollow::Lock;
        MelodyParams lockTight = applyStability (lock, 1.0f);
        lockTight.scale = Scale::Major;
        expectNoAllocation (("melody: Lock + process (settle, retrigger, big leap; block 256)" + sfx).c_str(), [&]
        {
            e.setParams (lock);
            feed (e, u, 256, buf);
            feed (e, l, 31, buf);
        });
        expectNoAllocation (("melody: Free <-> Lock + applyStability mid-note + flush" + sfx).c_str(), [&]
        {
            feed (e, uHalf, 128, buf);
            e.setParams (applyStability (mpm, 0.9f));
            feed (e, l, 64, buf);
            e.setParams (lockTight);
            feed (e, uHalf, 512, buf);
            buf.clear();
            e.flush (buf, 0);
        });
        expectNoAllocation (("melody: reset + process (block 4096)" + sfx).c_str(), [&]
        {
            e.reset();
            e.setParams (mpm);
            feed (e, c, 4096, buf);
            buf.clear();
            e.flush (buf);
        });
    }

    void beatboxChecks (double sr)
    {
        using namespace m2m;
        const auto beat = test::makeScenario ("basic_beat_120bpm", sr)->audio;
        const auto kicks = test::makeScenario ("kick_only", sr)->audio;
        const auto hats = test::makeScenario ("closed_hat_only", sr)->audio;
        const auto voice = test::makeScenario ("voice_beat_100bpm", sr)->audio;
        const std::vector<float> beatQuarter (beat.begin(), beat.begin() + static_cast<std::ptrdiff_t> (beat.size() / 4));

        BeatboxEngine e;
        e.prepare (sr, 512);
        MidiEventBuffer buf;
        buf.reserve (256);
        HitInfo hits[64];

        BeatboxParams p = e.getParams();
        BeatboxParams cal = p;
        cal.classifierMode = ClassifierMode::Calibrated;
        cal.analysisWindowMs = 30.0f;
        cal.gateMs = 500.0f;

        const std::string sfx = " @ " + std::to_string (static_cast<int> (sr)) + " Hz";
        expectNoAllocation (("beatbox: process + getRecentHits (block 256)" + sfx).c_str(), [&]
        {
            for (std::size_t i = 0; i < beat.size(); i += 256)
            {
                buf.clear();
                e.process (beat.data() + i, static_cast<int> (std::min<std::size_t> (256, beat.size() - i)), buf);
                e.getRecentHits (hits, 64);
            }
        });

        // Calibration commands are enqueued from the message thread (not
        // counted: they do not allocate either, but are not audio thread);
        // the capture itself happens inside process().
        e.beginCalibration (DrumClass::Kick);
        expectNoAllocation (("beatbox: calibration capture (kick) in process" + sfx).c_str(), [&] { feed (e, kicks, 128, buf); });
        e.endCalibration();
        e.beginCalibration (DrumClass::ClosedHat);
        expectNoAllocation (("beatbox: calibration capture (hat) in process" + sfx).c_str(), [&] { feed (e, hats, 128, buf); });
        e.endCalibration();
        e.cancelCalibration();
        e.clearCalibration (DrumClass::Snare);

        expectNoAllocation (("beatbox: setParams (calibrated) + process (block 1)" + sfx).c_str(), [&]
        {
            e.setParams (cal);
            feed (e, beatQuarter, 1, buf);
        });

        // A calibration loaded on the message thread (allocates there) is
        // swapped in by the audio thread.
        const std::string text = e.getCalibrationString();
        BeatboxEngine other;
        other.prepare (sr, 512);
        if (! other.setCalibrationString (text))
        {
            ++failures;
            std::printf ("FAIL  could not load calibration string\n");
        }
        other.setParams (cal);
        expectNoAllocation (("beatbox: calibration hand-over + process (block 8192)" + sfx).c_str(), [&]
        {
            feed (other, voice, 8192, buf);
            other.getRecentHits (hits, 64);
        });

        expectNoAllocation (("beatbox: flush + reset + process" + sfx).c_str(), [&]
        {
            buf.clear();
            e.flush (buf, 0);
            e.reset();
            e.setParams (p);
            feed (e, voice, 333, buf);
            e.getRecentHits (hits, 64);
            buf.clear();
            e.flush (buf, 5);
        });
    }
}

int main()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        melodyChecks (sr);
        beatboxChecks (sr);
    }

    // The counter itself must see an allocation.
    allocations = 0;
    counting = true;
    {
        auto* volatile p = new int (3);
        delete p;
    }
    counting = false;
    if (allocations.load() == 0)
    {
        std::printf ("FAIL  self-test: the allocation counter did not count\n");
        ++failures;
    }

    if (failures != 0)
    {
        std::printf ("%d failure(s)\n", failures);
        return 1;
    }
    std::printf ("all real-time checks passed\n");
    return 0;
}
