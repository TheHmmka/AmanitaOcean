#pragma once

#include <cstddef>

namespace amanita::dsp
{
class LateralDecay final
{
public:
    static constexpr std::size_t numDelayLines = 8;
    static constexpr float maximumLateralExtension = 0.14f;
    static constexpr float maximumFeedbackGain = 0.999f;

    [[nodiscard]] static bool isLateralLine(std::size_t lineIndex) noexcept;
    [[nodiscard]] static float decayTimeScale(std::size_t lineIndex,
                                              float width,
                                              float evolution) noexcept;
    [[nodiscard]] static float feedbackGain(float delaySeconds,
                                            float decaySeconds,
                                            std::size_t lineIndex,
                                            float width,
                                            float evolution) noexcept;
};
} // namespace amanita::dsp
