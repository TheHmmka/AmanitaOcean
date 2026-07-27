#include "LateralDecay.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr float logMinus60dB = -6.90775527898213705205f;

[[nodiscard]] float finiteClamped(float value,
                                  float minimum,
                                  float maximum,
                                  float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

[[nodiscard]] float smoothCurve(float value) noexcept
{
    const auto amount = std::clamp(value, 0.0f, 1.0f);
    return amount * amount * (3.0f - 2.0f * amount);
}
} // namespace

bool LateralDecay::isLateralLine(std::size_t lineIndex) noexcept
{
    // The current stereo geometry uses even lines as the shared Mid/core
    // projection and odd lines as the alternating lateral projection.
    return lineIndex < numDelayLines && (lineIndex & 1U) != 0U;
}

float LateralDecay::decayTimeScale(std::size_t lineIndex,
                                   float width,
                                   float evolution) noexcept
{
    if (!isLateralLine(lineIndex))
        return 1.0f;

    const auto widthAmount = smoothCurve(
        0.5f * finiteClamped(width, 0.0f, 2.0f, 0.0f));
    const auto evolutionAmount = smoothCurve(
        finiteClamped(evolution, 0.0f, 1.0f, 0.0f));
    return 1.0f
         + maximumLateralExtension * widthAmount * evolutionAmount;
}

float LateralDecay::feedbackGain(float delaySeconds,
                                 float decaySeconds,
                                 std::size_t lineIndex,
                                 float width,
                                 float evolution) noexcept
{
    const auto safeDelay = finiteClamped(delaySeconds, 0.0f, 10.0f, 0.0f);
    const auto safeDecay = finiteClamped(decaySeconds, 0.2f, 30.0f, 5.0f);
    const auto effectiveDecay = safeDecay
                              * decayTimeScale(lineIndex, width, evolution);
    const auto gain = std::exp(logMinus60dB * safeDelay / effectiveDecay);
    return std::clamp(gain, 0.0f, maximumFeedbackGain);
}
} // namespace amanita::dsp
