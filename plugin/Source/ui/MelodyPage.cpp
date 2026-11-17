#include "MelodyPage.h"

#include "../Parameters.h"

#include <m2m/midi/MidiEvent.h>

#include <cmath>

namespace m2m::ui
{

namespace
{
    juce::String nameOf (int note)
    {
        return juce::String (m2m::noteName (note));
    }

    void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float proportion, juce::Colour c)
    {
        g.setColour (colours::track);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (c);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, proportion)), 2.5f);
    }
}

//==============================================================================
void NoteDisplay::setState (const State& s)
{
    st = s;
    if (s.activeNote >= 0 || s.pitch > 0.0f)
    {
        shownNote = s.activeNote;
        shownPitch = s.pitch;
        holdTicks = 15;
    }
    else if (holdTicks > 0 && --holdTicks == 0)
    {
        shownNote = -1;
        shownPitch = 0.0f;
    }
    repaint();
}

void NoteDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (colours::outline.withAlpha (0.6f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    auto r = b.reduced (14.0f, 10.0f);
    const bool live = st.activeNote >= 0;
    const bool voiced = st.pitch > 0.0f;

    // Note name: the note being sent, else the nearest note to what is heard.
    juce::String big = juce::String::fromUTF8 ("\xe2\x80\x93"); // en dash
    if (shownNote >= 0)
        big = nameOf (shownNote);
    else if (shownPitch > 0.0f)
        big = nameOf ((int) std::lround (shownPitch));

    const float bigH = juce::jlimit (30.0f, 64.0f, r.getHeight() * 0.36f);
    auto nameRow = r.removeFromTop (bigH);
    g.setColour (live ? colours::melody : (holdTicks > 0 ? colours::textDim : colours::textFaint));
    g.setFont (fonts::bold (bigH));
    g.drawText (big, nameRow, juce::Justification::centredLeft, false);

    g.setFont (fonts::regular (11.5f));
    g.setColour (live ? colours::good : colours::textFaint);
    g.drawText (live ? "NOTE ON" : (voiced ? "listening" : "silent"), nameRow.removeFromTop (16.0f),
                juce::Justification::topRight, false);

    // Hz and cents.
    auto line = r.removeFromTop (20.0f);
    g.setColour (colours::text);
    g.setFont (fonts::regular (14.0f));
    const float cents = voiced ? (st.pitch - std::round (st.pitch)) * 100.0f : 0.0f;
    g.drawText (voiced ? juce::String (st.hz, 1) + " Hz" : juce::String ("-- Hz"), line, juce::Justification::centredLeft);
    g.drawText (voiced ? (cents >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (cents)) + " ct" : juce::String ("-- ct"),
                line, juce::Justification::centredRight);

    // Cents meter: -50 .. +50 around the nearest note.
    r.removeFromTop (4.0f);
    auto centsBar = r.removeFromTop (8.0f);
    g.setColour (colours::track);
    g.fillRoundedRectangle (centsBar, 3.0f);
    g.setColour (colours::textFaint);
    g.fillRect (centsBar.getCentreX() - 0.5f, centsBar.getY() - 2.0f, 1.0f, centsBar.getHeight() + 4.0f);
    if (voiced)
    {
        const float x = centsBar.getCentreX() + centsBar.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, cents / 50.0f);
        g.setColour (std::abs (cents) < 15.0f ? colours::good : colours::warn);
        g.fillRoundedRectangle (x - 2.0f, centsBar.getY() - 2.0f, 4.0f, centsBar.getHeight() + 4.0f, 1.5f);
    }

    // Confidence with threshold marker.
    r.removeFromTop (10.0f);
    auto confLabel = r.removeFromTop (16.0f);
    g.setColour (colours::textDim);
    g.setFont (fonts::regular (12.5f));
    g.drawText ("Confidence", confLabel, juce::Justification::centredLeft);
    g.drawText (juce::String (juce::roundToInt (st.confidence * 100.0f)) + " %", confLabel, juce::Justification::centredRight);
    auto confBar = r.removeFromTop (8.0f);
    drawBar (g, confBar, st.confidence, st.confidence >= st.confidenceThreshold ? colours::melody : colours::textFaint);
    g.setColour (colours::warn);
    g.fillRect (confBar.getX() + confBar.getWidth() * st.confidenceThreshold - 1.0f, confBar.getY() - 2.0f, 2.0f, confBar.getHeight() + 4.0f);

    // Last velocity.
    r.removeFromTop (8.0f);
    auto velLabel = r.removeFromTop (16.0f);
    g.setColour (colours::textDim);
    g.drawText ("Last velocity", velLabel, juce::Justification::centredLeft);
    g.drawText (st.lastVelocity > 0 ? juce::String (st.lastVelocity) : juce::String ("--"), velLabel, juce::Justification::centredRight);
    if (r.getHeight() >= 8.0f)
        drawBar (g, r.removeFromTop (8.0f), (float) st.lastVelocity / 127.0f, colours::melody.withAlpha (0.7f));
}

//==============================================================================
PitchTrace::PitchTrace()
{
    ring.resize (2048);
}

void PitchTrace::push (const TelemetryFrame& f)
{
    if (f.samplePos < now) // engine re-prepared: start over
        count = 0;
    ring[head] = f;
    head = (head + 1) % ring.size();
    count = juce::jmin (count + 1, ring.size());
    now = f.samplePos;
}

void PitchTrace::tick()
{
    const auto oldest = now - (std::int64_t) (windowSeconds * sampleRate);
    float mn = 1000.0f, mx = -1000.0f;
    for (size_t i = 0; i < count; ++i)
    {
        const auto& f = ring[(head + ring.size() - 1 - i) % ring.size()];
        if (f.samplePos < oldest)
            break;
        if (f.pitchMidi > 0.0f)
        {
            mn = juce::jmin (mn, f.pitchMidi);
            mx = juce::jmax (mx, f.pitchMidi);
        }
        if (f.activeNote >= 0)
        {
            mn = juce::jmin (mn, (float) f.activeNote);
            mx = juce::jmax (mx, (float) f.activeNote);
        }
    }

    float tLo = 50.0f, tHi = 74.0f;
    if (mx >= mn)
    {
        tLo = mn - 3.0f;
        tHi = mx + 3.0f;
        const float minSpan = 14.0f;
        if (tHi - tLo < minSpan)
        {
            const float c = 0.5f * (tLo + tHi);
            tLo = c - minSpan * 0.5f;
            tHi = c + minSpan * 0.5f;
        }
    }
    lo += (tLo - lo) * 0.12f;
    hi += (tHi - hi) * 0.12f;
    repaint();
}

float PitchTrace::xFor (std::int64_t pos, juce::Rectangle<float> area) const
{
    const double age = (double) (now - pos) / sampleRate;
    return area.getRight() - (float) (age / windowSeconds) * area.getWidth();
}

float PitchTrace::yFor (float midi, juce::Rectangle<float> area) const
{
    return area.getBottom() - (midi - lo) / (hi - lo) * area.getHeight();
}

void PitchTrace::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (colours::outline.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    auto area = bounds.reduced (6.0f);
    area.removeFromLeft (30.0f); // note-name gutter

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestInt());

        // Semitone grid; natural notes slightly brighter, C labelled.
        const float rowH = area.getHeight() / (hi - lo);
        for (int n = (int) std::floor (lo); n <= (int) std::ceil (hi); ++n)
        {
            const float y = yFor ((float) n, area);
            const int pc = ((n % 12) + 12) % 12;
            const bool natural = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
            if (natural && rowH > 3.0f)
            {
                g.setColour (colours::track.withAlpha (0.35f));
                g.fillRect (area.getX(), y - rowH * 0.5f, area.getWidth(), rowH);
            }
            g.setColour (pc == 0 ? colours::outline : colours::outline.withAlpha (0.35f));
            g.fillRect (area.getX(), y - rowH * 0.5f, area.getWidth(), 1.0f);
        }

        // Second ticks.
        g.setColour (colours::outline.withAlpha (0.5f));
        for (int s = 1; s < (int) windowSeconds; ++s)
        {
            const float x = area.getRight() - (float) s / (float) windowSeconds * area.getWidth();
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        }

        const auto oldest = now - (std::int64_t) (windowSeconds * sampleRate) - (std::int64_t) sampleRate / 10;

        // Note bars.
        int barNote = -1;
        std::int64_t barEnd = 0;
        auto flushBar = [&] (std::int64_t barStart)
        {
            if (barNote < 0)
                return;
            const float y0 = yFor ((float) barNote + 0.5f, area), y1 = yFor ((float) barNote - 0.5f, area);
            const auto rect = juce::Rectangle<float>::leftTopRightBottom (xFor (barStart, area), y0, xFor (barEnd, area), y1);
            g.setColour (colours::melody.withAlpha (0.28f));
            g.fillRoundedRectangle (rect, 2.0f);
            g.setColour (colours::melody.withAlpha (0.8f));
            g.drawRoundedRectangle (rect, 2.0f, 1.0f);
            if (rect.getWidth() > 26.0f && rect.getHeight() >= 9.0f)
            {
                g.setColour (colours::text);
                g.setFont (fonts::regular (juce::jmin (11.0f, rect.getHeight())));
                g.drawText (nameOf (barNote), rect.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        };

        std::int64_t barStart = 0;
        for (size_t i = count; i-- > 0;) // oldest -> newest
        {
            const auto& f = ring[(head + ring.size() - 1 - i) % ring.size()];
            if (f.samplePos < oldest)
                continue;
            if (f.activeNote != barNote)
            {
                if (barNote >= 0)
                    flushBar (barStart);
                barNote = f.activeNote;
                barStart = f.samplePos;
            }
            barEnd = f.samplePos;
        }
        flushBar (barStart);

        // Pitch line (alpha follows confidence; broken where unvoiced).
        bool penDown = false;
        juce::Point<float> prev;
        for (size_t i = count; i-- > 0;)
        {
            const auto& f = ring[(head + ring.size() - 1 - i) % ring.size()];
            if (f.samplePos < oldest || f.pitchMidi <= 0.0f)
            {
                penDown = false;
                continue;
            }
            const juce::Point<float> p (xFor (f.samplePos, area), yFor (f.pitchMidi, area));
            if (penDown)
            {
                g.setColour (colours::text.withAlpha (juce::jlimit (0.25f, 1.0f, f.confidence)));
                g.drawLine ({ prev, p }, 2.0f);
            }
            prev = p;
            penDown = true;
        }
    }

    // Note names in the gutter.
    g.setFont (fonts::regular (10.5f));
    const float rowH = area.getHeight() / (hi - lo);
    const int step = rowH >= 11.0f ? 1 : (rowH >= 5.0f ? 2 : 12);
    for (int n = (int) std::ceil (lo); n <= (int) std::floor (hi); ++n)
    {
        const int pc = ((n % 12) + 12) % 12;
        const bool natural = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
        if (! (pc == 0 || (step == 1 && natural) || (step == 2 && (pc == 0 || pc == 4 || pc == 7))))
            continue;
        const float y = yFor ((float) n, area);
        g.setColour (pc == 0 ? colours::textDim : colours::textFaint);
        g.drawText (nameOf (n), juce::Rectangle<float> (bounds.getX() + 4.0f, y - 6.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (colours::textFaint);
    g.setFont (fonts::regular (10.5f));
    g.drawText ("pitch, last 5 s", area.reduced (6.0f, 4.0f), juce::Justification::topLeft, false);
}

//==============================================================================
MelodyPage::MelodyPage (APVTS& s) : state (s)
{
    using namespace m2m::params;
    const auto accent = colours::melody;

    addAndMakeVisible (display);
    addAndMakeVisible (trace);

    detection.add<ParamCombo> (state, id::melDetector, "Detector",
                               "YIN: robust default. MPM: can track fast changes better.");
    minHz = &detection.add<ParamSlider> (state, id::melMinHz, "Min freq", accent,
                                         "Lowest pitch to detect. Lower values need a longer analysis window: more latency.");
    minHz->setHint ("Lower = more latency");
    detection.add<ParamSlider> (state, id::melMaxHz, "Max freq", accent, "Highest pitch to detect");
    detection.add<ParamSlider> (state, id::melConfidence, "Confidence", accent,
                                "How sure the detector must be before a pitch counts. Raise it if you get stray notes.");

    notes.add<ParamSlider> (state, id::melOnDb, "On level", accent, "Input level needed to start a note");
    notes.add<ParamSlider> (state, id::melOffDb, "Off level", accent, "Level below which a note ends");
    notes.add<ParamSlider> (state, id::melMinNoteMs, "Min note", accent, "Shortest note that is sent");
    notes.add<ParamToggle> (state, id::melRetrigger, "Retrigger on re-attack", accent,
                            "Start a new note when you re-attack the same pitch");

    scale.add<ParamCombo> (state, id::melScale, "Scale", "Snap notes to a scale (Chromatic = nearest semitone)");
    scale.add<ParamCombo> (state, id::melRoot, "Root", "Root note of the scale");
    scale.add<ParamSlider> (state, id::melTranspose, "Transpose", accent, "Shift the output in semitones");

    velocity.add<ParamSlider> (state, id::velSensitivity, "Sensitivity", accent,
                               "0 % = always the fixed velocity, 100 % = velocity follows how loud you are");
    velocity.add<ParamSlider> (state, id::velCurve, "Curve", accent, "Below 1: louder; above 1: softer");
    velocity.add<ParamToggle> (state, id::velFixed, "Fixed velocity", accent);
    velocity.add<ParamSlider> (state, id::velFixedValue, "Fixed value", accent);
    velocity.add<ParamSlider> (state, id::melChannel, "Channel", accent, "MIDI channel for melody notes");

    advanced.add<ParamSlider> (state, id::melHysteresis, "Hysteresis", accent,
                               "How far the pitch must move (cents) before the note changes. Higher = steadier through vibrato.");
    advanced.add<ParamSlider> (state, id::melMinStableMs, "Min stable", accent,
                               "Pitch must hold this long before a note starts or changes (adds latency)");
    advanced.add<ParamSlider> (state, id::melReleaseMs, "Release", accent,
                               "How long silence must last before a note ends");
    advanced.add<ParamSlider> (state, id::melRetrigSens, "Retrigger sens.", accent,
                               "How strong a re-attack must be to retrigger the same note");

    for (auto* gr : { &detection, &notes, &scale, &velocity })
        addAndMakeVisible (*gr);
    addChildComponent (advanced);

    advancedButton.setClickingTogglesState (true);
    advancedButton.setColour (juce::TextButton::buttonOnColourId, colours::melody);
    advancedButton.setTooltip ("Show the rarely-needed note tracking settings");
    advancedButton.onClick = [this]
    {
        advanced.setVisible (advancedButton.getToggleState());
        advanced.toFront (false);
        advancedButton.toFront (false);
    };
    addAndMakeVisible (advancedButton);
}

void MelodyPage::update (const NoteDisplay::State& s, double latencyMs, double sampleRate)
{
    display.setState (s);
    trace.setSampleRate (sampleRate);
    trace.tick();
    minHz->setHint ("Lower = more latency (now " + juce::String (latencyMs, 1) + " ms)");
}

void MelodyPage::paint (juce::Graphics&) {}

void MelodyPage::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    // Controls: four columns, height of the tallest group.
    int controlsH = 0;
    for (auto* gr : { &detection, &notes, &scale, &velocity })
        controlsH = juce::jmax (controlsH, gr->getPreferredHeight());
    auto controls = r.removeFromBottom (controlsH);
    r.removeFromBottom (10);

    const int gap = 8;
    const int colW = (controls.getWidth() - 3 * gap) / 4;
    for (auto* gr : { &detection, &notes, &scale, &velocity })
    {
        gr->setBounds (controls.removeFromLeft (colW));
        controls.removeFromLeft (gap);
    }

    // Top: readout on the left, trace on the right.
    auto top = r;
    display.setBounds (top.removeFromLeft (juce::jlimit (220, 300, top.getWidth() / 3)));
    top.removeFromLeft (10);
    trace.setBounds (top);

    // Advanced button in the trace's top-right corner; its panel floats
    // over the right part of the trace.
    advancedButton.setBounds (top.getRight() - 96, top.getY() + 6, 90, 22);
    const int advW = juce::jmin (300, top.getWidth());
    auto adv = top.withTrimmedTop (34).withLeft (top.getRight() - advW);
    advanced.setBounds (adv.withHeight (juce::jmin (adv.getHeight(), advanced.getPreferredHeight())));
}

} // namespace m2m::ui
