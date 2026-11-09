#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    constexpr float meterMinDb = -60.0f;

    float dbToProportion (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - meterMinDb) / -meterMinDb);
    }
}

MicToMidiEditor::MicToMidiEditor (MicToMidiProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setUp (threshold, "Threshold", m2m::params::threshold);
    setUp (note,      "Note",      m2m::params::note);
    setUp (channel,   "Channel",   m2m::params::channel);
    setUp (velocity,  "Velocity",  m2m::params::velocity);

    status.setJustificationType (juce::Justification::centredLeft);
    status.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (status);

    setSize (460, 300);
    startTimerHz (30);
}

MicToMidiEditor::~MicToMidiEditor() { stopTimer(); }

void MicToMidiEditor::setUp (LabelledSlider& s, const juce::String& name, const char* paramId)
{
    s.label.setText (name, juce::dontSendNotification);
    s.label.attachToComponent (&s.slider, true);
    s.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 22);
    addAndMakeVisible (s.slider);
    s.attachment = std::make_unique<SliderAttachment> (processor.getState(), paramId, s.slider);
}

void MicToMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1f24));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MICtoMIDI", 16, 10, 200, 24, juce::Justification::centredLeft);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Phase 1 routing test: level above threshold -> note",
                16, 32, getWidth() - 32, 16, juce::Justification::centredLeft);

    // Input meter with threshold marker.
    g.setColour (juce::Colour (0xff2c2e35));
    g.fillRect (meterArea);
    const auto meterFill = meterArea.toFloat().withWidth ((float) meterArea.getWidth() * dbToProportion (meterDb));
    g.setColour (juce::Colour (0xff4caf50));
    g.fillRect (meterFill);

    const float thrDb = processor.getState().getRawParameterValue (m2m::params::threshold)->load();
    const float thrX = (float) meterArea.getX() + (float) meterArea.getWidth() * dbToProportion (thrDb);
    g.setColour (juce::Colours::orange);
    g.drawLine (thrX, (float) meterArea.getY() - 3.0f, thrX, (float) meterArea.getBottom() + 3.0f, 2.0f);

    // Note LED.
    g.setColour (noteOn ? juce::Colour (0xffff5252) : juce::Colour (0xff3a3c44));
    g.fillEllipse (ledArea.toFloat());
}

void MicToMidiEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (44);

    auto meterRow = area.removeFromTop (20);
    ledArea = meterRow.removeFromRight (20);
    meterRow.removeFromRight (10);
    meterArea = meterRow;
    area.removeFromTop (16);

    for (auto* s : { &threshold, &note, &channel, &velocity })
    {
        auto row = area.removeFromTop (28);
        row.removeFromLeft (80); // label
        s->slider.setBounds (row);
        area.removeFromTop (6);
    }

    status.setBounds (area.removeFromBottom (22));
}

void MicToMidiEditor::timerCallback()
{
    meterDb = processor.getInputPeakDb();
    noteOn  = processor.isNoteActive();

    juce::String text;
    if (processor.isStandalone())
    {
        const auto& vo = processor.getVirtualMidiOut();
        text = vo.isOpen() ? "Virtual MIDI port: \"" + vo.getPortName() + "\""
                           : juce::String ("Virtual MIDI port unavailable on this OS");
    }
    else
    {
        text = "Plugin MIDI out";
    }
    text << "   |   notes sent: " << processor.getNotesSent();
    status.setText (text, juce::dontSendNotification);

    repaint (meterArea.expanded (4));
    repaint (ledArea);
}
