#include "m2m/trigger/LevelTriggerEngine.h"
#include "TestSignals.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using m2m::MidiEvent;

TEST_CASE ("LevelTriggerEngine: drives LevelTrigger through IEngine")
{
    constexpr double sr = 48000.0;
    auto concrete = std::make_unique<m2m::LevelTriggerEngine>();
    auto p = concrete->trigger().getParams();
    p.note = 42;
    concrete->trigger().setParams (p);

    std::unique_ptr<m2m::IEngine> engine = std::move (concrete);
    engine->prepare (sr, 512);
    REQUIRE (engine->latencySamples() == 0);

    m2m::MidiEventBuffer buf;
    buf.reserve (16);

    std::vector<float> sig;
    m2m::test::appendSine (sig, sr, 0.05, 200.0, -6.0f);
    engine->process (sig.data(), static_cast<int> (sig.size()), buf);
    REQUIRE (buf.size() == 1);
    REQUIRE (buf[0].type == MidiEvent::Type::NoteOn);
    REQUIRE (buf[0].note == 42);

    buf.clear();
    engine->flush (buf, 7);
    REQUIRE (buf.size() == 1);
    REQUIRE (buf[0].type == MidiEvent::Type::NoteOff);
    REQUIRE (buf[0].sampleOffset == 7);

    // After reset nothing is held, so flush emits nothing.
    engine->process (sig.data(), static_cast<int> (sig.size()), buf);
    engine->reset();
    buf.clear();
    engine->flush (buf);
    REQUIRE (buf.empty());
}
