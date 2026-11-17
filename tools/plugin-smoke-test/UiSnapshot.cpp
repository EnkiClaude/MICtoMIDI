// Renders the plugin editor to PNG files for UI review, after feeding the
// processor deterministic synthetic audio so the monitors show real data.
//
// Usage: m2m_ui_snapshot <output-dir> [width height]
// Writes melody.png and beatbox.png (and *-min.png at the minimum size).

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Signals.h"

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
bool snapshot (MicToMidiProcessor& p, const std::vector<float>& signal, const juce::File& file, int w, int h)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    editor->setSize (w, h);
    feed (p, signal);
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
