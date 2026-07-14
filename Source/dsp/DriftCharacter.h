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
                         float evolutionAmount,
                         bool applyOutput = true) noexcept;

private:
    class AirKernel
    {
    public:
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;
        void processFeedback(std::array<float, numFeedbackLines>& feedback,
                             float characterAmount,
                             float evolutionAmount) noexcept;

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

        std::array<EndpointFilters, 4> filters_;
        std::array<float, 4> phases_ {};
        std::array<float, 4> phaseIncrements_ {};
        float darkCoefficient_ = 0.85f;
        float brightCoefficient_ = 0.2f;
    };

    class BodyPresenceKernel
    {
    public:
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;
        void processFeedback(std::array<float, numFeedbackLines>& feedback,
                             float characterAmount,
                             float evolutionAmount) noexcept;

    private:
        class Biquad
        {
        public:
            void setBandPass(float sampleRate, float frequencyHz, float q) noexcept;
            void reset() noexcept;
            [[nodiscard]] float process(float sample) noexcept;

        private:
            void setCoefficients(float b0, float b1, float b2,
                                 float a0, float a1, float a2) noexcept;

            float b0_ = 1.0f;
            float b1_ = 0.0f;
            float b2_ = 0.0f;
            float a1_ = 0.0f;
            float a2_ = 0.0f;
            float state1_ = 0.0f;
            float state2_ = 0.0f;
        };

        class AxisFilters
        {
        public:
            void prepare(float sampleRate) noexcept;
            void reset() noexcept;
            [[nodiscard]] std::array<float, 2> process(float sample) noexcept;

        private:
            Biquad bodyBand_;
            Biquad presenceBand_;
        };

        void advancePhases() noexcept;

        std::array<AxisFilters, 4> filters_;
        std::array<float, 4> phases_ {};
        std::array<float, 4> phaseIncrements_ {};
    };

    AirKernel airKernel_;
    BodyPresenceKernel bodyPresenceKernel_;
};
} // namespace amanita::dsp
