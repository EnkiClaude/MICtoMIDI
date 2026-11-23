#include "Parameters.h"

#include <m2m/melody/Stability.h>
#include <m2m/midi/MidiEvent.h>

#include <cmath>

namespace m2m::params
{

namespace
{
    using namespace juce;

    // Version hint for every parameter added in state version 2 (Phase 1's
    // parameters used 1). New parameters added later must use a higher hint.
    constexpr int v2 = 2;
    constexpr int v3 = 3; // mel_follow, mel_stability, mel_manual

    String drumParamIdString (int slot, const char* suffix)
    {
        return String ("bb_") + drumSlots[(size_t) slot].key + "_" + suffix;
    }

    float clampTo (const NormalisableRange<float>& r, float v)
    {
        return std::isfinite (v) ? r.snapToLegalValue (jlimit (r.start, r.end, v)) : r.start;
    }

    int clampTo (int lo, int hi, int v) { return jlimit (lo, hi, v); }

    AudioParameterFloatAttributes withUnit (const char* unit, int decimals)
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([unit, decimals] (float v, int) { return String (v, decimals) + " " + unit; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue(); });
    }

    AudioParameterFloatAttributes percent()
    {
        return AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; });
    }

    void addFloat (AudioProcessorValueTreeState::ParameterLayout& layout, const String& paramId,
                   const String& name, NormalisableRange<float> range, float def,
                   AudioParameterFloatAttributes attributes, int version = v2)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { paramId, version }, name, range,
                                                           clampTo (range, def), std::move (attributes)));
    }

    void addInt (AudioProcessorValueTreeState::ParameterLayout& layout, const String& paramId,
                 const String& name, int lo, int hi, int def,
                 AudioParameterIntAttributes attributes = {})
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { paramId, v2 }, name, lo, hi,
                                                         clampTo (lo, hi, def), std::move (attributes)));
    }

    void addBool (AudioProcessorValueTreeState::ParameterLayout& layout, const String& paramId,
                  const String& name, bool def, int version = v2)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { paramId, version }, name, def));
    }

    void addChoice (AudioProcessorValueTreeState::ParameterLayout& layout, const String& paramId,
                    const String& name, const StringArray& choices, int def, int version = v2)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { paramId, version }, name, choices,
                                                            clampTo (0, choices.size() - 1, def)));
    }

    NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
    {
        NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    AudioParameterIntAttributes noteAttributes()
    {
        return AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return String (m2m::noteName (v)) + " (" + String (v) + ")"; });
    }

    AudioParameterIntAttributes semitoneAttributes()
    {
        return AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; });
    }
}

String drumParamId (int slot, const char* suffix)
{
    return drumParamIdString (slot, suffix);
}

StringArray scaleNames()
{
    static_assert (kNumScales == 9, "update scaleNames() when m2m::Scale changes");
    return { "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Major Pentatonic",
             "Minor Pentatonic", "Blues", "Dorian", "Mixolydian" };
}

StringArray rootNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Defaults always come from the core structs.
    const MelodyParams mp;
    const BeatboxParams bp;

    // --- Common -------------------------------------------------------------
    addChoice (layout, id::mode, "Mode", { "Melody", "Beatbox" }, (int) Mode::Melody);
    addFloat (layout, id::inputGain, "Input Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, withUnit ("dB", 1));
    addFloat (layout, id::velSensitivity, "Velocity Sensitivity", { 0.0f, 1.0f, 0.01f },
              mp.velocity.sensitivity, percent());
    addFloat (layout, id::velCurve, "Velocity Curve", skewed (0.25f, 4.0f, 0.01f, 1.0f),
              mp.velocity.curve, withUnit ("", 2));
    addBool (layout, id::velFixed, "Fixed Velocity", mp.useFixedVelocity);
    addInt (layout, id::velFixedValue, "Fixed Velocity Value", 1, 127, mp.velocity.fixedVelocity);

    // --- Melody -------------------------------------------------------------
    addChoice (layout, id::melDetector, "Melody Detector", { "YIN", "MPM" },
               mp.detector == PitchAlgorithm::Mpm ? 1 : 0);
    addFloat (layout, id::melMinHz, "Melody Min Freq", skewed (50.0f, 400.0f, 1.0f, 120.0f), mp.minHz, withUnit ("Hz", 0));
    addFloat (layout, id::melMaxHz, "Melody Max Freq", skewed (200.0f, 2000.0f, 1.0f, 800.0f), mp.maxHz, withUnit ("Hz", 0));
    addFloat (layout, id::melConfidence, "Melody Confidence", { 0.0f, 1.0f, 0.01f }, mp.confidenceThreshold, percent());
    addFloat (layout, id::melOnDb, "Melody On Level", { -80.0f, 0.0f, 0.5f }, mp.onThresholdDb, withUnit ("dB", 1));
    addFloat (layout, id::melOffDb, "Melody Off Level", { -80.0f, 0.0f, 0.5f }, mp.offThresholdDb, withUnit ("dB", 1));
    addFloat (layout, id::melMinNoteMs, "Melody Min Note", { 0.0f, 300.0f, 1.0f }, mp.minNoteMs, withUnit ("ms", 0));
    addFloat (layout, id::melHysteresis, "Melody Hysteresis", { 0.0f, 200.0f, 1.0f }, mp.changeHysteresisCents, withUnit ("ct", 0));
    addFloat (layout, id::melMinStableMs, "Melody Min Stable", { 0.0f, 100.0f, 1.0f }, mp.minStableMs, withUnit ("ms", 0));
    addFloat (layout, id::melReleaseMs, "Melody Release", { 0.0f, 300.0f, 1.0f }, mp.releaseMs, withUnit ("ms", 0));
    addBool (layout, id::melRetrigger, "Melody Retrigger", mp.retriggerEnabled);
    addFloat (layout, id::melRetrigSens, "Melody Retrigger Sensitivity", { 0.0f, 1.0f, 0.01f }, mp.retriggerSensitivity, percent());
    addChoice (layout, id::melScale, "Melody Scale", scaleNames(), (int) mp.scale);
    addChoice (layout, id::melRoot, "Melody Root", rootNames(), mp.scaleRoot);
    addInt (layout, id::melTranspose, "Melody Transpose", -36, 36, mp.transpose, semitoneAttributes());
    addInt (layout, id::melChannel, "Melody MIDI Channel", 1, 16, mp.channel);
    addChoice (layout, id::melFollow, "Melody Follow", { "Follow pitch", "Note lock" },
               mp.pitchFollow == PitchFollow::Lock ? 1 : 0, v3);
    addFloat (layout, id::melStability, "Melody Stability", { 0.0f, 1.0f, 0.01f }, kStabilityDefault, percent(), v3);
    addBool (layout, id::melManual, "Melody Manual Tracking", false, v3);

    // --- Beatbox ------------------------------------------------------------
    addFloat (layout, id::bbSensitivity, "Beatbox Onset Sensitivity", { 0.0f, 1.0f, 0.01f }, bp.onset.sensitivity, percent());
    addFloat (layout, id::bbMinLevelDb, "Beatbox Min Level", { -80.0f, 0.0f, 0.5f }, bp.onset.minLevelDb, withUnit ("dB", 1));
    addFloat (layout, id::bbRefractoryMs, "Beatbox Refractory", { 10.0f, 200.0f, 1.0f }, bp.onset.refractoryMs, withUnit ("ms", 0));
    addFloat (layout, id::bbWindowMs, "Beatbox Analysis Window", { 10.0f, 40.0f, 1.0f }, bp.analysisWindowMs, withUnit ("ms", 0));
    addChoice (layout, id::bbClassifier, "Beatbox Classifier", { "Rules", "Calibrated" },
               bp.classifierMode == ClassifierMode::Calibrated ? 1 : 0);
    addFloat (layout, id::bbGateMs, "Beatbox Gate", skewed (5.0f, 1000.0f, 1.0f, 100.0f), bp.gateMs, withUnit ("ms", 0));
    addInt (layout, id::bbChannel, "Beatbox MIDI Channel", 1, 16, bp.mapping.channel);

    for (int s = 0; s < numDrumSlots; ++s)
    {
        const auto& slot = drumSlots[(size_t) s];
        const auto c = (size_t) toIndex (slot.cls);
        const String name = String ("Beatbox ") + slot.name;
        addFloat (layout, drumParamId (s, "sens"), name + " Sensitivity", { 0.0f, 1.0f, 0.01f },
                  bp.classifier.sensitivity[c], percent());
        addInt (layout, drumParamId (s, "note"), name + " Note", 0, 127, bp.mapping.notes[c], noteAttributes());
        addBool (layout, drumParamId (s, "on"), name + " Enabled", bp.classifier.enabled[c] && bp.mapping.enabled[c]);
    }

    return layout;
}

//==============================================================================
ParamCache::ParamCache (juce::AudioProcessorValueTreeState& state)
{
    const auto raw = [&] (const juce::String& paramId)
    {
        auto* p = state.getRawParameterValue (paramId);
        jassert (p != nullptr);
        all.push_back (p);
        return p;
    };

    mode_         = raw (id::mode);
    gain          = raw (id::inputGain);
    velSens       = raw (id::velSensitivity);
    velCurve      = raw (id::velCurve);
    velFixed      = raw (id::velFixed);
    velFixedValue = raw (id::velFixedValue);

    detector    = raw (id::melDetector);
    minHz       = raw (id::melMinHz);
    maxHz       = raw (id::melMaxHz);
    confidence  = raw (id::melConfidence);
    onDb        = raw (id::melOnDb);
    offDb       = raw (id::melOffDb);
    minNoteMs   = raw (id::melMinNoteMs);
    hysteresis  = raw (id::melHysteresis);
    minStableMs = raw (id::melMinStableMs);
    releaseMs   = raw (id::melReleaseMs);
    retrigger   = raw (id::melRetrigger);
    retrigSens  = raw (id::melRetrigSens);
    scale       = raw (id::melScale);
    root        = raw (id::melRoot);
    transpose   = raw (id::melTranspose);
    melChannel  = raw (id::melChannel);
    follow      = raw (id::melFollow);
    stability   = raw (id::melStability);
    manual      = raw (id::melManual);

    bbSens       = raw (id::bbSensitivity);
    bbMinLevel   = raw (id::bbMinLevelDb);
    bbRefractory = raw (id::bbRefractoryMs);
    bbWindow     = raw (id::bbWindowMs);
    bbClassifier = raw (id::bbClassifier);
    bbGate       = raw (id::bbGateMs);
    bbChannel    = raw (id::bbChannel);

    for (int s = 0; s < numDrumSlots; ++s)
    {
        classSens[(size_t) s] = raw (drumParamId (s, "sens"));
        classNote[(size_t) s] = raw (drumParamId (s, "note"));
        classOn[(size_t) s]   = raw (drumParamId (s, "on"));
    }

    last.assign (all.size(), 0.0f);
}

bool ParamCache::update() noexcept
{
    bool changed = first;
    first = false;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const float v = get (all[i]);
        if (! juce::exactlyEqual (v, last[i]))
        {
            last[i] = v;
            changed = true;
        }
    }
    return changed;
}

int ParamCache::getInt (const std::atomic<float>* p) const noexcept
{
    return (int) std::lround (get (p));
}

Mode ParamCache::mode() const noexcept
{
    return getInt (mode_) == 1 ? Mode::Beatbox : Mode::Melody;
}

float ParamCache::inputGainLinear() const noexcept
{
    return juce::Decibels::decibelsToGain (get (gain));
}

void ParamCache::fill (MelodyParams& m) const noexcept
{
    m = MelodyParams {}; // unexposed fields keep the core defaults
    m.detector              = getInt (detector) == 1 ? PitchAlgorithm::Mpm : PitchAlgorithm::Yin;
    m.minHz                 = get (minHz);
    m.maxHz                 = juce::jmax (get (maxHz), m.minHz * 2.0f);
    m.confidenceThreshold   = get (confidence);
    m.onThresholdDb         = get (onDb);
    m.offThresholdDb        = get (offDb);
    m.minNoteMs             = get (minNoteMs);
    m.changeHysteresisCents = get (hysteresis);
    m.minStableMs           = get (minStableMs);
    m.releaseMs             = get (releaseMs);
    m.retriggerEnabled      = getBool (retrigger);
    m.retriggerSensitivity  = get (retrigSens);
    m.scale                 = static_cast<Scale> (juce::jlimit (0, kNumScales - 1, getInt (scale)));
    m.scaleRoot             = juce::jlimit (0, 11, getInt (root));
    m.transpose             = getInt (transpose);
    m.channel               = juce::jlimit (1, 16, getInt (melChannel));

    m.velocity.sensitivity   = get (velSens);
    m.velocity.curve         = get (velCurve);
    m.velocity.fixedVelocity = juce::jlimit (1, 127, getInt (velFixedValue));
    m.useFixedVelocity       = getBool (velFixed);

    m.pitchFollow = getInt (follow) == 1 ? PitchFollow::Lock : PitchFollow::Free;
    if (! getBool (manual))
        m = applyStability (m, get (stability)); // the macro drives hysteresis, min stable, min note, ...
}

void ParamCache::fill (BeatboxParams& b) const noexcept
{
    b = BeatboxParams {};
    b.onset.sensitivity  = get (bbSens);
    b.onset.minLevelDb   = get (bbMinLevel);
    b.onset.refractoryMs = get (bbRefractory);
    b.analysisWindowMs   = get (bbWindow);
    b.velocityWindowMs   = juce::jmin (b.velocityWindowMs, b.analysisWindowMs);
    b.classifierMode     = getInt (bbClassifier) == 1 ? ClassifierMode::Calibrated : ClassifierMode::Rules;
    b.gateMs             = get (bbGate);
    b.mapping.channel    = juce::jlimit (1, 16, getInt (bbChannel));

    for (int s = 0; s < numDrumSlots; ++s)
    {
        const auto c = (size_t) toIndex (drumSlots[(size_t) s].cls);
        const bool on = getBool (classOn[(size_t) s]);
        b.classifier.sensitivity[c] = get (classSens[(size_t) s]);
        b.classifier.enabled[c]     = on;
        b.mapping.enabled[c]        = on;
        b.mapping.notes[c]          = juce::jlimit (0, 127, getInt (classNote[(size_t) s]));
    }

    b.velocity.sensitivity   = getBool (velFixed) ? 0.0f : get (velSens);
    b.velocity.curve         = get (velCurve);
    b.velocity.fixedVelocity = juce::jlimit (1, 127, getInt (velFixedValue));
}

} // namespace m2m::params
