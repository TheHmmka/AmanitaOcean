#pragma once

#include "BloomCharacter.h"
#include "DriftCharacter.h"
#include "Drift2Character.h"
#include "VeilCharacter.h"

#include <array>
#include <cstddef>
#include <vector>

namespace amanita::dsp
{
enum class ReverbMode
{
    defaultMode = 0,
    bloom,
    drift,
    veil
};

enum class DriftModel
{
    original = 0,
    drift2
};

struct ReverbParameters
{
    ReverbMode mode = ReverbMode::defaultMode;
    DriftModel driftModel = DriftModel::original;
    float mix = 0.35f;
    float decaySeconds = 5.0f;
    float size = 1.0f;
    float preDelayMs = 20.0f;
    float lowCutHz = 80.0f;
    float highDampingHz = 9000.0f;
    float modulation = 0.2f;
    float width = 1.0f;
    bool freeze = false;
};

class FDNReverb
{
public:
    static constexpr std::size_t numDelayLines = 8;

    void prepare(double sampleRate, int maximumBlockSize);
    void reset() noexcept;

    void setParameters(const ReverbParameters& newParameters) noexcept;
    [[nodiscard]] const ReverbParameters& getParameters() const noexcept;

    void process(float* left, float* right, int numSamples) noexcept;
    void processSample(float& left, float& right) noexcept;

    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] const std::array<float, numDelayLines>& getNominalDelaySamples() const noexcept;

    static void applyFeedbackMatrix(std::array<float, numDelayLines>& values) noexcept;

private:
    class LinearSmoother
    {
    public:
        void prepare(double sampleRate, double rampSeconds, float initialValue) noexcept;
        void setTarget(float newTarget) noexcept;
        [[nodiscard]] float next() noexcept;

    private:
        float current_ = 0.0f;
        float target_ = 0.0f;
        float step_ = 0.0f;
        int remaining_ = 0;
        int rampSamples_ = 1;
    };

    class DelayLine
    {
    public:
        void prepare(std::size_t capacity);
        void reset() noexcept;
        [[nodiscard]] float read(float delaySamples) const noexcept;
        void write(float sample) noexcept;

    private:
        std::vector<float> buffer_;
        std::size_t writeIndex_ = 0;
    };

    class VariableDelay
    {
    public:
        void prepare(std::size_t capacity);
        void reset() noexcept;
        [[nodiscard]] float process(float sample, float delaySamples) noexcept;

    private:
        std::vector<float> buffer_;
        std::size_t writeIndex_ = 0;
    };

    class AllPass
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

    void updateTargets() noexcept;
    [[nodiscard]] float diffuseInput(float sample, std::array<AllPass, 4>& stages) noexcept;
    [[nodiscard]] static float sanitise(float sample, float limit = 8.0f) noexcept;
    [[nodiscard]] static float flushDenormal(float sample) noexcept;

    ReverbParameters parameters_;
    double sampleRate_ = 48000.0;
    bool prepared_ = false;

    std::array<DelayLine, numDelayLines> delayLines_;
    std::array<float, numDelayLines> nominalDelaySamples_ {};
    std::array<float, numDelayLines> lowCutStates_ {};
    std::array<float, numDelayLines> dampingStates_ {};
    std::array<float, numDelayLines> lfoPhases_ {};
    std::array<float, numDelayLines> lfoIncrements_ {};
    std::array<LinearSmoother, numDelayLines> feedbackGains_;

    std::array<VariableDelay, 2> preDelayLines_;
    std::array<AllPass, 4> diffusersLeft_;
    std::array<AllPass, 4> diffusersRight_;
    BloomCharacter bloom_;
    DriftCharacter drift_;
    Drift2Character drift2_;
    VeilCharacter veil_;

    LinearSmoother bloomAmount_;
    LinearSmoother driftAmount_;
    LinearSmoother drift2Amount_;
    LinearSmoother veilAmount_;
    LinearSmoother mix_;
    LinearSmoother size_;
    LinearSmoother preDelaySamples_;
    LinearSmoother lowCutCoefficient_;
    LinearSmoother dampingCoefficient_;
    LinearSmoother modulation_;
    LinearSmoother width_;
    LinearSmoother freeze_;
};
} // namespace amanita::dsp
