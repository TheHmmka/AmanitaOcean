#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace amanita::dsp
{
class CurrentField final
{
public:
    static constexpr std::size_t numLines = 8;
    static constexpr float maximumDelaySeconds = 0.00022f;
    static constexpr float dampingPoleWarp = 0.18f;

    struct Frame
    {
        std::array<float, numLines> delay {};
        std::array<float, numLines> damping {};
        std::array<float, numLines> stereo {};
        float flowX = 0.0f;
        float flowY = 0.0f;
    };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // The phase always advances so entering Current never restarts the flow.
    // Inactive calls skip all per-line field projection work.
    [[nodiscard]] const Frame& next(bool active) noexcept;
    [[nodiscard]] const Frame& getLastFrame() const noexcept;

private:
    void advancePhases() noexcept;

    double oscillatorACosine_ = 1.0;
    double oscillatorASine_ = 0.0;
    double oscillatorBCosine_ = 1.0;
    double oscillatorBSine_ = 0.0;
    double incrementACosine_ = 1.0;
    double incrementASine_ = 0.0;
    double incrementBCosine_ = 1.0;
    double incrementBSine_ = 0.0;
    std::uint32_t samplesUntilNormalisation_ = 65536;
    Frame lastFrame_;
    bool activeLastSample_ = false;
};
} // namespace amanita::dsp
