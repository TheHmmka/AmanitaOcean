#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace amanita::dsp
{
class VeilCharacter
{
public:
    struct StereoFrame
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    void prepare(double sampleRate);
    void reset() noexcept;

    [[nodiscard]] StereoFrame processExcitation(float left, float right) noexcept;

private:
    static constexpr std::size_t numStages = 6;

    class FixedAllPass
    {
    public:
        void prepare(std::size_t delaySamples, float coefficient);
        void reset() noexcept;
        [[nodiscard]] float process(float sample) noexcept;

    private:
        std::vector<float> buffer_;
        std::size_t index_ = 0;
        float coefficient_ = 0.5f;
    };

    [[nodiscard]] static float sanitise(float sample) noexcept;
    [[nodiscard]] static float processDiffuser(
        float sample,
        std::array<FixedAllPass, numStages>& stages) noexcept;

    std::array<FixedAllPass, numStages> diffusersLeft_;
    std::array<FixedAllPass, numStages> diffusersRight_;
};
} // namespace amanita::dsp
