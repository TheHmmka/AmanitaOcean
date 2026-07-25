#include "HarmonicTail.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float a4FrequencyHz = 440.0f;
constexpr int firstMidiNote = 48; // C3
constexpr float modeQ = 16.0f;
constexpr float weightAttackSeconds = 0.080f;
constexpr float weightReleaseSeconds = 0.240f;
constexpr float confidenceAttackSeconds = 0.100f;
constexpr float confidenceReleaseSeconds = 0.300f;
constexpr float maximumHaloDepth = 0.28f;
constexpr float softLimitThreshold = 2.0f;
constexpr float softLimitRange = 2.0f;
constexpr float maximumOutputMagnitude = 16.0f;
constexpr std::array<float, HarmonicTail::octaveCount> octaveGains {
    0.72f, 1.00f, 0.82f
};

[[nodiscard]] float midiNoteFrequency(int midiNote) noexcept
{
    return a4FrequencyHz
         * std::exp2(static_cast<float>(midiNote - 69) / 12.0f);
}

[[nodiscard]] float onePoleMemory(double sampleRate, float seconds) noexcept
{
    return static_cast<float>(std::exp(-1.0 / (sampleRate * seconds)));
}
} // namespace

void HarmonicTail::BandPass::prepare(double sampleRate,
                                     float frequencyHz,
                                     float q) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? static_cast<float>(sampleRate)
        : 48000.0f;
    const auto safeFrequency = std::clamp(frequencyHz, 20.0f,
                                          safeSampleRate * 0.45f);
    const auto safeQ = std::clamp(q, 0.5f, 60.0f);
    const auto g = std::tan(pi * safeFrequency / safeSampleRate);
    k_ = 1.0f / safeQ;
    a1_ = 1.0f / (1.0f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
    reset();
}

void HarmonicTail::BandPass::reset() noexcept
{
    integrator1_ = 0.0f;
    integrator2_ = 0.0f;
}

float HarmonicTail::BandPass::process(float sample) noexcept
{
    const auto v3 = sample - integrator2_;
    const auto v1 = a1_ * integrator1_ + a2_ * v3;
    const auto v2 = integrator2_ + a2_ * integrator1_ + a3_ * v3;
    const auto nextIntegrator1 = 2.0f * v1 - integrator1_;
    const auto nextIntegrator2 = 2.0f * v2 - integrator2_;
    const auto output = k_ * v1;

    if (!std::isfinite(output)
        || !std::isfinite(nextIntegrator1)
        || !std::isfinite(nextIntegrator2))
    {
        reset();
        return 0.0f;
    }

    integrator1_ = std::abs(nextIntegrator1) < 1.0e-20f
        ? 0.0f
        : std::clamp(nextIntegrator1, -128.0f, 128.0f);
    integrator2_ = std::abs(nextIntegrator2) < 1.0e-20f
        ? 0.0f
        : std::clamp(nextIntegrator2, -128.0f, 128.0f);
    return std::clamp(output, -4.0f, 4.0f);
}

void HarmonicTail::prepare(double sampleRate,
                           const PitchClassWeights& initialWeights,
                           float initialConfidence) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? sampleRate
        : 48000.0;

    for (std::size_t index = 0; index < modeCount; ++index)
    {
        const auto frequency = midiNoteFrequency(
            firstMidiNote + static_cast<int>(index));
        filtersLeft_[index].prepare(safeSampleRate, frequency, modeQ);
        filtersRight_[index].prepare(safeSampleRate, frequency, modeQ);
    }

    weightAttackCoefficient_ = onePoleMemory(safeSampleRate, weightAttackSeconds);
    weightReleaseCoefficient_ = onePoleMemory(safeSampleRate, weightReleaseSeconds);
    confidenceAttackCoefficient_ = onePoleMemory(safeSampleRate,
                                                  confidenceAttackSeconds);
    confidenceReleaseCoefficient_ = onePoleMemory(safeSampleRate,
                                                   confidenceReleaseSeconds);
    setPitchClassWeights(initialWeights, initialConfidence);
    currentWeights_ = targetWeights_;
    currentConfidence_ = targetConfidence_;
    prepared_ = true;
    reset();
}

void HarmonicTail::reset() noexcept
{
    resetResonators();
    currentWeights_ = targetWeights_;
    currentConfidence_ = targetConfidence_;
    resonatorsActive_ = false;
}

void HarmonicTail::setPitchClassWeights(const PitchClassWeights& weights,
                                        float confidence) noexcept
{
    for (std::size_t index = 0; index < pitchClassCount; ++index)
    {
        const auto value = weights[index];
        targetWeights_[index] = std::isfinite(value)
            ? std::clamp(value, 0.0f, 1.0f)
            : targetWeights_[index];
    }
    if (std::isfinite(confidence))
        targetConfidence_ = std::clamp(confidence, 0.0f, 1.0f);
}

HarmonicStereoSample HarmonicTail::process(float wetLeft,
                                           float wetRight,
                                           float amount,
                                           float freezeAmount) noexcept
{
    if (!prepared_)
        return { wetLeft, wetRight };

    const auto safeFreeze = std::isfinite(freezeAmount)
        ? std::clamp(freezeAmount, 0.0f, 1.0f)
        : 0.0f;
    auto weightEnergy = 0.0f;
    for (std::size_t index = 0; index < pitchClassCount; ++index)
    {
        auto& current = currentWeights_[index];
        const auto memory = targetWeights_[index] > current
            ? weightAttackCoefficient_
            : weightReleaseCoefficient_;
        current += (targetWeights_[index] - current)
                 * (1.0f - memory) * (1.0f - safeFreeze);
        current = sanitise(current, 1.0f);
        const auto shaped = current * current * (3.0f - 2.0f * current);
        weightEnergy += shaped * shaped;
    }

    const auto confidenceMemory = targetConfidence_ > currentConfidence_
        ? confidenceAttackCoefficient_
        : confidenceReleaseCoefficient_;
    currentConfidence_ += (targetConfidence_ - currentConfidence_)
                        * (1.0f - confidenceMemory) * (1.0f - safeFreeze);
    currentConfidence_ = sanitise(currentConfidence_, 1.0f);

    const auto safeAmount = std::isfinite(amount)
        ? std::clamp(amount, 0.0f, 1.0f)
        : 0.0f;
    const auto activity = std::clamp(std::sqrt(weightEnergy), 0.0f, 1.0f);
    if (safeAmount <= 0.0f
        || activity <= 1.0e-6f
        || currentConfidence_ <= 1.0e-6f)
    {
        if (resonatorsActive_)
            resetResonators();
        resonatorsActive_ = false;
        return { wetLeft, wetRight };
    }
    resonatorsActive_ = true;

    // The modal bank sees a bounded signal, while the unprocessed wet path keeps
    // its wider FDN headroom. Sharing the ±4 input clamp with the output base
    // would make the first non-zero Amount jump on legitimate FDN peaks.
    const auto filterInputLeft = sanitise(wetLeft, 4.0f);
    const auto filterInputRight = sanitise(wetRight, 4.0f);
    const auto baseLeft = sanitise(wetLeft, maximumOutputMagnitude);
    const auto baseRight = sanitise(wetRight, maximumOutputMagnitude);
    const auto denseFieldNormalisation = 1.0f
        / std::sqrt(std::max(1.0f, weightEnergy / 3.0f));
    auto haloLeft = 0.0f;
    auto haloRight = 0.0f;

    for (std::size_t index = 0; index < modeCount; ++index)
    {
        const auto filteredLeft = filtersLeft_[index].process(filterInputLeft);
        const auto filteredRight = filtersRight_[index].process(filterInputRight);
        const auto weight = currentWeights_[index % pitchClassCount];
        const auto shapedWeight = weight * weight * (3.0f - 2.0f * weight);
        const auto octaveGain = octaveGains[index / pitchClassCount];
        const auto modeGain = shapedWeight * octaveGain * denseFieldNormalisation;
        haloLeft += modeGain * filteredLeft;
        haloRight += modeGain * filteredRight;
    }

    const auto shapedAmount = safeAmount * safeAmount
                            * (3.0f - 2.0f * safeAmount)
                            * currentConfidence_
                            * activity
                            * maximumHaloDepth;

    return {
        sanitise(baseLeft + shapedAmount * softLimit(haloLeft),
                 maximumOutputMagnitude),
        sanitise(baseRight + shapedAmount * softLimit(haloRight),
                 maximumOutputMagnitude)
    };
}

void HarmonicTail::resetResonators() noexcept
{
    for (auto& filter : filtersLeft_)
        filter.reset();
    for (auto& filter : filtersRight_)
        filter.reset();
}

float HarmonicTail::sanitise(float value, float limit) noexcept
{
    if (!std::isfinite(value) || std::abs(value) < 1.0e-20f)
        return 0.0f;
    return std::clamp(value, -limit, limit);
}

float HarmonicTail::softLimit(float value) noexcept
{
    if (!std::isfinite(value))
        return 0.0f;

    const auto magnitude = std::abs(value);
    if (magnitude <= softLimitThreshold)
        return value;

    const auto excess = magnitude - softLimitThreshold;
    const auto compressed = softLimitThreshold
                          + excess / (1.0f + excess / softLimitRange);
    return std::copysign(compressed, value);
}
} // namespace amanita::dsp
