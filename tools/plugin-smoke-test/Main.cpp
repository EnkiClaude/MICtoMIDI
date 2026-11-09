// Loads the built VST3 through JUCE's VST3 hosting layer and checks that a
// tone on the main input, and separately on the sidechain input, produces a
// note-on/note-off pair, and that state save/restore round-trips.
//
// Usage: m2m_plugin_smoke_test <path/to/MICtoMIDI.vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

struct Counts { int ons = 0, offs = 0; };

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n";
    if (! ok)
        ++failures;
}

/** Feed 0.3 s silence, 0.4 s tone, 0.6 s silence into the given input bus. */
Counts runTone (juce::AudioPluginInstance& plugin, int toneBus)
{
    plugin.prepareToPlay (sampleRate, blockSize);

    const int totalChannels = juce::jmax (plugin.getTotalNumInputChannels(),
                                          plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (totalChannels, blockSize);
    juce::MidiBuffer midi;
    Counts c;

    const auto toneStart = (juce::int64) (0.3 * sampleRate);
    const auto toneEnd   = (juce::int64) (0.7 * sampleRate);
    const auto total     = (juce::int64) (1.3 * sampleRate);

    for (juce::int64 pos = 0; pos < total; pos += blockSize)
    {
        buffer.clear();
        auto bus = plugin.getBusBuffer (buffer, true, toneBus);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto n = pos + i;
            if (n >= toneStart && n < toneEnd)
            {
                const float v = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * 220.0 * (double) n / sampleRate);
                for (int ch = 0; ch < bus.getNumChannels(); ++ch)
                    bus.setSample (ch, i, v);
            }
        }

        midi.clear();
        plugin.processBlock (buffer, midi);
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())  ++c.ons;
            if (m.isNoteOff()) ++c.offs;
        }
    }

    plugin.releaseResources();
    return c;
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

    {
        const auto c = runTone (*plugin, 0);
        check (c.ons == 1 && c.offs == 1,
               "tone on main input -> 1 note on / 1 note off (got " + juce::String (c.ons) + "/" + juce::String (c.offs) + ")");
    }
    {
        const auto c = runTone (*plugin, 1);
        check (c.ons == 1 && c.offs == 1,
               "tone on sidechain -> 1 note on / 1 note off (got " + juce::String (c.ons) + "/" + juce::String (c.offs) + ")");
    }

    // State round-trip: change the note parameter, save, reset, restore.
    juce::AudioProcessorParameter* noteParam = nullptr;
    for (auto* p : plugin->getParameters())
        if (p->getName (64) == "Note")
            noteParam = p;
    check (noteParam != nullptr, "Note parameter exposed to host");

    if (noteParam != nullptr)
    {
        noteParam->setValueNotifyingHost (36.0f / 127.0f);
        const float saved = noteParam->getValue();
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        noteParam->setValueNotifyingHost (0.9f);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (noteParam->getValue() - saved) < 1.0e-4f, "state save/restore round-trips the Note parameter");
    }

    plugin.reset();
    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
