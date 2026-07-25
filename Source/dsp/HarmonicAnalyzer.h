#pragma once

#include <array>
#include <cstddef>

namespace amanita::dsp
{
struct HarmonicAnalysisFrame
{
    std::array<float, 12> pitchClassWeights {};
    float confidence = 0.0f;
    float activity = 0.0f;
    float transientReliability = 1.0f;
};

class HarmonicAnalyzer final
{
public:
    static constexpr std::size_t pitchClassCount = 12;
    static constexpr std::size_t octaveCount = 5;
    static constexpr std::size_t modeCount = pitchClassCount * octaveCount;
    using PitchClassWeights = std::array<float, pitchClassCount>;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Returns true only when a new analysis frame has been published.
    [[nodiscard]] bool processSample(float dryLeft, float dryRight) noexcept;
    [[nodiscard]] const HarmonicAnalysisFrame& getFrame() const noexcept;
    [[nodiscard]] double getAnalysisSampleRate() const noexcept;

private:
    class BandPass
    {
    public:
        void prepare(double sampleRate, float frequencyHz, float q) noexcept;
        void reset() noexcept;
        [[nodiscard]] float process(float sample) noexcept;

    private:
        float k_ = 0.0f;
        float a1_ = 0.0f;
        float a2_ = 0.0f;
        float a3_ = 0.0f;
        float integrator1_ = 0.0f;
        float integrator2_ = 0.0f;
    };

    [[nodiscard]] static float sanitise(float value, float limit = 4.0f) noexcept;
    [[nodiscard]] static float smoothStep(float value) noexcept;
    [[nodiscard]] static float asymmetricEnvelope(float input,
                                                  float current,
                                                  float attackMemory,
                                                  float releaseMemory) noexcept;
    [[nodiscard]] float conditionSample(float sample,
                                        std::array<float, 2>& highPassStates,
                                        std::array<float, 4>& lowPassStates) noexcept;
    void publishFrame() noexcept;

    std::array<BandPass, modeCount> filtersLeft_;
    std::array<BandPass, modeCount> filtersRight_;
    std::array<float, modeCount> fastModeEnergy_ {};
    std::array<float, modeCount> sustainedModeEnergy_ {};
    std::array<float, pitchClassCount> stableProfile_ {};
    HarmonicAnalysisFrame frame_;

    std::array<float, 2> highPassStatesLeft_ {};
    std::array<float, 2> highPassStatesRight_ {};
    std::array<float, 4> lowPassStatesLeft_ {};
    std::array<float, 4> lowPassStatesRight_ {};

    double sampleRate_ = 48000.0;
    double analysisSampleRate_ = 12000.0;
    int decimationFactor_ = 4;
    int decimationCounter_ = 0;
    int analysisHopSamples_ = 64;
    int hopCounter_ = 0;

    float highPassMemory_ = 0.0f;
    float lowPassMemory_ = 0.0f;
    float fastAttackMemory_ = 0.0f;
    float fastReleaseMemory_ = 0.0f;
    float sustainedAttackMemory_ = 0.0f;
    float sustainedReleaseMemory_ = 0.0f;
    float confidenceAttackMemory_ = 0.0f;
    float confidenceReleaseMemory_ = 0.0f;
    float profileMemory_ = 0.0f;
    float onsetHoldMemory_ = 0.0f;

    float fastBroadbandEnergy_ = 0.0f;
    float sustainedBroadbandEnergy_ = 0.0f;
    float onsetHold_ = 0.0f;
    bool prepared_ = false;
};
} // namespace amanita::dsp
