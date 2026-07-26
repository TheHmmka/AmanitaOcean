#include "StereoField.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 2.0f * pi;
constexpr float subAnchorFrequencyHz = 145.0f;
// A deliberately gentle anchor: measured Side is about -1.25 dB at 60 Hz,
// while the existing Focus vocal and 190-BPM transient balances stay intact.
constexpr float subAnchorLowSideGain = 0.84f;

// Edge-weighted equal-power positions keep the open stereo image of the
// original orthogonal decoder. Each line uses one shared polarity in L/R, so
// none of the eight delay lines can cancel itself during mono summing.
constexpr std::array<float, StereoField::numDelayLines> outputLeftGains {
     0.493844170298f,
     0.500000000000f,
    -0.078217232520f,
    -0.500000000000f,
     0.078217232520f,
     0.000000000000f,
    -0.493844170298f,
     0.000000000000f
};

constexpr std::array<float, StereoField::numDelayLines> outputRightGains {
     0.078217232520f,
     0.000000000000f,
    -0.493844170298f,
     0.000000000000f,
     0.493844170298f,
    -0.500000000000f,
    -0.078217232520f,
     0.500000000000f
};

constexpr float inverseSqrtEight = 0.35355339059327376220f;

constexpr std::array<float, StereoField::numDelayLines> legacyOutputLeftSigns {
    1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f
};

constexpr std::array<float, StereoField::numDelayLines> legacyOutputRightSigns {
    1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f
};

[[nodiscard]] float flushDenormal(float sample) noexcept
{
    return std::abs(sample) < 1.0e-20f ? 0.0f : sample;
}
} // namespace

void StereoField::prepare(double sampleRate) noexcept
{
    const auto safeSampleRate = std::max(1.0, sampleRate);
    subAnchorCoefficient_ = std::exp(
        -twoPi * subAnchorFrequencyHz / static_cast<float>(safeSampleRate));
    reset();
}

void StereoField::reset() noexcept
{
    subAnchorLowSide_ = 0.0f;
}

StereoField::Frame StereoField::decode(
    const std::array<float, numDelayLines>& delayOutputs) noexcept
{
    Frame output;
    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        output.left += outputLeftGains[index] * delayOutputs[index];
        output.right += outputRightGains[index] * delayOutputs[index];
    }
    return output;
}

StereoField::Frame StereoField::decodeLegacy(
    const std::array<float, numDelayLines>& delayOutputs) noexcept
{
    Frame output;
    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        output.left += legacyOutputLeftSigns[index] * delayOutputs[index];
        output.right += legacyOutputRightSigns[index] * delayOutputs[index];
    }
    output.left *= inverseSqrtEight;
    output.right *= inverseSqrtEight;
    return output;
}

StereoField::Frame StereoField::applyWidth(float left, float right,
                                           float width) noexcept
{
    const auto safeWidth = std::isfinite(width)
        ? std::clamp(width, 0.0f, 2.0f)
        : 1.0f;
    const auto mid = 0.5f * (left + right);
    const auto rawSide = 0.5f * (left - right);

    subAnchorLowSide_ = flushDenormal(
        subAnchorCoefficient_ * subAnchorLowSide_
        + (1.0f - subAnchorCoefficient_) * rawSide);
    const auto anchoredSide = safeWidth
                            * (rawSide
                               + (subAnchorLowSideGain - 1.0f)
                                     * subAnchorLowSide_);

    return { mid + anchoredSide, mid - anchoredSide };
}

StereoField::Frame StereoField::applyLegacyWidth(float left, float right,
                                                 float width) noexcept
{
    const auto safeWidth = std::isfinite(width)
        ? std::clamp(width, 0.0f, 2.0f)
        : 1.0f;
    const auto mid = 0.5f * (left + right);
    const auto side = 0.5f * (left - right) * safeWidth;
    return { mid + side, mid - side };
}
} // namespace amanita::dsp
