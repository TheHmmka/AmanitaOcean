#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

namespace amanita::ui
{
class CharacterSelector final : public juce::Component,
                                private juce::Timer
{
public:
    static constexpr int characterCount = 4;

    explicit CharacterSelector(juce::AudioProcessorValueTreeState& state);
    ~CharacterSelector() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    [[nodiscard]] int getSelectedIndex() const noexcept;

    std::function<void(int)> onSelectionChanged;

private:
    void selectIndex(int index);
    void moveSelection(int offset);
    void updateButtonStates(bool animate);
    void timerCallback() override;

    std::array<std::unique_ptr<juce::TextButton>, characterCount> buttons_;
    juce::ComboBox parameterCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;

    std::array<float, characterCount> selectionProgress_ {};
    int lastNotifiedIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CharacterSelector)
};
} // namespace amanita::ui
