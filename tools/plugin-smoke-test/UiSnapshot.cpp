// Renders the plugin editor to PNG files for UI review, after feeding the
// processor deterministic synthetic audio so the monitors show real data.
//
// Usage: m2m_ui_snapshot <output-dir> [width height]
// Writes melody.png and beatbox.png (and *-min.png at the minimum size).

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Signals.h"

#include <m2m/melody/Stability.h>

#include <iostream>

namespace
{
constexpr double sr = 48000.0;
constexpr int blockSize = 256;

void feed (MicToMidiProcessor& p, const std::vector<float>& signal)
{
    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < signal.size(); pos += blockSize)
    {
        buffer.clear();
        const int n = (int) std::min ((size_t) blockSize, signal.size() - pos);
        for (int i = 0; i < n; ++i)
            buffer.setSample (2, i, signal[pos + (size_t) i]); // sidechain, left
        p.processBlock (buffer, midi);
    }
}

void setParam (MicToMidiProcessor& p, const char* id, float value)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** Opens an editor, feeds the signal (the editor drains the telemetry) and
    renders it. */
juce::Button* findButton (juce::Component& c, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getButtonText() == text && b->isVisible())
        return b;
    for (auto* child : c.getChildren())
        if (auto* found = findButton (*child, text))
            return found;
    return nullptr;
}

bool snapshot (MicToMidiProcessor& p, const std::vector<float>& signal, const juce::File& file, int w, int h,
               const juce::String& clickButton = {})
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (w, h);
    feed (p, signal);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    if (clickButton.isNotEmpty())
        if (auto* b = findButton (*editor, clickButton))
            b->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600); // timers drain telemetry
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "\n";
    return ok;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: m2m_ui_snapshot <output-dir> [width height]\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();
    const int w = argc >= 4 ? std::atoi (argv[2]) : MicToMidiEditor::defaultWidth;
    const int h = argc >= 4 ? std::atoi (argv[3]) : MicToMidiEditor::defaultHeight;

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto& p = dynamic_cast<MicToMidiProcessor&> (*plugin);
    auto layout = p.getBusesLayout();
    for (auto& in : layout.inputBuses)
        in = juce::AudioChannelSet::stereo();
    p.setBusesLayout (layout);
    p.prepareToPlay (sr, blockSize);

    bool ok = true;

    // A Phase 1 (state version 1) session must load without trouble: retired
    // parameters are dropped, new ones keep their defaults.
    {
        juce::XmlElement old ("MICtoMIDI");
        old.setAttribute ("stateVersion", 1);
        for (auto [id, v] : { std::pair { "threshold", -20.0 }, { "note", 48.0 }, { "channel", 3.0 }, { "velocity", 90.0 } })
        {
            auto* e = old.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", v);
        }
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (old, block);
        p.setStateInformation (block.getData(), (int) block.getSize());
        juce::MemoryBlock now;
        p.getStateInformation (now);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (now.getData(), (int) now.getSize());
        const bool migrated = xml != nullptr && xml->getIntAttribute ("stateVersion") == m2m::params::stateVersion
                              && xml->getChildByAttribute ("id", "threshold") == nullptr
                              && p.getModeParam() == m2m::params::Mode::Melody;
        std::cout << (migrated ? "PASS" : "FAIL") << " Phase 1 state loads and is migrated\n";
        ok &= migrated;
    }

    // A version-2 session (before the Stability macro): customised tracking
    // values load in manual mode so they keep working; untouched ones load
    // with the macro on.
    for (const bool customised : { true, false })
    {
        juce::XmlElement old ("MICtoMIDI");
        old.setAttribute ("stateVersion", 2);
        auto* e = old.createNewChildElement ("PARAM");
        e->setAttribute ("id", m2m::params::id::melHysteresis);
        e->setAttribute ("value", customised ? 110.0 : 60.0);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (old, block);
        p.setStateInformation (block.getData(), (int) block.getSize());
        const bool manual = p.getState().getRawParameterValue (m2m::params::id::melManual)->load() >= 0.5f;
        const float hyst = p.getState().getRawParameterValue (m2m::params::id::melHysteresis)->load();
        const float stab = p.getState().getRawParameterValue (m2m::params::id::melStability)->load();
        const bool okMigration = manual == customised && std::abs (hyst - (customised ? 110.0f : 60.0f)) < 0.01f
                                 && std::abs (stab - m2m::kStabilityDefault) < 1.0e-4f;
        std::cout << (okMigration ? "PASS" : "FAIL") << " version-2 state ("
                  << (customised ? "customised" : "default") << " hysteresis) loads with manual "
                  << (manual ? "on" : "off") << "\n";
        ok &= okMigration;
    }
    {
        juce::XmlElement fresh ("MICtoMIDI");
        fresh.setAttribute ("stateVersion", m2m::params::stateVersion);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (fresh, block);
        p.setStateInformation (block.getData(), (int) block.getSize()); // back to defaults
    }

    // --- Melody: a short sung phrase ------------------------------------------
    {
        using namespace m2m::smoke;
        std::vector<float> s;
        silence (s, sr, 0.3);
        harmonicTone (s, sr, 220.00, 0.6, 0.30f);
        silence (s, sr, 0.15);
        harmonicTone (s, sr, 261.63, 0.45, 0.22f);
        harmonicTone (s, sr, 293.66, 0.4, 0.25f);
        silence (s, sr, 0.1);
        harmonicTone (s, sr, 329.63, 1.1, 0.30f, 35.0);
        silence (s, sr, 0.25);
        harmonicTone (s, sr, 246.94, 0.8, 0.2f, 15.0);
        setParam (p, m2m::params::id::mode, 0.0f);
        ok &= snapshot (p, s, dir.getChildFile ("melody.png"), w, h);
        ok &= snapshot (p, s, dir.getChildFile ("melody-min.png"), MicToMidiEditor::minWidth, MicToMidiEditor::minHeight);
        ok &= snapshot (p, s, dir.getChildFile ("melody-advanced.png"), w, h, "Advanced");
        ok &= snapshot (p, s, dir.getChildFile ("melody-advanced-min.png"), MicToMidiEditor::minWidth,
                        MicToMidiEditor::minHeight, "Advanced");

        // Note lock with a tight Stability; then manual mode.
        setParam (p, m2m::params::id::melFollow, 1.0f);
        setParam (p, m2m::params::id::melStability, 0.8f);
        ok &= snapshot (p, s, dir.getChildFile ("melody-lock.png"), w, h);
        ok &= snapshot (p, s, dir.getChildFile ("melody-lock-min.png"), MicToMidiEditor::minWidth, MicToMidiEditor::minHeight);
        setParam (p, m2m::params::id::melManual, 1.0f);
        ok &= snapshot (p, s, dir.getChildFile ("melody-manual-advanced.png"), w, h, "Advanced");
        setParam (p, m2m::params::id::melManual, 0.0f);
        setParam (p, m2m::params::id::melFollow, 0.0f);
        setParam (p, m2m::params::id::melStability, m2m::kStabilityDefault);
    }

    // --- Beatbox: calibrate a few kicks, then a groove ------------------------
    {
        using namespace m2m::smoke;
        setParam (p, m2m::params::id::mode, 1.0f);
        feed (p, std::vector<float> (blockSize, 0.0f));

        p.beginCalibration (m2m::DrumClass::Kick);
        std::vector<float> cal;
        for (int i = 0; i < 4; ++i)
        {
            silence (cal, sr, 0.2);
            kick (cal, sr, 0.5f);
        }
        feed (p, cal);
        p.endCalibration();

        std::vector<float> s;
        for (int bar = 0; bar < 2; ++bar)
        {
            silence (s, sr, 0.1); kick (s, sr, 0.6f);
            silence (s, sr, 0.05); hat (s, sr);
            silence (s, sr, 0.15); snare (s, sr, 0.5f);
            silence (s, sr, 0.1); hat (s, sr, 0.05, 0.008, 0.3f);
            silence (s, sr, 0.1); hat (s, sr, 0.35, 0.065, 0.4f);
        }
        silence (s, sr, 0.2);
        ok &= snapshot (p, s, dir.getChildFile ("beatbox.png"), w, h);
        ok &= snapshot (p, s, dir.getChildFile ("beatbox-min.png"), MicToMidiEditor::minWidth, MicToMidiEditor::minHeight);

        // Mid-calibration view.
        p.beginCalibration (m2m::DrumClass::Snare);
        std::vector<float> c2;
        for (int i = 0; i < 3; ++i)
        {
            silence (c2, sr, 0.2);
            snare (c2, sr, 0.5f);
        }
        ok &= snapshot (p, c2, dir.getChildFile ("beatbox-calibrating.png"), w, h);
        p.cancelCalibration();
        feed (p, std::vector<float> (blockSize, 0.0f));
    }

    p.releaseResources();
    plugin.reset();
    return ok ? 0 : 1;
}
