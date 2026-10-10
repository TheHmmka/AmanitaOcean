#include "FathomExactArithmetic.h"

#include "SpumeLayer.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// A gain one block on: of what separates it from its target a fixed share
// stays. Ocean's own: within a floor of the target the gain is on it, so that
// a gain at rest is its target to the bit.
[[nodiscard]] double moved(double gain, double target) noexcept
{
    const auto distance = gain - target;
    return std::abs(distance) < spume::gainFloor ? target
                                                 : target + distance * spume::gainBlockRetention;
}
} // namespace

void SpumeLayer::prepare()
{
    diffuser_.prepare();
    prepared_ = true;
    reset();
}

void SpumeLayer::reset() noexcept
{
    if (!prepared_)
        return;

    now_ = firstSample_;
    silence();
}

void SpumeLayer::silence() noexcept
{
    diffuser_.clear();
    plainGain_ = plainTarget_;
    diffusedGain_ = diffusedTarget_;
    silent_ = true;
}

void SpumeLayer::setMacro(double macro, bool atOnce) noexcept
{
    const auto position = std::isfinite(macro) ? std::clamp(macro, 0.0, 1.0) : 0.0;
    plainTarget_ = std::cos(0.5 * pi * position);
    diffusedTarget_ = std::sin(0.5 * pi * position);
    if (atOnce || silent_)
    {
        plainGain_ = plainTarget_;
        diffusedGain_ = diffusedTarget_;
    }
}

void SpumeLayer::setFirstSample(std::int64_t sample) noexcept
{
    firstSample_ = std::max<std::int64_t>(0, sample);
}

void SpumeLayer::moveGains() noexcept
{
    plainGain_ = moved(plainGain_, plainTarget_);
    diffusedGain_ = moved(diffusedGain_, diffusedTarget_);
}

void SpumeLayer::process(double& left, double& right, double inputShare) noexcept
{
    if (!prepared_)
        return;

    silent_ = false;
    if (now_ % spume::gainBlockSamples == spume::gainBlockPhase)
        moveGains();
    ++now_;

    auto diffusedLeft = 0.0;
    auto diffusedRight = 0.0;
    // Ocean's own: what is played under the engine's hold does not enter the
    // diffuser. What it took before runs out as it would.
    diffuser_.process(inputShare * left, inputShare * right, diffusedLeft, diffusedRight);
    if (atRest())
        return;
    left = plainGain_ * left + diffusedGain_ * diffusedLeft;
    right = plainGain_ * right + diffusedGain_ * diffusedRight;
}

void SpumeLayer::idle() noexcept
{
    if (!prepared_)
        return;

    if (!silent_)
        silence();
    ++now_;
}

bool SpumeLayer::atRest() const noexcept
{
    return !(plainGain_ < 1.0) && !(plainGain_ > 1.0) && !(diffusedGain_ > 0.0)
        && !(diffusedGain_ < 0.0);
}
} // namespace amanita::dsp
