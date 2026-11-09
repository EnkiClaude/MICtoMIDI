#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace m2m::params
{
    // Parameter IDs are part of the saved state: never rename them.
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* note      = "note";
    inline constexpr const char* channel   = "channel";
    inline constexpr const char* velocity  = "velocity";

    // Bump when the parameter set changes in a way that needs migration.
    inline constexpr int stateVersion = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
