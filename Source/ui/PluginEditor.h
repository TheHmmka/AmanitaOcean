#pragma once

#include "PluginProcessor.h"
#include "ui/CharacterSelector.h"
#include "ui/DeepCurrentRenderer.h"
#include "ui/OceanLookAndFeel.h"
#include "ui/ParameterKnob.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

namespace amanita::ui
{
class OceanShaderBackground;
}

class AmanitaOceanAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    static constexpr int defaultWidth = 960;
    static constexpr int defaultHeight = 640;
    static constexpr int minimumWidth = 804;
    static constexpr int minimumHeight = 536;
    static constexpr int maximumWidth = 1440;
    static constexpr int maximumHeight = 960;

    explicit AmanitaOceanAudioProcessorEditor(AmanitaOceanAudioProcessor& processorToUse);
    ~AmanitaOceanAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateCharacterVisuals(int characterIndex);
    void drawBathymetricField(juce::Graphics& graphics,
                              juce::Rectangle<float> field,
                              juce::Point<float> centre,
                              float evolution) const;
    [[nodiscard]] juce::Rectangle<int> scaledBounds(float x,
                                                    float y,
                                                    float width,
                                                    float height) const;
    [[nodiscard]] juce::Point<float> evolutionDialCentre() const;
    // The outer edge of the Evolution ring from that centre, as the dial is
    // laid out.
    [[nodiscard]] float evolutionRingRadius() const;
    // The height at which the whole Evolution control has its middle: half way
    // from the top of its ring to the foot of its value's digits.
    [[nodiscard]] float evolutionControlMiddle();

    AmanitaOceanAudioProcessor& processor_;
    amanita::ui::OceanLookAndFeel lookAndFeel_;
    std::unique_ptr<amanita::ui::OceanShaderBackground> shaderBackground_;
    amanita::ui::DeepCurrentRenderer deepCurrent_;
    amanita::ui::CharacterSelector characterSelector_;
    amanita::ui::CharacterDescription characterDescription_;
    amanita::ui::ParameterKnob evolutionKnob_;
    amanita::ui::ParameterKnob preDelayKnob_;
    amanita::ui::ParameterKnob sizeKnob_;
    amanita::ui::ParameterKnob decayKnob_;
    amanita::ui::ParameterKnob lowCutKnob_;
    amanita::ui::ParameterKnob dampingKnob_;
    amanita::ui::ParameterKnob harmonyKnob_;
    amanita::ui::ParameterKnob widthKnob_;
    amanita::ui::ParameterKnob focusKnob_;
    amanita::ui::ParameterKnob mixKnob_;
    juce::ToggleButton monoSafeButton_;
    juce::ToggleButton freezeButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoSafeAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment_;
    juce::TooltipWindow tooltipWindow_ { this, 700 };

    juce::Colour currentAccent_;
    juce::Colour targetAccent_;
    int visualCharacter_ = 0;
    bool backgroundDirty_ = true;
    bool shaderReadyPreviously_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmanitaOceanAudioProcessorEditor)
};
