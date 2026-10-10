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
    [[nodiscard]] static juce::Colour labelText() noexcept;
    [[nodiscard]] static juce::Colour focusColour() noexcept;
    // The text colour on a filled area: Ocean's darkest tone or white,
    // whichever has the higher contrast ratio against the fill.
    [[nodiscard]] static juce::Colour textOn(juce::Colour fill) noexcept;

    void drawRotarySlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosition,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& graphics,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& graphics,
                      int width,
                      int height,
                      bool isButtonDown,
                      int buttonX,
                      int buttonY,
                      int buttonWidth,
                      int buttonHeight,
                      juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    // A chevron that stands beside a drop-down and steps through its items:
    // the drop-down's own chevron turned on its side, drawn in the middle of
    // an area as high as the field, over a dark line that keeps its outline
    // on a light ground. It has the tone of the field's chevron at emphasis 0
    // and the accent at 1.
    void drawStepChevron(juce::Graphics& graphics,
                         juce::Rectangle<float> area,
                         bool pointsRight,
                         float emphasis) const;
    [[nodiscard]] juce::Font getComboBoxFont(juce::ComboBox& box) override;
    [[nodiscard]] juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(
        juce::ComboBox& box,
        juce::Label& label) override;

    void drawPopupMenuBackgroundWithOptions(juce::Graphics& graphics,
                                            int width,
                                            int height,
                                            const juce::PopupMenu::Options& options) override;
    void drawPopupMenuItemWithOptions(juce::Graphics& graphics,
                                      const juce::Rectangle<int>& area,
                                      bool isHighlighted,
                                      const juce::PopupMenu::Item& item,
                                      const juce::PopupMenu::Options& options) override;
    void getIdealPopupMenuItemSizeWithOptions(const juce::String& text,
                                              bool isSeparator,
                                              int standardMenuItemHeight,
                                              int& idealWidth,
                                              int& idealHeight,
                                              const juce::PopupMenu::Options& options) override;
    [[nodiscard]] int getPopupMenuBorderSizeWithOptions(
        const juce::PopupMenu::Options& options) override;

    void drawLabel(juce::Graphics& graphics, juce::Label& label) override;
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
