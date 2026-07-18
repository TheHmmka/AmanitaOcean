#pragma once

#include "PluginProcessor.h"
#include "ui/CharacterSelector.h"
#include "ui/DeepCurrentRenderer.h"
#include "ui/OceanLookAndFeel.h"
#include "ui/ParameterKnob.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>

class AmanitaOceanAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    static constexpr int defaultWidth = 960;
    static constexpr int defaultHeight = 600;
    static constexpr int minimumWidth = 800;
    static constexpr int minimumHeight = 500;
    static constexpr int maximumWidth = 1440;
    static constexpr int maximumHeight = 900;

    explicit AmanitaOceanAudioProcessorEditor(AmanitaOceanAudioProcessor& processorToUse);
    ~AmanitaOceanAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateCharacterVisuals(int characterIndex);
    void drawBathymetricField(juce::Graphics& graphics,
                              juce::Rectangle<float> field,
                              float evolution) const;
    [[nodiscard]] juce::Rectangle<int> scaledBounds(float x,
                                                    float y,
                                                    float width,
                                                    float height) const;
    [[nodiscard]] static juce::String descriptionForCharacter(int characterIndex);

    AmanitaOceanAudioProcessor& processor_;
    amanita::ui::OceanLookAndFeel lookAndFeel_;
    amanita::ui::DeepCurrentRenderer deepCurrent_;
    amanita::ui::CharacterSelector characterSelector_;
    amanita::ui::ParameterKnob evolutionKnob_;
    amanita::ui::ParameterKnob preDelayKnob_;
    amanita::ui::ParameterKnob sizeKnob_;
    amanita::ui::ParameterKnob decayKnob_;
    amanita::ui::ParameterKnob lowCutKnob_;
    amanita::ui::ParameterKnob dampingKnob_;
    amanita::ui::ParameterKnob widthKnob_;
    amanita::ui::ParameterKnob focusKnob_;
    amanita::ui::ParameterKnob mixKnob_;
    juce::ToggleButton freezeButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment_;
    juce::TooltipWindow tooltipWindow_ { this, 700 };

    juce::Colour currentAccent_;
    juce::Colour targetAccent_;
    int visualCharacter_ = 0;
    unsigned int backgroundFrameCounter_ = 0;
    bool backgroundDirty_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmanitaOceanAudioProcessorEditor)
};
