#include "OceanLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace amanita::ui
{
namespace
{
constexpr auto heroProperty = "hero";
constexpr auto suppressFocusOutlineProperty = "suppressFocusOutline";

// Design sizes of a drop-down and of its list, for a field 40 px high.
namespace choice
{
constexpr float fieldHeight = 40.0f;
constexpr float fieldCornerRadius = 10.0f;
constexpr float fieldTextInset = 32.0f;
constexpr float fieldFontHeight = 12.5f;
constexpr float fieldTracking = 0.025f;
constexpr float chevronInset = 16.0f;
constexpr float chevronHalfWidth = 3.5f;
constexpr float chevronRise = 1.7f;
constexpr float chevronDrop = 1.9f;
constexpr float chevronStroke = 1.35f;
constexpr float listGap = 4.0f;
constexpr float listPadding = 6.0f;
constexpr float listCornerRadius = 10.0f;
constexpr float itemHeight = 32.0f;
constexpr float itemInset = 6.0f;
constexpr float itemCornerRadius = 7.0f;
constexpr float itemTextInset = 36.0f;
constexpr float itemMarkCentre = 20.0f;
constexpr float itemMarkSize = 6.0f;
constexpr float separatorHeight = 9.0f;
} // namespace choice

[[nodiscard]] bool propertyIsEnabled(const juce::Component& component, const char* property)
{
    return static_cast<bool>(component.getProperties().getWithDefault(
        juce::Identifier(property), false));
}

[[nodiscard]] juce::Font systemFont(float height, bool bold, float tracking = 0.0f)
{
    const auto style = bold ? juce::Font::bold : juce::Font::plain;
    const auto options = juce::FontOptions {
        juce::Font::getDefaultSansSerifFontName(), height, style
    }.withKerningFactor(tracking);
    return juce::Font(options);
}

void drawOpticallyCentredText(juce::Graphics& graphics,
                              const juce::String& text,
                              const juce::Font& font,
                              juce::Colour colour,
                              juce::Rectangle<float> area,
                              bool centredHorizontally)
{
    if (text.isEmpty() || area.isEmpty())
        return;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    if (glyphs.getNumGlyphs() == 0)
        return;

    auto glyphBounds = glyphs.getBoundingBox(0, -1, false);
    if (glyphBounds.getWidth() > area.getWidth() && glyphBounds.getWidth() > 0.0f)
    {
        glyphs.stretchRangeOfGlyphs(0, -1, area.getWidth() / glyphBounds.getWidth());
        glyphBounds = glyphs.getBoundingBox(0, -1, false);
    }

    const auto targetX = centredHorizontally
        ? area.getCentreX() - glyphBounds.getCentreX()
        : area.getX() - glyphBounds.getX();
    const auto targetY = area.getCentreY() - glyphBounds.getCentreY();
    glyphs.moveRangeOfGlyphs(0, -1, targetX, targetY);
    graphics.setColour(colour);
    glyphs.draw(graphics);
}

// A drop-down scales with its own height, which the editor sets from its canvas.
[[nodiscard]] float choiceScale(const juce::ComboBox& box)
{
    return juce::jlimit(0.65f, 1.75f,
                        static_cast<float>(box.getHeight()) / choice::fieldHeight);
}

// A list opened from a drop-down takes that drop-down's scale.
[[nodiscard]] float choiceScale(const juce::PopupMenu::Options& options)
{
    if (const auto* box = dynamic_cast<const juce::ComboBox*>(options.getTargetComponent()))
        return choiceScale(*box);

    return 1.0f;
}

// The text of a drop-down and of the items in its list.
[[nodiscard]] juce::Font choiceFont(float scale)
{
    return systemFont(choice::fieldFontHeight * scale, true, choice::fieldTracking);
}

// Relative luminance of an opaque sRGB colour, as WCAG 2 defines it.
[[nodiscard]] float relativeLuminance(juce::Colour colour) noexcept
{
    const auto linear = [](float channel)
    {
        return channel <= 0.04045f ? channel / 12.92f
                                   : std::pow((channel + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * linear(colour.getFloatRed())
         + 0.7152f * linear(colour.getFloatGreen())
         + 0.0722f * linear(colour.getFloatBlue());
}

// Contrast ratio of two opaque colours, 1 to 21, as WCAG 2 defines it.
[[nodiscard]] float contrastRatio(juce::Colour first, juce::Colour second) noexcept
{
    const auto firstLuminance = relativeLuminance(first);
    const auto secondLuminance = relativeLuminance(second);
    return (std::max(firstLuminance, secondLuminance) + 0.05f)
         / (std::min(firstLuminance, secondLuminance) + 0.05f);
}

// The fill of the highlighted item of a list.
[[nodiscard]] juce::Colour listHighlight(juce::Colour accent) noexcept
{
    return accent.withAlpha(0.88f);
}

// The text on that fill as it is painted, over the list's surface.
[[nodiscard]] juce::Colour listHighlightText(juce::Colour accent) noexcept
{
    return OceanLookAndFeel::textOn(
        OceanLookAndFeel::surface().overlaidWith(listHighlight(accent)));
}

void addCentredArc(juce::Path& path,
                   juce::Point<float> centre,
                   float radius,
                   float startAngle,
                   float endAngle)
{
    path.addCentredArc(centre.x,
                       centre.y,
                       radius,
                       radius,
                       0.0f,
                       startAngle,
                       endAngle,
                       true);
}
} // namespace

OceanLookAndFeel::OceanLookAndFeel()
    : accentColour_(focusColour())
{
    setColour(juce::Slider::rotarySliderOutlineColourId, hairline());
    setColour(juce::Slider::rotarySliderFillColourId, accentColour_);
    setColour(juce::Slider::thumbColourId, accentColour_);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, primaryText());

    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, primaryText());

    setColour(juce::ComboBox::textColourId, primaryText());

    // A list background short of opaque makes JUCE open a non-opaque window,
    // which the rounded corners of the list need.
    setColour(juce::PopupMenu::backgroundColourId,
              surface().withAlpha(static_cast<juce::uint8>(254)));
    setColour(juce::PopupMenu::textColourId, primaryText());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, listHighlight(accentColour_));
    setColour(juce::PopupMenu::highlightedTextColourId, listHighlightText(accentColour_));

    setColour(juce::ToggleButton::textColourId, primaryText());
    setColour(juce::ToggleButton::tickColourId, accentColour_);
    setColour(juce::ToggleButton::tickDisabledColourId,
              secondaryText().withMultipliedAlpha(0.45f));

    setColour(juce::ResizableWindow::backgroundColourId, backgroundBottom());
    setColour(juce::TooltipWindow::backgroundColourId, surface());
    setColour(juce::TooltipWindow::textColourId, primaryText());
    setColour(juce::TooltipWindow::outlineColourId, hairline());
}

void OceanLookAndFeel::setAccentColour(juce::Colour colour) noexcept
{
    accentColour_ = colour;
    setColour(juce::Slider::rotarySliderFillColourId, accentColour_);
    setColour(juce::Slider::thumbColourId, accentColour_);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, listHighlight(accentColour_));
    setColour(juce::PopupMenu::highlightedTextColourId, listHighlightText(accentColour_));
    setColour(juce::ToggleButton::tickColourId, accentColour_);
}

juce::Colour OceanLookAndFeel::getAccentColour() const noexcept
{
    return accentColour_;
}

juce::Colour OceanLookAndFeel::backgroundTop() noexcept
{
    return juce::Colour::fromRGB(13, 26, 29);
}

juce::Colour OceanLookAndFeel::backgroundBottom() noexcept
{
    return juce::Colour::fromRGB(5, 11, 13);
}

juce::Colour OceanLookAndFeel::surface() noexcept
{
    return juce::Colour::fromRGB(17, 34, 38);
}

juce::Colour OceanLookAndFeel::hairline() noexcept
{
    return juce::Colour::fromRGB(43, 62, 66);
}

juce::Colour OceanLookAndFeel::primaryText() noexcept
{
    return juce::Colour::fromRGB(241, 237, 228);
}

juce::Colour OceanLookAndFeel::secondaryText() noexcept
{
    return juce::Colour::fromRGB(133, 153, 156);
}

juce::Colour OceanLookAndFeel::labelText() noexcept
{
    return juce::Colour::fromRGB(154, 168, 167);
}

juce::Colour OceanLookAndFeel::focusColour() noexcept
{
    return juce::Colour::fromRGB(112, 214, 194);
}

juce::Colour OceanLookAndFeel::textOn(juce::Colour fill) noexcept
{
    const auto dark = backgroundBottom();
    const auto light = juce::Colours::white;
    return contrastRatio(fill, dark) >= contrastRatio(fill, light) ? dark : light;
}

void OceanLookAndFeel::drawRotarySlider(juce::Graphics& graphics,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        float sliderPosition,
                                        float rotaryStartAngle,
                                        float rotaryEndAngle,
                                        juce::Slider& slider)
{
    juce::Graphics::ScopedSaveState saveState(graphics);

    const auto isHero = propertyIsEnabled(slider, heroProperty);
    const auto isActive = slider.isEnabled();
    const auto position = juce::jlimit(0.0f, 1.0f, sliderPosition);
    const auto angle = rotaryStartAngle + position * (rotaryEndAngle - rotaryStartAngle);
    const auto accent = accentColour_.withMultipliedAlpha(isActive ? 1.0f : 0.35f);

    auto available = juce::Rectangle<float>(static_cast<float>(x),
                                             static_cast<float>(y),
                                             static_cast<float>(width),
                                             static_cast<float>(height));
    const auto referenceDiameter = isHero ? 224.0f : 80.0f;
    const auto controlScale = juce::jlimit(0.65f, 1.50f,
                                           std::min(available.getWidth(), available.getHeight())
                                               / referenceDiameter);
    available = available.reduced((isHero ? 8.0f : 6.0f) * controlScale);
    const auto diameter = std::max(0.0f, std::min(available.getWidth(), available.getHeight()));
    const auto outer = available.withSizeKeepingCentre(diameter, diameter);
    const auto centre = outer.getCentre();
    const auto radius = diameter * 0.5f;

    if (radius <= 3.0f)
        return;

    if (isHero)
    {
        constexpr auto markerCount = 11;
        for (auto marker = 0; marker < markerCount; ++marker)
        {
            const auto markerPosition = static_cast<float>(marker)
                / static_cast<float>(markerCount - 1);
            const auto markerAngle = rotaryStartAngle
                + markerPosition * (rotaryEndAngle - rotaryStartAngle);
            const auto markerRadius = radius + 2.5f * controlScale;
            const auto markerCentre = centre + juce::Point<float> {
                std::sin(markerAngle) * markerRadius,
                -std::cos(markerAngle) * markerRadius
            };
            const auto isFilled = markerPosition <= position;
            graphics.setColour(isFilled ? accent.withAlpha(0.52f)
                                        : hairline().withAlpha(0.72f));
            graphics.fillEllipse(juce::Rectangle<float>(1.8f * controlScale,
                                                        1.8f * controlScale)
                                     .withCentre(markerCentre));
        }
    }

    const auto ringWidth = (isHero ? 4.0f : 2.6f) * controlScale;
    const auto ringRadius = radius - ringWidth * 0.6f;
    juce::Path track;
    addCentredArc(track, centre, ringRadius, rotaryStartAngle, rotaryEndAngle);
    graphics.setColour(hairline().withMultipliedAlpha(isActive ? 0.72f : 0.34f));
    graphics.strokePath(track,
                        juce::PathStrokeType(ringWidth,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (position > 0.0001f)
    {
        juce::Path valueArc;
        addCentredArc(valueArc, centre, ringRadius, rotaryStartAngle, angle);

        graphics.setColour(accent.withAlpha(isHero ? 0.10f : 0.075f));
        graphics.strokePath(valueArc,
                            juce::PathStrokeType(ringWidth
                                                     + (isHero ? 7.0f : 5.0f) * controlScale,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        graphics.setColour(accent);
        graphics.strokePath(valueArc,
                            juce::PathStrokeType(ringWidth,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    auto dial = outer.reduced((isHero ? 13.0f : 10.0f) * controlScale);
    graphics.setColour(juce::Colours::black.withAlpha(0.34f));
    graphics.fillEllipse(dial.translated(0.0f,
                                         (isHero ? 2.5f : 1.7f) * controlScale));

    juce::ColourGradient dialGradient(surface().brighter(0.11f),
                                      dial.getTopLeft(),
                                      backgroundBottom().brighter(0.08f),
                                      dial.getBottomRight(),
                                      false);
    dialGradient.addColour(0.46, surface().darker(0.12f));
    graphics.setGradientFill(dialGradient);
    graphics.fillEllipse(dial);

    graphics.setColour(hairline().brighter(0.07f).withAlpha(0.88f));
    graphics.drawEllipse(dial.reduced(0.5f * controlScale), 1.0f * controlScale);
    graphics.setColour(primaryText().withAlpha(0.045f));
    graphics.drawEllipse(dial.reduced((isHero ? 5.0f : 3.5f) * controlScale),
                         0.8f * controlScale);

}

void OceanLookAndFeel::drawToggleButton(juce::Graphics& graphics,
                                        juce::ToggleButton& button,
                                        bool,
                                        bool)
{
    juce::Graphics::ScopedSaveState saveState(graphics);
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.7f);
    const auto isOn = button.getToggleState();
    const auto enabledAlpha = button.isEnabled() ? 1.0f : 0.40f;
    const auto controlScale = juce::jlimit(0.65f, 1.75f, bounds.getHeight() / 38.6f);
    const auto radius = bounds.getHeight() * 0.5f;

    graphics.setColour(surface().withAlpha(0.74f).withMultipliedAlpha(enabledAlpha));
    graphics.fillRoundedRectangle(bounds, radius);
    graphics.setColour(hairline().withAlpha(0.84f).withMultipliedAlpha(enabledAlpha));
    graphics.drawRoundedRectangle(bounds, radius, 1.0f);

    const auto dotSize = 7.0f * controlScale;
    const auto dot = juce::Rectangle<float>(dotSize, dotSize)
                         .withCentre({ bounds.getX() + 17.0f * controlScale,
                                       bounds.getCentreY() });
    graphics.setColour((isOn ? accentColour_
                             : secondaryText().darker(0.28f).withAlpha(0.66f))
                           .withMultipliedAlpha(enabledAlpha));
    graphics.fillEllipse(dot);

    if (isOn)
    {
        graphics.setColour(accentColour_.withAlpha(0.15f * enabledAlpha));
        graphics.drawEllipse(dot.expanded(3.0f * controlScale), 1.0f);
    }

    drawOpticallyCentredText(
        graphics,
        button.getButtonText().toUpperCase(),
        systemFont(11.0f * controlScale, true, 0.075f),
        (isOn ? accentColour_ : secondaryText()).withMultipliedAlpha(enabledAlpha),
        bounds.withTrimmedLeft(32.0f * controlScale)
              .withTrimmedRight(10.0f * controlScale),
        false);
}

void OceanLookAndFeel::drawComboBox(juce::Graphics& graphics,
                                    int width,
                                    int height,
                                    bool isButtonDown,
                                    int,
                                    int,
                                    int,
                                    int,
                                    juce::ComboBox& box)
{
    juce::Graphics::ScopedSaveState saveState(graphics);
    const auto scale = choiceScale(box);
    const auto bounds = juce::Rectangle<float>(static_cast<float>(width),
                                               static_cast<float>(height))
                            .reduced(0.5f);
    const auto cornerRadius = choice::fieldCornerRadius * scale;
    const auto enabledAlpha = box.isEnabled() ? 1.0f : 0.42f;
    const auto isEngaged = box.hasKeyboardFocus(true) || box.isPopupActive();

    graphics.setColour((isButtonDown ? surface().brighter(0.10f) : surface())
                           .withAlpha(0.78f * enabledAlpha));
    graphics.fillRoundedRectangle(bounds, cornerRadius);
    graphics.setColour((isEngaged ? accentColour_ : hairline())
                           .withAlpha(0.86f * enabledAlpha));
    graphics.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

    const auto chevronX = bounds.getRight() - choice::chevronInset * scale;
    const auto chevronY = bounds.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath(chevronX - choice::chevronHalfWidth * scale,
                            chevronY - choice::chevronRise * scale);
    chevron.lineTo(chevronX, chevronY + choice::chevronDrop * scale);
    chevron.lineTo(chevronX + choice::chevronHalfWidth * scale,
                   chevronY - choice::chevronRise * scale);
    graphics.setColour(secondaryText().withAlpha(0.88f * enabledAlpha));
    graphics.strokePath(chevron,
                        juce::PathStrokeType(choice::chevronStroke * scale,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void OceanLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    const auto inset = juce::roundToInt(choice::fieldTextInset * choiceScale(box));
    label.setBounds(inset, 0, box.getWidth() - inset * 2, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centred);
}

juce::Font OceanLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return choiceFont(choiceScale(box));
}

juce::PopupMenu::Options OceanLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                          juce::Label&)
{
    // The list opens under its field, as wide as the field and a gap away
    // from it. It names no item to bring into view, which would lay the
    // selected item over the field instead.
    const auto scale = choiceScale(box);
    return juce::PopupMenu::Options()
        .withTargetComponent(&box)
        .withTargetScreenArea(box.getScreenBounds().expanded(
            0, juce::roundToInt(choice::listGap * scale)))
        .withInitiallySelectedItem(box.getSelectedId())
        .withMinimumWidth(box.getWidth())
        .withMaximumNumColumns(1)
        .withStandardItemHeight(juce::roundToInt(choice::itemHeight * scale));
}

void OceanLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& graphics,
                                                          int width,
                                                          int height,
                                                          const juce::PopupMenu::Options& options)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(width),
                                               static_cast<float>(height))
                            .reduced(0.5f);
    const auto cornerRadius = choice::listCornerRadius * choiceScale(options);
    const auto background = findColour(juce::PopupMenu::backgroundColourId);

    // An opaque window has no open corners to show what lies behind it.
    if (! juce::Desktop::canUseSemiTransparentWindows())
        graphics.fillAll(background.withAlpha(1.0f));

    graphics.setColour(background);
    graphics.fillRoundedRectangle(bounds, cornerRadius);
    graphics.setColour(hairline().brighter(0.07f));
    graphics.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
}

void OceanLookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& graphics,
                                                    const juce::Rectangle<int>& area,
                                                    bool isHighlighted,
                                                    const juce::PopupMenu::Item& item,
                                                    const juce::PopupMenu::Options& options)
{
    const auto scale = choiceScale(options);
    const auto bounds = area.toFloat();

    if (item.isSeparator)
    {
        graphics.setColour(hairline().withAlpha(0.86f));
        graphics.fillRect(bounds.withSizeKeepingCentre(
            bounds.getWidth() - 2.0f * choice::itemInset * scale, 1.0f));
        return;
    }

    const auto showsHighlight = isHighlighted && item.isEnabled;
    if (showsHighlight)
    {
        graphics.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        graphics.fillRoundedRectangle(bounds.reduced(choice::itemInset * scale, scale),
                                      choice::itemCornerRadius * scale);
    }

    const auto textColour = findColour(showsHighlight ? juce::PopupMenu::highlightedTextColourId
                                                      : juce::PopupMenu::textColourId)
                                .withMultipliedAlpha(item.isEnabled ? 1.0f : 0.42f);

    if (item.isTicked)
    {
        const auto markSize = choice::itemMarkSize * scale;
        graphics.setColour(showsHighlight ? textColour : accentColour_);
        graphics.fillEllipse(juce::Rectangle<float>(markSize, markSize)
                                 .withCentre({ bounds.getX() + choice::itemMarkCentre * scale,
                                               bounds.getCentreY() }));
    }

    graphics.setColour(textColour);
    graphics.setFont(choiceFont(scale));
    graphics.drawText(item.text,
                      bounds.withTrimmedLeft(choice::itemTextInset * scale)
                            .withTrimmedRight(choice::itemTextInset * scale),
                      juce::Justification::centredLeft,
                      true);
}

void OceanLookAndFeel::getIdealPopupMenuItemSizeWithOptions(
    const juce::String& text,
    bool isSeparator,
    int standardMenuItemHeight,
    int& idealWidth,
    int& idealHeight,
    const juce::PopupMenu::Options& options)
{
    const auto scale = choiceScale(options);
    const auto insets = 2.0f * choice::itemTextInset * scale;

    if (isSeparator)
    {
        idealWidth = juce::roundToInt(insets);
        idealHeight = juce::roundToInt(choice::separatorHeight * scale);
        return;
    }

    idealWidth = juce::roundToInt(std::ceil(
        juce::GlyphArrangement::getStringWidth(choiceFont(scale), text) + insets));
    idealHeight = standardMenuItemHeight > 0
        ? standardMenuItemHeight
        : juce::roundToInt(choice::itemHeight * scale);
}

int OceanLookAndFeel::getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options& options)
{
    return juce::roundToInt(choice::listPadding * choiceScale(options));
}

void OceanLookAndFeel::drawLabel(juce::Graphics& graphics, juce::Label& label)
{
    if (label.findColour(juce::Label::backgroundColourId).isTransparent() == false)
    {
        graphics.setColour(label.findColour(juce::Label::backgroundColourId));
        graphics.fillRoundedRectangle(label.getLocalBounds().toFloat(), 5.0f);
    }

    if (!label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.42f;
        const auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());
        const auto font = getLabelFont(label);

        graphics.setColour(label.findColour(juce::Label::textColourId)
                               .withMultipliedAlpha(alpha));
        graphics.setFont(font);
        graphics.drawFittedText(label.getText(),
                                textArea,
                                label.getJustificationType(),
                                std::max(1,
                                         juce::roundToInt(static_cast<float>(textArea.getHeight())
                                                          / font.getHeight())),
                                label.getMinimumHorizontalScale());
    }

    const auto outline = label.findColour(juce::Label::outlineColourId);
    if (!outline.isTransparent())
    {
        graphics.setColour(outline.withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.42f));
        graphics.drawRoundedRectangle(label.getLocalBounds().toFloat().reduced(0.5f), 5.0f, 1.0f);
    }

    if (label.hasKeyboardFocus(false)
        && !label.isBeingEdited()
        && !propertyIsEnabled(label, suppressFocusOutlineProperty))
    {
        graphics.setColour(focusColour().interpolatedWith(accentColour_, 0.55f)
                               .withAlpha(0.78f));
        graphics.drawRoundedRectangle(label.getLocalBounds().toFloat().reduced(1.5f),
                                      4.0f,
                                      1.0f);
    }
}

juce::Font OceanLookAndFeel::getLabelFont(juce::Label& label)
{
    const auto requestedHeight = label.getFont().getHeight();
    const auto height = juce::jlimit(9.0f,
                                     std::max(9.0f, static_cast<float>(label.getHeight()) * 0.78f),
                                     requestedHeight);
    const auto isBold = (label.getFont().getStyleFlags() & juce::Font::bold) != 0;
    return systemFont(height, isBold, label.getFont().getExtraKerningFactor());
}

void OceanLookAndFeel::drawCornerResizer(juce::Graphics& graphics,
                                         int width,
                                         int height,
                                         bool isMouseOver,
                                         bool isMouseDragging)
{
    const auto emphasis = isMouseDragging ? 0.90f : (isMouseOver ? 0.66f : 0.36f);
    const auto colour = (isMouseOver || isMouseDragging) ? accentColour_ : secondaryText();
    const auto right = static_cast<float>(width) - 4.5f;
    const auto bottom = static_cast<float>(height) - 4.5f;

    graphics.setColour(colour.withAlpha(emphasis));
    for (auto index = 0; index < 3; ++index)
    {
        const auto inset = static_cast<float>(index) * 4.0f;
        const auto length = 3.0f + static_cast<float>(index) * 4.0f;
        graphics.drawLine(right - length,
                          bottom - inset,
                          right - inset,
                          bottom - length,
                          index == 0 ? 1.4f : 1.0f);
    }
}
} // namespace amanita::ui
