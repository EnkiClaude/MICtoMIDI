#include "Parameters.h"

#include <m2m/midi/MidiEvent.h>

namespace m2m::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { threshold, 1 }, "Threshold",
        NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { note, 1 }, "Note", 0, 127, 60,
        AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return String (m2m::noteName (v)) + " (" + String (v) + ")"; })));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { channel, 1 }, "MIDI Channel", 1, 16, 1));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { velocity, 1 }, "Velocity", 1, 127, 100));

    return layout;
}

} // namespace m2m::params
