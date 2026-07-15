#include "OceanLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace amanita::ui
{
namespace
{
constexpr auto heroProperty = "hero";
constexpr auto characterSegmentProperty = "characterSegment";

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

[[nodiscard]] juce::Path makeButtonPath(const juce::Button& button,
                                         juce::Rectangle<float> bounds,
                                         float cornerRadius)
{
    juce::Path path;
    path.addRoundedRectangle(bounds.getX(),
                             bounds.getY(),
                             bounds.getWidth(),
                             bounds.getHeight(),
                             cornerRadius,
                             cornerRadius,
                             !button.isConnectedOnLeft(),
                             !button.isConnectedOnRight(),
                             !button.isConnectedOnLeft(),
                             !button.isConnectedOnRight());
    return path;
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

    setColour(juce::TextButton::buttonColourId, surface());
    setColour(juce::TextButton::buttonOnColourId, accentColour_.withAlpha(0.18f));
    setColour(juce::TextButton::textColourOffId, secondaryText());
    setColour(juce::TextButton::textColourOnId, primaryText());

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
    setColour(juce::TextButton::buttonOnColourId, accentColour_.withAlpha(0.18f));
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

juce::Colour OceanLookAndFeel::focusColour() noexcept
{
    return juce::Colour::fromRGB(112, 214, 194);
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
    const auto controlScale = juce::jlimit(0.75f, 1.50f,
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

    const auto pointerInnerRadius = dial.getWidth() * 0.08f;
    const auto pointerOuterRadius = dial.getWidth() * (isHero ? 0.32f : 0.34f);
    const auto direction = juce::Point<float> { std::sin(angle), -std::cos(angle) };
    const auto pointerStart = centre + direction * pointerInnerRadius;
    const auto pointerEnd = centre + direction * pointerOuterRadius;
    graphics.setColour(accent.withAlpha(0.92f));
    graphics.drawLine({ pointerStart, pointerEnd },
                      (isHero ? 2.0f : 1.6f) * controlScale);
    graphics.fillEllipse(juce::Rectangle<float>((isHero ? 4.2f : 3.2f) * controlScale,
                                                (isHero ? 4.2f : 3.2f) * controlScale)
                             .withCentre(pointerEnd));

    graphics.setColour(primaryText().withAlpha(0.12f));
    graphics.fillEllipse(juce::Rectangle<float>((isHero ? 4.0f : 3.0f) * controlScale,
                                                (isHero ? 4.0f : 3.0f) * controlScale)
                             .withCentre(centre));

}

void OceanLookAndFeel::drawButtonBackground(juce::Graphics& graphics,
                                            juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    juce::Graphics::ScopedSaveState saveState(graphics);
    const auto isSegment = propertyIsEnabled(button, characterSegmentProperty);
    const auto isSelected = button.getToggleState();
    const auto enabledAlpha = button.isEnabled() ? 1.0f : 0.42f;
    auto bounds = button.getLocalBounds().toFloat().reduced(0.6f);

    if (shouldDrawButtonAsDown)
        bounds = bounds.reduced(0.6f);

    const auto cornerRadius = isSegment ? 8.0f : std::min(10.0f, bounds.getHeight() * 0.25f);
    const auto path = makeButtonPath(button, bounds, cornerRadius);

    auto fill = isSegment ? surface().withAlpha(0.58f) : backgroundColour;
    if (isSelected)
        fill = accentColour_.withAlpha(shouldDrawButtonAsDown ? 0.24f : 0.17f);
    else if (shouldDrawButtonAsHighlighted)
        fill = surface().brighter(0.10f);

    graphics.setColour(fill.withMultipliedAlpha(enabledAlpha));
    graphics.fillPath(path);

    auto edge = isSelected ? accentColour_.withAlpha(0.56f)
                           : hairline().withAlpha(shouldDrawButtonAsHighlighted ? 0.95f : 0.68f);
    graphics.setColour(edge.withMultipliedAlpha(enabledAlpha));
    graphics.strokePath(path, juce::PathStrokeType(isSelected ? 1.2f : 0.8f));

    if (isSegment && isSelected)
    {
        const auto underlineWidth = std::min(26.0f, bounds.getWidth() * 0.28f);
        const auto underlineY = bounds.getBottom() - 3.0f;
        graphics.setColour(accentColour_.withAlpha(0.90f * enabledAlpha));
        graphics.drawLine(bounds.getCentreX() - underlineWidth * 0.5f,
                          underlineY,
                          bounds.getCentreX() + underlineWidth * 0.5f,
                          underlineY,
                          1.4f);
    }

    if (button.hasKeyboardFocus(true))
    {
        graphics.setColour(accentColour_.withAlpha(0.50f * enabledAlpha));
        graphics.strokePath(makeButtonPath(button, bounds.reduced(2.2f),
                                           std::max(1.0f, cornerRadius - 2.0f)),
                            juce::PathStrokeType(0.8f));
    }
}

void OceanLookAndFeel::drawButtonText(juce::Graphics& graphics,
                                      juce::TextButton& button,
                                      bool,
                                      bool shouldDrawButtonAsDown)
{
    const auto isSelected = button.getToggleState();
    const auto isSegment = propertyIsEnabled(button, characterSegmentProperty);
    const auto alpha = button.isEnabled() ? 1.0f : 0.42f;
    auto colour = isSelected ? primaryText() : secondaryText();

    if (isSelected && isSegment)
        colour = accentColour_.interpolatedWith(primaryText(), 0.42f);

    graphics.setColour(colour.withMultipliedAlpha(alpha));
    graphics.setFont(getTextButtonFont(button, button.getHeight()));

    auto textBounds = button.getLocalBounds().reduced(7, 2);
    if (shouldDrawButtonAsDown)
        textBounds.translate(0, 1);

    graphics.drawFittedText(button.getButtonText(),
                            textBounds,
                            juce::Justification::centred,
                            1,
                            0.82f);
}

void OceanLookAndFeel::drawToggleButton(juce::Graphics& graphics,
                                        juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted,
                                        bool shouldDrawButtonAsDown)
{
    juce::Graphics::ScopedSaveState saveState(graphics);
    const auto isOn = button.getToggleState();
    const auto alpha = button.isEnabled() ? 1.0f : 0.40f;
    const auto controlScale = juce::jlimit(0.75f, 1.50f,
                                           static_cast<float>(button.getHeight()) / 34.0f);
    auto bounds = button.getLocalBounds().toFloat().reduced(0.7f * controlScale);

    if (shouldDrawButtonAsDown)
        bounds = bounds.reduced(0.6f * controlScale);

    const auto radius = bounds.getHeight() * 0.5f;
    auto fill = isOn ? accentColour_.withAlpha(0.16f) : surface().withAlpha(0.72f);
    if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.08f);

    if (isOn)
    {
        graphics.setColour(accentColour_.withAlpha(0.055f * alpha));
        graphics.fillRoundedRectangle(bounds.expanded(2.2f * controlScale),
                                      radius + 2.2f * controlScale);
    }

    graphics.setColour(fill.withMultipliedAlpha(alpha));
    graphics.fillRoundedRectangle(bounds, radius);
    graphics.setColour((isOn ? accentColour_.withAlpha(0.58f) : hairline().withAlpha(0.82f))
                           .withMultipliedAlpha(alpha));
    graphics.drawRoundedRectangle(bounds, radius,
                                  (isOn ? 1.2f : 0.8f) * controlScale);

    const auto indicatorArea = bounds.removeFromLeft(bounds.getHeight());
    const auto indicatorCentre = indicatorArea.getCentre();
    const auto indicatorDiameter = std::max(4.0f, indicatorArea.getHeight() * 0.22f);

    graphics.setColour((isOn ? accentColour_ : secondaryText().withAlpha(0.45f))
                           .withMultipliedAlpha(alpha));
    graphics.fillEllipse(juce::Rectangle<float>(indicatorDiameter, indicatorDiameter)
                             .withCentre(indicatorCentre));

    if (isOn)
    {
        graphics.setColour(accentColour_.withAlpha(0.17f * alpha));
        graphics.drawEllipse(juce::Rectangle<float>(indicatorDiameter
                                                        + 6.0f * controlScale,
                                                    indicatorDiameter
                                                        + 6.0f * controlScale)
                                 .withCentre(indicatorCentre),
                             2.0f * controlScale);
    }

    graphics.setFont(systemFont(
        juce::jlimit(9.0f, 17.0f, static_cast<float>(button.getHeight()) * 0.32f),
        true,
        0.055f));
    graphics.setColour((isOn ? primaryText() : secondaryText()).withMultipliedAlpha(alpha));
    graphics.drawFittedText(button.getButtonText(),
                            bounds.reduced(2.0f * controlScale, 0.0f).toNearestInt(),
                            juce::Justification::centredLeft,
                            1,
                            0.86f);

    if (button.hasKeyboardFocus(false))
    {
        graphics.setColour(focusColour().interpolatedWith(accentColour_, 0.55f)
                               .withAlpha(0.78f * alpha));
        graphics.drawRoundedRectangle(button.getLocalBounds().toFloat()
                                          .reduced(2.5f * controlScale),
                                      radius - 1.5f * controlScale,
                                      1.0f * controlScale);
    }
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

    if (label.hasKeyboardFocus(false) && !label.isBeingEdited())
    {
        graphics.setColour(focusColour().interpolatedWith(accentColour_, 0.55f)
                               .withAlpha(0.78f));
        graphics.drawRoundedRectangle(label.getLocalBounds().toFloat().reduced(1.5f),
                                      4.0f,
                                      1.0f);
    }
}

juce::Font OceanLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    const auto isSegment = propertyIsEnabled(button, characterSegmentProperty);
    const auto height = std::min(isSegment ? 13.0f : 14.0f,
                                 static_cast<float>(buttonHeight) * 0.40f);
    return systemFont(std::max(9.0f, height), true, isSegment ? 0.045f : 0.02f);
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
