#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    using m2m::params::Mode;

    // Capacity of the per-chunk event list (both engines emit a handful at most).
    constexpr std::size_t maxEventsPerChunk = 512;

    // Telemetry frames per second sent to the UI (pitch trace resolution).
    constexpr double telemetryRateHz = 200.0;

    const juce::Identifier stateVersionId { "stateVersion" };
    const juce::Identifier calibrationType { "Calibration" };
    const juce::Identifier calibrationData { "data" };
}

MicToMidiProcessor::MicToMidiProcessor()
    : AudioProcessor (BusesProperties()
                          // Main input: used by the standalone app and by hosts
                          // that feed instruments directly.
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          // Sidechain: how Ableton Live routes a mic track into an instrument.
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "MICtoMIDI", m2m::params::createLayout()),
      paramCache (state)
{
    modeParam = state.getRawParameterValue (m2m::params::id::mode);

    // Give the engines their parameters (and the host a latency) before the
    // first prepareToPlay.
    applyParams();
    activeMode = paramCache.mode();
    const int latency = engineFor (activeMode).latencySamples();
    wantedLatency.store (latency);
    setLatencySamples (latency);

    if (isStandalone())
        virtualOut.open (virtualPortName);

    startTimerHz (20);
}

MicToMidiProcessor::~MicToMidiProcessor()
{
    stopTimer();
    virtualOut.close();
}

//==============================================================================
m2m::params::Mode MicToMidiProcessor::getModeParam() const noexcept
{
    return std::lround (modeParam->load (std::memory_order_relaxed)) == 1 ? Mode::Beatbox : Mode::Melody;
}

m2m::IEngine& MicToMidiProcessor::engineFor (Mode m) noexcept
{
    if (m == Mode::Beatbox)
        return beatbox;
    return melody;
}

double MicToMidiProcessor::getLatencyMs() const noexcept
{
    return 1000.0 * (double) wantedLatency.load (std::memory_order_relaxed) / getCurrentSampleRate();
}

void MicToMidiProcessor::applyParams() noexcept
{
    if (! paramCache.update())
        return;

    paramCache.fill (melodyParams);
    paramCache.fill (beatboxParams);
    melody.setParams (melodyParams);
    beatbox.setParams (beatboxParams);
}

//==============================================================================
void MicToMidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int block = juce::jmax (1, samplesPerBlock);

    mono.assign ((size_t) block, 0.0f);
    events.reserve (maxEventsPerChunk);

    melody.prepare (sr, block);
    beatbox.prepare (sr, block);
    paramCache.update();
    paramCache.fill (melodyParams);
    paramCache.fill (beatboxParams);
    melody.setParams (melodyParams);
    beatbox.setParams (beatboxParams);
    activeMode = paramCache.mode();

    samplePos = 0;
    beatboxBase = 0;
    frameInterval = juce::jmax (1, (int) std::lround (sr / telemetryRateHz));
    samplesSinceFrame = 0;
    framePeak = 0.0f;
    sampleRateAtomic.store (sr);

    const int latency = engineFor (activeMode).latencySamples();
    wantedLatency.store (latency);
    setLatencySamples (latency);
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

void MicToMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    midi.clear();

    // Hosts may exceed the announced block size, so work in chunks that fit
    // the scratch buffer instead of allocating on the audio thread.
    const int chunkSize = (int) mono.size();
    if (chunkSize == 0) // processBlock before prepareToPlay: do nothing.
    {
        buffer.clear();
        return;
    }

    applyParams();

    // Mode switch: release the old engine's notes right here, start the new
    // engine from a clean state.
    const Mode wanted = paramCache.mode();
    if (wanted != activeMode)
    {
        events.clear();
        auto& old = engineFor (activeMode);
        old.flush (events, 0);
        old.reset();
        emit (events, midi, numSamples, 0);

        activeMode = wanted;
        engineFor (activeMode).reset();
        if (activeMode == Mode::Beatbox)
            beatboxBase = samplePos;
        activeNote.store (-1, std::memory_order_relaxed);
        melodyHz.store (0.0f, std::memory_order_relaxed);
        melodyPitch.store (0.0f, std::memory_order_relaxed);
        melodyConfidence.store (0.0f, std::memory_order_relaxed);
    }

    auto& engine = engineFor (activeMode);
    wantedLatency.store (engine.latencySamples(), std::memory_order_relaxed);

    const float gain = paramCache.inputGainLinear();
    float blockPeak = 0.0f;

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
        const float scale = gain / (float) juce::jmax (1, numSources);
        juce::FloatVectorOperations::multiply (mono.data(), scale, n);

        const auto range = juce::FloatVectorOperations::findMinAndMax (mono.data(), n);
        const float chunkPeak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
        blockPeak = juce::jmax (blockPeak, chunkPeak);

        events.clear();
        engine.process (mono.data(), n, events);
        emit (events, midi, numSamples, start);

        if (activeMode == Mode::Beatbox)
            drainHits();

        samplePos += n;
        publishTelemetry (n, chunkPeak);
    }

    if (activeMode == Mode::Melody)
    {
        // Lets queued calibration commands / restored calibrations reach the
        // (idle) beatbox engine; processes no audio.
        events.clear();
        beatbox.process (mono.data(), 0, events);
    }
    beatboxProcessCount.fetch_add (1, std::memory_order_release);

    inputPeakDb.store (juce::Decibels::gainToDecibels (blockPeak, -120.0f), std::memory_order_relaxed);

    // Instrument output is silent (no monitoring: avoids feedback).
    // Cleared after reading because input and output share channels.
    buffer.clear();
}

void MicToMidiProcessor::publishTelemetry (int numSamples, float chunkPeak) noexcept
{
    if (activeMode == Mode::Melody)
    {
        melodyHz.store (melody.currentFrequencyHz(), std::memory_order_relaxed);
        melodyPitch.store (melody.currentPitchMidi(), std::memory_order_relaxed);
        melodyConfidence.store (melody.currentConfidence(), std::memory_order_relaxed);
        activeNote.store (melody.activeNote(), std::memory_order_relaxed);
    }

    framePeak = juce::jmax (framePeak, chunkPeak);
    samplesSinceFrame += numSamples;
    if (samplesSinceFrame < frameInterval)
        return;

    m2m::TelemetryFrame f;
    f.samplePos = samplePos;
    f.levelDb = juce::Decibels::gainToDecibels (framePeak, -120.0f);
    f.mode = (int) activeMode;
    if (activeMode == Mode::Melody)
    {
        f.pitchMidi = melody.currentPitchMidi();
        f.confidence = melody.currentConfidence();
        f.activeNote = melody.activeNote();
    }
    frames.push (f); // dropped if the editor is closed and the FIFO is full

    samplesSinceFrame = 0;
    framePeak = 0.0f;
}

void MicToMidiProcessor::drainHits() noexcept
{
    for (;;)
    {
        const int got = beatbox.getRecentHits (hitScratch.data(), (int) hitScratch.size());
        for (int i = 0; i < got; ++i)
        {
            const auto& h = hitScratch[(size_t) i];
            m2m::HitTelemetry t;
            t.samplePos = beatboxBase + h.emitSample;
            t.cls = m2m::toIndex (h.cls);
            t.velocity = h.velocity;
            t.score = h.score;
            t.accepted = h.accepted;
            t.emitted = h.emitted;
            t.calibration = h.calibration;
            hitFifo.push (t);
        }
        if (got < (int) hitScratch.size())
            return;
    }
}

void MicToMidiProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    if (! mono.empty())
    {
        events.clear();
        engineFor (activeMode).flush (events, 0);
        emit (events, midi, buffer.getNumSamples(), 0);
    }
    activeNote.store (-1, std::memory_order_relaxed);
    buffer.clear();
}

void MicToMidiProcessor::emit (const m2m::MidiEventBuffer& evs, juce::MidiBuffer& midi,
                               int numSamples, int baseOffset) noexcept
{
    for (const auto& e : evs)
    {
        const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), baseOffset + (int) e.sampleOffset);
        if (e.type == m2m::MidiEvent::Type::NoteOn)
        {
            midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), offset);
            notesSent.fetch_add (1, std::memory_order_relaxed);
            lastVelocity.store (e.velocity, std::memory_order_relaxed);
        }
        else
        {
            midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), offset);
        }
        virtualOut.push (e);
    }
}

//==============================================================================
void MicToMidiProcessor::timerCallback()
{
    // Latency is reported from the message thread, never from processBlock.
    const int want = wantedLatency.load();
    if (want != getLatencySamples())
        setLatencySamples (want);

    // A calibration take only makes sense in Beatbox mode.
    if (getModeParam() != Mode::Beatbox && beatbox.isCalibrating())
        cancelCalibration();
}

//==============================================================================
void MicToMidiProcessor::beginCalibration (m2m::DrumClass c)
{
    const juce::ScopedLock sl (calibLock);
    beatbox.beginCalibration (c);
    restorePending = false;
}

void MicToMidiProcessor::endCalibration()
{
    const juce::ScopedLock sl (calibLock);
    beatbox.endCalibration();
}

void MicToMidiProcessor::cancelCalibration()
{
    const juce::ScopedLock sl (calibLock);
    beatbox.cancelCalibration();
}

void MicToMidiProcessor::clearCalibration (m2m::DrumClass c)
{
    const juce::ScopedLock sl (calibLock);
    beatbox.clearCalibration (c);
    restorePending = false;
}

void MicToMidiProcessor::clearAllCalibration()
{
    const juce::ScopedLock sl (calibLock);
    beatbox.clearAllCalibration();
    restorePending = false;
}

juce::String MicToMidiProcessor::getCalibrationString()
{
    const juce::ScopedLock sl (calibLock);

    // A restored calibration only becomes visible in the engine after the
    // audio thread's next process() call; until then report what was restored.
    if (restorePending && beatboxProcessCount.load (std::memory_order_acquire) - restoreMark < 2)
        return restoredCalibration;

    restorePending = false;
    return juce::String (beatbox.getCalibrationString());
}

bool MicToMidiProcessor::setCalibrationString (const juce::String& text)
{
    const juce::ScopedLock sl (calibLock);
    if (! beatbox.setCalibrationString (text.toStdString()))
        return false;

    restoredCalibration = text;
    restorePending = true;
    restoreMark = beatboxProcessCount.load (std::memory_order_acquire);
    return true;
}

//==============================================================================
void MicToMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty (stateVersionId, m2m::params::stateVersion, nullptr);

    juce::ValueTree calib (calibrationType);
    calib.setProperty (calibrationData, getCalibrationString(), nullptr);
    tree.appendChild (calib, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void MicToMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // Calibration travels next to the parameters, not inside the APVTS tree.
    juce::String calibration;
    bool hasCalibration = false;
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto child = tree.getChild (i);
        if (child.hasType (calibrationType))
        {
            calibration = child[calibrationData].toString();
            hasCalibration = true;
            tree.removeChild (i, nullptr);
        }
        else if (child.hasType ("PARAM") && state.getParameter (child["id"].toString()) == nullptr)
        {
            // Retired parameter (e.g. Phase 1's threshold/note/channel/velocity).
            tree.removeChild (i, nullptr);
        }
    }
    const int version = (int) tree.getProperty (stateVersionId, 0);
    tree.removeProperty (stateVersionId, nullptr);

    // Version 2 -> 3: the Stability macro (on by default) now drives the
    // note-tracking values. A session that changed any of them keeps them:
    // it loads in manual mode.
    if (version < 3 && ! tree.getChildWithProperty ("id", m2m::params::id::melManual).isValid())
    {
        bool customised = false;
        for (const auto* paramId : m2m::params::stabilityControlledIds)
        {
            const auto child = tree.getChildWithProperty ("id", paramId);
            auto* param = state.getParameter (paramId);
            if (child.isValid() && param != nullptr)
            {
                const float def = param->convertFrom0to1 (param->getDefaultValue());
                customised |= std::abs ((float) child["value"] - def) > 1.0e-3f;
            }
        }
        if (customised)
        {
            juce::ValueTree manual ("PARAM");
            manual.setProperty ("id", m2m::params::id::melManual, nullptr);
            manual.setProperty ("value", 1.0f, nullptr);
            tree.appendChild (manual, nullptr);
        }
    }

    state.replaceState (tree);

    if (hasCalibration)
    {
        if (calibration.isEmpty())
            clearAllCalibration();
        else
            setCalibrationString (calibration); // invalid text: keep the current calibration
    }
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
