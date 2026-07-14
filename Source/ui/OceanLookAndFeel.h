#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace amanita::ui
{
class OceanLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OceanLookAndFeel();
    ~OceanLookAndFeel() override = default;

    void setAccentColour(juce::Colour colour) noexcept;
    [[nodiscard]] juce::Colour getAccentColour() const noexcept;

    [[nodiscard]] static juce::Colour backgroundTop() noexcept;
    [[nodiscard]] static juce::Colour backgroundBottom() noexcept;
    [[nodiscard]] static juce::Colour surface() noexcept;
    [[nodiscard]] static juce::Colour hairline() noexcept;
    [[nodiscard]] static juce::Colour primaryText() noexcept;
    [[nodiscard]] static juce::Colour secondaryText() noexcept;
    [[nodiscard]] static juce::Colour focusColour() noexcept;

    void drawRotarySlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosition,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& graphics,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& graphics,
                        juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& graphics,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawLabel(juce::Graphics& graphics, juce::Label& label) override;
    [[nodiscard]] juce::Font getTextButtonFont(juce::TextButton& button,
                                                int buttonHeight) override;
    [[nodiscard]] juce::Font getLabelFont(juce::Label& label) override;

    void drawCornerResizer(juce::Graphics& graphics,
                           int width,
                           int height,
                           bool isMouseOver,
                           bool isMouseDragging) override;

private:
    juce::Colour accentColour_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OceanLookAndFeel)
};
} // namespace amanita::ui
