#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace m2m::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff15171c };
    inline const juce::Colour header     { 0xff1b1e24 };
    inline const juce::Colour panel      { 0xff1f2329 };
    inline const juce::Colour panelHi    { 0xff292e36 };
    inline const juce::Colour outline    { 0xff353b45 };
    inline const juce::Colour track      { 0xff2c313a };
    inline const juce::Colour text       { 0xffe8eaee };
    inline const juce::Colour textDim    { 0xff9ba2ad };
    inline const juce::Colour textFaint  { 0xff6b7280 };
    inline const juce::Colour melody     { 0xff4fc3f7 };
    inline const juce::Colour beatbox    { 0xffffa94d };
    inline const juce::Colour good       { 0xff66d17a };
    inline const juce::Colour warn       { 0xffffc857 };
    inline const juce::Colour bad        { 0xffff6b6b };

    /** Lane colour per drum slot (Kick, Snare, Clap, Closed Hat, Open Hat). */
    juce::Colour forDrumSlot (int slot);
}

namespace fonts
{
    inline juce::FontOptions regular (float h) { return juce::FontOptions (h); }
    inline juce::FontOptions bold (float h)    { return juce::FontOptions (h, juce::Font::bold); }
}

/** Dark look: flat value-bar sliders, pill buttons, quiet combo boxes. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getLabelFont (juce::Label&) override;
};

} // namespace m2m::ui
