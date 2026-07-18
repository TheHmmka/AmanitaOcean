#include "CharacterSelector.h"
#include "CharacterPalette.h"

#include <cmath>

namespace amanita::ui
{
namespace
{
constexpr auto parameterId = "algorithm";
constexpr int radioGroupId = 0x414d;
constexpr float selectedTextAlpha = 0.98f;
constexpr float inactiveTextAlpha = 0.15f;

constexpr std::array<const char*, CharacterSelector::characterCount> characterNames {
    "Default", "Bloom", "Drift", "Veil"
};

constexpr std::array<const char*, CharacterSelector::characterCount> componentIds {
    "character-default", "character-bloom", "character-drift", "character-veil"
};

constexpr std::array<const char*, CharacterSelector::characterCount> descriptions {
    "Selects the balanced Default reverb character.",
    "Selects the expanding Bloom reverb character.",
    "Selects the moving Drift reverb character.",
    "Selects the softened Veil reverb character."
};

class SegmentButton final : public juce::TextButton
{
public:
    explicit SegmentButton(const juce::String& name)
        : juce::TextButton(name)
    {
    }

    void setSelectionProgress(float progress)
    {
        const auto nextProgress = juce::jlimit(0.0f, 1.0f, progress);
        if (std::abs(selectionProgress_ - nextProgress) <= 1.0e-6f)
            return;

        selectionProgress_ = nextProgress;
        repaint();
    }

    void paintButton(juce::Graphics& graphics, bool, bool) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto inactiveColour = findColour(juce::TextButton::textColourOffId);
        const auto selectedColour = findColour(juce::TextButton::textColourOnId);
        graphics.setColour(inactiveColour.interpolatedWith(selectedColour,
                                                           selectionProgress_)
                               .withMultipliedAlpha(isEnabled() ? 1.0f : 0.45f));

        const auto fontHeight = juce::jlimit(10.5f, 21.0f, bounds.getHeight() * 0.31f);
        graphics.setFont(getLookAndFeel().getTextButtonFont(*this, getHeight())
                             .withHeight(fontHeight)
                             .withExtraKerningFactor(0.045f));
        graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(7, 2),
                                juce::Justification::centred, 1, 0.8f);

    }

private:
    float selectionProgress_ = 0.0f;
};
} // namespace

CharacterSelector::CharacterSelector(juce::AudioProcessorValueTreeState& state)
{
    setComponentID("character-selector");
    setAccessible(true);
    setTitle("Reverb character");
    setDescription("Selects the reverb character. Use the left and right arrow keys to navigate.");
    setHelpText("Choose Default, Bloom, Drift, or Veil.");
    setWantsKeyboardFocus(true);
    setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);

    parameterCombo_.setName("Reverb character parameter");
    parameterCombo_.setComponentID(parameterId);
    parameterCombo_.setAccessible(false);
    parameterCombo_.setVisible(false);
    addChildComponent(parameterCombo_);

    for (int index = 0; index < characterCount; ++index)
    {
        const auto arrayIndex = static_cast<std::size_t>(index);
        parameterCombo_.addItem(characterNames[arrayIndex], index + 1);

        auto button = std::make_unique<SegmentButton>(characterNames[arrayIndex]);
        button->setAccessible(true);
        button->setWantsKeyboardFocus(false);
        button->getProperties().set("characterSegment", true);
        button->setComponentID(componentIds[arrayIndex]);
        button->setTitle(juce::String(characterNames[arrayIndex]) + " reverb character");
        button->setDescription(descriptions[arrayIndex]);
        button->setHelpText("Activate this button to select the character.");
        button->setClickingTogglesState(true);
        button->setRadioGroupId(radioGroupId, juce::dontSendNotification);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button->setColour(juce::TextButton::textColourOffId,
                          juce::Colours::white.withAlpha(inactiveTextAlpha));
        button->setColour(juce::TextButton::textColourOnId,
                          characterAccent(index).withAlpha(selectedTextAlpha));
        button->onClick = [this, index]
        {
            selectIndex(index);
        };

        addAndMakeVisible(*button);
        buttons_[arrayIndex] = std::move(button);
    }

    parameterCombo_.onChange = [this]
    {
        updateButtonStates(true);

        const auto selectedIndex = getSelectedIndex();
        if (selectedIndex == lastNotifiedIndex_)
            return;

        lastNotifiedIndex_ = selectedIndex;
        if (onSelectionChanged != nullptr)
            onSelectionChanged(selectedIndex);
    };

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, parameterId, parameterCombo_);

    updateButtonStates(false);
    lastNotifiedIndex_ = getSelectedIndex();
}

CharacterSelector::~CharacterSelector()
{
    stopTimer();
    parameterCombo_.onChange = nullptr;

    for (auto& button : buttons_)
        button->onClick = nullptr;

    attachment_.reset();
}

void CharacterSelector::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (bounds.isEmpty())
        return;

    const auto scale = juce::jlimit(0.75f, 1.50f, bounds.getHeight() / 44.0f);
    const auto cornerRadius = 9.0f * scale;
    graphics.setColour(juce::Colours::black.withAlpha(0.18f));
    graphics.fillRoundedRectangle(bounds, cornerRadius);

    graphics.setColour(juce::Colours::white.withAlpha(0.075f));
    graphics.drawRoundedRectangle(bounds, cornerRadius, 1.0f * scale);

    graphics.setColour(juce::Colours::white.withAlpha(0.055f));
    const auto segmentWidth = bounds.getWidth() / static_cast<float>(characterCount);
    for (int index = 1; index < characterCount; ++index)
    {
        const auto x = bounds.getX() + segmentWidth * static_cast<float>(index);
        graphics.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 9.0f * scale,
                                  bounds.getBottom() - 9.0f * scale);
    }

}

void CharacterSelector::resized()
{
    const auto bounds = getLocalBounds();

    for (int index = 0; index < characterCount; ++index)
    {
        const auto left = bounds.getX() + bounds.getWidth() * index / characterCount;
        const auto right = bounds.getX() + bounds.getWidth() * (index + 1) / characterCount;
        buttons_[static_cast<std::size_t>(index)]->setBounds(left, bounds.getY(),
                                                             right - left, bounds.getHeight());
    }

    parameterCombo_.setBounds({});
}

bool CharacterSelector::keyPressed(const juce::KeyPress& key)
{
    if (key.isKeyCode(juce::KeyPress::leftKey))
    {
        moveSelection(-1);
        return true;
    }

    if (key.isKeyCode(juce::KeyPress::rightKey))
    {
        moveSelection(1);
        return true;
    }

    return false;
}

int CharacterSelector::getSelectedIndex() const noexcept
{
    return parameterCombo_.getSelectedItemIndex();
}

void CharacterSelector::selectIndex(int index)
{
    if (! juce::isPositiveAndBelow(index, characterCount))
        return;

    if (index != getSelectedIndex())
    {
        const juce::Component::SafePointer<CharacterSelector> safeThis(this);
        parameterCombo_.setSelectedItemIndex(index, juce::sendNotificationSync);
        if (safeThis == nullptr)
            return;
    }
    else
        updateButtonStates(true);

}

void CharacterSelector::moveSelection(int offset)
{
    auto selectedIndex = getSelectedIndex();
    if (! juce::isPositiveAndBelow(selectedIndex, characterCount))
        selectedIndex = 0;

    const auto wrappedIndex = (selectedIndex + offset + characterCount) % characterCount;
    selectIndex(wrappedIndex);
}

void CharacterSelector::updateButtonStates(bool animate)
{
    const auto selectedIndex = getSelectedIndex();

    for (int index = 0; index < characterCount; ++index)
        buttons_[static_cast<std::size_t>(index)]->setToggleState(
            index == selectedIndex, juce::dontSendNotification);

    if (animate)
    {
        startTimerHz(60);
    }
    else
    {
        stopTimer();
        for (int index = 0; index < characterCount; ++index)
        {
            const auto arrayIndex = static_cast<std::size_t>(index);
            selectionProgress_[arrayIndex] = index == selectedIndex ? 1.0f : 0.0f;
            static_cast<SegmentButton&>(*buttons_[arrayIndex])
                .setSelectionProgress(selectionProgress_[arrayIndex]);
        }
    }

    repaint();
}

void CharacterSelector::timerCallback()
{
    constexpr auto transitionAmount = 0.16f;
    constexpr auto completionThreshold = 0.002f;
    const auto selectedIndex = getSelectedIndex();
    auto isAnimating = false;

    for (int index = 0; index < characterCount; ++index)
    {
        const auto arrayIndex = static_cast<std::size_t>(index);
        const auto target = index == selectedIndex ? 1.0f : 0.0f;
        const auto difference = target - selectionProgress_[arrayIndex];

        if (std::abs(difference) > completionThreshold)
        {
            selectionProgress_[arrayIndex] += difference * transitionAmount;
            isAnimating = true;
        }
        else
        {
            selectionProgress_[arrayIndex] = target;
        }

        static_cast<SegmentButton&>(*buttons_[arrayIndex])
            .setSelectionProgress(selectionProgress_[arrayIndex]);
    }

    if (! isAnimating)
        stopTimer();
}
} // namespace amanita::ui
