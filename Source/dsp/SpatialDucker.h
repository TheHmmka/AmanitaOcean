#pragma once

#include <array>
#include <cstddef>

namespace amanita::dsp
{
class SpatialDucker
{
public:
    struct Gains
    {
        float left = 1.0f;
        float right = 1.0f;
    };

    void prepare(double sampleRate, float initialAmount) noexcept;
    void reset() noexcept;
    void setAmount(float newAmount) noexcept;

    [[nodiscard]] Gains process(float left, float right) noexcept;

private:
    [[nodiscard]] static float timeCoefficient(double sampleRate,
                                               float milliseconds) noexcept;
    [[nodiscard]] static float smoothCurve(float value) noexcept;
    [[nodiscard]] static float finiteInput(float value) noexcept;
    [[nodiscard]] float nextAmount() noexcept;
    [[nodiscard]] float targetReductionDb(float detectorEnergy) const noexcept;
    [[nodiscard]] float updateReduction(std::size_t channel,
                                        float targetReduction,
                                        float spatialRelease) noexcept;

    double sampleRate_ = 48000.0;
    bool prepared_ = false;

    float rmsCoefficient_ = 0.0f;
    float peakReleaseCoefficient_ = 0.0f;
    float spatialCoefficient_ = 0.0f;
    float reductionAttackCoefficient_ = 0.0f;
    float reductionFastReleaseCoefficient_ = 0.0f;
    float reductionSlowReleaseCoefficient_ = 0.0f;
    float reductionSpatialReleaseCoefficient_ = 0.0f;

    float amountCurrent_ = 0.0f;
    float amountTarget_ = 0.0f;
    float amountStep_ = 0.0f;
    int amountRemaining_ = 0;
    int amountRampSamples_ = 1;

    std::array<float, 2> rmsPower_ {};
    std::array<float, 2> peakEnvelope_ {};
    std::array<float, 2> reductionDb_ {};
    float spatialPowerLeft_ = 0.0f;
    float spatialPowerRight_ = 0.0f;
    float spatialCross_ = 0.0f;
};
} // namespace amanita::dsp
