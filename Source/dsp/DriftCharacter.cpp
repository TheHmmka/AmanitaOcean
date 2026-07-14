#include "DriftCharacter.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr float twoPi = 6.28318530717958647692f;
constexpr float inverseSqrtEight = 0.35355339059327376220f;

constexpr std::array<std::array<float, DriftCharacter::numFeedbackLines>, 4> spectralAxes {{
    { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f },
    { 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f }
}};
constexpr std::array<float, 4> lfoFrequenciesHz {
    0.0137f, 0.0311f, 0.0179f, 0.0373f
};
constexpr std::array<float, 4> initialPhases {
    0.11f, 0.67f, 0.43f, 0.89f
};

[[nodiscard]] float onePoleCoefficient(float cutoffHz, float sampleRate) noexcept
{
    const auto cutoff = std::clamp(cutoffHz, 20.0f, sampleRate * 0.45f);
    return std::exp(-twoPi * cutoff / sampleRate);
}
} // namespace

void DriftCharacter::EndpointFilters::reset() noexcept
{
    darkState_ = 0.0f;
    brightState_ = 0.0f;
}

std::array<float, 2> DriftCharacter::EndpointFilters::process(
    float sample,
    float darkCoefficient,
    float brightCoefficient) noexcept
{
    darkState_ = DriftCharacter::sanitise(
        darkCoefficient * darkState_ + (1.0f - darkCoefficient) * sample);
    brightState_ = DriftCharacter::sanitise(
        brightCoefficient * brightState_ + (1.0f - brightCoefficient) * sample);
    return { darkState_, brightState_ };
}

void DriftCharacter::prepare(double sampleRate) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? static_cast<float>(sampleRate)
        : 48000.0f;
    darkCoefficient_ = onePoleCoefficient(600.0f, safeSampleRate);
    brightCoefficient_ = onePoleCoefficient(18000.0f, safeSampleRate);

    for (std::size_t index = 0; index < phaseIncrements_.size(); ++index)
        phaseIncrements_[index] = lfoFrequenciesHz[index] / safeSampleRate;

    reset();
}

void DriftCharacter::reset() noexcept
{
    for (auto& filter : filters_)
        filter.reset();
    phases_ = initialPhases;
}

void DriftCharacter::processFeedback(std::array<float, numFeedbackLines>& feedback,
                                     float characterAmount,
                                     float evolutionAmount) noexcept
{
    std::array<float, spectralAxes.size()> components {};
    for (std::size_t index = 0; index < numFeedbackLines; ++index)
    {
        for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
            components[axis] += inverseSqrtEight * spectralAxes[axis][index] * feedback[index];
    }

    std::array<std::array<float, 2>, spectralAxes.size()> endpoints {};
    for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
    {
        components[axis] = sanitise(components[axis]);
        endpoints[axis] = filters_[axis].process(components[axis], darkCoefficient_,
                                                 brightCoefficient_);
    }

    const auto amount = std::isfinite(characterAmount)
        ? std::clamp(characterAmount, 0.0f, 1.0f)
        : 0.0f;
    if (amount > 0.0f)
    {
        const auto evolution = std::isfinite(evolutionAmount)
            ? std::clamp(evolutionAmount, 0.0f, 1.0f)
            : 0.0f;
        const auto span = 0.25f + 0.75f * evolution;
        const auto depth = amount * (0.15f + 0.70f * evolution);
        const auto leftMotion = 0.72f * std::sin(twoPi * phases_[0])
                              + 0.28f * std::sin(twoPi * phases_[1]);
        const auto rightMotion = 0.69f * std::sin(twoPi * phases_[2])
                               + 0.31f * std::sin(twoPi * phases_[3]);
        const auto leftBlend = std::clamp(0.5f + 0.5f * span * leftMotion, 0.0f, 1.0f);
        const auto rightBlend = std::clamp(0.5f + 0.5f * span * rightMotion, 0.0f, 1.0f);
        std::array<float, spectralAxes.size()> filteredComponents {};
        for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
        {
            const auto blend = axis < 2 ? leftBlend : rightBlend;
            const auto filtered = endpoints[axis][0]
                                + blend * (endpoints[axis][1] - endpoints[axis][0]);
            filteredComponents[axis] = components[axis]
                                     + depth * (filtered - components[axis]);
        }

        // Keep this path linear: a sample-wise norm guard is invalid for filters
        // with stored energy and turns ordinary phase shift into harmonic distortion.
        for (std::size_t index = 0; index < numFeedbackLines; ++index)
        {
            auto reconstructed = feedback[index];
            for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
            {
                const auto delta = filteredComponents[axis] - components[axis];
                reconstructed += inverseSqrtEight * spectralAxes[axis][index] * delta;
            }
            feedback[index] = sanitise(reconstructed);
        }
    }

    advancePhases();
}

void DriftCharacter::advancePhases() noexcept
{
    for (std::size_t index = 0; index < phases_.size(); ++index)
    {
        phases_[index] += phaseIncrements_[index];
        if (phases_[index] >= 1.0f)
            phases_[index] -= 1.0f;
    }
}

float DriftCharacter::sanitise(float sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-20f)
        return 0.0f;
    return std::clamp(sample, -16.0f, 16.0f);
}
} // namespace amanita::dsp
