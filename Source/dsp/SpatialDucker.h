#pragma once

#include <array>
#include <cstddef>

namespace amanita::dsp
{
class SpatialDucker
{
public:
    struct Output
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    void prepare(double sampleRate, float initialAmount) noexcept;
    void reset() noexcept;
    void setAmount(float newAmount) noexcept;

    [[nodiscard]] Output process(float dryLeft,
                                 float dryRight,
                                 float wetLeft,
                                 float wetRight) noexcept;

private:
    static constexpr std::size_t numChannels = 2;
    static constexpr std::size_t numBands = 4;

    struct TptBandpass
    {
        void prepare(double sampleRate, float frequency, float q) noexcept;
        void reset() noexcept;
        [[nodiscard]] float process(float input) noexcept;

        float k = 1.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float a3 = 0.0f;
        float ic1 = 0.0f;
        float ic2 = 0.0f;
    };

    using ChannelValues = std::array<float, numChannels>;
    using BandValues = std::array<float, numBands>;
    using StereoBandValues = std::array<BandValues, numChannels>;
    using StereoBandpasses = std::array<std::array<TptBandpass, numBands>, numChannels>;

    [[nodiscard]] static float timeCoefficient(double sampleRate,
                                               float milliseconds) noexcept;
    [[nodiscard]] static float smoothCurve(float value) noexcept;
    [[nodiscard]] static float finiteDetectorInput(float value) noexcept;
    [[nodiscard]] static float finiteWetProcessing(float value) noexcept;
    [[nodiscard]] static float updatePower(float& state,
                                           float inputPower,
                                           float attackCoefficient,
                                           float releaseCoefficient) noexcept;
    [[nodiscard]] float nextAmount() noexcept;
    [[nodiscard]] float targetBandReductionDb(float dryPower,
                                              float wetPower,
                                              std::size_t band) const noexcept;
    [[nodiscard]] float updateBandReduction(std::size_t channel,
                                            std::size_t band,
                                            float targetReduction,
                                            float spatialRelease) noexcept;
    [[nodiscard]] float updateTransientReduction(std::size_t channel,
                                                 float targetReduction,
                                                 float spatialRelease) noexcept;

    double sampleRate_ = 48000.0;
    bool prepared_ = false;

    float dryAttackCoefficient_ = 0.0f;
    float dryReleaseCoefficient_ = 0.0f;
    float wetAttackCoefficient_ = 0.0f;
    float wetReleaseCoefficient_ = 0.0f;
    float bandReductionAttackCoefficient_ = 0.0f;
    float bandReductionFastReleaseCoefficient_ = 0.0f;
    float bandReductionSlowReleaseCoefficient_ = 0.0f;
    float reductionSpatialReleaseCoefficient_ = 0.0f;
    float fastPowerAttackCoefficient_ = 0.0f;
    float fastPowerReleaseCoefficient_ = 0.0f;
    float slowPowerAttackCoefficient_ = 0.0f;
    float slowPowerReleaseCoefficient_ = 0.0f;
    float transientAttackCoefficient_ = 0.0f;
    float transientReleaseCoefficient_ = 0.0f;
    float spatialCoefficient_ = 0.0f;

    float amountCurrent_ = 0.0f;
    float amountTarget_ = 0.0f;
    float amountStep_ = 0.0f;
    int amountRemaining_ = 0;
    int amountRampSamples_ = 1;

    StereoBandpasses dryBandpasses_ {};
    StereoBandpasses wetBandpasses_ {};
    StereoBandpasses wetShapers_ {};
    StereoBandValues dryBandPower_ {};
    StereoBandValues wetBandPower_ {};
    StereoBandValues bandReductionDb_ {};

    ChannelValues fastPower_ {};
    ChannelValues slowPower_ {};
    ChannelValues transientReductionDb_ {};
    float spatialPowerLeft_ = 0.0f;
    float spatialPowerRight_ = 0.0f;
    float spatialCross_ = 0.0f;
};
} // namespace amanita::dsp
