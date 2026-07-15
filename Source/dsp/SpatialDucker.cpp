#include "SpatialDucker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace amanita::dsp
{
namespace
{
constexpr float amountRampSeconds = 0.05f;
constexpr float rmsTimeMs = 20.0f;
constexpr float peakReleaseMs = 55.0f;
constexpr float spatialAnalysisMs = 40.0f;
constexpr float reductionAttackMs = 10.0f;
constexpr float reductionFastReleaseMs = 180.0f;
constexpr float reductionSlowReleaseMs = 420.0f;
constexpr float reductionSpatialReleaseMs = 45.0f;
constexpr float detectorThresholdDb = -36.0f;
constexpr float detectorKneeDb = 10.0f;
constexpr float detectorKneeStartEnergy = 0.0000794328f; // -41 dBFS
constexpr float maximumReductionDb = 15.0f;
constexpr float minimumGain = 0.177827941f; // -15 dB
constexpr float decibelsToNaturalLog = 0.115129255f;
constexpr float stateFloor = 1.0e-20f;

[[nodiscard]] float flushState(float value) noexcept
{
    return std::abs(value) < stateFloor || !std::isfinite(value) ? 0.0f : value;
}
} // namespace

void SpatialDucker::prepare(double sampleRate, float initialAmount) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    rmsCoefficient_ = timeCoefficient(sampleRate_, rmsTimeMs);
    peakReleaseCoefficient_ = timeCoefficient(sampleRate_, peakReleaseMs);
    spatialCoefficient_ = timeCoefficient(sampleRate_, spatialAnalysisMs);
    reductionAttackCoefficient_ = timeCoefficient(sampleRate_, reductionAttackMs);
    reductionFastReleaseCoefficient_ = timeCoefficient(sampleRate_, reductionFastReleaseMs);
    reductionSlowReleaseCoefficient_ = timeCoefficient(sampleRate_, reductionSlowReleaseMs);
    reductionSpatialReleaseCoefficient_ = timeCoefficient(sampleRate_, reductionSpatialReleaseMs);
    amountRampSamples_ = std::max(1, static_cast<int>(std::round(
        sampleRate_ * static_cast<double>(amountRampSeconds))));
    amountTarget_ = std::isfinite(initialAmount) ? std::clamp(initialAmount, 0.0f, 1.0f) : 0.0f;
    amountCurrent_ = amountTarget_;
    prepared_ = true;
    reset();
}

void SpatialDucker::reset() noexcept
{
    rmsPower_.fill(0.0f);
    peakEnvelope_.fill(0.0f);
    reductionDb_.fill(0.0f);
    spatialPowerLeft_ = 0.0f;
    spatialPowerRight_ = 0.0f;
    spatialCross_ = 0.0f;
    amountCurrent_ = amountTarget_;
    amountStep_ = 0.0f;
    amountRemaining_ = 0;
}

void SpatialDucker::setAmount(float newAmount) noexcept
{
    const auto amount = std::isfinite(newAmount)
        ? std::clamp(newAmount, 0.0f, 1.0f)
        : amountTarget_;
    if (!prepared_)
    {
        amountCurrent_ = amountTarget_ = amount;
        return;
    }
    if (std::abs(amount - amountTarget_) <= std::numeric_limits<float>::epsilon())
        return;

    amountTarget_ = amount;
    amountRemaining_ = amountRampSamples_;
    amountStep_ = (amountTarget_ - amountCurrent_) / static_cast<float>(amountRemaining_);
}

SpatialDucker::Gains SpatialDucker::process(float left, float right) noexcept
{
    const std::array input { finiteInput(left), finiteInput(right) };
    std::array<float, 2> detectorEnergy {};

    for (std::size_t channel = 0; channel < input.size(); ++channel)
    {
        const auto power = input[channel] * input[channel];
        rmsPower_[channel] = flushState(
            rmsCoefficient_ * rmsPower_[channel] + (1.0f - rmsCoefficient_) * power);
        peakEnvelope_[channel] = flushState(std::max(std::abs(input[channel]),
                                                     peakEnvelope_[channel]
                                                         * peakReleaseCoefficient_));
        const auto peakPower = peakEnvelope_[channel] * peakEnvelope_[channel];
        detectorEnergy[channel] = std::max(rmsPower_[channel], 0.35f * peakPower);
    }

    const auto leftPower = input[0] * input[0];
    const auto rightPower = input[1] * input[1];
    spatialPowerLeft_ = flushState(spatialCoefficient_ * spatialPowerLeft_
                                   + (1.0f - spatialCoefficient_) * leftPower);
    spatialPowerRight_ = flushState(spatialCoefficient_ * spatialPowerRight_
                                    + (1.0f - spatialCoefficient_) * rightPower);
    spatialCross_ = flushState(spatialCoefficient_ * spatialCross_
                               + (1.0f - spatialCoefficient_) * input[0] * input[1]);

    auto stereoLink = 0.0f;
    std::array<float, 2> spatialShare { 0.5f, 0.5f };
    const auto spatialSum = spatialPowerLeft_ + spatialPowerRight_;
    const auto spatialProduct = spatialPowerLeft_ * spatialPowerRight_;
    if (spatialSum > 1.0e-12f)
    {
        spatialShare[0] = std::clamp(spatialPowerLeft_ / spatialSum, 0.0f, 1.0f);
        spatialShare[1] = 1.0f - spatialShare[0];
        if (spatialProduct > 1.0e-20f)
        {
            const auto balanceSquared = std::clamp(
                4.0f * spatialProduct / (spatialSum * spatialSum), 0.0f, 1.0f);
            const auto positiveCross = std::max(spatialCross_, 0.0f);
            const auto coherenceSquared = std::clamp(
                positiveCross * positiveCross / spatialProduct, 0.0f, 1.0f);
            const auto linkTarget = balanceSquared * (0.15f + 0.85f * coherenceSquared);
            stereoLink = smoothCurve(linkTarget);
        }
    }

    const auto amount = nextAmount();
    std::array<float, 2> independentReduction {
        targetReductionDb(detectorEnergy[0]),
        targetReductionDb(detectorEnergy[1])
    };
    const auto linkedReduction = std::max(independentReduction[0], independentReduction[1]);
    std::array<float, 2> outputGain {};
    for (std::size_t channel = 0; channel < outputGain.size(); ++channel)
    {
        const auto otherChannel = 1u - channel;
        const auto ownership = smoothCurve(std::clamp(
            (spatialShare[channel] - 0.05f) / 0.20f, 0.0f, 1.0f));
        const auto oppositeActivity = smoothCurve(
            independentReduction[otherChannel] / 3.0f);
        const auto separation = (1.0f - ownership)
                              * (1.0f - stereoLink)
                              * oppositeActivity;
        const auto localReduction = independentReduction[channel] * (1.0f - separation);
        const auto spatialReduction = localReduction
                                    + stereoLink * (linkedReduction
                                                    - localReduction);
        const auto reduction = updateReduction(channel, spatialReduction, separation);
        const auto appliedReduction = amount * reduction;
        outputGain[channel] = appliedReduction > 0.0f
            ? std::clamp(std::exp(-decibelsToNaturalLog * appliedReduction),
                         minimumGain, 1.0f)
            : 1.0f;
    }

    return { outputGain[0], outputGain[1] };
}

float SpatialDucker::timeCoefficient(double sampleRate, float milliseconds) noexcept
{
    const auto samples = std::max(1.0, sampleRate * static_cast<double>(milliseconds) * 0.001);
    return static_cast<float>(std::exp(-1.0 / samples));
}

float SpatialDucker::smoothCurve(float value) noexcept
{
    const auto amount = std::clamp(value, 0.0f, 1.0f);
    return amount * amount * (3.0f - 2.0f * amount);
}

float SpatialDucker::finiteInput(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, -4.0f, 4.0f) : 0.0f;
}

float SpatialDucker::nextAmount() noexcept
{
    if (amountRemaining_ <= 0)
        return amountCurrent_;

    --amountRemaining_;
    if (amountRemaining_ == 0)
    {
        amountCurrent_ = amountTarget_;
        amountStep_ = 0.0f;
    }
    else
    {
        amountCurrent_ += amountStep_;
    }
    return amountCurrent_;
}

float SpatialDucker::targetReductionDb(float detectorEnergy) const noexcept
{
    if (!(detectorEnergy > detectorKneeStartEnergy) || !std::isfinite(detectorEnergy))
        return 0.0f;

    const auto levelDb = 10.0f * std::log10(detectorEnergy);
    const auto distance = levelDb - detectorThresholdDb;
    auto reduction = 0.0f;
    if (distance <= -0.5f * detectorKneeDb)
        reduction = 0.0f;
    else if (distance < 0.5f * detectorKneeDb)
    {
        const auto kneePosition = distance + 0.5f * detectorKneeDb;
        reduction = kneePosition * kneePosition / (2.0f * detectorKneeDb);
    }
    else
        reduction = distance;

    return std::clamp(reduction, 0.0f, maximumReductionDb);
}

float SpatialDucker::updateReduction(std::size_t channel,
                                     float targetReduction,
                                     float spatialRelease) noexcept
{
    auto& reduction = reductionDb_[channel];
    const auto target = std::isfinite(targetReduction)
        ? std::clamp(targetReduction, 0.0f, maximumReductionDb)
        : 0.0f;
    auto coefficient = reductionAttackCoefficient_;
    if (target < reduction)
    {
        const auto depth = smoothCurve(reduction / maximumReductionDb);
        const auto programCoefficient = reductionFastReleaseCoefficient_
                                      + depth * (reductionSlowReleaseCoefficient_
                                                 - reductionFastReleaseCoefficient_);
        const auto releaseBlend = std::clamp(spatialRelease, 0.0f, 1.0f);
        coefficient = programCoefficient
                    + releaseBlend * (reductionSpatialReleaseCoefficient_
                                      - programCoefficient);
    }

    reduction = flushState(coefficient * reduction + (1.0f - coefficient) * target);
    if (target == 0.0f && reduction < 1.0e-5f)
        reduction = 0.0f;
    return reduction;
}
} // namespace amanita::dsp
