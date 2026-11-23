#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <m2m/beatbox/BeatboxEngine.h>
#include <m2m/melody/MelodyEngine.h>

#include <array>
#include <atomic>
#include <vector>

namespace m2m::params
{
    // Parameter IDs are part of the saved state and of host automation:
    // never rename them. (Phase 1's "threshold", "note", "channel" and
    // "velocity" were retired in state version 2.)
    namespace id
    {
        // Common
        inline constexpr const char* mode           = "mode";
        inline constexpr const char* inputGain      = "input_gain";
        inline constexpr const char* velSensitivity = "vel_sensitivity";
        inline constexpr const char* velCurve       = "vel_curve";
        inline constexpr const char* velFixed       = "vel_fixed";
        inline constexpr const char* velFixedValue  = "vel_fixed_value";

        // Melody
        inline constexpr const char* melDetector    = "mel_detector";
        inline constexpr const char* melMinHz       = "mel_min_hz";
        inline constexpr const char* melMaxHz       = "mel_max_hz";
        inline constexpr const char* melConfidence  = "mel_confidence";
        inline constexpr const char* melOnDb        = "mel_on_db";
        inline constexpr const char* melOffDb       = "mel_off_db";
        inline constexpr const char* melMinNoteMs   = "mel_min_note_ms";
        inline constexpr const char* melHysteresis  = "mel_hysteresis_cents";
        inline constexpr const char* melMinStableMs = "mel_min_stable_ms";
        inline constexpr const char* melReleaseMs   = "mel_release_ms";
        inline constexpr const char* melRetrigger   = "mel_retrigger";
        inline constexpr const char* melRetrigSens  = "mel_retrigger_sens";
        inline constexpr const char* melScale       = "mel_scale";
        inline constexpr const char* melRoot        = "mel_root";
        inline constexpr const char* melTranspose   = "mel_transpose";
        inline constexpr const char* melChannel     = "mel_channel";
        // State version 3: note lock + Stability macro.
        inline constexpr const char* melFollow      = "mel_follow";     // Follow pitch / Note lock
        inline constexpr const char* melStability   = "mel_stability";  // 0..1, Loose .. Tight
        inline constexpr const char* melManual      = "mel_manual";     // Advanced sliders instead of the macro

        // Beatbox (per-class IDs: see drumParamId())
        inline constexpr const char* bbSensitivity  = "bb_sensitivity";
        inline constexpr const char* bbMinLevelDb   = "bb_min_level_db";
        inline constexpr const char* bbRefractoryMs = "bb_refractory_ms";
        inline constexpr const char* bbWindowMs     = "bb_window_ms";
        inline constexpr const char* bbClassifier   = "bb_classifier";
        inline constexpr const char* bbGateMs       = "bb_gate_ms";
        inline constexpr const char* bbChannel      = "bb_channel";
    }

    // Bump when the saved state changes in a way that needs migration.
    // 1: Phase 1 level trigger. 2: Melody/Beatbox modes + calibration.
    // 3: mel_follow, mel_stability, mel_manual. A version-2 session whose
    //    macro-controlled values (hysteresis, min stable, min note) differ
    //    from the defaults loads with mel_manual on, so it sounds the same.
    inline constexpr int stateVersion = 3;

    /** IDs of the parameters the Stability macro overrides unless mel_manual is on. */
    inline constexpr std::array<const char*, 3> stabilityControlledIds { id::melHysteresis, id::melMinStableMs,
                                                                           id::melMinNoteMs };

    enum class Mode : int { Melody = 0, Beatbox = 1 };

    /** The drum classes shown in the UI / exposed as parameters. */
    struct DrumSlot
    {
        DrumClass cls;
        const char* key;    // used in parameter IDs: "bb_<key>_sens"
        const char* name;   // display name
    };

    inline constexpr std::array<DrumSlot, 5> drumSlots { {
        { DrumClass::Kick,      "kick",  "Kick" },
        { DrumClass::Snare,     "snare", "Snare" },
        { DrumClass::Clap,      "clap",  "Clap" },
        { DrumClass::ClosedHat, "chat",  "Closed Hat" },
        { DrumClass::OpenHat,   "ohat",  "Open Hat" },
    } };

    inline constexpr int numDrumSlots = (int) drumSlots.size();

    /** "bb_kick_sens", "bb_kick_note", "bb_kick_on". */
    juce::String drumParamId (int slot, const char* suffix);

    /** Scale names in m2m::Scale order. */
    juce::StringArray scaleNames();
    juce::StringArray rootNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Reads every parameter's raw atomic and turns it into engine params.
        Built once on the message thread; update()/fill() are real-time safe. */
    class ParamCache
    {
    public:
        explicit ParamCache (juce::AudioProcessorValueTreeState& state);

        /** Re-reads all values; returns true if anything changed since the
            last call (always true the first time). */
        bool update() noexcept;

        Mode mode() const noexcept;
        float inputGainLinear() const noexcept;

        void fill (MelodyParams& m) const noexcept;
        void fill (BeatboxParams& b) const noexcept;

    private:
        float get (const std::atomic<float>* p) const noexcept { return p->load (std::memory_order_relaxed); }
        int getInt (const std::atomic<float>* p) const noexcept;
        bool getBool (const std::atomic<float>* p) const noexcept { return get (p) >= 0.5f; }

        std::vector<std::atomic<float>*> all;
        std::vector<float> last;
        bool first = true;

        std::atomic<float>* mode_ = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* velSens = nullptr;
        std::atomic<float>* velCurve = nullptr;
        std::atomic<float>* velFixed = nullptr;
        std::atomic<float>* velFixedValue = nullptr;

        std::atomic<float>* detector = nullptr;
        std::atomic<float>* minHz = nullptr;
        std::atomic<float>* maxHz = nullptr;
        std::atomic<float>* confidence = nullptr;
        std::atomic<float>* onDb = nullptr;
        std::atomic<float>* offDb = nullptr;
        std::atomic<float>* minNoteMs = nullptr;
        std::atomic<float>* hysteresis = nullptr;
        std::atomic<float>* minStableMs = nullptr;
        std::atomic<float>* releaseMs = nullptr;
        std::atomic<float>* retrigger = nullptr;
        std::atomic<float>* retrigSens = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* root = nullptr;
        std::atomic<float>* transpose = nullptr;
        std::atomic<float>* melChannel = nullptr;
        std::atomic<float>* follow = nullptr;
        std::atomic<float>* stability = nullptr;
        std::atomic<float>* manual = nullptr;

        std::atomic<float>* bbSens = nullptr;
        std::atomic<float>* bbMinLevel = nullptr;
        std::atomic<float>* bbRefractory = nullptr;
        std::atomic<float>* bbWindow = nullptr;
        std::atomic<float>* bbClassifier = nullptr;
        std::atomic<float>* bbGate = nullptr;
        std::atomic<float>* bbChannel = nullptr;
        std::array<std::atomic<float>*, numDrumSlots> classSens {}, classNote {}, classOn {};
    };
}
