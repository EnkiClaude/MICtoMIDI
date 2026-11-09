#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

#include <cmath>

namespace
{
    // Max events one block can produce; generous for Phase 1.
    constexpr std::size_t maxEventsPerBlock = 256;
}

MicToMidiProcessor::MicToMidiProcessor()
    : AudioProcessor (BusesProperties()
                          // Main input: used by the standalone app and by hosts
                          // that feed instruments directly.
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          // Sidechain: how Ableton Live routes a mic track into an instrument.
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "MICtoMIDI", m2m::params::createLayout())
{
    thresholdParam = state.getRawParameterValue (m2m::params::threshold);
    noteParam      = state.getRawParameterValue (m2m::params::note);
    channelParam   = state.getRawParameterValue (m2m::params::channel);
    velocityParam  = state.getRawParameterValue (m2m::params::velocity);

    if (isStandalone())
        virtualOut.open (virtualPortName);
}

MicToMidiProcessor::~MicToMidiProcessor()
{
    virtualOut.close();
}

//==============================================================================
void MicToMidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mono.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    events.reserve (maxEventsPerBlock);
    trigger.prepare (sampleRate);
    updateTriggerParams();
    setLatencySamples (0); // Level trigger has no look-ahead.
}

void MicToMidiProcessor::releaseResources() {}

bool MicToMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
            return false;

    return true;
}

void MicToMidiProcessor::updateTriggerParams() noexcept
{
    auto p = trigger.getParams();
    p.onThresholdDb = thresholdParam->load();
    p.note          = (int) std::lround (noteParam->load());
    p.channel       = (int) std::lround (channelParam->load());
    p.velocity      = (int) std::lround (velocityParam->load());
    trigger.setParams (p);
}

void MicToMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    midi.clear();
    updateTriggerParams();

    // Hosts may exceed the announced block size, so work in chunks that fit
    // the scratch buffer instead of allocating on the audio thread.
    const int chunkSize = (int) mono.size();
    if (chunkSize == 0) // processBlock before prepareToPlay: do nothing.
    {
        buffer.clear();
        return;
    }
    float peak = 0.0f;

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int n = juce::jmin (chunkSize, numSamples - start);

        // Sum every enabled input channel (main + sidechain) to mono.
        juce::FloatVectorOperations::clear (mono.data(), n);
        int numSources = 0;
        for (int bus = 0; bus < getBusCount (true); ++bus)
        {
            const auto in = getBusBuffer (buffer, true, bus);
            for (int ch = 0; ch < in.getNumChannels(); ++ch)
            {
                juce::FloatVectorOperations::add (mono.data(), in.getReadPointer (ch, start), n);
                ++numSources;
            }
        }
        if (numSources > 1)
            juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) numSources, n);

        const auto range = juce::FloatVectorOperations::findMinAndMax (mono.data(), n);
        peak = juce::jmax (peak, std::abs (range.getStart()), std::abs (range.getEnd()));

        events.clear();
        trigger.process (mono.data(), n, events);
        emit (events, midi, numSamples, start);
    }

    inputPeakDb.store (juce::Decibels::gainToDecibels (peak, -100.0f), std::memory_order_relaxed);

    // Instrument output is silent (no monitoring: avoids feedback).
    // Cleared after reading because input and output share channels.
    buffer.clear();
}

void MicToMidiProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    midi.clear();
    events.clear();
    trigger.flush (events, 0);
    emit (events, midi, buffer.getNumSamples(), 0);
}

void MicToMidiProcessor::emit (const m2m::MidiEventBuffer& evs, juce::MidiBuffer& midi,
                               int numSamples, int baseOffset)
{
    for (const auto& e : evs)
    {
        const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), baseOffset + (int) e.sampleOffset);
        if (e.type == m2m::MidiEvent::Type::NoteOn)
        {
            midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), offset);
            notesSent.fetch_add (1, std::memory_order_relaxed);
        }
        else
        {
            midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), offset);
        }
        virtualOut.push (e);
    }
    noteActive.store (trigger.isNoteOn(), std::memory_order_relaxed);
}

//==============================================================================
void MicToMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("stateVersion", m2m::params::stateVersion, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void MicToMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MicToMidiProcessor::createEditor()
{
    return new MicToMidiEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MicToMidiProcessor();
}
