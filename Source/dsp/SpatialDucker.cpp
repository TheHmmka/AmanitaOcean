#include "SpatialDucker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace amanita::dsp
{
namespace
{
constexpr float amountRampSeconds = 0.05f;
constexpr float pi = 3.14159265358979323846f;
constexpr float decibelsToNaturalLog = 0.115129255f;
constexpr float stateFloor = 1.0e-20f;
constexpr float wetProcessingLimit = 32.0f;

constexpr float dryAttackMs = 4.0f;
constexpr float dryReleaseMs = 75.0f;
constexpr float wetAttackMs = 12.0f;
constexpr float wetReleaseMs = 180.0f;
constexpr float bandReductionAttackMs = 9.0f;
constexpr float bandReductionFastReleaseMs = 120.0f;
constexpr float bandReductionSlowReleaseMs = 220.0f;
constexpr float spatialReleaseMs = 35.0f;
constexpr float fastPowerAttackMs = 1.5f;
constexpr float fastPowerReleaseMs = 20.0f;
constexpr float slowPowerAttackMs = 50.0f;
constexpr float slowPowerReleaseMs = 120.0f;
constexpr float transientAttackMs = 2.0f;
constexpr float transientReleaseMs = 72.0f;
constexpr float spatialAnalysisMs = 40.0f;

constexpr std::array<float, 4> bandFrequencies { 160.0f, 630.0f, 2200.0f, 6500.0f };
constexpr std::array<float, 4> detectorQ { 1.50f, 1.50f, 1.45f, 1.50f };
constexpr std::array<float, 4> shaperQ { 1.00f, 1.00f, 0.90f, 1.00f };
constexpr std::array<float, 4> maximumBandReductionDb { 0.45f, 1.5f, 5.5f, 1.3f };

constexpr float detectorGatePower = 0.0000316228f; // -45 dBFS
constexpr float maskingReferenceRatio = 0.0630957f;
constexpr float conflictStart = 0.15f;
constexpr float conflictRange = 0.69f;
constexpr float spectralFocusStart = 0.04f;
constexpr float spectralFocusRange = 0.21f;
constexpr float maximumTransientReductionDb = 1.8f;
constexpr float sidePreservation = 0.85f;

[[nodiscard]] float flushState(float value) noexcept
{
    return std::abs(value) < stateFloor || !std::isfinite(value) ? 0.0f : value;
}
} // namespace

void SpatialDucker::TptBandpass::prepare(double sampleRate,
                                         float frequency,
                                         float q) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? sampleRate
        : 48000.0;
    const auto safeFrequency = std::clamp(
        static_cast<double>(frequency), 20.0, safeSampleRate * 0.45);
    const auto safeQ = std::clamp(q, 0.25f, 4.0f);
    const auto g = static_cast<float>(std::tan(
        static_cast<double>(pi) * safeFrequency / safeSampleRate));
    k = 1.0f / safeQ;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    reset();
}

void SpatialDucker::TptBandpass::reset() noexcept
{
    ic1 = 0.0f;
    ic2 = 0.0f;
}

float SpatialDucker::TptBandpass::process(float input) noexcept
{
    const auto safeInput = std::isfinite(input)
        ? std::clamp(input, -wetProcessingLimit, wetProcessingLimit)
        : 0.0f;
    const auto v3 = safeInput - ic2;
    const auto v1 = a1 * ic1 + a2 * v3;
    const auto v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = flushState(2.0f * v1 - ic1);
    ic2 = flushState(2.0f * v2 - ic2);
    // k * v1 is the unit-gain band-pass output. Mixing (G - 1) times this
    // signal back into the input creates a fixed-denominator, cut-only bell:
    // G == 1 is exact identity and 0 <= G <= 1 cannot create a spectral peak.
    return flushState(k * v1);
}

void SpatialDucker::prepare(double sampleRate, float initialAmount) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;
    dryAttackCoefficient_ = timeCoefficient(sampleRate_, dryAttackMs);
    dryReleaseCoefficient_ = timeCoefficient(sampleRate_, dryReleaseMs);
    wetAttackCoefficient_ = timeCoefficient(sampleRate_, wetAttackMs);
    wetReleaseCoefficient_ = timeCoefficient(sampleRate_, wetReleaseMs);
    bandReductionAttackCoefficient_ = timeCoefficient(sampleRate_, bandReductionAttackMs);
    bandReductionFastReleaseCoefficient_ = timeCoefficient(
        sampleRate_, bandReductionFastReleaseMs);
    bandReductionSlowReleaseCoefficient_ = timeCoefficient(
        sampleRate_, bandReductionSlowReleaseMs);
    reductionSpatialReleaseCoefficient_ = timeCoefficient(sampleRate_, spatialReleaseMs);
    fastPowerAttackCoefficient_ = timeCoefficient(sampleRate_, fastPowerAttackMs);
    fastPowerReleaseCoefficient_ = timeCoefficient(sampleRate_, fastPowerReleaseMs);
    slowPowerAttackCoefficient_ = timeCoefficient(sampleRate_, slowPowerAttackMs);
    slowPowerReleaseCoefficient_ = timeCoefficient(sampleRate_, slowPowerReleaseMs);
    transientAttackCoefficient_ = timeCoefficient(sampleRate_, transientAttackMs);
    transientReleaseCoefficient_ = timeCoefficient(sampleRate_, transientReleaseMs);
    spatialCoefficient_ = timeCoefficient(sampleRate_, spatialAnalysisMs);

    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        for (std::size_t band = 0; band < numBands; ++band)
        {
            dryBandpasses_[channel][band].prepare(sampleRate_,
                                                  bandFrequencies[band],
                                                  detectorQ[band]);
            wetBandpasses_[channel][band].prepare(sampleRate_,
                                                  bandFrequencies[band],
                                                  detectorQ[band]);
            wetShapers_[channel][band].prepare(sampleRate_,
                                               bandFrequencies[band],
                                               shaperQ[band]);
        }
    }

    amountRampSamples_ = std::max(1, static_cast<int>(std::round(
        sampleRate_ * static_cast<double>(amountRampSeconds))));
    amountTarget_ = std::isfinite(initialAmount) ? std::clamp(initialAmount, 0.0f, 1.0f) : 0.0f;
    amountCurrent_ = amountTarget_;
    prepared_ = true;
    reset();
}

void SpatialDucker::reset() noexcept
{
    dryBandPower_ = {};
    wetBandPower_ = {};
    bandReductionDb_ = {};
    fastPower_ = {};
    slowPower_ = {};
    transientReductionDb_ = {};
    spatialPowerLeft_ = 0.0f;
    spatialPowerRight_ = 0.0f;
    spatialCross_ = 0.0f;

    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        for (std::size_t band = 0; band < numBands; ++band)
        {
            dryBandpasses_[channel][band].reset();
            wetBandpasses_[channel][band].reset();
            wetShapers_[channel][band].reset();
        }
    }

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

SpatialDucker::Output SpatialDucker::process(float dryLeft,
                                             float dryRight,
                                             float wetLeft,
                                             float wetRight) noexcept
{
    const ChannelValues dry {
        finiteDetectorInput(dryLeft), finiteDetectorInput(dryRight)
    };
    const ChannelValues originalWet {
        std::isfinite(wetLeft) ? wetLeft : 0.0f,
        std::isfinite(wetRight) ? wetRight : 0.0f
    };
    const ChannelValues wet {
        finiteWetProcessing(originalWet[0]), finiteWetProcessing(originalWet[1])
    };
    StereoBandValues dryBands {};
    StereoBandValues wetBands {};

    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        for (std::size_t band = 0; band < numBands; ++band)
        {
            dryBands[channel][band] = dryBandpasses_[channel][band].process(dry[channel]);
            wetBands[channel][band] = wetBandpasses_[channel][band].process(wet[channel]);
            dryBandPower_[channel][band] = updatePower(
                dryBandPower_[channel][band],
                dryBands[channel][band] * dryBands[channel][band],
                dryAttackCoefficient_,
                dryReleaseCoefficient_);
            wetBandPower_[channel][band] = updatePower(
                wetBandPower_[channel][band],
                wetBands[channel][band] * wetBands[channel][band],
                wetAttackCoefficient_,
                wetReleaseCoefficient_);
        }

        const auto power = dry[channel] * dry[channel];
        fastPower_[channel] = updatePower(fastPower_[channel],
                                          power,
                                          fastPowerAttackCoefficient_,
                                          fastPowerReleaseCoefficient_);
        slowPower_[channel] = updatePower(slowPower_[channel],
                                          power,
                                          slowPowerAttackCoefficient_,
                                          slowPowerReleaseCoefficient_);
    }

    const auto leftPower = dry[0] * dry[0];
    const auto rightPower = dry[1] * dry[1];
    spatialPowerLeft_ = flushState(spatialCoefficient_ * spatialPowerLeft_
                                   + (1.0f - spatialCoefficient_) * leftPower);
    spatialPowerRight_ = flushState(spatialCoefficient_ * spatialPowerRight_
                                    + (1.0f - spatialCoefficient_) * rightPower);
    spatialCross_ = flushState(spatialCoefficient_ * spatialCross_
                               + (1.0f - spatialCoefficient_) * dry[0] * dry[1]);

    auto stereoLink = 0.0f;
    auto centreWeight = 0.0f;
    ChannelValues spatialShare { 0.5f, 0.5f };
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
            stereoLink = smoothCurve(
                balanceSquared * (0.15f + 0.85f * coherenceSquared));
            centreWeight = smoothCurve(balanceSquared * coherenceSquared);
        }
    }

    ChannelValues maximumDryBandPower {};
    for (std::size_t channel = 0; channel < numChannels; ++channel)
        maximumDryBandPower[channel] = *std::max_element(
            dryBandPower_[channel].begin(), dryBandPower_[channel].end());

    for (std::size_t band = 0; band < numBands; ++band)
    {
        ChannelValues independentTarget {
            targetBandReductionDb(dryBandPower_[0][band], wetBandPower_[0][band], band),
            targetBandReductionDb(dryBandPower_[1][band], wetBandPower_[1][band], band)
        };
        for (std::size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto relativeDryPower = dryBandPower_[channel][band]
                / (maximumDryBandPower[channel] + 1.0e-20f);
            // Reject leakage from the deliberately broad detector bands. A
            // band participates only when it owns a meaningful part of the
            // current dry spectrum, rather than merely seeing a loud input.
            const auto spectralFocus = smoothCurve(std::clamp(
                (relativeDryPower - spectralFocusStart) / spectralFocusRange,
                0.0f,
                1.0f));
            independentTarget[channel] *= spectralFocus;
        }
        const auto linkedTarget = std::max(independentTarget[0], independentTarget[1]);
        for (std::size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto otherChannel = 1u - channel;
            const auto ownership = smoothCurve(std::clamp(
                (spatialShare[channel] - 0.05f) / 0.20f, 0.0f, 1.0f));
            const auto oppositeActivity = smoothCurve(
                independentTarget[otherChannel] / maximumBandReductionDb[band]);
            const auto separation = (1.0f - ownership)
                                  * (1.0f - stereoLink)
                                  * oppositeActivity;
            const auto localTarget = independentTarget[channel] * (1.0f - separation);
            const auto spatialTarget = localTarget
                                     + stereoLink * (linkedTarget - localTarget);
            bandReductionDb_[channel][band] = updateBandReduction(
                channel, band, spatialTarget, separation);
        }
    }

    ChannelValues independentTransient {};
    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        const auto fastToSlow = fastPower_[channel] / (slowPower_[channel] + 1.0e-12f);
        const auto novelty = smoothCurve(std::clamp(
            (fastToSlow - 3.0f) / 5.0f, 0.0f, 1.0f));
        const auto activity = fastPower_[channel]
                            / (fastPower_[channel] + detectorGatePower);
        independentTransient[channel] = maximumTransientReductionDb
                                      * novelty
                                      * activity;
    }
    const auto linkedTransient = std::max(independentTransient[0], independentTransient[1]);
    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        const auto otherChannel = 1u - channel;
        const auto ownership = smoothCurve(std::clamp(
            (spatialShare[channel] - 0.05f) / 0.20f, 0.0f, 1.0f));
        const auto oppositeActivity = smoothCurve(
            independentTransient[otherChannel] / maximumTransientReductionDb);
        const auto separation = (1.0f - ownership)
                              * (1.0f - stereoLink)
                              * oppositeActivity;
        const auto localTarget = independentTransient[channel] * (1.0f - separation);
        const auto spatialTarget = localTarget
                                 + stereoLink * (linkedTransient - localTarget);
        transientReductionDb_[channel] = updateTransientReduction(
            channel, spatialTarget, separation);
    }

    const auto amount = smoothCurve(nextAmount());
    ChannelValues shaped = wet;
    for (std::size_t channel = 0; channel < numChannels; ++channel)
    {
        auto stageInput = wet[channel];
        for (std::size_t band = 0; band < numBands; ++band)
        {
            const auto bandPass = wetShapers_[channel][band].process(stageInput);
            const auto gain = std::exp(-decibelsToNaturalLog
                                       * amount
                                       * bandReductionDb_[channel][band]);
            stageInput = finiteWetProcessing(stageInput + (gain - 1.0f) * bandPass);
        }
        const auto transientGain = std::exp(-decibelsToNaturalLog
                                            * amount
                                            * transientReductionDb_[channel]);
        shaped[channel] = finiteWetProcessing(stageInput * transientGain);
    }

    if (amount > 0.0f && centreWeight > 0.0f)
    {
        // A coherent centre source needs a pocket in wet Mid, not a collapse
        // of the surrounding field. Restore most of the original wet Side;
        // hard-panned and uncorrelated sources keep independent L/R shaping.
        const auto originalSide = 0.5f * (wet[0] - wet[1]);
        const auto shapedMid = 0.5f * (shaped[0] + shaped[1]);
        const auto shapedSide = 0.5f * (shaped[0] - shaped[1]);
        const auto preservation = sidePreservation * centreWeight * amount;
        const auto preservedSide = shapedSide
                                 + preservation * (originalSide - shapedSide);
        shaped[0] = finiteWetProcessing(shapedMid + preservedSide);
        shaped[1] = finiteWetProcessing(shapedMid - preservedSide);
    }

    if (amount <= 0.0f)
        return { originalWet[0], originalWet[1] };
    return { shaped[0], shaped[1] };
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

float SpatialDucker::finiteDetectorInput(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, -4.0f, 4.0f) : 0.0f;
}

float SpatialDucker::finiteWetProcessing(float value) noexcept
{
    return std::isfinite(value)
        ? std::clamp(value, -wetProcessingLimit, wetProcessingLimit)
        : 0.0f;
}

float SpatialDucker::updatePower(float& state,
                                 float inputPower,
                                 float attackCoefficient,
                                 float releaseCoefficient) noexcept
{
    const auto power = std::isfinite(inputPower)
        ? std::clamp(inputPower, 0.0f, 16.0f)
        : 0.0f;
    const auto coefficient = power > state ? attackCoefficient : releaseCoefficient;
    state = flushState(coefficient * state + (1.0f - coefficient) * power);
    return state;
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

float SpatialDucker::targetBandReductionDb(float dryPower,
                                           float wetPower,
                                           std::size_t band) const noexcept
{
    if (band >= numBands || !(dryPower > 0.0f) || !std::isfinite(dryPower)
        || !std::isfinite(wetPower))
        return 0.0f;

    const auto activity = dryPower / (dryPower + detectorGatePower);
    const auto conflictRatio = wetPower
        / (wetPower + maskingReferenceRatio * dryPower + 1.0e-20f);
    const auto conflict = smoothCurve(std::clamp(
        (conflictRatio - conflictStart) / conflictRange, 0.0f, 1.0f));
    return maximumBandReductionDb[band] * activity * conflict;
}

float SpatialDucker::updateBandReduction(std::size_t channel,
                                         std::size_t band,
                                         float targetReduction,
                                         float spatialRelease) noexcept
{
    auto& reduction = bandReductionDb_[channel][band];
    const auto maximum = maximumBandReductionDb[band];
    const auto target = std::isfinite(targetReduction)
        ? std::clamp(targetReduction, 0.0f, maximum)
        : 0.0f;
    auto coefficient = bandReductionAttackCoefficient_;
    if (target < reduction)
    {
        const auto depth = smoothCurve(reduction / maximum);
        const auto programCoefficient = bandReductionFastReleaseCoefficient_
                                      + depth * (bandReductionSlowReleaseCoefficient_
                                                 - bandReductionFastReleaseCoefficient_);
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

float SpatialDucker::updateTransientReduction(std::size_t channel,
                                              float targetReduction,
                                              float spatialRelease) noexcept
{
    auto& reduction = transientReductionDb_[channel];
    const auto target = std::isfinite(targetReduction)
        ? std::clamp(targetReduction, 0.0f, maximumTransientReductionDb)
        : 0.0f;
    auto coefficient = transientAttackCoefficient_;
    if (target < reduction)
    {
        const auto releaseBlend = std::clamp(spatialRelease, 0.0f, 1.0f);
        coefficient = transientReleaseCoefficient_
                    + releaseBlend * (reductionSpatialReleaseCoefficient_
                                      - transientReleaseCoefficient_);
    }

    reduction = flushState(coefficient * reduction + (1.0f - coefficient) * target);
    if (target == 0.0f && reduction < 1.0e-5f)
        reduction = 0.0f;
    return reduction;
}
} // namespace amanita::dsp
