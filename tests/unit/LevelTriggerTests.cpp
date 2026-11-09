#include "m2m/trigger/LevelTrigger.h"
#include "TestSignals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using m2m::MidiEvent;

namespace
{
constexpr double sr = 48000.0;

struct AbsEvent
{
    MidiEvent::Type type;
    int note, channel, velocity;
    std::int64_t sample; // absolute position in the signal

    bool operator== (const AbsEvent&) const = default;
};

/** Run a whole signal through a fresh trigger in blocks of blockSize,
    converting block-relative offsets to absolute sample positions. */
std::vector<AbsEvent> run (const std::vector<float>& signal, int blockSize,
                           m2m::LevelTrigger::Params p = {})
{
    m2m::LevelTrigger trig;
    trig.prepare (sr);
    trig.setParams (p);

    m2m::MidiEventBuffer buf;
    buf.reserve (64);

    std::vector<AbsEvent> result;
    for (std::size_t pos = 0; pos < signal.size(); pos += static_cast<std::size_t> (blockSize))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize),
                                                               signal.size() - pos));
        buf.clear();
        trig.process (signal.data() + pos, n, buf);
        for (const auto& e : buf)
            result.push_back ({ e.type, e.note, e.channel, e.velocity,
                                static_cast<std::int64_t> (pos) + e.sampleOffset });
    }
    return result;
}

std::size_t countType (const std::vector<AbsEvent>& ev, MidiEvent::Type t)
{
    return static_cast<std::size_t> (std::count_if (ev.begin(), ev.end(),
                                                    [t] (const AbsEvent& e) { return e.type == t; }));
}
} // namespace

TEST_CASE ("LevelTrigger: silence produces no events")
{
    std::vector<float> sig;
    m2m::test::appendSilence (sig, sr, 1.0);
    REQUIRE (run (sig, 256).empty());
}

TEST_CASE ("LevelTrigger: quiet signal below threshold produces no events")
{
    std::vector<float> sig;
    m2m::test::appendSine (sig, sr, 1.0, 220.0, -45.0f);
    REQUIRE (run (sig, 256).empty());
}

TEST_CASE ("LevelTrigger: one burst gives exactly one note on/off pair")
{
    std::vector<float> sig;
    m2m::test::appendSilence (sig, sr, 0.25);
    m2m::test::appendSine (sig, sr, 0.5, 220.0, -12.0f);
    m2m::test::appendSilence (sig, sr, 0.5);

    const auto ev = run (sig, 256);
    REQUIRE (ev.size() == 2);
    REQUIRE (ev[0].type == MidiEvent::Type::NoteOn);
    REQUIRE (ev[0].note == 60);
    REQUIRE (ev[0].channel == 1);
    REQUIRE (ev[0].velocity == 100);
    REQUIRE (ev[1].type == MidiEvent::Type::NoteOff);
    REQUIRE (ev[1].velocity == 0);

    // Note-on within 2 ms of the burst start (sample-accurate, not block-quantised).
    const auto burstStart = static_cast<std::int64_t> (0.25 * sr);
    REQUIRE (ev[0].sample >= burstStart);
    REQUIRE (ev[0].sample - burstStart < static_cast<std::int64_t> (0.002 * sr));

    // Note-off after the burst ends, within the release window.
    const auto burstEnd = static_cast<std::int64_t> (0.75 * sr);
    REQUIRE (ev[1].sample >= burstEnd);
    REQUIRE (ev[1].sample - burstEnd < static_cast<std::int64_t> (0.2 * sr));
}

TEST_CASE ("LevelTrigger: output is identical for any host block size")
{
    std::vector<float> sig;
    for (int k = 0; k < 5; ++k)
    {
        m2m::test::appendSilence (sig, sr, 0.3);
        m2m::test::appendSine (sig, sr, 0.2, 180.0 + 40.0 * k, -10.0f - 3.0f * static_cast<float> (k));
    }
    m2m::test::appendSilence (sig, sr, 0.3);

    const auto reference = run (sig, 1);
    REQUIRE (reference.size() == 10);

    const int blockSize = GENERATE (7, 32, 64, 128, 441, 512, 1024, 4096);
    CAPTURE (blockSize);
    REQUIRE (run (sig, blockSize) == reference);
}

TEST_CASE ("LevelTrigger: hysteresis prevents chatter near the threshold")
{
    // Level wobbles between -28 and -33 dB around a -30 dB threshold with
    // 6 dB hysteresis: once on, it must stay on until real silence.
    std::vector<float> sig;
    for (int k = 0; k < 20; ++k)
    {
        m2m::test::appendSine (sig, sr, 0.05, 200.0, -28.0f);
        m2m::test::appendSine (sig, sr, 0.05, 200.0, -33.0f);
    }
    m2m::test::appendSilence (sig, sr, 0.5);

    const auto ev = run (sig, 128);
    REQUIRE (countType (ev, MidiEvent::Type::NoteOn) == 1);
    REQUIRE (countType (ev, MidiEvent::Type::NoteOff) == 1);
}

TEST_CASE ("LevelTrigger: minimum note length holds very short clicks")
{
    m2m::LevelTrigger::Params p;
    p.minNoteMs = 40.0f;
    p.releaseMs = 1.0f;

    std::vector<float> sig;
    m2m::test::appendSilence (sig, sr, 0.1);
    m2m::test::appendSine (sig, sr, 0.005, 1000.0, -6.0f); // 5 ms click
    m2m::test::appendSilence (sig, sr, 0.3);

    const auto ev = run (sig, 64, p);
    REQUIRE (ev.size() == 2);
    REQUIRE (ev[1].sample - ev[0].sample >= static_cast<std::int64_t> (0.040 * sr));
}

TEST_CASE ("LevelTrigger: note/channel/velocity parameters are used")
{
    m2m::LevelTrigger::Params p;
    p.note = 36;
    p.channel = 10;
    p.velocity = 127;

    std::vector<float> sig;
    m2m::test::appendSine (sig, sr, 0.2, 200.0, -6.0f);
    m2m::test::appendSilence (sig, sr, 0.3);

    const auto ev = run (sig, 256, p);
    REQUIRE (ev.size() == 2);
    REQUIRE (ev[0].note == 36);
    REQUIRE (ev[0].channel == 10);
    REQUIRE (ev[0].velocity == 127);
    REQUIRE (ev[1].note == 36);
    REQUIRE (ev[1].channel == 10);
}

TEST_CASE ("LevelTrigger: out-of-range parameters are clamped")
{
    m2m::LevelTrigger::Params p;
    p.note = 200;
    p.channel = 0;
    p.velocity = 0;

    std::vector<float> sig;
    m2m::test::appendSine (sig, sr, 0.2, 200.0, -6.0f);

    const auto ev = run (sig, 256, p);
    REQUIRE_FALSE (ev.empty());
    REQUIRE (ev[0].note == 127);
    REQUIRE (ev[0].channel == 1);
    REQUIRE (ev[0].velocity == 1);
}

TEST_CASE ("LevelTrigger: changing the note while held releases the old note first")
{
    m2m::LevelTrigger trig;
    trig.prepare (sr);
    m2m::MidiEventBuffer buf;
    buf.reserve (16);

    std::vector<float> loud;
    m2m::test::appendSine (loud, sr, 0.1, 200.0, -6.0f);

    trig.process (loud.data(), 2400, buf);
    REQUIRE (buf.size() == 1);
    REQUIRE (buf[0].note == 60);

    auto p = trig.getParams();
    p.note = 64;
    trig.setParams (p);

    buf.clear();
    trig.process (loud.data() + 2400, 2400, buf);
    REQUIRE (buf.size() == 2);
    REQUIRE (buf[0].type == MidiEvent::Type::NoteOff);
    REQUIRE (buf[0].note == 60);
    REQUIRE (buf[0].sampleOffset == 0);
    REQUIRE (buf[1].type == MidiEvent::Type::NoteOn);
    REQUIRE (buf[1].note == 64);
    REQUIRE (buf[1].sampleOffset == 0);
}

TEST_CASE ("LevelTrigger: flush releases a held note exactly once")
{
    m2m::LevelTrigger trig;
    trig.prepare (sr);
    m2m::MidiEventBuffer buf;
    buf.reserve (16);

    std::vector<float> loud;
    m2m::test::appendSine (loud, sr, 0.05, 200.0, -6.0f);
    trig.process (loud.data(), static_cast<int> (loud.size()), buf);
    REQUIRE (trig.isNoteOn());

    buf.clear();
    trig.flush (buf, 5);
    trig.flush (buf, 6);
    REQUIRE (buf.size() == 1);
    REQUIRE (buf[0].type == MidiEvent::Type::NoteOff);
    REQUIRE (buf[0].sampleOffset == 5);
    REQUIRE_FALSE (trig.isNoteOn());
}
