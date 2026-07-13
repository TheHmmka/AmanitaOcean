#include "VeilCharacter.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr std::array<float, 6> leftDelayTimesMs {
    1.57f, 2.53f, 3.89f, 5.83f, 8.47f, 12.53f
};
constexpr std::array<float, 6> rightDelayTimesMs {
    1.67f, 2.63f, 4.01f, 5.89f, 8.59f, 12.67f
};
constexpr std::array<float, 6> leftCoefficients {
    0.65f, 0.62f, 0.59f, 0.56f, 0.53f, 0.50f
};
constexpr std::array<float, 6> rightCoefficients {
    0.63f, 0.60f, 0.58f, 0.55f, 0.52f, 0.49f
};

[[nodiscard]] bool isPrime(int value) noexcept
{
    if (value < 2)
        return false;
    if (value == 2)
        return true;
    if ((value & 1) == 0)
        return false;

    for (auto divisor = 3; divisor <= value / divisor; divisor += 2)
        if (value % divisor == 0)
            return false;

    return true;
}

[[nodiscard]] int nearestOddPrime(int value) noexcept
{
    value = std::max(3, value);
    if ((value & 1) == 0)
        ++value;

    for (auto offset = 0; offset < value; offset += 2)
    {
        const auto lower = value - offset;
        if (lower >= 3 && isPrime(lower))
            return lower;

        if (offset != 0 && isPrime(value + offset))
            return value + offset;
    }

    return value;
}
} // namespace

void VeilCharacter::FixedAllPass::prepare(std::size_t delaySamples, float coefficient)
{
    buffer_.assign(std::max<std::size_t>(delaySamples, 1), 0.0f);
    index_ = 0;
    coefficient_ = std::clamp(coefficient, -0.75f, 0.75f);
}

void VeilCharacter::FixedAllPass::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    index_ = 0;
}

float VeilCharacter::FixedAllPass::process(float sample) noexcept
{
    if (buffer_.empty())
        return sample;

    const auto input = VeilCharacter::sanitise(sample);
    const auto delayed = buffer_[index_];
    const auto output = VeilCharacter::sanitise(delayed - coefficient_ * input);
    buffer_[index_] = VeilCharacter::sanitise(input + coefficient_ * output);

    ++index_;
    if (index_ == buffer_.size())
        index_ = 0;

    return output;
}

void VeilCharacter::prepare(double sampleRate)
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? sampleRate
        : 48000.0;

    for (std::size_t index = 0; index < numStages; ++index)
    {
        const auto leftSamples = nearestOddPrime(static_cast<int>(std::round(
            static_cast<double>(leftDelayTimesMs[index]) * 0.001 * safeSampleRate)));
        const auto rightSamples = nearestOddPrime(static_cast<int>(std::round(
            static_cast<double>(rightDelayTimesMs[index]) * 0.001 * safeSampleRate)));
        diffusersLeft_[index].prepare(static_cast<std::size_t>(leftSamples),
                                      leftCoefficients[index]);
        diffusersRight_[index].prepare(static_cast<std::size_t>(rightSamples),
                                       rightCoefficients[index]);
    }

    reset();
}

void VeilCharacter::reset() noexcept
{
    for (auto& stage : diffusersLeft_)
        stage.reset();
    for (auto& stage : diffusersRight_)
        stage.reset();
}

VeilCharacter::StereoFrame VeilCharacter::processExcitation(float left, float right) noexcept
{
    return {
        processDiffuser(left, diffusersLeft_),
        processDiffuser(right, diffusersRight_)
    };
}

float VeilCharacter::sanitise(float sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-20f)
        return 0.0f;
    return std::clamp(sample, -4.0f, 4.0f);
}

float VeilCharacter::processDiffuser(
    float sample,
    std::array<FixedAllPass, numStages>& stages) noexcept
{
    auto output = sample;
    for (auto& stage : stages)
        output = stage.process(output);
    return sanitise(output);
}
} // namespace amanita::dsp
