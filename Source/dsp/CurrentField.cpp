#include "CurrentField.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr double twoPi = 6.28318530717958647692;
constexpr double frequencyAHz = 0.0173;
constexpr double frequencyBHz = 0.0067;
constexpr double initialPhaseA = 0.13;
constexpr double initialPhaseB = 0.61;
constexpr std::uint32_t normalisationInterval = 65536;

// Symmetric coordinates on the unit circle. Their x/y sums are zero, so the
// field cannot move every delay or damping pole in the same direction.
constexpr float axisLong = 0.923879532511f;
constexpr float axisShort = 0.382683432365f;
constexpr std::array<float, CurrentField::numLines> lineX {
    -axisLong, -axisShort, axisLong, -axisShort,
     axisShort, axisShort, -axisLong, axisLong
};
constexpr std::array<float, CurrentField::numLines> lineY {
    -axisShort, -axisLong, -axisShort, axisLong,
    -axisLong, axisLong, axisShort, axisShort
};

constexpr float stereoRotationCos = 0.866025403784f;
constexpr float stereoRotationSin = 0.5f;
} // namespace

void CurrentField::prepare(double sampleRate) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? sampleRate
        : 48000.0;
    const auto incrementAAngle = twoPi * frequencyAHz / safeSampleRate;
    const auto incrementBAngle = twoPi * frequencyBHz / safeSampleRate;
    incrementACosine_ = std::cos(incrementAAngle);
    incrementASine_ = std::sin(incrementAAngle);
    incrementBCosine_ = std::cos(incrementBAngle);
    incrementBSine_ = std::sin(incrementBAngle);
    reset();
}

void CurrentField::reset() noexcept
{
    const auto initialAngleA = twoPi * initialPhaseA;
    const auto initialAngleB = twoPi * initialPhaseB;
    oscillatorACosine_ = std::cos(initialAngleA);
    oscillatorASine_ = std::sin(initialAngleA);
    oscillatorBCosine_ = std::cos(initialAngleB);
    oscillatorBSine_ = std::sin(initialAngleB);
    samplesUntilNormalisation_ = normalisationInterval;
    lastFrame_ = {};
    activeLastSample_ = false;
}

const CurrentField::Frame& CurrentField::next(bool active) noexcept
{
    if (!active)
    {
        if (activeLastSample_)
            lastFrame_ = {};
        activeLastSample_ = false;
        advancePhases();
        return lastFrame_;
    }

    activeLastSample_ = true;
    const auto flowX =
        0.78 * oscillatorACosine_ + 0.22 * oscillatorBCosine_;
    const auto flowY =
        0.78 * oscillatorASine_ - 0.22 * oscillatorBSine_;

    lastFrame_.flowX = static_cast<float>(flowX);
    lastFrame_.flowY = static_cast<float>(flowY);
    for (std::size_t index = 0; index < numLines; ++index)
    {
        const auto radial = static_cast<double>(lineX[index]) * flowX
                          + static_cast<double>(lineY[index]) * flowY;
        const auto tangential = -static_cast<double>(lineX[index]) * flowY
                              + static_cast<double>(lineY[index]) * flowX;
        lastFrame_.delay[index] = static_cast<float>(radial);
        lastFrame_.damping[index] = static_cast<float>(tangential);
        lastFrame_.stereo[index] = static_cast<float>(
            stereoRotationCos * radial + stereoRotationSin * tangential);
    }

    advancePhases();
    return lastFrame_;
}

const CurrentField::Frame& CurrentField::getLastFrame() const noexcept
{
    return lastFrame_;
}

void CurrentField::advancePhases() noexcept
{
    const auto nextACosine =
        oscillatorACosine_ * incrementACosine_
        - oscillatorASine_ * incrementASine_;
    oscillatorASine_ =
        oscillatorASine_ * incrementACosine_
        + oscillatorACosine_ * incrementASine_;
    oscillatorACosine_ = nextACosine;

    const auto nextBCosine =
        oscillatorBCosine_ * incrementBCosine_
        - oscillatorBSine_ * incrementBSine_;
    oscillatorBSine_ =
        oscillatorBSine_ * incrementBCosine_
        + oscillatorBCosine_ * incrementBSine_;
    oscillatorBCosine_ = nextBCosine;

    if (--samplesUntilNormalisation_ != 0)
        return;

    const auto normalise = [](double& cosine, double& sine) noexcept
    {
        const auto magnitudeSquared = cosine * cosine + sine * sine;
        const auto inverseMagnitude = magnitudeSquared > 1.0e-24
            ? 1.0 / std::sqrt(magnitudeSquared)
            : 1.0;
        cosine *= inverseMagnitude;
        sine *= inverseMagnitude;
    };
    normalise(oscillatorACosine_, oscillatorASine_);
    normalise(oscillatorBCosine_, oscillatorBSine_);
    samplesUntilNormalisation_ = normalisationInterval;
}
} // namespace amanita::dsp
