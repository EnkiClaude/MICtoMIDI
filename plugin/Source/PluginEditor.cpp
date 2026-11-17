#include "PluginEditor.h"
#include "Parameters.h"

using m2m::params::Mode;
namespace ui = m2m::ui;

MicToMidiEditor::MicToMidiEditor (MicToMidiProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      header (p.getState()),
      melodyPage (p.getState()),
      beatboxPage (p, p.getState())
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (header);
    addChildComponent (melodyPage);
    addChildComponent (beatboxPage);

    ui::styleLabel (footer, 12.0f, ui::colours::textFaint);
    footer.setText (juce::String::fromUTF8 ("Tip: use headphones. Sound from speakers leaking into the mic causes stray notes."),
                    juce::dontSendNotification);
    addAndMakeVisible (footer);

    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, 1800, 1300);
    setSize (defaultWidth, defaultHeight);

    showPage (processor.getModeParam());
    timerCallback();
    startTimerHz (30);
}

MicToMidiEditor::~MicToMidiEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MicToMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);
}

void MicToMidiEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (ui::HeaderBar::preferredHeight));
    footer.setBounds (r.removeFromBottom (22).reduced (16, 0));
    melodyPage.setBounds (r);
    beatboxPage.setBounds (r);
}

void MicToMidiEditor::showPage (Mode m)
{
    if ((int) m == shownMode)
        return;
    shownMode = (int) m;
    melodyPage.setVisible (m == Mode::Melody);
    beatboxPage.setVisible (m == Mode::Beatbox);
}

void MicToMidiEditor::timerCallback()
{
    const auto mode = processor.getModeParam();
    showPage (mode);

    // Drain the audio thread's telemetry.
    m2m::TelemetryFrame f;
    bool gotFrame = false;
    float level = -120.0f;
    while (processor.popTelemetry (f))
    {
        melodyPage.pushFrame (f);
        lastSamplePos = f.samplePos;
        level = juce::jmax (level, f.levelDb);
        gotFrame = true;
    }
    frameLevelDb = gotFrame ? level : -120.0f;

    m2m::HitTelemetry h;
    while (processor.popHit (h))
        beatboxPage.addHit (h);

    auto& state = processor.getState();
    const auto raw = [&state] (const char* paramId) { return state.getRawParameterValue (paramId)->load(); };

    const double sr = processor.getCurrentSampleRate();
    const double latencyMs = processor.getLatencyMs();

    // Header.
    ui::HeaderBar::Info info;
    info.mode = (int) mode;
    info.levelDb = frameLevelDb;
    info.gateDb = mode == Mode::Melody ? raw (m2m::params::id::melOnDb) : raw (m2m::params::id::bbMinLevelDb);
    info.latencyMs = latencyMs;
    info.notesSent = processor.getNotesSent();
    if (processor.isStandalone())
    {
        const auto& vo = processor.getVirtualMidiOut();
        info.statusOk = vo.isOpen();
        info.status = vo.isOpen() ? "MIDI out: virtual port \"" + vo.getPortName() + "\""
                                  : juce::String ("Virtual MIDI port unavailable on this OS");
    }
    else
    {
        info.status = "Plugin MIDI out";
    }
    if (processor.isCalibrating())
    {
        info.status = "Calibrating: no MIDI is sent";
        info.statusOk = false;
    }
    header.update (info);

    // Pages.
    if (mode == Mode::Melody)
    {
        ui::NoteDisplay::State s;
        s.hz = processor.getMelodyHz();
        s.pitch = processor.getMelodyPitch();
        s.confidence = processor.getMelodyConfidence();
        s.activeNote = processor.getActiveNote();
        s.lastVelocity = processor.getLastVelocity();
        s.confidenceThreshold = raw (m2m::params::id::melConfidence);
        melodyPage.update (s, latencyMs, sr);
    }
    else
    {
        beatboxPage.update (lastSamplePos, sr, latencyMs);
    }
}
