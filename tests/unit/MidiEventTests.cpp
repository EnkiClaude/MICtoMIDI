#include "m2m/midi/MidiEvent.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("noteName uses C4 = 60")
{
    REQUIRE (m2m::noteName (60) == "C4");
    REQUIRE (m2m::noteName (69) == "A4");
    REQUIRE (m2m::noteName (0) == "C-1");
    REQUIRE (m2m::noteName (127) == "G9");
    REQUIRE (m2m::noteName (-1) == "?");
    REQUIRE (m2m::noteName (128) == "?");
}

TEST_CASE ("MidiEventBuffer is fixed capacity and drops on overflow")
{
    m2m::MidiEventBuffer buf;
    buf.reserve (2);
    REQUIRE (buf.push ({}));
    REQUIRE (buf.push ({}));
    REQUIRE_FALSE (buf.push ({}));
    REQUIRE (buf.size() == 2);
    buf.clear();
    REQUIRE (buf.empty());
    REQUIRE (buf.capacity() == 2);
}
