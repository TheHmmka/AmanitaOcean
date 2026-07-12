#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace amanita::dsp
{
class BloomCharacter
{
public:
    static constexpr std::size_t numFeedbackLines = 8;
    static constexpr float maximumDriftSeconds = 0.00033f;

    struct StereoFrame
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    void prepare(double sampleRate);
    void reset() noexcept;

    [[nodiscard]] StereoFrame processExcitation(float left, float right) noexcept;
    [[nodiscard]] float nextDriftSamples(std::size_t lineIndex,
                                         float modulationAmount,
                                         bool renderOffset) noexcept;

private:
    class FixedAllPass
    {
    public:
        void prepare(std::size_t delaySamples, float coefficient);
        void reset() noexcept;
        [[nodiscard]] float process(float sample) noexcept;

    private:
        std::vector<float> buffer_;
        std::size_t index_ = 0;
        float coefficient_ = 0.47f;
    };

    class RisingTapFIR
    {
    public:
        void prepare(double sampleRate, bool rightChannel);
        void reset() noexcept;
        [[nodiscard]] float process(float sample) noexcept;

    private:
        static constexpr std::size_t numTaps = 4;

        std::vector<float> buffer_;
        std::array<std::size_t, numTaps> tapSamples_ {};
        std::size_t writeIndex_ = 0;
    };

    [[nodiscard]] static float sanitise(float sample) noexcept;
    [[nodiscard]] static float processDiffuser(float sample,
                                               std::array<FixedAllPass, 4>& stages) noexcept;

    RisingTapFIR swellLeft_;
    RisingTapFIR swellRight_;
    std::array<FixedAllPass, 4> diffusersLeft_;
    std::array<FixedAllPass, 4> diffusersRight_;
    std::array<float, numFeedbackLines> driftPhases_ {};
    std::array<float, numFeedbackLines> driftIncrements_ {};
    float sampleRate_ = 48000.0f;
};
} // namespace amanita::dsp
