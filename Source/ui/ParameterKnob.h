#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace amanita::ui
{
class ParameterKnob final : public juce::Component
{
public:
    using Formatter = std::function<juce::String(double)>;

    // Design size of a hero control and of the dial at its top; its name and
    // its value take the rest of the height.
    static constexpr int heroWidth = 208;
    static constexpr int heroHeight = 238;
    static constexpr int heroDialSize = 194;

    ParameterKnob(juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterId,
                  const juce::String& displayName,
                  Formatter formatter,
                  bool heroControl = false);
    ~ParameterKnob() override;

    void resized() override;
    void setFocusOrder(int order);

    [[nodiscard]] juce::Slider& getSlider() noexcept;
    [[nodiscard]] const juce::Slider& getSlider() const noexcept;
    [[nodiscard]] juce::Label& getValueLabel() noexcept;
    // The baseline of the value in this component's coordinates: the foot of
    // its digits.
    [[nodiscard]] float getValueBaseline();

private:
    void updateDisplayedValue();

    const juce::String parameterId_;
    const bool heroControl_;
    Formatter formatter_;
    juce::RangedAudioParameter* parameter_ = nullptr;
    juce::Slider slider_;
    juce::Label nameLabel_;
    juce::Label valueLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterKnob)
};
} // namespace amanita::ui
