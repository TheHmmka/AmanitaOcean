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
