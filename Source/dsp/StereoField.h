#pragma once

#include <array>
#include <cstddef>

namespace amanita::dsp
{
class StereoField
{
public:
    static constexpr std::size_t numDelayLines = 8;

    struct Frame
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    [[nodiscard]] static Frame decode(
        const std::array<float, numDelayLines>& delayOutputs) noexcept;
    [[nodiscard]] static Frame decodeLegacy(
        const std::array<float, numDelayLines>& delayOutputs) noexcept;
    [[nodiscard]] static Frame decodeCurrentLegacy(
        const std::array<float, numDelayLines>& delayOutputs,
        const std::array<float, numDelayLines>& fieldPosition,
        float depth) noexcept;
    [[nodiscard]] static Frame decodeCurrentMonoSafe(
        const std::array<float, numDelayLines>& delayOutputs,
        const std::array<float, numDelayLines>& fieldPosition,
        float depth) noexcept;
    [[nodiscard]] Frame applyWidth(float left, float right, float width) noexcept;
    [[nodiscard]] static Frame applyLegacyWidth(float left, float right,
                                                float width) noexcept;

private:
    float subAnchorLowSide_ = 0.0f;
    float subAnchorCoefficient_ = 0.0f;
};
} // namespace amanita::dsp
