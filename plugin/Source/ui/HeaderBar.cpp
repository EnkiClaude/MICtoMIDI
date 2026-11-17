#include "HeaderBar.h"

#include "../Parameters.h"

namespace m2m::ui
{

namespace
{
    float proportion (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - InputMeter::minDb) / -InputMeter::minDb);
    }
}

//==============================================================================
void InputMeter::setLevels (float levelDb, float gateDb, juce::Colour a)
{
    // Fast attack, ~20 dB/s fall at 30 Hz, peak hold ~1 s.
    level = juce::jmax (levelDb, level - 0.7f);
    if (levelDb >= hold)
    {
        hold = levelDb;
        holdTicks = 30;
    }
    else if (--holdTicks <= 0)
    {
        hold = juce::jmax (level, hold - 1.0f);
    }
    gate = gateDb;
    accent = a;
    repaint();
}

void InputMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (colours::track);
    g.fillRoundedRectangle (b, 3.0f);

    const float w = b.getWidth();
    const float gateX = b.getX() + w * proportion (gate);
    const float lx = b.getX() + w * proportion (level);

    // Below the gate: dim; above: accent; near 0 dB: red.
    const auto fill = b.withRight (lx);
    g.setColour (accent.withAlpha (0.35f));
    g.fillRoundedRectangle (fill.withRight (juce::jmin (lx, gateX)), 3.0f);
    if (lx > gateX)
    {
        g.setColour (accent);
        g.fillRect (fill.withLeft (gateX));
    }
    const float hotX = b.getX() + w * proportion (-3.0f);
    if (lx > hotX)
    {
        g.setColour (colours::bad);
        g.fillRect (fill.withLeft (hotX));
    }

    // dB ticks.
    g.setColour (colours::background.withAlpha (0.6f));
    for (float db = -50.0f; db < 0.0f; db += 10.0f)
    {
        const float x = b.getX() + w * proportion (db);
        g.fillRect (x, b.getY(), 1.0f, b.getHeight());
    }

    // Peak hold.
    if (hold > minDb)
    {
        g.setColour (colours::text.withAlpha (0.7f));
        g.fillRect (b.getX() + w * proportion (hold) - 1.0f, b.getY(), 2.0f, b.getHeight());
    }

    // Gate / threshold marker.
    g.setColour (colours::warn);
    g.fillRect (gateX - 1.0f, b.getY() - 3.0f, 2.0f, b.getHeight() + 6.0f);
}

//==============================================================================
HeaderBar::HeaderBar (APVTS& state)
{
    for (auto* b : { &melodyButton, &beatboxButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    melodyButton.setColour (juce::TextButton::buttonOnColourId, colours::melody);
    beatboxButton.setColour (juce::TextButton::buttonOnColourId, colours::beatbox);
    melodyButton.setTooltip ("Sing, hum or whistle -> MIDI notes");
    beatboxButton.setTooltip ("Vocal percussion -> MIDI drum notes");

    auto* modeParam = state.getParameter (m2m::params::id::mode);
    modeAttachment = std::make_unique<juce::ParameterAttachment> (*modeParam, [this] (float v)
    {
        currentMode = juce::roundToInt (v);
        melodyButton.setToggleState (currentMode == 0, juce::dontSendNotification);
        beatboxButton.setToggleState (currentMode == 1, juce::dontSendNotification);
    });
    melodyButton.onClick  = [this] { modeAttachment->setValueAsCompleteGesture (0.0f); };
    beatboxButton.onClick = [this] { modeAttachment->setValueAsCompleteGesture (1.0f); };
    modeAttachment->sendInitialUpdate();

    addAndMakeVisible (meter);
    meter.setTooltip ("Input level. The yellow line is the level a sound must reach to count.");

    gainLabel.setText ("Gain", juce::dontSendNotification);
    styleLabel (gainLabel, 12.5f);
    addAndMakeVisible (gainLabel);
    gain.setColour (juce::Slider::trackColourId, colours::textDim);
    gain.setTooltip ("Input gain applied before analysis");
    addAndMakeVisible (gain);
    gainAttachment = std::make_unique<APVTS::SliderAttachment> (state, m2m::params::id::inputGain, gain);
    gain.setDoubleClickReturnValue (true, 0.0);

    styleLabel (meterCaption, 11.5f, colours::textFaint);
    meterCaption.setText ("INPUT", juce::dontSendNotification);
    addAndMakeVisible (meterCaption);

    styleLabel (latency, 13.0f, colours::text, juce::Justification::centredRight);
    styleLabel (notes, 13.0f, colours::text, juce::Justification::centredRight);
    styleLabel (status, 12.5f, colours::textDim, juce::Justification::centredRight);
    latency.setTooltip ("Detection delay reported to the host (depends on mode and settings)");
    addAndMakeVisible (latency);
    addAndMakeVisible (notes);
    addAndMakeVisible (status);
}

void HeaderBar::update (const Info& info)
{
    meter.setLevels (info.levelDb, info.gateDb, info.mode == 1 ? colours::beatbox : colours::melody);

    const auto lat = "Latency " + juce::String (info.latencyMs, 1) + " ms";
    if (latency.getText() != lat)
        latency.setText (lat, juce::dontSendNotification);

    const auto n = "Notes sent " + juce::String (info.notesSent);
    if (notes.getText() != n)
        notes.setText (n, juce::dontSendNotification);

    if (status.getText() != info.status)
        status.setText (info.status, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, info.statusOk ? colours::good : colours::warn);
}

void HeaderBar::paint (juce::Graphics& g)
{
    g.setColour (colours::header);
    g.fillRect (getLocalBounds());
    g.setColour (colours::outline);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    g.setColour (colours::text);
    g.setFont (fonts::bold (20.0f));
    g.drawText ("MICtoMIDI", 16, 10, 140, 26, juce::Justification::centredLeft);
    g.setColour (colours::textFaint);
    g.setFont (fonts::regular (11.5f));
    g.drawText ("voice to MIDI", 16, 32, 140, 14, juce::Justification::centredLeft);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    auto top = r.removeFromTop (34);
    top.removeFromLeft (150);

    auto modes = top.removeFromLeft (220);
    melodyButton.setBounds (modes.removeFromLeft (108).reduced (0, 2));
    modes.removeFromLeft (4);
    beatboxButton.setBounds (modes.removeFromLeft (108).reduced (0, 2));

    auto right = top.removeFromRight (150);
    latency.setBounds (right.removeFromTop (17));
    notes.setBounds (right);

    r.removeFromTop (8);
    auto bottom = r.removeFromTop (22);
    meterCaption.setBounds (bottom.removeFromLeft (44));
    auto meterArea = bottom.removeFromLeft (juce::jmax (180, bottom.getWidth() / 2 - 60));
    meter.setBounds (meterArea.reduced (0, 5));
    bottom.removeFromLeft (14);
    gainLabel.setBounds (bottom.removeFromLeft (34));
    gain.setBounds (bottom.removeFromLeft (110).reduced (0, 1));
    bottom.removeFromLeft (10);
    status.setBounds (bottom);
}

} // namespace m2m::ui
