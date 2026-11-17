#include "BeatboxPage.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

#include <algorithm>

namespace m2m::ui
{

namespace
{
    using m2m::params::drumSlots;
    using m2m::params::numDrumSlots;

    int slotForClass (int cls)
    {
        for (int s = 0; s < numDrumSlots; ++s)
            if (m2m::toIndex (drumSlots[(size_t) s].cls) == cls)
                return s;
        return -1;
    }

    juce::String plural (const char* name)
    {
        return juce::String (name).toLowerCase() + "s";
    }
}

//==============================================================================
DrumLane::DrumLane (int s, const juce::String& n)
    : slot (s), name (n), colour (colours::forDrumSlot (s))
{
}

void DrumLane::addHit (const HitTelemetry& h)
{
    hits[head] = h;
    head = (head + 1) % hits.size();
    count = juce::jmin (count + 1, hits.size());
    if (h.emitted || h.calibration)
    {
        flash = 1.0f;
        lastVelocity = h.velocity;
    }
}

void DrumLane::setNow (std::int64_t samplePos, double sr)
{
    if (samplePos < now)
        count = 0; // re-prepared
    now = samplePos;
    sampleRate = sr > 0.0 ? sr : 48000.0;
}

void DrumLane::setInfo (int calibrationExamples, bool calibratingThis, bool isEnabled)
{
    examples = calibrationExamples;
    calibrating = calibratingThis;
    enabled = isEnabled;
}

void DrumLane::tick()
{
    flash *= 0.82f;
    repaint();
}

void DrumLane::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto base = enabled ? colour : colours::textFaint;

    g.setColour (colours::panel.interpolatedWith (base, 0.10f + 0.45f * flash));
    g.fillRoundedRectangle (b, 4.0f);
    if (calibrating)
    {
        g.setColour (colours::warn);
        g.drawRoundedRectangle (b.reduced (1.0f), 4.0f, 2.0f);
    }

    // Name block with a velocity-sized pip.
    auto nameArea = b.removeFromLeft (juce::jmin (100.0f, b.getWidth() * 0.4f));
    g.setColour (base.withAlpha (0.25f + 0.75f * flash));
    const float pip = 6.0f + 6.0f * (float) lastVelocity / 127.0f * flash;
    g.fillEllipse (nameArea.getX() + 12.0f - pip * 0.5f, nameArea.getCentreY() - pip * 0.5f, pip, pip);
    g.setColour (enabled ? colours::text : colours::textFaint);
    g.setFont (fonts::bold (13.0f));
    g.drawText (name, nameArea.withTrimmedLeft (24.0f), juce::Justification::centredLeft, true);

    // Calibration example count on the right.
    auto calArea = b.removeFromRight (48.0f);
    g.setFont (fonts::regular (11.5f));
    g.setColour (examples >= 3 ? colours::good : (examples > 0 ? colours::warn : colours::textFaint));
    g.drawText (examples > 0 ? juce::String (examples) + " ex" : juce::String ("no cal"),
                calArea, juce::Justification::centred, false);

    // Hit history.
    auto lane = b.reduced (2.0f, 4.0f);
    g.setColour (colours::background.withAlpha (0.45f));
    g.fillRoundedRectangle (lane, 3.0f);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& h = hits[(head + hits.size() - 1 - i) % hits.size()];
        const double age = (double) (now - h.samplePos) / sampleRate;
        if (age > windowSeconds || age < -0.05)
            continue;
        const float x = lane.getRight() - (float) (age / windowSeconds) * lane.getWidth();
        const float hgt = lane.getHeight() * (0.3f + 0.7f * (float) h.velocity / 127.0f);
        const auto tickRect = juce::Rectangle<float> (x - 2.0f, lane.getBottom() - hgt, 4.0f, hgt);
        if (h.calibration)
        {
            g.setColour (colours::warn);
            g.drawEllipse (x - 4.0f, lane.getCentreY() - 4.0f, 8.0f, 8.0f, 1.5f);
        }
        else if (h.emitted)
        {
            g.setColour (base);
            g.fillRoundedRectangle (tickRect, 1.5f);
        }
        else
        {
            // Rejected (score too low) or class disabled: a dim outline.
            g.setColour (colours::textFaint);
            g.drawRoundedRectangle (tickRect, 1.5f, 1.0f);
        }
    }
}

//==============================================================================
BeatboxPage::BeatboxPage (MicToMidiProcessor& p, APVTS& s) : processor (p), state (s)
{
    using namespace m2m::params;
    const auto accent = colours::beatbox;

    for (int i = 0; i < numDrumSlots; ++i)
    {
        auto& row = rows[(size_t) i];
        const auto& slot = drumSlots[(size_t) i];
        const auto c = colours::forDrumSlot (i);

        row.lane = std::make_unique<DrumLane> (i, slot.name);
        addAndMakeVisible (*row.lane);

        row.enable.setColour (juce::ToggleButton::tickColourId, c);
        row.enable.setTooltip (juce::String ("Detect and send ") + slot.name);
        addAndMakeVisible (row.enable);
        row.enableAttachment = std::make_unique<APVTS::ButtonAttachment> (state, drumParamId (i, "on"), row.enable);

        row.sensitivity.setColour (juce::Slider::trackColourId, c);
        row.sensitivity.setTooltip ("Higher = this sound is accepted more easily");
        row.sensitivity.setDoubleClickReturnValue (true, 0.5);
        addAndMakeVisible (row.sensitivity);
        row.sensAttachment = std::make_unique<APVTS::SliderAttachment> (state, drumParamId (i, "sens"), row.sensitivity);

        row.note.setColour (juce::Slider::trackColourId, c);
        row.note.setTooltip ("MIDI note sent for this sound (drag, or double-click to type)");
        addAndMakeVisible (row.note);
        row.noteAttachment = std::make_unique<APVTS::SliderAttachment> (state, drumParamId (i, "note"), row.note);
    }

    for (auto* l : { &colOn, &colSound, &colSens, &colNote })
    {
        styleLabel (*l, 11.5f, colours::textFaint);
        addAndMakeVisible (*l);
    }
    colOn.setText ("ON", juce::dontSendNotification);
    colSound.setText ("SOUND  /  HITS (last 4 s)", juce::dontSendNotification);
    colSens.setText ("SENSITIVITY", juce::dontSendNotification);
    colNote.setText ("MIDI NOTE", juce::dontSendNotification);

    // --- classifier + calibration -----------------------------------------
    classifier = std::make_unique<ParamCombo> (state, id::bbClassifier, "Classifier",
        "Rules: works out of the box. Calibrated: matches your own recorded examples.");
    addAndMakeVisible (*classifier);

    styleLabel (classifierStatus, 12.0f);
    classifierStatus.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (classifierStatus);

    styleLabel (calibTitle, 12.5f, colours::text);
    calibTitle.setFont (fonts::bold (12.5f));
    calibTitle.setText ("CALIBRATION", juce::dontSendNotification);
    addAndMakeVisible (calibTitle);

    for (int i = 0; i < numDrumSlots; ++i)
        calibClass.addItem (drumSlots[(size_t) i].name, i + 1);
    calibClass.setSelectedId (1, juce::dontSendNotification);
    calibClass.setTooltip ("Which sound to record examples for");
    addAndMakeVisible (calibClass);

    styleLabel (calibPrompt, 14.0f, colours::text);
    calibPrompt.setFont (fonts::bold (14.0f));
    calibPrompt.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (calibPrompt);
    styleLabel (calibCount, 12.5f, colours::textDim);
    addChildComponent (calibCount);

    calibrateButton.setColour (juce::TextButton::buttonColourId, colours::beatbox.withAlpha (0.85f));
    calibrateButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff10131a));
    calibrateButton.setTooltip ("Record a few examples of the chosen sound. No MIDI is sent while recording.");
    doneButton.setColour (juce::TextButton::buttonColourId, colours::good.withAlpha (0.85f));
    doneButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff10131a));
    clearButton.setTooltip ("Forget the examples of the chosen sound");
    clearAllButton.setTooltip ("Forget all calibration examples");
    exportButton.setTooltip ("Save your calibration to a file");
    importButton.setTooltip ("Load a calibration file");

    calibrateButton.onClick = [this] { startCalibration(); };
    doneButton.onClick      = [this] { processor.endCalibration(); };
    cancelButton.onClick    = [this] { processor.cancelCalibration(); };
    clearButton.onClick     = [this]
    {
        const int slot = juce::jmax (0, calibClass.getSelectedId() - 1);
        processor.clearCalibration (drumSlots[(size_t) slot].cls);
    };
    clearAllButton.onClick  = [this]
    {
        juce::NativeMessageBox::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear all calibration?",
            "This forgets every recorded example.", this,
            juce::ModalCallbackFunction::create ([safe = juce::WeakReference<BeatboxPage> (this)] (int result)
            {
                if (safe != nullptr && result != 0)
                    safe->processor.clearAllCalibration();
            }));
    };
    exportButton.onClick = [this] { exportCalibration(); };
    importButton.onClick = [this] { importCalibration(); };

    styleLabel (calibHelp, 12.0f, colours::textFaint);
    calibHelp.setJustificationType (juce::Justification::topLeft);
    calibHelp.setText ("How to calibrate:\n1. Pick a sound and press Calibrate.\n2. Perform it " + juce::String (examplesToAsk)
                           + " times, the way you will use it.\n3. Press Done. Repeat for each sound you use.\n"
                             "4. Set Classifier to Calibrated.\nCalibration is saved with your project.",
                       juce::dontSendNotification);
    addAndMakeVisible (calibHelp);

    for (auto* b : { &calibrateButton, &clearButton, &clearAllButton, &exportButton, &importButton })
        addAndMakeVisible (*b);
    addChildComponent (doneButton);
    addChildComponent (cancelButton);

    // --- settings groups ---------------------------------------------------
    detection.add<ParamSlider> (state, id::bbSensitivity, "Sensitivity", accent,
                                "Onset sensitivity: higher catches softer hits (and more noise)");
    detection.add<ParamSlider> (state, id::bbMinLevelDb, "Min level", accent,
                                "Hits quieter than this are ignored (yellow line on the meter)");
    detection.add<ParamSlider> (state, id::bbRefractoryMs, "Min gap", accent,
                                "Shortest time between two hits");
    windowSlider = &detection.add<ParamSlider> (state, id::bbWindowMs, "Analysis", accent,
        "Audio analysed after each hit before deciding what it was. Longer = more accurate, more latency.");
    windowSlider->setHint ("Longer = more accurate, more latency");

    output.add<ParamSlider> (state, id::bbGateMs, "Note length", accent, "Time between note-on and note-off");
    output.add<ParamSlider> (state, id::bbChannel, "Channel", accent, "10 = General MIDI drums");
    velocity.add<ParamSlider> (state, id::velSensitivity, "Sensitivity", accent,
                               "0 % = always the fixed velocity, 100 % = follows how hard you hit");
    velocity.add<ParamSlider> (state, id::velCurve, "Curve", accent, "Below 1: louder; above 1: softer");
    velocity.add<ParamToggle> (state, id::velFixed, "Fixed velocity", accent);
    velocity.add<ParamSlider> (state, id::velFixedValue, "Fixed value", accent);

    addAndMakeVisible (detection);
    addAndMakeVisible (output);
    addAndMakeVisible (velocity);

    refreshCalibrationUi();
}

BeatboxPage::~BeatboxPage() = default;

void BeatboxPage::addHit (const HitTelemetry& h)
{
    const int slot = slotForClass (h.cls);
    if (slot >= 0)
        rows[(size_t) slot].lane->addHit (h);
}

void BeatboxPage::update (std::int64_t nowSamples, double sampleRate, double latencyMs)
{
    const bool calibrating = processor.isCalibrating();
    const int calSlot = slotForClass (m2m::toIndex (processor.calibrationClass()));

    for (int i = 0; i < numDrumSlots; ++i)
    {
        auto& row = rows[(size_t) i];
        row.lane->setNow (nowSamples, sampleRate);
        row.lane->setInfo (processor.calibrationExampleCount (drumSlots[(size_t) i].cls),
                           calibrating && calSlot == i, row.enable.getToggleState());
        row.lane->tick();
    }

    windowSlider->setHint ("Longer = more accurate, more latency (now " + juce::String (latencyMs, 1) + " ms)");
    refreshCalibrationUi();
}

void BeatboxPage::startCalibration()
{
    const int slot = juce::jmax (0, calibClass.getSelectedId() - 1);
    processor.beginCalibration (drumSlots[(size_t) slot].cls);
}

void BeatboxPage::refreshCalibrationUi()
{
    const bool calibrating = processor.isCalibrating();

    // Classifier status.
    const bool calibratedMode = std::lround (state.getRawParameterValue (m2m::params::id::bbClassifier)->load()) == 1;
    const bool usable = processor.isCalibrationUsable();
    juce::String text;
    juce::Colour c = colours::textDim;
    if (calibratedMode)
    {
        if (usable)
        {
            text = juce::String::fromUTF8 ("\xe2\x97\x8f Using your calibration");
            c = colours::good;
        }
        else
        {
            text = juce::String::fromUTF8 ("\xe2\x97\x8f Calibration not usable yet: record 3+ examples of at least 2 sounds. Using Rules meanwhile.");
            c = colours::warn;
        }
    }
    else
    {
        text = usable ? juce::String::fromUTF8 ("\xe2\x97\x8f Rules active. Your calibration is ready: choose Calibrated to use it.")
                      : juce::String::fromUTF8 ("\xe2\x97\x8f Rules active: works without calibration.");
        c = usable ? colours::good : colours::textDim;
    }
    if (classifierStatus.getText() != text)
        classifierStatus.setText (text, juce::dontSendNotification);
    classifierStatus.setColour (juce::Label::textColourId, c);

    // Calibration flow.
    if (calibrating)
    {
        const auto cls = processor.calibrationClass();
        const int slot = juce::jmax (0, slotForClass (m2m::toIndex (cls)));
        const int n = processor.calibrationExampleCount (cls);
        calibPrompt.setText ("Perform " + juce::String (examplesToAsk) + " " + plural (drumSlots[(size_t) slot].name)
                                 + juce::String::fromUTF8 ("\xe2\x80\xa6"),
                             juce::dontSendNotification);
        calibPrompt.setColour (juce::Label::textColourId, colours::forDrumSlot (slot));
        calibCount.setText (n >= examplesToAsk
                                ? juce::String (n) + " captured. Press Done (or keep going, up to 16)."
                                : juce::String (n) + " of " + juce::String (examplesToAsk) + " captured. No MIDI is sent while recording.",
                            juce::dontSendNotification);
        calibCount.setColour (juce::Label::textColourId, n >= examplesToAsk ? colours::good : colours::textDim);
    }

    if (calibrating != wasCalibrating || ! calibPrompt.isVisible() == calibrating)
    {
        wasCalibrating = calibrating;
        calibPrompt.setVisible (calibrating);
        calibCount.setVisible (calibrating);
        doneButton.setVisible (calibrating);
        cancelButton.setVisible (calibrating);
        for (auto* comp : std::initializer_list<juce::Component*> { &calibHelp, &calibClass, &calibrateButton, &clearButton,
                                                                    &clearAllButton, &exportButton, &importButton })
            comp->setVisible (! calibrating);
        layoutCalibrationPanel (calibArea);
        repaint (calibArea);
    }
}

void BeatboxPage::exportCalibration()
{
    chooser = std::make_unique<juce::FileChooser> ("Export calibration",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MICtoMIDI calibration.m2mcal"),
        "*.m2mcal");
    const auto text = processor.getCalibrationString();
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [text] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (! file.replaceWithText (text))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed",
                                                        "Could not write " + file.getFullPathName());
        });
}

void BeatboxPage::importCalibration()
{
    chooser = std::make_unique<juce::FileChooser> ("Import calibration",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.m2mcal");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::WeakReference<BeatboxPage> (this)] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe == nullptr || ! file.existsAsFile())
                return;
            if (! safe->processor.setCalibrationString (file.loadFileAsString()))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed",
                                                        file.getFileName() + " is not a MICtoMIDI calibration file.");
        });
}

void BeatboxPage::paint (juce::Graphics& g)
{
    const auto b = calibArea.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour ((processor.isCalibrating() ? colours::warn : colours::outline).withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
}

void BeatboxPage::layoutCalibrationPanel (juce::Rectangle<int> area)
{
    auto r = area.reduced (10, 8);
    classifier->setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    classifierStatus.setBounds (r.removeFromTop (30));
    r.removeFromTop (6);
    calibTitle.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    if (processor.isCalibrating())
    {
        calibPrompt.setBounds (r.removeFromTop (24));
        calibCount.setBounds (r.removeFromTop (34));
        calibCount.setJustificationType (juce::Justification::topLeft);
        r.removeFromTop (6);
        auto buttons = r.removeFromTop (28);
        doneButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
        buttons.removeFromLeft (6);
        cancelButton.setBounds (buttons);
        return;
    }

    auto pick = r.removeFromTop (26);
    calibClass.setBounds (pick.removeFromLeft (pick.getWidth() / 2 - 3));
    pick.removeFromLeft (6);
    calibrateButton.setBounds (pick);
    r.removeFromTop (6);

    auto row1 = r.removeFromTop (24);
    clearButton.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 3));
    row1.removeFromLeft (6);
    clearAllButton.setBounds (row1);
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (24);
    exportButton.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
    row2.removeFromLeft (6);
    importButton.setBounds (row2);
    r.removeFromTop (10);
    calibHelp.setBounds (r);
}

void BeatboxPage::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    // Bottom: three settings groups across the full width.
    const int groupsH = std::max ({ detection.getPreferredHeight(), output.getPreferredHeight(),
                                      velocity.getPreferredHeight() });
    auto groups = r.removeFromBottom (groupsH);
    r.removeFromBottom (10);
    const int gap = 8;
    const int colW = (groups.getWidth() - 2 * gap) / 3;
    detection.setBounds (groups.removeFromLeft (colW));
    groups.removeFromLeft (gap);
    output.setBounds (groups.removeFromLeft (colW));
    groups.removeFromLeft (gap);
    velocity.setBounds (groups);

    // Top right: classifier + calibration.
    calibArea = r.removeFromRight (juce::jlimit (240, 320, r.getWidth() / 3));
    r.removeFromRight (10);
    layoutCalibrationPanel (calibArea);

    // Top left: column header + one row per sound.
    lanesArea = r;
    auto header = r.removeFromTop (16);
    const int onW = 40, sensW = juce::jlimit (80, 140, r.getWidth() / 5), noteW = juce::jlimit (80, 120, r.getWidth() / 6);
    colOn.setBounds (header.removeFromLeft (onW));
    colNote.setBounds (header.removeFromRight (noteW));
    header.removeFromRight (6);
    colSens.setBounds (header.removeFromRight (sensW));
    header.removeFromRight (6);
    colSound.setBounds (header);

    const int rowGap = 4;
    const int rowH = juce::jlimit (18, 56, (r.getHeight() - 4 * rowGap) / 5);
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rowH);
        r.removeFromTop (rowGap);
        row.enable.setBounds (line.removeFromLeft (onW).withSizeKeepingCentre (34, juce::jmin (20, rowH)));
        auto note = line.removeFromRight (noteW);
        row.note.setBounds (note.withSizeKeepingCentre (note.getWidth(), juce::jmin (22, note.getHeight())));
        line.removeFromRight (6);
        auto sens = line.removeFromRight (sensW);
        row.sensitivity.setBounds (sens.withSizeKeepingCentre (sens.getWidth(), juce::jmin (22, sens.getHeight())));
        line.removeFromRight (6);
        row.lane->setBounds (line);
    }
}

} // namespace m2m::ui
