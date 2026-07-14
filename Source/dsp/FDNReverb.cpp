#include "FDNReverb.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace amanita::dsp
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 2.0f * pi;
constexpr float inverseSqrtEight = 0.35355339059327376220f;
constexpr float logMinus60dB = -6.90775527898213705205f;
constexpr float freezeFeedback = 0.9995f;
constexpr float bloomFreezeFeedback = 0.9985f;
constexpr float maximumSize = 2.0f;
constexpr float maximumPreDelaySeconds = 0.25f;
constexpr float maximumDelayMotionSeconds = 0.00065f;

constexpr float defaultMinimumMotionSeconds = 0.000025f;
constexpr float bloomMinimumMotionSeconds = 0.000030f;
constexpr float driftMinimumMotionSeconds = 0.000020f;
constexpr float veilMinimumMotionSeconds = 0.000020f;
constexpr float defaultMaximumMotionSeconds = 0.000650f;
constexpr float bloomMaximumMotionSeconds = 0.000500f;
constexpr float driftMaximumMotionSeconds = 0.000100f;
constexpr float veilMaximumMotionSeconds = 0.000400f;

constexpr std::array<int, FDNReverb::numDelayLines> basePrimeSamples48k {
    1423, 1601, 1871, 2089, 2393, 2687, 3011, 3449
};

constexpr std::array<float, FDNReverb::numDelayLines> lfoFrequenciesHz {
    0.071f, 0.083f, 0.097f, 0.109f, 0.127f, 0.139f, 0.157f, 0.173f
};

constexpr std::array<float, FDNReverb::numDelayLines> initialLfoPhases {
    0.00f, 0.37f, 0.73f, 0.19f, 0.61f, 0.89f, 0.43f, 0.27f
};

constexpr std::array<float, FDNReverb::numDelayLines> inputLeftSigns {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

constexpr std::array<float, FDNReverb::numDelayLines> inputRightSigns {
    1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f
};

constexpr std::array<float, FDNReverb::numDelayLines> outputLeftSigns {
    1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f
};

constexpr std::array<float, FDNReverb::numDelayLines> outputRightSigns {
    1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f
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

        if (offset != 0)
        {
            const auto upper = value + offset;
            if (isPrime(upper))
                return upper;
        }
    }

    return value;
}

[[nodiscard]] float clampFinite(float value, float minimum, float maximum, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

[[nodiscard]] float onePoleCoefficient(float cutoffHz, double sampleRate) noexcept
{
    const auto safeCutoff = std::clamp(cutoffHz, 1.0f, static_cast<float>(sampleRate * 0.45));
    return std::exp(-twoPi * safeCutoff / static_cast<float>(sampleRate));
}

[[nodiscard]] float smoothCurve(float value) noexcept
{
    const auto amount = std::clamp(value, 0.0f, 1.0f);
    return amount * amount * (3.0f - 2.0f * amount);
}

[[nodiscard]] float scaleEvolution(float minimum, float maximum, float evolution) noexcept
{
    return minimum + evolution * (maximum - minimum);
}
} // namespace

void FDNReverb::LinearSmoother::prepare(double sampleRate,
                                        double rampSeconds,
                                        float initialValue) noexcept
{
    rampSamples_ = std::max(1, static_cast<int>(std::round(sampleRate * rampSeconds)));
    current_ = initialValue;
    target_ = initialValue;
    step_ = 0.0f;
    remaining_ = 0;
}

void FDNReverb::LinearSmoother::setTarget(float newTarget) noexcept
{
    if (std::abs(newTarget - target_) <= std::numeric_limits<float>::epsilon())
        return;

    target_ = newTarget;
    remaining_ = rampSamples_;
    step_ = (target_ - current_) / static_cast<float>(remaining_);
}

float FDNReverb::LinearSmoother::next() noexcept
{
    if (remaining_ <= 0)
        return current_;

    --remaining_;
    if (remaining_ == 0)
    {
        current_ = target_;
        step_ = 0.0f;
    }
    else
    {
        current_ += step_;
    }

    return current_;
}

void FDNReverb::DelayLine::prepare(std::size_t capacity)
{
    buffer_.assign(std::max<std::size_t>(capacity, 4), 0.0f);
    writeIndex_ = 0;
}

void FDNReverb::DelayLine::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

float FDNReverb::DelayLine::read(float delaySamples) const noexcept
{
    if (buffer_.empty())
        return 0.0f;

    const auto maximumDelay = static_cast<float>(buffer_.size() - 2);
    const auto delay = std::clamp(delaySamples, 1.0f, maximumDelay);
    const auto bufferSize = static_cast<double>(buffer_.size());
    auto readPosition = static_cast<double>(writeIndex_) - static_cast<double>(delay);
    if (readPosition < 0.0f)
        readPosition += bufferSize;
    if (readPosition >= bufferSize)
        readPosition -= bufferSize;

    const auto index0 = static_cast<std::size_t>(readPosition) % buffer_.size();
    const auto index1 = (index0 + 1) % buffer_.size();
    const auto fraction = static_cast<float>(readPosition - static_cast<double>(index0));
    return buffer_[index0] + fraction * (buffer_[index1] - buffer_[index0]);
}

void FDNReverb::DelayLine::write(float sample) noexcept
{
    if (buffer_.empty())
        return;

    buffer_[writeIndex_] = sample;
    writeIndex_ = (writeIndex_ + 1) % buffer_.size();
}

void FDNReverb::VariableDelay::prepare(std::size_t capacity)
{
    buffer_.assign(std::max<std::size_t>(capacity, 4), 0.0f);
    writeIndex_ = 0;
}

void FDNReverb::VariableDelay::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

float FDNReverb::VariableDelay::process(float sample, float delaySamples) noexcept
{
    if (buffer_.empty())
        return sample;

    buffer_[writeIndex_] = sample;

    const auto maximumDelay = static_cast<float>(buffer_.size() - 2);
    const auto delay = std::clamp(delaySamples, 0.0f, maximumDelay);
    const auto bufferSize = static_cast<double>(buffer_.size());
    auto readPosition = static_cast<double>(writeIndex_) - static_cast<double>(delay);
    if (readPosition < 0.0f)
        readPosition += bufferSize;
    if (readPosition >= bufferSize)
        readPosition -= bufferSize;

    const auto index0 = static_cast<std::size_t>(readPosition) % buffer_.size();
    const auto index1 = (index0 + 1) % buffer_.size();
    const auto fraction = static_cast<float>(readPosition - static_cast<double>(index0));
    const auto output = buffer_[index0] + fraction * (buffer_[index1] - buffer_[index0]);

    writeIndex_ = (writeIndex_ + 1) % buffer_.size();
    return output;
}

void FDNReverb::AllPass::prepare(std::size_t delaySamples, float coefficient)
{
    buffer_.assign(std::max<std::size_t>(delaySamples, 1), 0.0f);
    index_ = 0;
    coefficient_ = std::clamp(coefficient, -0.75f, 0.75f);
}

void FDNReverb::AllPass::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    index_ = 0;
}

float FDNReverb::AllPass::process(float sample) noexcept
{
    if (buffer_.empty())
        return sample;

    const auto delayed = buffer_[index_];
    auto output = delayed - coefficient_ * sample;
    auto writeValue = sample + coefficient_ * output;

    if (!std::isfinite(output))
        output = 0.0f;
    if (!std::isfinite(writeValue))
        writeValue = 0.0f;

    buffer_[index_] = std::clamp(writeValue, -8.0f, 8.0f);
    index_ = (index_ + 1) % buffer_.size();
    return std::clamp(output, -8.0f, 8.0f);
}

void FDNReverb::prepare(double sampleRate, int maximumBlockSize)
{
    (void) maximumBlockSize;
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1000.0 ? sampleRate : 48000.0;

    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        const auto scaled = static_cast<int>(std::round(
            static_cast<double>(basePrimeSamples48k[index]) * sampleRate_ / 48000.0));
        nominalDelaySamples_[index] = static_cast<float>(nearestOddPrime(scaled));

        const auto maximumDelay = nominalDelaySamples_[index] * maximumSize
                                + static_cast<float>(sampleRate_
                                                     * (maximumDelayMotionSeconds
                                                        + BloomCharacter::maximumDriftSeconds))
                                + 8.0f;
        delayLines_[index].prepare(static_cast<std::size_t>(std::ceil(maximumDelay)) + 2);
        lfoIncrements_[index] = lfoFrequenciesHz[index] / static_cast<float>(sampleRate_);
    }

    const auto preDelayCapacity = static_cast<std::size_t>(
        std::ceil(sampleRate_ * maximumPreDelaySeconds)) + 4;
    for (auto& delay : preDelayLines_)
        delay.prepare(preDelayCapacity);

    constexpr std::array<float, 4> diffuserTimesLeftMs { 1.31f, 2.11f, 3.17f, 4.37f };
    constexpr std::array<float, 4> diffuserTimesRightMs { 1.47f, 2.29f, 3.47f, 4.79f };

    for (std::size_t index = 0; index < diffusersLeft_.size(); ++index)
    {
        const auto leftSamples = nearestOddPrime(static_cast<int>(
            std::round(diffuserTimesLeftMs[index] * 0.001 * sampleRate_)));
        const auto rightSamples = nearestOddPrime(static_cast<int>(
            std::round(diffuserTimesRightMs[index] * 0.001 * sampleRate_)));
        diffusersLeft_[index].prepare(static_cast<std::size_t>(leftSamples), 0.55f);
        diffusersRight_[index].prepare(static_cast<std::size_t>(rightSamples), 0.55f);
    }

    bloom_.prepare(sampleRate_);
    drift_.prepare(sampleRate_);
    veil_.prepare(sampleRate_);
    bloomAmount_.prepare(sampleRate_, 0.20,
                         parameters_.mode == ReverbMode::bloom ? 1.0f : 0.0f);
    driftAmount_.prepare(sampleRate_, 0.20,
                         parameters_.mode == ReverbMode::drift ? 1.0f : 0.0f);
    veilAmount_.prepare(sampleRate_, 0.20,
                        parameters_.mode == ReverbMode::veil ? 1.0f : 0.0f);
    mix_.prepare(sampleRate_, 0.02, parameters_.mix);
    size_.prepare(sampleRate_, 0.25, parameters_.size);
    preDelaySamples_.prepare(sampleRate_, 0.10,
                             parameters_.preDelayMs * 0.001f * static_cast<float>(sampleRate_));
    lowCutCoefficient_.prepare(sampleRate_, 0.05,
                               onePoleCoefficient(parameters_.lowCutHz, sampleRate_));
    dampingCoefficient_.prepare(sampleRate_, 0.05,
                                onePoleCoefficient(parameters_.highDampingHz, sampleRate_));
    evolution_.prepare(sampleRate_, 0.15, parameters_.evolution);
    width_.prepare(sampleRate_, 0.05, parameters_.width);
    freeze_.prepare(sampleRate_, 0.05, parameters_.freeze ? 1.0f : 0.0f);

    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        const auto delaySeconds = nominalDelaySamples_[index] * parameters_.size
                                / static_cast<float>(sampleRate_);
        const auto gain = std::exp(logMinus60dB * delaySeconds / parameters_.decaySeconds);
        feedbackGains_[index].prepare(sampleRate_, 0.25, std::min(gain, 0.999f));
    }

    prepared_ = true;
    reset();
}

void FDNReverb::reset() noexcept
{
    bloomAmount_.prepare(sampleRate_, 0.20,
                         parameters_.mode == ReverbMode::bloom ? 1.0f : 0.0f);
    driftAmount_.prepare(sampleRate_, 0.20,
                         parameters_.mode == ReverbMode::drift ? 1.0f : 0.0f);
    veilAmount_.prepare(sampleRate_, 0.20,
                        parameters_.mode == ReverbMode::veil ? 1.0f : 0.0f);
    for (auto& delay : delayLines_)
        delay.reset();
    for (auto& delay : preDelayLines_)
        delay.reset();
    for (auto& stage : diffusersLeft_)
        stage.reset();
    for (auto& stage : diffusersRight_)
        stage.reset();
    bloom_.reset();
    drift_.reset();
    veil_.reset();

    lowCutStates_.fill(0.0f);
    dampingStates_.fill(0.0f);
    lfoPhases_ = initialLfoPhases;
}

void FDNReverb::setParameters(const ReverbParameters& newParameters) noexcept
{
    switch (newParameters.mode)
    {
        case ReverbMode::bloom:
        case ReverbMode::drift:
        case ReverbMode::veil:
            parameters_.mode = newParameters.mode;
            break;
        case ReverbMode::defaultMode:
        default:
            parameters_.mode = ReverbMode::defaultMode;
            break;
    }
    parameters_.mix = clampFinite(newParameters.mix, 0.0f, 1.0f, parameters_.mix);
    parameters_.decaySeconds = clampFinite(newParameters.decaySeconds, 0.2f, 30.0f,
                                            parameters_.decaySeconds);
    parameters_.size = clampFinite(newParameters.size, 0.5f, maximumSize, parameters_.size);
    parameters_.preDelayMs = clampFinite(newParameters.preDelayMs, 0.0f, 250.0f,
                                          parameters_.preDelayMs);
    parameters_.lowCutHz = clampFinite(newParameters.lowCutHz, 20.0f, 1000.0f,
                                        parameters_.lowCutHz);
    parameters_.highDampingHz = clampFinite(newParameters.highDampingHz, 1000.0f, 20000.0f,
                                             parameters_.highDampingHz);
    parameters_.evolution = clampFinite(newParameters.evolution, 0.0f, 1.0f,
                                        parameters_.evolution);
    parameters_.width = clampFinite(newParameters.width, 0.0f, 2.0f, parameters_.width);
    parameters_.freeze = newParameters.freeze;

    if (prepared_)
        updateTargets();
}

const ReverbParameters& FDNReverb::getParameters() const noexcept
{
    return parameters_;
}

void FDNReverb::updateTargets() noexcept
{
    bloomAmount_.setTarget(parameters_.mode == ReverbMode::bloom ? 1.0f : 0.0f);
    driftAmount_.setTarget(parameters_.mode == ReverbMode::drift ? 1.0f : 0.0f);
    veilAmount_.setTarget(parameters_.mode == ReverbMode::veil ? 1.0f : 0.0f);
    mix_.setTarget(parameters_.mix);
    size_.setTarget(parameters_.size);
    preDelaySamples_.setTarget(parameters_.preDelayMs * 0.001f * static_cast<float>(sampleRate_));
    lowCutCoefficient_.setTarget(onePoleCoefficient(parameters_.lowCutHz, sampleRate_));
    dampingCoefficient_.setTarget(onePoleCoefficient(parameters_.highDampingHz, sampleRate_));
    evolution_.setTarget(parameters_.evolution);
    width_.setTarget(parameters_.width);
    freeze_.setTarget(parameters_.freeze ? 1.0f : 0.0f);

    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        const auto delaySeconds = nominalDelaySamples_[index] * parameters_.size
                                / static_cast<float>(sampleRate_);
        const auto gain = std::exp(logMinus60dB * delaySeconds / parameters_.decaySeconds);
        feedbackGains_[index].setTarget(std::min(gain, 0.999f));
    }
}

float FDNReverb::diffuseInput(float sample, std::array<AllPass, 4>& stages) noexcept
{
    auto output = sample;
    for (auto& stage : stages)
        output = stage.process(output);
    return sanitise(output);
}

void FDNReverb::process(float* left, float* right, int numSamples) noexcept
{
    if (!prepared_ || left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (auto sample = 0; sample < numSamples; ++sample)
        processSample(left[sample], right[sample]);
}

void FDNReverb::processSample(float& left, float& right) noexcept
{
    if (!prepared_)
        return;

    const auto dryLeft = sanitise(left, 4.0f);
    const auto dryRight = sanitise(right, 4.0f);
    const auto preDelay = preDelaySamples_.next();
    const auto diffusedLeft = diffuseInput(preDelayLines_[0].process(dryLeft, preDelay),
                                           diffusersLeft_);
    const auto diffusedRight = diffuseInput(preDelayLines_[1].process(dryRight, preDelay),
                                            diffusersRight_);
    const auto bloomExcitation = bloom_.processExcitation(diffusedLeft, diffusedRight);
    const auto veilExcitation = veil_.processExcitation(diffusedLeft, diffusedRight);
    const auto bloomAmount = bloomAmount_.next();
    const auto driftAmount = driftAmount_.next();
    const auto veilAmount = veilAmount_.next();
    const auto evolution = smoothCurve(evolution_.next());
    const auto defaultAmount = std::clamp(1.0f - bloomAmount - driftAmount - veilAmount,
                                          0.0f, 1.0f);
    const auto bloomStrength = 0.08f + 0.92f * evolution;
    const auto veilStrength = 0.04f + 0.96f * evolution;
    const auto effectiveBloomAmount = bloomAmount * bloomStrength;
    const auto effectiveVeilAmount = veilAmount * veilStrength;
    auto excitationLeft = diffusedLeft;
    auto excitationRight = diffusedRight;
    if (effectiveBloomAmount > 0.0f)
    {
        excitationLeft += effectiveBloomAmount * (bloomExcitation.left - diffusedLeft);
        excitationRight += effectiveBloomAmount * (bloomExcitation.right - diffusedRight);
    }
    if (effectiveVeilAmount > 0.0f)
    {
        excitationLeft += effectiveVeilAmount * (veilExcitation.left - diffusedLeft);
        excitationRight += effectiveVeilAmount * (veilExcitation.right - diffusedRight);
    }

    const auto size = size_.next();
    const auto motionSeconds = defaultAmount
                                   * scaleEvolution(defaultMinimumMotionSeconds,
                                                    defaultMaximumMotionSeconds, evolution)
                             + bloomAmount
                                   * scaleEvolution(bloomMinimumMotionSeconds,
                                                    bloomMaximumMotionSeconds, evolution)
                             + driftAmount
                                   * scaleEvolution(driftMinimumMotionSeconds,
                                                    driftMaximumMotionSeconds, evolution)
                             + veilAmount
                                   * scaleEvolution(veilMinimumMotionSeconds,
                                                    veilMaximumMotionSeconds, evolution);
    const auto modulationDepth = static_cast<float>(sampleRate_) * motionSeconds;
    const auto lowCutCoefficient = lowCutCoefficient_.next();
    const auto dampingCoefficient = dampingCoefficient_.next();
    const auto freeze = freeze_.next();

    std::array<float, numDelayLines> delayed {};
    std::array<float, numDelayLines> feedback {};

    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        const auto modulation = modulationDepth * std::sin(twoPi * lfoPhases_[index]);
        auto delaySamples = nominalDelaySamples_[index] * size + modulation;
        const auto bloomDrift = bloom_.nextDriftSamples(index, evolution,
                                                        bloomAmount > 0.0f);
        if (bloomAmount > 0.0f)
            delaySamples += effectiveBloomAmount * bloomDrift;
        delayed[index] = sanitise(delayLines_[index].read(delaySamples));

        lfoPhases_[index] += lfoIncrements_[index];
        if (lfoPhases_[index] >= 1.0f)
            lfoPhases_[index] -= 1.0f;

        lowCutStates_[index] = flushDenormal(
            lowCutCoefficient * lowCutStates_[index]
            + (1.0f - lowCutCoefficient) * delayed[index]);
        const auto highPassed = delayed[index] - lowCutStates_[index];

        dampingStates_[index] = flushDenormal(
            dampingCoefficient * dampingStates_[index]
            + (1.0f - dampingCoefficient) * highPassed);

        const auto filtered = sanitise(dampingStates_[index]);
        const auto freezeMorphed = filtered + freeze * (delayed[index] - filtered);
        const auto normalGain = feedbackGains_[index].next();
        auto freezeGain = freezeFeedback
                        + bloomAmount * (bloomFreezeFeedback - freezeFeedback);
        const auto loopGain = normalGain + freeze * (freezeGain - normalGain);
        feedback[index] = sanitise(freezeMorphed * loopGain, 4.0f);
    }

    // Freeze holds the spectrum already present in the tail. Fading the
    // subtractive spectral kernel to bypass keeps that hold linear and stable;
    // the independently modulated FDN delays continue to provide slow motion.
    const auto feedbackDriftAmount = driftAmount * (1.0f - freeze);
    drift_.processFeedback(feedback, feedbackDriftAmount, evolution,
                           driftAmount > 0.0f);
    applyFeedbackMatrix(feedback);

    const auto inputGain = 1.0f - freeze;
    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        const auto injection = inverseSqrtEight
                             * (inputLeftSigns[index] * excitationLeft
                                + inputRightSigns[index] * excitationRight);
        delayLines_[index].write(sanitise(feedback[index] + inputGain * injection, 4.0f));
    }

    auto wetLeft = 0.0f;
    auto wetRight = 0.0f;
    for (std::size_t index = 0; index < numDelayLines; ++index)
    {
        wetLeft += outputLeftSigns[index] * delayed[index];
        wetRight += outputRightSigns[index] * delayed[index];
    }
    wetLeft *= inverseSqrtEight;
    wetRight *= inverseSqrtEight;

    const auto width = width_.next();
    const auto mid = 0.5f * (wetLeft + wetRight);
    const auto side = 0.5f * (wetLeft - wetRight) * width;
    wetLeft = mid + side;
    wetRight = mid - side;

    const auto mix = mix_.next();
    left = flushDenormal(sanitise(dryLeft + mix * (wetLeft - dryLeft)));
    right = flushDenormal(sanitise(dryRight + mix * (wetRight - dryRight)));
}

void FDNReverb::applyFeedbackMatrix(std::array<float, numDelayLines>& values) noexcept
{
    for (std::size_t stride = 1; stride < numDelayLines; stride *= 2)
    {
        for (std::size_t block = 0; block < numDelayLines; block += stride * 2)
        {
            for (std::size_t offset = 0; offset < stride; ++offset)
            {
                const auto first = values[block + offset];
                const auto second = values[block + offset + stride];
                values[block + offset] = first + second;
                values[block + offset + stride] = first - second;
            }
        }
    }

    for (auto& value : values)
        value *= inverseSqrtEight;
}

float FDNReverb::sanitise(float sample, float limit) noexcept
{
    if (!std::isfinite(sample))
        return 0.0f;
    return std::clamp(sample, -limit, limit);
}

float FDNReverb::flushDenormal(float sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < 1.0e-20f)
        return 0.0f;
    return std::clamp(sample, -8.0f, 8.0f);
}

double FDNReverb::getSampleRate() const noexcept
{
    return sampleRate_;
}

const std::array<float, FDNReverb::numDelayLines>& FDNReverb::getNominalDelaySamples() const noexcept
{
    return nominalDelaySamples_;
}
} // namespace amanita::dsp
