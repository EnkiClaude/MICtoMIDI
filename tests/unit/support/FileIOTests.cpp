#include "m2m/test/MidiFile.h"
#include "m2m/test/Report.h"
#include "m2m/test/Scenarios.h"
#include "m2m/test/SignalGen.h"
#include "m2m/test/WavIO.h"
#include "SupportTestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <fstream>

using namespace m2m::test;
using Catch::Matchers::WithinAbs;
using m2m::MidiEvent;

namespace
{
    void put16 (std::vector<unsigned char>& b, unsigned v) { b.push_back (static_cast<unsigned char> (v & 0xff)); b.push_back (static_cast<unsigned char> ((v >> 8) & 0xff)); }
    void put32 (std::vector<unsigned char>& b, unsigned v) { put16 (b, v & 0xffff); put16 (b, v >> 16); }
    void tag (std::vector<unsigned char>& b, const char* t) { b.insert (b.end(), t, t + 4); }

    /** Build a WAV by hand: fmt (optionally extensible), extra chunks, data. */
    std::vector<unsigned char> makeWav (unsigned format, unsigned channels, unsigned rate, unsigned bits,
                                        const std::vector<unsigned char>& data, bool extensible, bool junkChunks)
    {
        std::vector<unsigned char> body;
        tag (body, "WAVE");
        if (junkChunks)
        {
            tag (body, "JUNK");
            put32 (body, 3);                  // odd size -> pad byte
            body.insert (body.end(), { 1, 2, 3, 0 });
        }
        tag (body, "fmt ");
        put32 (body, extensible ? 40u : 16u);
        put16 (body, extensible ? 0xFFFEu : format);
        put16 (body, channels);
        put32 (body, rate);
        put32 (body, rate * channels * bits / 8);
        put16 (body, channels * bits / 8);
        put16 (body, bits);
        if (extensible)
        {
            put16 (body, 22);
            put16 (body, bits);
            put32 (body, 0);
            put16 (body, format);             // sub-format GUID starts with the format tag
            const unsigned char guidRest[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
            body.insert (body.end(), guidRest, guidRest + 14);
        }
        if (junkChunks)
        {
            tag (body, "LIST");
            put32 (body, 4);
            tag (body, "INFO");
        }
        tag (body, "data");
        put32 (body, static_cast<unsigned> (data.size()));
        body.insert (body.end(), data.begin(), data.end());
        if (junkChunks)
        {
            tag (body, "id3 ");
            put32 (body, 2);
            body.insert (body.end(), { 9, 9 });
        }

        std::vector<unsigned char> file;
        tag (file, "RIFF");
        put32 (file, static_cast<unsigned> (body.size()));
        file.insert (file.end(), body.begin(), body.end());
        return file;
    }
}

TEST_CASE ("support/WavIO: float32 and pcm16 round trips")
{
    const auto s = sine (48000.0, 0.25, 440.0, -3.0);

    util::TempFile f32 ("roundtrip_f32.wav");
    std::string err;
    REQUIRE (writeWavMono (f32.path, s, 48000.0, WavFormat::Float32, err));
    WavData w;
    REQUIRE (readWav (f32.path, w, err));
    REQUIRE (w.sampleRate == 48000.0);
    REQUIRE (w.numChannels == 1);
    REQUIRE (w.isFloat);
    REQUIRE (w.bitsPerSample == 32);
    REQUIRE (w.samples == s);

    util::TempFile p16 ("roundtrip_p16.wav");
    REQUIRE (writeWavMono (p16.path, s, 44100.0, WavFormat::Pcm16, err));
    REQUIRE (readWav (p16.path, w, err));
    REQUIRE (w.sampleRate == 44100.0);
    REQUIRE_FALSE (w.isFloat);
    REQUIRE (w.bitsPerSample == 16);
    REQUIRE (w.samples.size() == s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
        REQUIRE (std::abs (w.samples[i] - s[i]) <= 0.5f / 32768.0f + 1e-7f);

    // Clipping on PCM write.
    util::TempFile clip ("roundtrip_clip.wav");
    REQUIRE (writeWavMono (clip.path, { 2.0f, -2.0f, 0.0f }, 8000.0, WavFormat::Pcm16, err));
    REQUIRE (readWav (clip.path, w, err));
    REQUIRE (w.samples[0] == 32767.0f / 32768.0f);
    REQUIRE (w.samples[1] == -1.0f);

    // Stereo write -> mono mixdown.
    util::TempFile st ("roundtrip_stereo.wav");
    REQUIRE (writeWav (st.path, { 0.5f, -0.5f, 1.0f, 0.0f }, 2, 48000.0, WavFormat::Float32, err));
    REQUIRE (readWav (st.path, w, err));
    REQUIRE (w.numChannels == 2);
    REQUIRE (w.numFrames() == 2);
    REQUIRE (mixToMono (w) == std::vector<float> { 0.0f, 0.5f });
    REQUIRE (extractChannel (w, 1) == std::vector<float> { -0.5f, 0.0f });
    REQUIRE (extractChannel (w, 2).empty());
}

TEST_CASE ("support/WavIO: PCM 8/24/32-bit, extensible, extra chunks")
{
    WavData w;
    std::string err;

    // 24-bit stereo: frames (+0.5, -0.5), (max, min).
    std::vector<unsigned char> d24 { 0x00, 0x00, 0x40,  0x00, 0x00, 0xC0,  0xFF, 0xFF, 0x7F,  0x00, 0x00, 0x80 };
    REQUIRE (parseWav (makeWav (1, 2, 96000, 24, d24, false, true), w, err));
    REQUIRE (w.sampleRate == 96000.0);
    REQUIRE (w.numChannels == 2);
    REQUIRE (w.samples.size() == 4);
    REQUIRE (w.samples[0] == 0.5f);
    REQUIRE (w.samples[1] == -0.5f);
    REQUIRE_THAT (w.samples[2], WithinAbs (1.0, 1e-6));
    REQUIRE (w.samples[3] == -1.0f);
    REQUIRE (mixToMono (w)[0] == 0.0f);

    // Same data through WAVE_FORMAT_EXTENSIBLE.
    REQUIRE (parseWav (makeWav (1, 2, 96000, 24, d24, true, true), w, err));
    REQUIRE (w.samples[0] == 0.5f);

    // 32-bit int.
    std::vector<unsigned char> d32 { 0x00, 0x00, 0x00, 0x40,  0x00, 0x00, 0x00, 0xC0 };
    REQUIRE (parseWav (makeWav (1, 1, 48000, 32, d32, false, false), w, err));
    REQUIRE (w.samples == std::vector<float> { 0.5f, -0.5f });

    // 8-bit unsigned.
    REQUIRE (parseWav (makeWav (1, 1, 8000, 8, { 128, 192, 0 }, false, false), w, err));
    REQUIRE (w.samples == std::vector<float> { 0.0f, 0.5f, -1.0f });

    // Float32 via extensible.
    std::vector<unsigned char> df (4);
    const float quarter = 0.25f;
    std::memcpy (df.data(), &quarter, 4);
    REQUIRE (parseWav (makeWav (3, 1, 48000, 32, df, true, true), w, err));
    REQUIRE (w.isFloat);
    REQUIRE (w.samples == std::vector<float> { 0.25f });

    // Data chunk claiming more bytes than present: truncated, not an error.
    auto truncated = makeWav (1, 1, 48000, 16, { 0x00, 0x40, 0x00, 0xC0 }, false, false);
    truncated[truncated.size() - 8] = 0xFF; // data size low byte -> claims 255 bytes
    REQUIRE (parseWav (truncated, w, err));
    REQUIRE (w.samples == std::vector<float> { 0.5f, -0.5f });
}

TEST_CASE ("support/WavIO: errors")
{
    WavData w;
    std::string err;
    REQUIRE_FALSE (parseWav ({ 'n', 'o', 'p', 'e' }, w, err));
    REQUIRE_FALSE (err.empty());
    REQUIRE_FALSE (parseWav (makeWav (2 /* ADPCM */, 1, 8000, 4, { 0, 0 }, false, false), w, err));
    REQUIRE_FALSE (parseWav (makeWav (1, 1, 8000, 12, { 0, 0 }, false, false), w, err));
    REQUIRE_FALSE (readWav ("/nonexistent/dir/file.wav", w, err));
    REQUIRE_FALSE (writeWavMono ("/nonexistent/dir/file.wav", { 0.0f }, 48000.0, WavFormat::Pcm16, err));
}

TEST_CASE ("support/MidiFile: round trip of timed events")
{
    constexpr double sr = 48000.0;
    std::vector<TimedEvent> ev {
        { 12000, { MidiEvent::Type::NoteOn, 1, 60, 100, 0 } },
        { 24000, { MidiEvent::Type::NoteOff, 1, 60, 0, 0 } },
        { 24000, { MidiEvent::Type::NoteOn, 10, 36, 127, 0 } },
        { 24480, { MidiEvent::Type::NoteOff, 10, 36, 0, 0 } },
        { 96000, { MidiEvent::Type::NoteOn, 2, 72, 1, 0 } },
        { 144000, { MidiEvent::Type::NoteOff, 2, 72, 0, 0 } },
    };

    util::TempFile f ("roundtrip.mid");
    std::string err;
    REQUIRE (writeMidiFile (f.path, ev, sr, err));

    MidiFileContents c;
    REQUIRE (readMidiFile (f.path, c, err));
    REQUIRE (c.format == 0);
    REQUIRE (c.ppq == 960);
    REQUIRE_THAT (c.initialBpm, WithinAbs (120.0, 1e-9));
    REQUIRE (c.events.size() == ev.size());

    const double tick = 0.5 / 960.0;
    for (std::size_t i = 0; i < ev.size(); ++i)
    {
        REQUIRE_THAT (c.events[i].timeSec, WithinAbs (static_cast<double> (ev[i].samplePos) / sr, tick));
        REQUIRE (c.events[i].event.type == ev[i].event.type);
        REQUIRE (c.events[i].event.channel == ev[i].event.channel);
        REQUIRE (c.events[i].event.note == ev[i].event.note);
        REQUIRE (c.events[i].event.velocity == ev[i].event.velocity);
    }

    const auto notes = notesFromMidiFile (c);
    REQUIRE (notes.size() == 3);
    REQUIRE (notes[0].midiNote == 60);
    REQUIRE_THAT (notes[0].onsetSec, WithinAbs (0.25, tick));
    REQUIRE_THAT (notes[0].offsetSec, WithinAbs (0.5, tick));
    REQUIRE (notes[1].channel == 10);
    REQUIRE (notes[2].velocity == 1);

    // From notes, with a different tempo/resolution.
    util::TempFile f2 ("roundtrip_notes.mid");
    REQUIRE (writeMidiFile (f2.path, notes, err, { 90.0, 480 }));
    REQUIRE (readMidiFile (f2.path, c, err));
    REQUIRE (c.ppq == 480);
    const auto back = notesFromMidiFile (c);
    REQUIRE (back.size() == notes.size());
    const double tick2 = 0.5 * (60.0 / 90.0) / 480.0;
    for (std::size_t i = 0; i < back.size(); ++i)
    {
        REQUIRE (back[i].midiNote == notes[i].midiNote);
        REQUIRE_THAT (back[i].onsetSec, WithinAbs (notes[i].onsetSec, tick + tick2));
        REQUIRE_THAT (back[i].offsetSec, WithinAbs (notes[i].offsetSec, tick + tick2));
    }
}

TEST_CASE ("support/MidiFile: reads format 1, running status and tempo changes")
{
    // ppq 100; track 1: tempo 500000 at 0, tempo 1000000 at tick 100.
    // track 2: running-status note on/off (NoteOn vel 0 = off).
    std::vector<unsigned char> f { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 100 };
    const std::vector<unsigned char> t1 { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
                                          0x64, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,
                                          0x00, 0xFF, 0x2F, 0x00 };
    const std::vector<unsigned char> t2 { 0x00, 0x90, 60, 100,       // tick 0: on
                                          0x64, 60, 0,               // tick 100: off (running status)
                                          0x00, 0xF0, 0x01, 0xF7,    // sysex, skipped
                                          0x64, 0x90, 62, 90,        // tick 200: on (status repeated)
                                          0x64, 0x80, 62, 0,         // tick 300: off
                                          0x00, 0xFF, 0x2F, 0x00 };
    for (const auto* t : { &t1, &t2 })
    {
        f.insert (f.end(), { 'M', 'T', 'r', 'k', 0, 0, 0, static_cast<unsigned char> (t->size()) });
        f.insert (f.end(), t->begin(), t->end());
    }

    MidiFileContents c;
    std::string err;
    REQUIRE (parseMidiFile (f, c, err));
    REQUIRE (c.format == 1);
    REQUIRE (c.events.size() == 4);
    // Tick 100 at 0.5 s/quarter = 0.5 s; then 1 s/quarter.
    REQUIRE_THAT (c.events[1].timeSec, WithinAbs (0.5, 1e-9));
    REQUIRE_THAT (c.events[2].timeSec, WithinAbs (1.5, 1e-9));
    REQUIRE_THAT (c.events[3].timeSec, WithinAbs (2.5, 1e-9));
    REQUIRE (c.events[1].event.type == MidiEvent::Type::NoteOff);

    REQUIRE_FALSE (parseMidiFile ({ 'R', 'I', 'F', 'F' }, c, err));
}

TEST_CASE ("support/Json: parse, dump, round trip")
{
    const std::string text = R"({"a": 1, "b": [true, false, null, -2.5e3], "c": {"s": "x\"y\\z\né"}, "d": []})";
    std::string err;
    const auto j = Json::parse (text, &err);
    REQUIRE (j.has_value());
    REQUIRE (j->getNumber ("a") == 1.0);
    REQUIRE (j->find ("b")->size() == 4);
    REQUIRE (j->find ("b")->at (0).asBool());
    REQUIRE (j->find ("b")->at (2).isNull());
    REQUIRE (j->find ("b")->at (3).asNumber() == -2500.0);
    REQUIRE (j->find ("c")->getString ("s") == "x\"y\\z\n\xC3\xA9");
    REQUIRE (j->keys() == std::vector<std::string> { "a", "b", "c", "d" });

    const auto again = Json::parse (j->dump());
    REQUIRE (again.has_value());
    REQUIRE (again->dump() == j->dump());
    REQUIRE (Json::parse (j->dump (-1))->dump() == j->dump());

    // Doubles round trip exactly; non-finite becomes null.
    Json o = Json::object();
    o["x"] = 0.1 + 0.2;
    o["n"] = std::nan ("");
    const auto p = Json::parse (o.dump());
    REQUIRE (p->getNumber ("x") == 0.1 + 0.2);
    REQUIRE (p->find ("n")->isNull());

    for (const char* bad : { "", "{", "[1,]", "{\"a\" 1}", "tru", "\"abc", "1 2", "{\"a\":01x}" })
    {
        INFO (bad);
        REQUIRE_FALSE (Json::parse (bad, &err).has_value());
        REQUIRE_FALSE (err.empty());
    }
}

TEST_CASE ("support/Report: truth file round trip")
{
    const auto sc = makeScenario ("basic_beat_120bpm");
    REQUIRE (sc.has_value());

    util::TempFile f ("truth.json");
    std::string err;
    REQUIRE (writeJsonFile (f.path, truthToJson (*sc), err));
    TruthFile t;
    REQUIRE (readTruthFile (f.path, t, err));
    REQUIRE (t.name == "basic_beat_120bpm");
    REQUIRE (t.sampleRate == 48000.0);
    REQUIRE (t.durationSec == sc->durationSec());
    REQUIRE (t.notes == sc->notes);

    // Missing offsetSec defaults to onsetSec; notes get sorted.
    const auto j = Json::parse (R"({"notes":[{"onsetSec":2,"note":38},{"onsetSec":1,"offsetSec":1.5,"note":60}]})");
    REQUIRE (truthFromJson (*j, t, err));
    REQUIRE (t.notes.size() == 2);
    REQUIRE (t.notes[0] == TruthNote { 1.0, 1.5, 60 });
    REQUIRE (t.notes[1] == TruthNote { 2.0, 2.0, 38 });

    REQUIRE_FALSE (truthFromJson (*Json::parse (R"({"notes":[{"note":38}]})"), t, err));
    REQUIRE_FALSE (truthFromJson (*Json::parse (R"([1])"), t, err));
    REQUIRE_FALSE (readTruthFile ("/nonexistent/t.json", t, err));
}

TEST_CASE ("support/Report: baseline write, read and gate")
{
    Metrics good;
    good.f1 = 0.95;
    good.meanAbsOnsetErrorMs = 4.0;
    good.falseTriggersPerMinute = 0.0;

    Baseline b;
    b.mode = "beatbox";
    b.blockSize = 128;
    b.scenarios["x"] = makeBaselineEntry (good, b.tolerance);
    REQUIRE_THAT (b.scenarios["x"].minF1, WithinAbs (0.93, 1e-12));
    REQUIRE_THAT (b.scenarios["x"].maxMeanAbsOnsetErrorMs, WithinAbs (6.0, 1e-12));
    REQUIRE_THAT (b.scenarios["x"].maxFalseTriggersPerMinute, WithinAbs (1.0, 1e-12));

    Metrics silent; // no matches -> NaN onset error -> onset gate disabled
    silent.f1 = 1.0;
    silent.meanAbsOnsetErrorMs = std::nan ("");
    b.scenarios["silence"] = makeBaselineEntry (silent, b.tolerance);

    util::TempFile f ("baseline.json");
    std::string err;
    REQUIRE (writeBaselineFile (f.path, b, err));
    Baseline r;
    REQUIRE (readBaselineFile (f.path, r, err));
    REQUIRE (r.mode == "beatbox");
    REQUIRE (r.blockSize == 128);
    REQUIRE (r.scenarios.size() == 2);
    REQUIRE (std::isnan (r.scenarios["silence"].maxMeanAbsOnsetErrorMs));

    const auto& e = r.scenarios["x"];
    REQUIRE (checkBaseline ("x", good, e).empty());

    Metrics worse = good;
    worse.f1 = 0.9;
    REQUIRE (checkBaseline ("x", worse, e).size() == 1);
    worse.meanAbsOnsetErrorMs = 7.0;
    worse.falseTriggersPerMinute = 2.0;
    REQUIRE (checkBaseline ("x", worse, e).size() == 3);

    Metrics edge = good;
    edge.f1 = 0.93;                 // exactly at the gate passes
    edge.meanAbsOnsetErrorMs = 6.0;
    edge.falseTriggersPerMinute = 1.0;
    REQUIRE (checkBaseline ("x", edge, e).empty());

    REQUIRE (checkBaseline ("silence", silent, r.scenarios["silence"]).empty());
}
