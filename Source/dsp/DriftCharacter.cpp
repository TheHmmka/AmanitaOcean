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

constexpr std::array<float, 4> airLfoFrequenciesHz {
    0.0137f, 0.0311f, 0.0179f, 0.0373f
};
constexpr std::array<float, 4> airInitialPhases {
    0.11f, 0.67f, 0.43f, 0.89f
};
constexpr std::array<float, 4> bodyPresenceLfoFrequenciesHz {
    0.1813f, 0.0917f, 0.0209f, 0.0147f
};
constexpr std::array<float, 4> bodyPresenceInitialPhases {
    0.07f, 0.41f, 0.19f, 0.61f
};

[[nodiscard]] float onePoleCoefficient(float cutoffHz, float sampleRate) noexcept
{
    const auto cutoff = std::clamp(cutoffHz, 20.0f, sampleRate * 0.45f);
    return std::exp(-twoPi * cutoff / sampleRate);
}

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

[[nodiscard]] float sanitise(float sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-20f)
        return 0.0f;
    return std::clamp(sample, -16.0f, 16.0f);
}
} // namespace

void DriftCharacter::prepare(double sampleRate) noexcept
{
    airKernel_.prepare(sampleRate);
    bodyPresenceKernel_.prepare(sampleRate);
}

void DriftCharacter::reset() noexcept
{
    airKernel_.reset();
    bodyPresenceKernel_.reset();
}

void DriftCharacter::processFeedback(std::array<float, numFeedbackLines>& feedback,
                                     float characterAmount,
                                     float evolutionAmount,
                                     bool applyOutput) noexcept
{
    auto airFeedback = feedback;
    auto bodyPresenceFeedback = feedback;
    airKernel_.processFeedback(airFeedback, characterAmount, evolutionAmount);
    bodyPresenceKernel_.processFeedback(bodyPresenceFeedback, characterAmount,
                                        evolutionAmount);

    // Both kernels stay warm even while another Character owns the feedback path.
    if (!applyOutput)
        return;

    const auto evolution = std::isfinite(evolutionAmount)
        ? std::clamp(evolutionAmount, 0.0f, 1.0f)
        : 0.0f;
    for (std::size_t index = 0; index < numFeedbackLines; ++index)
        feedback[index] = airFeedback[index]
                        + evolution * (bodyPresenceFeedback[index] - airFeedback[index]);
}

void DriftCharacter::AirKernel::EndpointFilters::reset() noexcept
{
    darkState_ = 0.0f;
    brightState_ = 0.0f;
}

std::array<float, 2> DriftCharacter::AirKernel::EndpointFilters::process(
    float sample,
    float darkCoefficient,
    float brightCoefficient) noexcept
{
    darkState_ = sanitise(
        darkCoefficient * darkState_ + (1.0f - darkCoefficient) * sample);
    brightState_ = sanitise(
        brightCoefficient * brightState_ + (1.0f - brightCoefficient) * sample);
    return { darkState_, brightState_ };
}

void DriftCharacter::AirKernel::prepare(double sampleRate) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? static_cast<float>(sampleRate)
        : 48000.0f;
    darkCoefficient_ = onePoleCoefficient(600.0f, safeSampleRate);
    brightCoefficient_ = onePoleCoefficient(18000.0f, safeSampleRate);

    for (std::size_t index = 0; index < phaseIncrements_.size(); ++index)
        phaseIncrements_[index] = airLfoFrequenciesHz[index] / safeSampleRate;

    reset();
}

void DriftCharacter::AirKernel::reset() noexcept
{
    for (auto& filter : filters_)
        filter.reset();
    phases_ = airInitialPhases;
}

void DriftCharacter::AirKernel::processFeedback(
    std::array<float, numFeedbackLines>& feedback,
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

void DriftCharacter::AirKernel::advancePhases() noexcept
{
    for (std::size_t index = 0; index < phases_.size(); ++index)
    {
        phases_[index] += phaseIncrements_[index];
        if (phases_[index] >= 1.0f)
            phases_[index] -= 1.0f;
    }
}

void DriftCharacter::BodyPresenceKernel::Biquad::setBandPass(float sampleRate,
                                                              float frequencyHz,
                                                              float q) noexcept
{
    const auto omega = twoPi * safeFrequency(frequencyHz, sampleRate) / sampleRate;
    const auto alpha = std::sin(omega) / (2.0f * std::max(q, 0.05f));
    setCoefficients(alpha, 0.0f, -alpha,
                    1.0f + alpha, -2.0f * std::cos(omega), 1.0f - alpha);
}

void DriftCharacter::BodyPresenceKernel::Biquad::setCoefficients(
    float b0, float b1, float b2,
    float a0, float a1, float a2) noexcept
{
    const auto inverseA0 = 1.0f / a0;
    b0_ = b0 * inverseA0;
    b1_ = b1 * inverseA0;
    b2_ = b2 * inverseA0;
    a1_ = a1 * inverseA0;
    a2_ = a2 * inverseA0;
}

void DriftCharacter::BodyPresenceKernel::Biquad::reset() noexcept
{
    state1_ = 0.0f;
    state2_ = 0.0f;
}

float DriftCharacter::BodyPresenceKernel::Biquad::process(float sample) noexcept
{
    const auto output = sanitise(b0_ * sample + state1_);
    state1_ = sanitise(b1_ * sample - a1_ * output + state2_);
    state2_ = sanitise(b2_ * sample - a2_ * output);
    return output;
}

void DriftCharacter::BodyPresenceKernel::AxisFilters::prepare(float sampleRate) noexcept
{
    bodyBand_.setBandPass(sampleRate, 360.0f, 0.90f);
    presenceBand_.setBandPass(sampleRate, 3100.0f, 0.48f);
    reset();
}

void DriftCharacter::BodyPresenceKernel::AxisFilters::reset() noexcept
{
    bodyBand_.reset();
    presenceBand_.reset();
}

std::array<float, 2> DriftCharacter::BodyPresenceKernel::AxisFilters::process(
    float sample) noexcept
{
    const auto body = bodyBand_.process(sample);
    const auto presence = presenceBand_.process(sample);
    return { body, presence };
}

void DriftCharacter::BodyPresenceKernel::prepare(double sampleRate) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? static_cast<float>(sampleRate)
        : 48000.0f;
    for (auto& filter : filters_)
        filter.prepare(safeSampleRate);
    for (std::size_t index = 0; index < phaseIncrements_.size(); ++index)
        phaseIncrements_[index] = bodyPresenceLfoFrequenciesHz[index] / safeSampleRate;
    reset();
}

void DriftCharacter::BodyPresenceKernel::reset() noexcept
{
    for (auto& filter : filters_)
        filter.reset();
    phases_ = bodyPresenceInitialPhases;
}

void DriftCharacter::BodyPresenceKernel::processFeedback(
    std::array<float, numFeedbackLines>& feedback,
    float characterAmount,
    float evolutionAmount) noexcept
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
        const auto evolution = std::isfinite(evolutionAmount)
            ? std::clamp(evolutionAmount, 0.0f, 1.0f)
            : 0.0f;
        const auto fastMotion = 0.62f * std::sin(twoPi * phases_[0])
                              + 0.38f * std::sin(twoPi * phases_[1]);
        const auto leftMotion = 0.86f * fastMotion
                              + 0.14f * std::sin(twoPi * phases_[2]);
        const auto rightMotion = -0.86f * fastMotion
                               + 0.14f * std::sin(twoPi * phases_[3]);
        const auto span = 0.35f + 0.65f * evolution;
        const auto leftLinear = std::clamp(0.5f + 0.5f * span * leftMotion, 0.0f, 1.0f);
        const auto rightLinear = std::clamp(0.5f + 0.5f * span * rightMotion, 0.0f, 1.0f);
        const auto leftPosition = shapePosition(leftLinear, evolution);
        const auto rightPosition = shapePosition(rightLinear, evolution);
        const auto presenceContrast = 1.0f + 0.20f * evolution;
        const auto leftPresencePosition = std::clamp(
            0.5f + presenceContrast * (leftPosition - 0.5f), 0.0f, 1.0f);
        const auto rightPresencePosition = std::clamp(
            0.5f + presenceContrast * (rightPosition - 0.5f), 0.0f, 1.0f);
        // A moderate per-pass range is enough to create large RT60 movement in
        // the recursive loop without carving a near-null through vocal presence.
        const auto bodyDepth = 0.15f + 0.27f * evolution;
        const auto presenceDepth = 0.22f + 0.36f * evolution;

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

        // The band filters are stateful, so instantaneous energy limiting would
        // become a signal-dependent waveshaper around waveform zero crossings.
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

void DriftCharacter::BodyPresenceKernel::advancePhases() noexcept
{
    for (std::size_t index = 0; index < phases_.size(); ++index)
    {
        phases_[index] += phaseIncrements_[index];
        if (phases_[index] >= 1.0f)
            phases_[index] -= 1.0f;
    }
}
} // namespace amanita::dsp
