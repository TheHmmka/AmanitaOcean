#pragma once

#include "dsp/FDNReverb.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

class AmanitaOceanAudioProcessor final : public juce::AudioProcessor
{
public:
    AmanitaOceanAudioProcessor();

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    [[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
    [[nodiscard]] bool hasEditor() const override;

    [[nodiscard]] const juce::String getName() const override;
    [[nodiscard]] bool acceptsMidi() const override;
    [[nodiscard]] bool producesMidi() const override;
    [[nodiscard]] bool isMidiEffect() const override;
    [[nodiscard]] double getTailLengthSeconds() const override;

    [[nodiscard]] int getNumPrograms() override;
    [[nodiscard]] int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    [[nodiscard]] const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

private:
    [[nodiscard]] static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    [[nodiscard]] amanita::dsp::ReverbParameters readDspParameters() const noexcept;

    amanita::dsp::FDNReverb reverb_;
    juce::AudioProcessorValueTreeState state_;

    std::atomic<float>* characterParameter_ = nullptr;
    std::atomic<float>* mixParameter_ = nullptr;
    std::atomic<float>* decayParameter_ = nullptr;
    std::atomic<float>* sizeParameter_ = nullptr;
    std::atomic<float>* preDelayParameter_ = nullptr;
    std::atomic<float>* lowCutParameter_ = nullptr;
    std::atomic<float>* highDampingParameter_ = nullptr;
    std::atomic<float>* modulationParameter_ = nullptr;
    std::atomic<float>* widthParameter_ = nullptr;
    std::atomic<float>* freezeParameter_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmanitaOceanAudioProcessor)
};
