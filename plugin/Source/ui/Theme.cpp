#include "Theme.h"

namespace m2m::ui
{

juce::Colour colours::forDrumSlot (int slot)
{
    static const juce::Colour lane[] = {
        juce::Colour (0xffff6b6b), // kick
        juce::Colour (0xffffc857), // snare
        juce::Colour (0xffc792ea), // clap
        juce::Colour (0xff4fc3f7), // closed hat
        juce::Colour (0xff66d17a), // open hat
    };
    return lane[juce::jlimit (0, 4, slot)];
}

LookAndFeel::LookAndFeel()
{
    using namespace juce;
    setColourScheme ({ colours::background, colours::panel, colours::panel, colours::outline,
                       colours::text, colours::melody, colours::text, colours::panelHi, colours::text });

    setColour (ResizableWindow::backgroundColourId, colours::background);
    setColour (Label::textColourId, colours::textDim);
    setColour (Slider::trackColourId, colours::melody);
    setColour (Slider::backgroundColourId, colours::track);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, colours::melody.withAlpha (0.4f));
    setColour (TextButton::buttonColourId, colours::panelHi);
    setColour (TextButton::buttonOnColourId, colours::melody);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, Colour (0xff10131a));
    setColour (ToggleButton::textColourId, colours::textDim);
    setColour (ToggleButton::tickColourId, colours::melody);
    setColour (ComboBox::backgroundColourId, colours::track);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    setColour (ComboBox::arrowColourId, colours::textDim);
    setColour (PopupMenu::backgroundColourId, colours::panelHi);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::melody.withAlpha (0.35f));
    setColour (PopupMenu::highlightedTextColourId, colours::text);
    setColour (TooltipWindow::backgroundColourId, colours::panelHi);
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (TooltipWindow::outlineColourId, colours::outline);
    setColour (AlertWindow::backgroundColourId, colours::panel);
    setColour (AlertWindow::textColourId, colours::text);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                    float sliderPos, float, float, juce::Slider::SliderStyle style,
                                    juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (0.5f);
    const float radius = 3.0f;
    const bool enabled = slider.isEnabled();
    auto accent = slider.findColour (juce::Slider::trackColourId);
    if (! enabled)
        accent = accent.withSaturation (0.0f).withAlpha (0.4f);

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (bounds, radius);

    // Bipolar ranges (e.g. transpose, gain) fill from zero.
    float from = bounds.getX();
    const auto range = slider.getRange();
    if (range.getStart() < 0.0 && range.getEnd() > 0.0)
        from = bounds.getX() + bounds.getWidth() * (float) slider.valueToProportionOfLength (0.0);

    const float to = juce::jlimit (bounds.getX(), bounds.getRight(), sliderPos);
    auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, to), bounds.getY(),
                                                             juce::jmax (from, to), bounds.getBottom());
    g.setColour (accent.withAlpha (0.45f));
    g.fillRoundedRectangle (fill, radius);
    g.setColour (accent);
    g.fillRect (juce::Rectangle<float> (to - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));

    if (slider.isMouseOverOrDragging() && enabled)
    {
        g.setColour (colours::text.withAlpha (0.25f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (fonts::regular (12.5f));
    l->setColour (juce::Label::textColourId, colours::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                        bool over, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (! b.isEnabled())
        c = c.withAlpha (0.4f);
    else if (down)
        c = c.brighter (0.15f);
    else if (over)
        c = c.brighter (0.08f);

    g.setColour (c);
    g.fillRoundedRectangle (bounds, 4.0f);
    if (! b.getToggleState())
    {
        g.setColour (colours::outline);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::bold (juce::jmin (14.0f, (float) buttonHeight * 0.5f));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const float h = juce::jmin (16.0f, bounds.getHeight() - 4.0f);
    const auto box = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getCentreY() - h * 0.5f, h * 1.7f, h);
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();

    // Small switch: pill track + knob.
    g.setColour (on ? accent.withAlpha (b.isEnabled() ? 0.85f : 0.35f) : colours::track.brighter (over ? 0.1f : 0.0f));
    g.fillRoundedRectangle (box, h * 0.5f);
    const float knob = h - 4.0f;
    const float kx = on ? box.getRight() - knob - 2.0f : box.getX() + 2.0f;
    g.setColour (on ? juce::Colour (0xff10131a) : colours::textDim);
    g.fillEllipse (kx, box.getY() + 2.0f, knob, knob);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.setFont (fonts::regular (13.0f));
        g.drawText (b.getButtonText(), bounds.withTrimmedLeft (box.getWidth() + 8.0f), juce::Justification::centredLeft, true);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).brighter (box.isMouseOver (true) ? 0.06f : 0.0f));
    g.fillRoundedRectangle (bounds, 3.0f);

    const float aw = 8.0f;
    const auto arrowArea = bounds.removeFromRight (20.0f);
    juce::Path p;
    const float cx = arrowArea.getCentreX(), cy = arrowArea.getCentreY();
    p.startNewSubPath (cx - aw * 0.5f, cy - 2.0f);
    p.lineTo (cx, cy + 3.0f);
    p.lineTo (cx + aw * 0.5f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts::regular (13.0f);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 26, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font LookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

} // namespace m2m::ui
