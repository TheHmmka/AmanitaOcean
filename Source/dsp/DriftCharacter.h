#pragma once

#include <array>
#include <cstddef>

namespace amanita::dsp
{
class DriftCharacter
{
public:
    static constexpr std::size_t numFeedbackLines = 8;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void processFeedback(std::array<float, numFeedbackLines>& feedback,
                         float characterAmount,
                         float modulationAmount) noexcept;

private:
    class EndpointFilters
    {
    public:
        void reset() noexcept;
        [[nodiscard]] std::array<float, 2> process(float sample,
                                                   float darkCoefficient,
                                                   float brightCoefficient) noexcept;

    private:
        float darkState_ = 0.0f;
        float brightState_ = 0.0f;
    };

    void advancePhases() noexcept;
    [[nodiscard]] static float sanitise(float sample) noexcept;

    std::array<EndpointFilters, 4> filters_;
    std::array<float, 4> phases_ {};
    std::array<float, 4> phaseIncrements_ {};
    float darkCoefficient_ = 0.85f;
    float brightCoefficient_ = 0.2f;
};
} // namespace amanita::dsp
