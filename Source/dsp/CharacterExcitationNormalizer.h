#pragma once

namespace amanita::dsp
{
class CharacterExcitationNormalizer final
{
public:
    static constexpr float maximumGain = 1.25f;

    [[nodiscard]] static float predictedPower(float bloomAmount,
                                              float veilAmount) noexcept;
    [[nodiscard]] static float gain(float bloomAmount,
                                    float veilAmount) noexcept;
};
} // namespace amanita::dsp
