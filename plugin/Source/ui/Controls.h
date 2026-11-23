#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

#include <functional>
#include <memory>
#include <vector>

namespace m2m::ui
{

using APVTS = juce::AudioProcessorValueTreeState;

/** One row in a control group. */
class ControlRow : public juce::Component
{
public:
    static constexpr int rowHeight = 22;
    virtual int getPreferredHeight() const { return rowHeight; }
};

/** Label on the left, value bar (drag / double-click to reset) on the right,
    optional small hint line underneath. */
class ParamSlider final : public ControlRow
{
public:
    ParamSlider (APVTS& state, const juce::String& paramId, const juce::String& labelText,
                 juce::Colour accent, const juce::String& tooltip = {});

    void setHint (const juce::String& text, juce::Colour colour = colours::textFaint);

    /** Greys the control out (no editing) while something else, e.g. the
        Stability macro, sets the value; `why` is shown as the hint. */
    void setLocked (bool locked, const juce::String& why = {});
    int getPreferredHeight() const override;
    void resized() override;

    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Label label, hint;
    juce::Slider slider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

/** Switch + text, bound to a bool parameter. */
class ParamToggle final : public ControlRow
{
public:
    ParamToggle (APVTS& state, const juce::String& paramId, const juce::String& text,
                 juce::Colour accent, const juce::String& tooltip = {});
    void resized() override;
    juce::ToggleButton& getButton() noexcept { return button; }

private:
    juce::ToggleButton button;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** Label + combo box, bound to a choice parameter. */
class ParamCombo final : public ControlRow
{
public:
    ParamCombo (APVTS& state, const juce::String& paramId, const juce::String& labelText,
                const juce::String& tooltip = {});
    void resized() override;
    juce::ComboBox& getCombo() noexcept { return combo; }

private:
    juce::Label label;
    juce::ComboBox combo;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

/** Titled panel that stacks ControlRows vertically. */
class ControlGroup final : public juce::Component
{
public:
    explicit ControlGroup (const juce::String& title, juce::Colour accent = colours::textDim);

    template <typename Row, typename... Args>
    Row& add (Args&&... args)
    {
        auto row = std::make_unique<Row> (std::forward<Args> (args)...);
        auto& ref = *row;
        addAndMakeVisible (ref);
        rows.push_back (std::move (row));
        return ref;
    }

    int getPreferredHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int titleHeight = 24;
    static constexpr int padding = 8;
    static constexpr int gap = 5;

private:
    juce::String title;
    juce::Colour accent;
    std::vector<std::unique_ptr<ControlRow>> rows;
};

/** Small helper: a label styled as dim body text. */
void styleLabel (juce::Label& l, float size = 13.0f, juce::Colour c = colours::textDim,
                 juce::Justification j = juce::Justification::centredLeft);

} // namespace m2m::ui
