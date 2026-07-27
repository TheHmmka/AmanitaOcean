#include "CharacterExcitationNormalizer.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
// Broadband covariance of the fixed Character kernels. These values are
// sample-rate invariant apart from negligible prime-delay rounding:
// Bloom = 0.62 * direct + AP4(rising-tap FIR), Veil = lossless AP6.
constexpr float bloomPower = 0.641060965328f;
constexpr float defaultBloomCorrelation = 0.625855617200f;
constexpr float defaultVeilCorrelation = 0.033004680800f;
constexpr float bloomVeilCorrelation = 0.021000000000f;
constexpr float minimumPredictedPower = 0.35f;
constexpr float minimumGain = 0.75f;

[[nodiscard]] float safeAmount(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
} // namespace

float CharacterExcitationNormalizer::predictedPower(float bloomAmount,
                                                     float veilAmount) noexcept
{
    const auto bloom = safeAmount(bloomAmount);
    const auto veil = safeAmount(veilAmount);
    const auto direct = 1.0f - bloom - veil;
    const auto power = direct * direct
                     + bloomPower * bloom * bloom
                     + veil * veil
                     + 2.0f * defaultBloomCorrelation * direct * bloom
                     + 2.0f * defaultVeilCorrelation * direct * veil
                     + 2.0f * bloomVeilCorrelation * bloom * veil;
    return std::isfinite(power)
        ? std::max(power, minimumPredictedPower)
        : 1.0f;
}

float CharacterExcitationNormalizer::gain(float bloomAmount,
                                           float veilAmount) noexcept
{
    const auto bloom = safeAmount(bloomAmount);
    const auto veil = safeAmount(veilAmount);
    const auto power = predictedPower(bloomAmount, veilAmount);
    const auto characterAmount = bloom + veil;
    const auto veilShare = characterAmount > 1.0e-6f
        ? veil / characterAmount
        : 0.0f;
    // Bloom's softened attacks need only half of the mathematical energy
    // correction. Veil's nearly orthogonal all-pass morph needs slightly more
    // at intermediate amounts, so interpolate up to a one-third-power law.
    // This is still static and signal-independent: no pumping or stereo drift.
    const auto perceptualExponent = 0.25f + veilShare / 12.0f;
    const auto normalisingGain = std::pow(power, -perceptualExponent);
    return std::clamp(normalisingGain, minimumGain, maximumGain);
}
} // namespace amanita::dsp
