#pragma once

#include <array>
#include <cstddef>

namespace amanita::dsp
{
struct HarmonicStereoSample
{
    float left = 0.0f;
    float right = 0.0f;
};

class HarmonicTail final
{
public:
    static constexpr std::size_t pitchClassCount = 12;
    static constexpr std::size_t octaveCount = 3;
    static constexpr std::size_t modeCount = pitchClassCount * octaveCount;
    using PitchClassWeights = std::array<float, pitchClassCount>;

    void prepare(double sampleRate,
                 const PitchClassWeights& initialWeights,
                 float initialConfidence) noexcept;
    void reset() noexcept;
    void setPitchClassWeights(const PitchClassWeights& weights,
                              float confidence) noexcept;

    [[nodiscard]] HarmonicStereoSample process(float wetLeft,
                                               float wetRight,
                                               float amount,
                                               float freezeAmount) noexcept;

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

    [[nodiscard]] static float sanitise(float value, float limit) noexcept;
    [[nodiscard]] static float softLimit(float value) noexcept;
    void resetResonators() noexcept;

    std::array<BandPass, modeCount> filtersLeft_;
    std::array<BandPass, modeCount> filtersRight_;
    PitchClassWeights targetWeights_ {};
    PitchClassWeights currentWeights_ {};
    float targetConfidence_ = 0.0f;
    float currentConfidence_ = 0.0f;
    float weightAttackCoefficient_ = 0.0f;
    float weightReleaseCoefficient_ = 0.0f;
    float confidenceAttackCoefficient_ = 0.0f;
    float confidenceReleaseCoefficient_ = 0.0f;
    bool prepared_ = false;
    bool resonatorsActive_ = false;
};
} // namespace amanita::dsp
