#pragma once

#include "dsp/FDNReverb.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

class AmanitaOceanAudioProcessor final : public juce::AudioProcessor
{
public:
    struct CurrentVisualSnapshot
    {
        float flowX = 0.0f;
        float flowY = 0.0f;
        float strength = 0.0f;
    };

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

    [[nodiscard]] juce::AudioProcessorValueTreeState& getParameterState() noexcept;
    [[nodiscard]] const juce::AudioProcessorValueTreeState& getParameterState() const noexcept;
    [[nodiscard]] CurrentVisualSnapshot getCurrentVisualSnapshot() const noexcept;

    // Seed of the voice phase of Fathom in this instance. It is drawn when the
    // instance is created and kept for its lifetime, and it is no part of the
    // saved state: a copy of a track and a project that is opened again each
    // get a phase of their own.
    [[nodiscard]] std::uint64_t getFathomVoiceSeed() const noexcept;

private:
    [[nodiscard]] static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    [[nodiscard]] amanita::dsp::ReverbParameters readDspParameters() const noexcept;
    // What the host's play head says of the block that is being processed.
    // Call it from processBlock only; a host that gives no position reads as a
    // stopped transport, one that gives no tempo as having none.
    [[nodiscard]] amanita::dsp::HostTransport readHostTransport() const noexcept;

    amanita::dsp::FDNReverb reverb_;
    juce::AudioProcessorValueTreeState state_;
    const std::uint64_t fathomVoiceSeed_;

    std::atomic<float>* characterParameter_ = nullptr;
    std::atomic<float>* mixParameter_ = nullptr;
    std::atomic<float>* decayParameter_ = nullptr;
    std::atomic<float>* sizeParameter_ = nullptr;
    std::atomic<float>* preDelayParameter_ = nullptr;
    std::atomic<float>* lowCutParameter_ = nullptr;
    std::atomic<float>* highDampingParameter_ = nullptr;
    std::atomic<float>* evolutionParameter_ = nullptr;
    std::atomic<float>* widthParameter_ = nullptr;
    std::atomic<float>* focusParameter_ = nullptr;
    std::atomic<float>* freezeParameter_ = nullptr;
    std::atomic<float>* harmonyParameter_ = nullptr;
    std::atomic<float>* monoSafeParameter_ = nullptr;

    std::atomic<std::uint32_t> currentVisualRevision_ { 0 };
    std::atomic<float> currentVisualFlowX_ { 0.0f };
    std::atomic<float> currentVisualFlowY_ { 0.0f };
    std::atomic<float> currentVisualStrength_ { 0.0f };

    // Tempo of the block processed last, within the range Undertow follows,
    // or the one Undertow takes where the host gave none: written on the audio
    // thread, read where the host asks for the tail.
    std::atomic<float> tailTempoBpm_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmanitaOceanAudioProcessor)
};
