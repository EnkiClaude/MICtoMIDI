#include "Controls.h"

namespace m2m::ui
{

void styleLabel (juce::Label& l, float size, juce::Colour c, juce::Justification j)
{
    l.setFont (fonts::regular (size));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (j);
    l.setMinimumHorizontalScale (0.8f);
    l.setInterceptsMouseClicks (false, false);
}

//==============================================================================
ParamSlider::ParamSlider (APVTS& state, const juce::String& paramId, const juce::String& labelText,
                          juce::Colour accent, const juce::String& tooltip)
{
    label.setText (labelText, juce::dontSendNotification);
    styleLabel (label);
    addAndMakeVisible (label);

    styleLabel (hint, 11.5f, colours::textFaint);
    addChildComponent (hint);

    slider.setColour (juce::Slider::trackColourId, accent);
    slider.setTextBoxIsEditable (true);
    slider.setVelocityBasedMode (false);
    slider.setTooltip (tooltip);
    label.setTooltip (tooltip);
    addAndMakeVisible (slider);

    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
}

void ParamSlider::setHint (const juce::String& text, juce::Colour colour)
{
    if (hint.getText() != text)
        hint.setText (text, juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colour);
    if (hint.isVisible() != text.isNotEmpty())
    {
        hint.setVisible (text.isNotEmpty());
        if (auto* parent = getParentComponent())
            parent->resized();
    }
}

void ParamSlider::setLocked (bool locked, const juce::String& why)
{
    if (slider.isEnabled() == locked)
    {
        slider.setEnabled (! locked);
        slider.setAlpha (locked ? 0.38f : 1.0f);
        label.setAlpha (locked ? 0.55f : 1.0f);
    }
    setHint (locked ? why : juce::String());
}

int ParamSlider::getPreferredHeight() const
{
    return rowHeight + (hint.isVisible() ? 15 : 0);
}

void ParamSlider::resized()
{
    auto r = getLocalBounds();
    if (hint.isVisible())
        hint.setBounds (r.removeFromBottom (15));
    r = r.removeFromTop (rowHeight);
    label.setBounds (r.removeFromLeft (juce::roundToInt ((float) r.getWidth() * 0.42f)));
    slider.setBounds (r);
}

//==============================================================================
ParamToggle::ParamToggle (APVTS& state, const juce::String& paramId, const juce::String& text,
                          juce::Colour accent, const juce::String& tooltip)
{
    button.setButtonText (text);
    button.setColour (juce::ToggleButton::tickColourId, accent);
    button.setTooltip (tooltip);
    addAndMakeVisible (button);
    attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, button);
}

void ParamToggle::resized()
{
    button.setBounds (getLocalBounds());
}

//==============================================================================
ParamCombo::ParamCombo (APVTS& state, const juce::String& paramId, const juce::String& labelText,
                        const juce::String& tooltip)
{
    label.setText (labelText, juce::dontSendNotification);
    styleLabel (label);
    addAndMakeVisible (label);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        combo.addItemList (choice->choices, 1);
    combo.setTooltip (tooltip);
    addAndMakeVisible (combo);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, combo);
}

void ParamCombo::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (juce::roundToInt ((float) r.getWidth() * 0.42f)));
    combo.setBounds (r);
}

//==============================================================================
ControlGroup::ControlGroup (const juce::String& t, juce::Colour a) : title (t), accent (a) {}

int ControlGroup::getPreferredHeight() const
{
    int h = titleHeight + padding;
    for (auto& r : rows)
        h += r->getPreferredHeight() + gap;
    return h;
}

void ControlGroup::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (colours::outline.withAlpha (0.6f));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);

    g.setColour (accent);
    g.fillRoundedRectangle ((float) padding, 9.0f, 3.0f, 10.0f, 1.5f);
    g.setColour (colours::text);
    g.setFont (fonts::bold (12.5f));
    g.drawText (title.toUpperCase(), padding + 8, 4, getWidth() - 2 * padding, titleHeight - 4,
                juce::Justification::centredLeft, true);
}

void ControlGroup::resized()
{
    auto r = getLocalBounds().reduced (padding, 0);
    r.removeFromTop (titleHeight);
    for (auto& row : rows)
    {
        row->setBounds (r.removeFromTop (row->getPreferredHeight()));
        r.removeFromTop (gap);
    }
}

} // namespace m2m::ui
