#include "FathomExactArithmetic.h"

#include "SpumeDiffuser.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
// A smaller number counts as zero, and so does what is no number: a line that
// only rings out reaches silence instead of the denormal range.
constexpr double signalFloor = 1.0e-30;

[[nodiscard]] double settled(double value) noexcept
{
    return std::isfinite(value) && std::abs(value) >= signalFloor ? value : 0.0;
}
} // namespace

void SpumeDiffuser::prepare()
{
    std::size_t frames = 0;
    for (std::size_t stage = 0; stage < static_cast<std::size_t>(spume::stageCount); ++stage)
    {
        for (std::size_t path = 0; path < static_cast<std::size_t>(spume::pathCount); ++path)
        {
            lineStart_[stage * spume::pathCount + path] = 2 * frames;
            frames += static_cast<std::size_t>(spume::delaySamples[stage][path]);
        }
    }
    memory_.assign(2 * frames, 0.0);
    position_ = {};
    clear();
}

void SpumeDiffuser::clear() noexcept
{
    held_ = 0;
}

void SpumeDiffuser::process(double left, double right, double& diffusedLeft,
                            double& diffusedRight) noexcept
{
    constexpr auto paths = static_cast<std::size_t>(spume::pathCount);
    constexpr auto stages = static_cast<std::size_t>(spume::stageCount);
    if (memory_.empty())
    {
        diffusedLeft = 0.0;
        diffusedRight = 0.0;
        return;
    }

    // Each input goes to all four paths.
    std::array<std::array<double, paths>, 2> signal;
    signal[0].fill(spume::pathInputGain * left);
    signal[1].fill(spume::pathInputGain * right);

    for (std::size_t stage = 0; stage < stages; ++stage)
    {
        const auto gain = spume::stageGain[stage];
        for (std::size_t path = 0; path < paths; ++path)
        {
            // y[n] = g x[n] + x[n - D] - g y[n - D] with one line: the line
            // holds w[n] = x[n] - g w[n - D], and y[n] = g w[n] + w[n - D].
            const auto allPass = stage * paths + path;
            const auto delay = spume::delaySamples[stage][path];
            auto* frame = memory_.data() + lineStart_[allPass]
                        + 2 * static_cast<std::size_t>(position_[allPass]);
            const auto filled = held_ >= delay;
            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                const auto delayed = filled ? frame[channel] : 0.0;
                const auto written = signal[channel][path] - gain * delayed;
                signal[channel][path] = gain * written + delayed;
                frame[channel] = settled(written);
            }
            if (++position_[allPass] == delay)
                position_[allPass] = 0;
        }

        if (stage + 1 == stages)
            break;
        // Between two stages: half the Sylvester Hadamard matrix of order 4.
        for (auto& four : signal)
        {
            const auto sum01 = four[0] + four[1];
            const auto difference01 = four[0] - four[1];
            const auto sum23 = four[2] + four[3];
            const auto difference23 = four[2] - four[3];
            four = { spume::mixGain * (sum01 + sum23), spume::mixGain * (difference01 + difference23),
                     spume::mixGain * (sum01 - sum23), spume::mixGain * (difference01 - difference23) };
        }
    }

    if (held_ < spume::longestDelaySamples)
        ++held_;
    diffusedLeft = (signal[0][0] + signal[0][1]) + (signal[0][2] + signal[0][3]);
    diffusedRight = (signal[1][0] + signal[1][1]) + (signal[1][2] + signal[1][3]);
}
} // namespace amanita::dsp
