// Loads the built VST3 through JUCE's VST3 hosting layer and checks, end to
// end through the real plugin binary:
//   - it is an instrument with MIDI out and two input buses (main + sidechain)
//   - Melody mode: 220 Hz -> A3 (57) on/off, 330 Hz -> E4 (64), main and sidechain
//   - Beatbox mode: kick-like burst -> 36, hat-like burst -> 42, channel 10
//   - switching mode while a note is held sends its note-off
//   - reported latency is > 0 and follows the mode
//   - Note lock: a 220 Hz tone wandering +-80 cents gives exactly one A3
//     (Follow pitch gives several notes on the same tone)
//   - state save/restore round-trips the mode and a few parameters,
//     including Melody Follow / Stability / Manual Tracking
// All signals are synthetic and deterministic.
//
// Usage: m2m_plugin_smoke_test <path/to/MICtoMIDI.vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "Signals.h"

#include <cmath>
#include <iostream>
#include <map>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n";
    if (! ok)
        ++failures;
}

struct Note
{
    bool on;
    int note, channel;
    juce::int64 pos;
};

struct Host
{
    juce::AudioPluginInstance& plugin;
    std::map<juce::String, juce::AudioProcessorParameter*> params;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    juce::int64 pos = 0;

    explicit Host (juce::AudioPluginInstance& p) : plugin (p)
    {
        for (auto* param : plugin.getParameters())
            params[param->getName (128)] = param;
    }

    juce::AudioProcessorParameter* param (const juce::String& name)
    {
        auto it = params.find (name);
        return it == params.end() ? nullptr : it->second;
    }

    /** Sets a parameter from its real value (only for simple linear ranges
        used here: choice/int/float with known bounds). */
    void set (const juce::String& name, float normalised)
    {
        if (auto* p = param (name))
            p->setValueNotifyingHost (normalised);
        else
            check (false, "parameter \"" + name + "\" exists");
    }

    void prepare()
    {
        plugin.prepareToPlay (sampleRate, blockSize);
        buffer.setSize (juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()), blockSize);
        pos = 0;
    }

    /** Process `signal` on input bus `bus`; returns the MIDI notes. A callback
        may run before each block (e.g. to change a parameter mid-stream). */
    std::vector<Note> run (const std::vector<float>& signal, int bus,
                           const std::function<void (juce::int64 offset, const std::vector<Note>&)>& beforeBlock = {})
    {
        std::vector<Note> notes;
        for (size_t start = 0; start < signal.size(); start += blockSize)
        {
            if (beforeBlock)
                beforeBlock ((juce::int64) start, notes);

            buffer.clear();
            auto in = plugin.getBusBuffer (buffer, true, bus);
            for (int i = 0; i < blockSize && start + (size_t) i < signal.size(); ++i)
                for (int ch = 0; ch < in.getNumChannels(); ++ch)
                    in.setSample (ch, i, signal[start + (size_t) i]);

            midi.clear();
            plugin.processBlock (buffer, midi);
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())
                    notes.push_back ({ true, m.getNoteNumber(), m.getChannel(), pos + meta.samplePosition });
                else if (m.isNoteOff())
                    notes.push_back ({ false, m.getNoteNumber(), m.getChannel(), pos + meta.samplePosition });
            }
            pos += blockSize;
        }
        return notes;
    }

    void silence (double seconds, int bus = 1)
    {
        std::vector<float> s;
        m2m::smoke::silence (s, sampleRate, seconds);
        run (s, bus);
    }
};

juce::String describe (const std::vector<Note>& notes)
{
    juce::String s;
    for (const auto& n : notes)
        s << (n.on ? "on " : "off ") << n.note << "/ch" << n.channel << "  ";
    return s.isEmpty() ? juce::String ("no notes") : s.trim();
}

/** True if the notes are exactly: on(n0) off(n0) on(n1) off(n1) ... on `channel`. */
bool isSequence (const std::vector<Note>& notes, std::initializer_list<int> expected, int channel)
{
    if (notes.size() != expected.size() * 2)
        return false;
    size_t i = 0;
    for (int n : expected)
    {
        const auto& on = notes[i++];
        const auto& off = notes[i++];
        if (! on.on || off.on || on.note != n || off.note != n || on.channel != channel || off.channel != channel
            || off.pos < on.pos)
            return false;
    }
    return true;
}

/** Pumps the message loop (the plugin reports latency from a timer) until
    the hosted latency differs from `old`, or the timeout. */
int waitForLatencyChange (juce::AudioPluginInstance& plugin, int old, int timeoutMs = 3000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (plugin.getLatencySamples() == old && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    return plugin.getLatencySamples();
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: m2m_plugin_smoke_test <MICtoMIDI.vst3>\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());

    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());
    if (found.isEmpty())
    {
        std::cerr << "No plugin found at " << argv[1] << "\n";
        return 2;
    }

    const auto& desc = *found.getFirst();
    std::cout << "Loaded: " << desc.name << " (" << desc.pluginFormatName << ")\n";
    check (desc.isInstrument, "plugin reports itself as an instrument");

    juce::String error;
    auto plugin = formats.createPluginInstance (desc, sampleRate, blockSize, error);
    if (plugin == nullptr)
    {
        std::cerr << "Failed to instantiate: " << error << "\n";
        return 2;
    }

    check (plugin->producesMidi(), "plugin produces MIDI");
    check (plugin->getBusCount (true) == 2, "plugin has two input buses (main + sidechain)");

    // Make sure both inputs are enabled (hosts like Live enable the sidechain on routing).
    auto layout = plugin->getBusesLayout();
    for (auto& in : layout.inputBuses)
        in = juce::AudioChannelSet::stereo();
    check (plugin->setBusesLayout (layout), "both input buses can be enabled");

    Host host (*plugin);
    for (auto* name : { "Mode", "Melody Min Freq", "Beatbox Kick Note", "Beatbox MIDI Channel", "Melody MIDI Channel",
                        "Melody Follow", "Melody Stability", "Melody Manual Tracking" })
        check (host.param (name) != nullptr, juce::String ("parameter \"") + name + "\" exposed to host");
    if (failures > 0)
        return 1;

    using namespace m2m::smoke;
    const auto mainBus = 0, sidechain = 1;

    // ---- Melody mode -----------------------------------------------------------
    host.set ("Mode", 0.0f);
    host.prepare();
    host.silence (0.1);
    const int melodyLatency = plugin->getLatencySamples();
    check (melodyLatency > 0, "Melody mode reports latency > 0 (" + juce::String (melodyLatency) + " samples)");

    {
        std::vector<float> s;
        silence (s, sampleRate, 0.3);
        harmonicTone (s, sampleRate, 220.0, 0.4);
        silence (s, sampleRate, 0.5);
        const auto n = host.run (s, mainBus);
        check (isSequence (n, { 57 }, 1), "220 Hz on main input -> A3 (57) on/off, ch 1 (got " + describe (n) + ")");
    }
    {
        std::vector<float> s;
        silence (s, sampleRate, 0.3);
        harmonicTone (s, sampleRate, 220.0, 0.4);
        silence (s, sampleRate, 0.4);
        harmonicTone (s, sampleRate, 330.0, 0.4);
        silence (s, sampleRate, 0.5);
        const auto n = host.run (s, sidechain);
        check (isSequence (n, { 57, 64 }, 1), "220 Hz then 330 Hz on sidechain -> 57 then 64 (got " + describe (n) + ")");
    }

    // ---- Note lock: a wandering held note is one note ------------------------------
    {
        std::vector<float> s;
        silence (s, sampleRate, 0.3);
        wanderingTone (s, sampleRate, 220.0, 2.5, 80.0);
        silence (s, sampleRate, 0.5);

        const auto freeNotes = host.run (s, sidechain);
        check (freeNotes.size() > 2, "Follow pitch: 220 Hz wandering +-80 ct gives several notes (got "
                                         + describe (freeNotes) + ")");

        host.set ("Melody Follow", 1.0f); // Note lock
        host.silence (0.1);
        const auto lockNotes = host.run (s, sidechain);
        check (isSequence (lockNotes, { 57 }, 1), "Note lock: 220 Hz wandering +-80 ct -> exactly one A3 (got "
                                                      + describe (lockNotes) + ")");
        host.set ("Melody Follow", 0.0f);
        host.silence (0.1);
    }

    // ---- Mode switch while a note is held ----------------------------------------
    {
        std::vector<float> s;
        silence (s, sampleRate, 0.2);
        harmonicTone (s, sampleRate, 220.0, 1.0);
        silence (s, sampleRate, 0.3);
        bool switched = false;
        juce::int64 switchPos = -1;
        const auto n = host.run (s, sidechain, [&] (juce::int64 offset, const std::vector<Note>& sofar)
        {
            if (! switched && ! sofar.empty() && sofar.back().on && offset > (juce::int64) (0.5 * sampleRate))
            {
                host.set ("Mode", 1.0f); // Beatbox
                switched = true;
                switchPos = host.pos;
            }
        });
        const bool heldThenReleased = n.size() == 2 && n[0].on && n[0].note == 57 && ! n[1].on && n[1].note == 57
                                      && switched && n[1].pos >= switchPos && n[1].pos < switchPos + 2 * blockSize;
        check (heldThenReleased, "switching to Beatbox while A3 is held sends its note-off at the switch (got "
                                     + describe (n) + ")");
    }

    // ---- Latency follows the mode -------------------------------------------------
    {
        const int beatboxLatency = waitForLatencyChange (*plugin, melodyLatency);
        check (beatboxLatency > 0 && beatboxLatency != melodyLatency,
               "latency changes with the mode (melody " + juce::String (melodyLatency) + ", beatbox "
                   + juce::String (beatboxLatency) + " samples)");
    }

    // ---- Beatbox mode ---------------------------------------------------------------
    {
        host.prepare(); // mode is Beatbox now
        host.silence (0.2);
        std::vector<float> s;
        silence (s, sampleRate, 0.3);
        kick (s, sampleRate, 0.5f);
        silence (s, sampleRate, 0.4);
        hat (s, sampleRate);
        silence (s, sampleRate, 0.5);
        const auto n = host.run (s, sidechain);
        check (isSequence (n, { 36, 42 }, 10), "kick-like then hat-like burst -> 36 then 42 on ch 10 (got " + describe (n) + ")");
        check (plugin->getLatencySamples() > 0, "Beatbox mode reports latency > 0 after prepare");
    }

    // ---- State save / restore ---------------------------------------------------------
    {
        auto* mode = host.param ("Mode");
        auto* minHz = host.param ("Melody Min Freq");
        auto* kickNote = host.param ("Beatbox Kick Note");
        auto* channel = host.param ("Melody MIDI Channel");
        auto* follow = host.param ("Melody Follow");
        auto* stability = host.param ("Melody Stability");
        auto* manual = host.param ("Melody Manual Tracking");

        mode->setValueNotifyingHost (1.0f);
        minHz->setValueNotifyingHost (0.3f);
        kickNote->setValueNotifyingHost (40.0f / 127.0f);
        channel->setValueNotifyingHost (4.0f / 15.0f);
        follow->setValueNotifyingHost (1.0f);
        stability->setValueNotifyingHost (0.73f);
        manual->setValueNotifyingHost (1.0f);
        host.silence (0.05);
        // Compare as text: the plugin snaps values to its parameter steps.
        const auto snapshot = [&]
        {
            juce::StringArray s;
            for (auto* p : { mode, minHz, kickNote, channel, follow, stability, manual })
                s.add (p->getCurrentValueAsText());
            return s.joinIntoString (", ");
        };
        const auto saved = snapshot();

        juce::MemoryBlock state;
        plugin->getStateInformation (state);

        mode->setValueNotifyingHost (0.0f);
        minHz->setValueNotifyingHost (0.9f);
        kickNote->setValueNotifyingHost (0.0f);
        channel->setValueNotifyingHost (0.0f);
        follow->setValueNotifyingHost (0.0f);
        stability->setValueNotifyingHost (0.1f);
        manual->setValueNotifyingHost (0.0f);
        host.silence (0.05);

        plugin->setStateInformation (state.getData(), (int) state.getSize());
        host.silence (0.05);

        const auto restored = snapshot();
        check (restored == saved && saved.startsWith ("Beatbox"),
               "state save/restore round-trips Mode, Melody Min Freq, Beatbox Kick Note, Melody MIDI Channel, "
                   "Melody Follow, Melody Stability, Melody Manual Tracking ("
                   + saved + " -> " + restored + ")");
    }

    plugin->releaseResources();
    plugin.reset();
    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
