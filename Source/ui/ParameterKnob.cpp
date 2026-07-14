#include "ParameterKnob.h"

#include <utility>

namespace amanita::ui
{
namespace
{
[[nodiscard]] juce::Font controlLabelFont(float height)
{
    return juce::Font { juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                          height,
                                          juce::Font::bold)
                            .withKerningFactor(0.075f) };
}

[[nodiscard]] juce::Font valueFont(float height)
{
    return juce::Font { juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                          height,
                                          juce::Font::plain) };
}
} // namespace

ParameterKnob::ParameterKnob(juce::AudioProcessorValueTreeState& state,
                             const juce::String& parameterId,
                             const juce::String& displayName,
                             Formatter formatter,
                             bool heroControl)
    : parameterId_(parameterId),
      heroControl_(heroControl),
      formatter_(std::move(formatter)),
      parameter_(state.getParameter(parameterId_))
{
    jassert(parameter_ != nullptr);
    setComponentID("knob-" + parameterId_);

    slider_.setComponentID(parameterId_);
    slider_.setAccessible(true);
    slider_.setName(displayName);
    slider_.setTitle(displayName);
    slider_.setDescription(displayName + " parameter");
    slider_.setHelpText("Drag vertically to change. Double-click to restore the default value.");
    slider_.setTooltip(displayName);
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setScrollWheelEnabled(true);
    slider_.setWantsKeyboardFocus(true);
    slider_.getProperties().set("hero", heroControl_);
    addAndMakeVisible(slider_);

    nameLabel_.setText(displayName.toUpperCase(), juce::dontSendNotification);
    nameLabel_.setComponentID(parameterId_ + "-name");
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setFont(controlLabelFont(heroControl_ ? 11.0f : 10.5f));
    nameLabel_.setColour(juce::Label::textColourId,
                         juce::Colour::fromRGB(154, 168, 167));
    nameLabel_.setInterceptsMouseClicks(false, false);
    nameLabel_.setAccessible(false);
    addAndMakeVisible(nameLabel_);

    valueLabel_.setComponentID(parameterId_ + "-value");
    valueLabel_.setAccessible(true);
    valueLabel_.setName(displayName + " value");
    valueLabel_.setTitle(displayName + " value");
    valueLabel_.setDescription("Editable value for " + displayName);
    valueLabel_.setJustificationType(juce::Justification::centred);
    valueLabel_.setFont(valueFont(heroControl_ ? 35.0f : 14.0f));
    valueLabel_.setColour(juce::Label::textColourId,
                          juce::Colour::fromRGB(241, 239, 232));
    valueLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    valueLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    valueLabel_.setColour(juce::Label::textWhenEditingColourId,
                          juce::Colour::fromRGB(241, 239, 232));
    valueLabel_.setColour(juce::Label::backgroundWhenEditingColourId,
                          juce::Colour::fromRGB(17, 29, 32));
    valueLabel_.setColour(juce::Label::outlineWhenEditingColourId,
                          juce::Colour::fromRGB(226, 192, 131));
    valueLabel_.setEditable(true, false, false);
    valueLabel_.setWantsKeyboardFocus(true);
    addAndMakeVisible(valueLabel_);

    slider_.onValueChange = [this]
    {
        updateDisplayedValue();
        repaint();
    };
    valueLabel_.onTextChange = [this]
    {
        slider_.setValue(slider_.getValueFromText(valueLabel_.getText()),
                         juce::dontSendNotification);

        if (parameter_ != nullptr)
        {
            parameter_->beginChangeGesture();
            parameter_->setValueNotifyingHost(
                parameter_->convertTo0to1(static_cast<float>(slider_.getValue())));
            parameter_->endChangeGesture();
        }

        updateDisplayedValue();
    };

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        state, parameterId_, slider_);
    updateDisplayedValue();
}

ParameterKnob::~ParameterKnob() = default;

void ParameterKnob::resized()
{
    const auto bounds = getLocalBounds();
    if (heroControl_)
    {
        const auto scale = juce::jmin(static_cast<float>(bounds.getWidth()) / 240.0f,
                                     static_cast<float>(bounds.getHeight()) / 268.0f);
        const auto dialSize = juce::roundToInt(224.0f * scale);
        const auto nameTop = bounds.getY() + dialSize + juce::roundToInt(2.0f * scale);
        const auto nameHeight = juce::roundToInt(18.0f * scale);
        const auto valueHeight = juce::roundToInt(24.0f * scale);

        slider_.setBounds(bounds.getCentreX() - dialSize / 2,
                          bounds.getY(),
                          dialSize,
                          dialSize);
        nameLabel_.setFont(controlLabelFont(11.0f * scale));
        valueLabel_.setFont(valueFont(18.0f * scale));
        nameLabel_.setBounds(bounds.getX(), nameTop, bounds.getWidth(), nameHeight);
        valueLabel_.setBounds(bounds.getX(),
                              nameTop + nameHeight,
                              bounds.getWidth(),
                              valueHeight);
        return;
    }

    const auto scale = juce::jmin(static_cast<float>(bounds.getWidth()) / 120.0f,
                                 static_cast<float>(bounds.getHeight()) / 126.0f);
    const auto dialSize = juce::roundToInt(80.0f * scale);
    const auto nameTop = bounds.getY() + dialSize + juce::roundToInt(scale);
    const auto nameHeight = juce::roundToInt(18.0f * scale);
    const auto valueInset = juce::roundToInt(5.0f * scale);
    const auto valueHeight = juce::roundToInt(24.0f * scale);
    nameLabel_.setFont(controlLabelFont(10.5f * scale));
    valueLabel_.setFont(valueFont(14.0f * scale));
    slider_.setBounds(bounds.getCentreX() - dialSize / 2, bounds.getY(), dialSize, dialSize);
    nameLabel_.setBounds(bounds.getX(), nameTop, bounds.getWidth(), nameHeight);
    valueLabel_.setBounds(bounds.getX() + valueInset,
                          nameTop + nameHeight,
                          bounds.getWidth() - valueInset * 2,
                          valueHeight);
}

void ParameterKnob::setFocusOrder(int order)
{
    slider_.setExplicitFocusOrder(order);
    valueLabel_.setExplicitFocusOrder(order + 100);
}

juce::Slider& ParameterKnob::getSlider() noexcept
{
    return slider_;
}

const juce::Slider& ParameterKnob::getSlider() const noexcept
{
    return slider_;
}

juce::Label& ParameterKnob::getValueLabel() noexcept
{
    return valueLabel_;
}

void ParameterKnob::updateDisplayedValue()
{
    const auto text = formatter_ != nullptr
        ? formatter_(slider_.getValue())
        : slider_.getTextFromValue(slider_.getValue());
    valueLabel_.setText(text, juce::dontSendNotification);
}
} // namespace amanita::ui
