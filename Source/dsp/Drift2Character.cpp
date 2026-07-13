#include "Drift2Character.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr float twoPi = 6.28318530717958647692f;
constexpr float inverseSqrtEight = 0.35355339059327376220f;

constexpr std::array<std::array<float, Drift2Character::numFeedbackLines>, 4> spectralAxes {{
    { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f },
    { 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f }
}};
constexpr std::array<float, 4> lfoFrequenciesHz {
    0.1813f, 0.0917f, 0.0209f, 0.0147f
};
constexpr std::array<float, 4> initialPhases {
    0.07f, 0.41f, 0.19f, 0.61f
};

[[nodiscard]] float safeFrequency(float frequencyHz, float sampleRate) noexcept
{
    return std::clamp(frequencyHz, 20.0f, sampleRate * 0.45f);
}

[[nodiscard]] float shapePosition(float position, float amount) noexcept
{
    const auto smooth = position * position * (3.0f - 2.0f * position);
    const auto pronounced = smooth * smooth * (3.0f - 2.0f * smooth);
    return position + amount * (pronounced - position);
}
} // namespace

void Drift2Character::Biquad::setBandPass(float sampleRate,
                                          float frequencyHz,
                                          float q) noexcept
{
    const auto omega = twoPi * safeFrequency(frequencyHz, sampleRate) / sampleRate;
    const auto alpha = std::sin(omega) / (2.0f * std::max(q, 0.05f));
    setCoefficients(alpha, 0.0f, -alpha,
                    1.0f + alpha, -2.0f * std::cos(omega), 1.0f - alpha);
}

void Drift2Character::Biquad::setCoefficients(float b0, float b1, float b2,
                                               float a0, float a1, float a2) noexcept
{
    const auto inverseA0 = 1.0f / a0;
    b0_ = b0 * inverseA0;
    b1_ = b1 * inverseA0;
    b2_ = b2 * inverseA0;
    a1_ = a1 * inverseA0;
    a2_ = a2 * inverseA0;
}

void Drift2Character::Biquad::reset() noexcept
{
    state1_ = 0.0f;
    state2_ = 0.0f;
}

float Drift2Character::Biquad::process(float sample) noexcept
{
    const auto output = Drift2Character::sanitise(b0_ * sample + state1_);
    state1_ = Drift2Character::sanitise(b1_ * sample - a1_ * output + state2_);
    state2_ = Drift2Character::sanitise(b2_ * sample - a2_ * output);
    return output;
}

void Drift2Character::AxisFilters::prepare(float sampleRate) noexcept
{
    bodyBand_.setBandPass(sampleRate, 360.0f, 0.90f);
    presenceBand_.setBandPass(sampleRate, 3100.0f, 0.48f);
    reset();
}

void Drift2Character::AxisFilters::reset() noexcept
{
    bodyBand_.reset();
    presenceBand_.reset();
}

std::array<float, 2> Drift2Character::AxisFilters::process(float sample) noexcept
{
    const auto body = bodyBand_.process(sample);
    const auto presence = presenceBand_.process(sample);
    return { body, presence };
}

void Drift2Character::prepare(double sampleRate) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? static_cast<float>(sampleRate)
        : 48000.0f;
    for (auto& filter : filters_)
        filter.prepare(safeSampleRate);
    for (std::size_t index = 0; index < phaseIncrements_.size(); ++index)
        phaseIncrements_[index] = lfoFrequenciesHz[index] / safeSampleRate;
    reset();
}

void Drift2Character::reset() noexcept
{
    for (auto& filter : filters_)
        filter.reset();
    phases_ = initialPhases;
}

void Drift2Character::processFeedback(std::array<float, numFeedbackLines>& feedback,
                                      float characterAmount,
                                      float modulationAmount) noexcept
{
    std::array<float, spectralAxes.size()> components {};
    for (std::size_t index = 0; index < numFeedbackLines; ++index)
    {
        for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
            components[axis] += inverseSqrtEight * spectralAxes[axis][index] * feedback[index];
    }

    std::array<std::array<float, 2>, spectralAxes.size()> bands {};
    for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
    {
        components[axis] = sanitise(components[axis]);
        bands[axis] = filters_[axis].process(components[axis]);
    }

    const auto amount = std::isfinite(characterAmount)
        ? std::clamp(characterAmount, 0.0f, 1.0f)
        : 0.0f;
    if (amount > 0.0f)
    {
        const auto modulation = std::isfinite(modulationAmount)
            ? std::clamp(modulationAmount, 0.0f, 1.0f)
            : 0.0f;
        const auto fastMotion = 0.62f * std::sin(twoPi * phases_[0])
                              + 0.38f * std::sin(twoPi * phases_[1]);
        const auto leftMotion = 0.86f * fastMotion
                              + 0.14f * std::sin(twoPi * phases_[2]);
        const auto rightMotion = -0.86f * fastMotion
                               + 0.14f * std::sin(twoPi * phases_[3]);
        const auto span = 0.35f + 0.65f * modulation;
        const auto leftLinear = std::clamp(0.5f + 0.5f * span * leftMotion, 0.0f, 1.0f);
        const auto rightLinear = std::clamp(0.5f + 0.5f * span * rightMotion, 0.0f, 1.0f);
        const auto leftPosition = shapePosition(leftLinear, modulation);
        const auto rightPosition = shapePosition(rightLinear, modulation);
        const auto presenceContrast = 1.0f + 0.20f * modulation;
        const auto leftPresencePosition = std::clamp(
            0.5f + presenceContrast * (leftPosition - 0.5f), 0.0f, 1.0f);
        const auto rightPresencePosition = std::clamp(
            0.5f + presenceContrast * (rightPosition - 0.5f), 0.0f, 1.0f);
        const auto bodyDepth = 0.15f + 0.37f * modulation;
        const auto presenceDepth = 0.22f + 0.73f * modulation;

        std::array<float, spectralAxes.size()> filteredComponents {};
        for (std::size_t axis = 0; axis < spectralAxes.size(); ++axis)
        {
            const auto position = axis < 2 ? leftPosition : rightPosition;
            const auto presencePosition = axis < 2
                ? leftPresencePosition
                : rightPresencePosition;
            const auto bodyAttenuation = bodyDepth * position;
            const auto presenceAttenuation = presenceDepth * (1.0f - presencePosition);
            const auto delta = -bodyAttenuation * bands[axis][0]
                             - presenceAttenuation * bands[axis][1];
            filteredComponents[axis] = components[axis] + amount * delta;
        }

        for (std::size_t pairStart = 0; pairStart < spectralAxes.size(); pairStart += 2)
        {
            const auto delta0 = filteredComponents[pairStart] - components[pairStart];
            const auto delta1 = filteredComponents[pairStart + 1] - components[pairStart + 1];
            const auto projection = static_cast<double>(components[pairStart]) * delta0
                                  + static_cast<double>(components[pairStart + 1]) * delta1;
            const auto deltaEnergy = static_cast<double>(delta0) * delta0
                                   + static_cast<double>(delta1) * delta1;
            if (2.0 * projection + deltaEnergy > 0.0)
            {
                auto safeDeltaScale = 0.0;
                if (projection < 0.0 && deltaEnergy > 1.0e-20)
                {
                    constexpr auto roundOffMargin = 0.999999;
                    safeDeltaScale = std::clamp(
                        roundOffMargin * (-2.0 * projection / deltaEnergy), 0.0, 1.0);
                }
                filteredComponents[pairStart] = components[pairStart]
                                              + static_cast<float>(safeDeltaScale) * delta0;
                filteredComponents[pairStart + 1] = components[pairStart + 1]
                                                  + static_cast<float>(safeDeltaScale) * delta1;
            }
        }

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

void Drift2Character::advancePhases() noexcept
{
    for (std::size_t index = 0; index < phases_.size(); ++index)
    {
        phases_[index] += phaseIncrements_[index];
        if (phases_[index] >= 1.0f)
            phases_[index] -= 1.0f;
    }
}

float Drift2Character::sanitise(float sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-20f)
        return 0.0f;
    return std::clamp(sample, -16.0f, 16.0f);
}
} // namespace amanita::dsp
