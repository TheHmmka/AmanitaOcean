#include "BloomCharacter.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr std::array<float, 4> leftSwellTimesMs { 7.9f, 18.7f, 34.1f, 55.7f };
constexpr std::array<float, 4> rightSwellTimesMs { 9.1f, 21.1f, 37.3f, 60.1f };
constexpr std::array<float, 4> swellGains { 0.10f, 0.16f, 0.25f, 0.37f };
constexpr float swellDirectGain = 0.12f;

constexpr std::array<float, 4> leftDiffuserTimesMs { 5.83f, 8.11f, 11.47f, 16.09f };
constexpr std::array<float, 4> rightDiffuserTimesMs { 6.37f, 8.89f, 12.53f, 17.41f };

constexpr float twoPi = 6.28318530717958647692f;
constexpr std::array<float, BloomCharacter::numFeedbackLines> driftFrequenciesHz {
    0.013f, 0.017f, 0.019f, 0.023f, 0.029f, 0.031f, 0.037f, 0.041f
};
constexpr std::array<float, BloomCharacter::numFeedbackLines> initialDriftPhases {
    0.11f, 0.67f, 0.31f, 0.83f, 0.47f, 0.23f, 0.91f, 0.56f
};
constexpr std::array<float, BloomCharacter::numFeedbackLines> driftDepthScales {
    0.86f, 1.08f, 0.94f, 1.15f, 1.00f, 0.89f, 1.12f, 0.97f
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

void BloomCharacter::FixedAllPass::prepare(std::size_t delaySamples, float coefficient)
{
    buffer_.assign(std::max<std::size_t>(delaySamples, 1), 0.0f);
    index_ = 0;
    coefficient_ = std::clamp(coefficient, -0.75f, 0.75f);
}

void BloomCharacter::FixedAllPass::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    index_ = 0;
}

float BloomCharacter::FixedAllPass::process(float sample) noexcept
{
    if (buffer_.empty())
        return sample;

    const auto delayed = buffer_[index_];
    const auto output = delayed - coefficient_ * sample;
    const auto writeValue = sample + coefficient_ * output;
    buffer_[index_] = BloomCharacter::sanitise(writeValue);
    ++index_;
    if (index_ == buffer_.size())
        index_ = 0;
    return BloomCharacter::sanitise(output);
}

void BloomCharacter::RisingTapFIR::prepare(double sampleRate, bool rightChannel)
{
    const auto& times = rightChannel ? rightSwellTimesMs : leftSwellTimesMs;
    auto maximumTap = std::size_t { 0 };

    for (std::size_t index = 0; index < numTaps; ++index)
    {
        const auto scaled = static_cast<int>(std::round(
            static_cast<double>(times[index]) * 0.001 * sampleRate));
        tapSamples_[index] = static_cast<std::size_t>(nearestOddPrime(scaled));
        maximumTap = std::max(maximumTap, tapSamples_[index]);
    }

    buffer_.assign(maximumTap + 2, 0.0f);
    writeIndex_ = 0;
}

void BloomCharacter::RisingTapFIR::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

float BloomCharacter::RisingTapFIR::process(float sample) noexcept
{
    if (buffer_.empty())
        return sample;

    const auto input = BloomCharacter::sanitise(sample);
    buffer_[writeIndex_] = input;
    auto output = swellDirectGain * input;

    for (std::size_t index = 0; index < numTaps; ++index)
    {
        const auto readIndex = writeIndex_ >= tapSamples_[index]
            ? writeIndex_ - tapSamples_[index]
            : writeIndex_ + buffer_.size() - tapSamples_[index];
        output += swellGains[index] * buffer_[readIndex];
    }

    ++writeIndex_;
    if (writeIndex_ == buffer_.size())
        writeIndex_ = 0;
    return BloomCharacter::sanitise(output);
}

void BloomCharacter::prepare(double sampleRate)
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? sampleRate
        : 48000.0;
    sampleRate_ = static_cast<float>(safeSampleRate);

    swellLeft_.prepare(safeSampleRate, false);
    swellRight_.prepare(safeSampleRate, true);

    for (std::size_t index = 0; index < diffusersLeft_.size(); ++index)
    {
        const auto leftSamples = nearestOddPrime(static_cast<int>(std::round(
            static_cast<double>(leftDiffuserTimesMs[index]) * 0.001 * safeSampleRate)));
        const auto rightSamples = nearestOddPrime(static_cast<int>(std::round(
            static_cast<double>(rightDiffuserTimesMs[index]) * 0.001 * safeSampleRate)));
        diffusersLeft_[index].prepare(static_cast<std::size_t>(leftSamples), 0.47f);
        diffusersRight_[index].prepare(static_cast<std::size_t>(rightSamples), 0.47f);
    }

    for (std::size_t index = 0; index < numFeedbackLines; ++index)
        driftIncrements_[index] = driftFrequenciesHz[index] / sampleRate_;

    reset();
}

void BloomCharacter::reset() noexcept
{
    swellLeft_.reset();
    swellRight_.reset();
    for (auto& stage : diffusersLeft_)
        stage.reset();
    for (auto& stage : diffusersRight_)
        stage.reset();
    driftPhases_ = initialDriftPhases;
}

BloomCharacter::StereoFrame BloomCharacter::processExcitation(float left, float right) noexcept
{
    const auto swellLeft = swellLeft_.process(left);
    const auto swellRight = swellRight_.process(right);
    const auto layeredLeft = processDiffuser(swellLeft, diffusersLeft_);
    const auto layeredRight = processDiffuser(swellRight, diffusersRight_);

    return {
        sanitise(0.62f * left + layeredLeft),
        sanitise(0.62f * right + layeredRight)
    };
}

float BloomCharacter::nextDriftSamples(std::size_t lineIndex,
                                       float modulationAmount,
                                       bool renderOffset) noexcept
{
    if (lineIndex >= numFeedbackLines)
        return 0.0f;

    auto offset = 0.0f;
    if (renderOffset)
    {
        const auto amount = std::isfinite(modulationAmount)
            ? std::clamp(modulationAmount, 0.0f, 1.0f)
            : 0.0f;
        const auto depthSeconds = (0.00010f + 0.00018f * amount)
                                * driftDepthScales[lineIndex];
        offset = sampleRate_ * depthSeconds * std::sin(twoPi * driftPhases_[lineIndex]);
    }

    driftPhases_[lineIndex] += driftIncrements_[lineIndex];
    if (driftPhases_[lineIndex] >= 1.0f)
        driftPhases_[lineIndex] -= 1.0f;

    return std::isfinite(offset) ? offset : 0.0f;
}

float BloomCharacter::sanitise(float sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-20f)
        return 0.0f;
    return std::clamp(sample, -4.0f, 4.0f);
}

float BloomCharacter::processDiffuser(float sample,
                                      std::array<FixedAllPass, 4>& stages) noexcept
{
    auto output = sample;
    for (auto& stage : stages)
        output = stage.process(output);
    return sanitise(output);
}
} // namespace amanita::dsp
