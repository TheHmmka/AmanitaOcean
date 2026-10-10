#pragma once

#include "BloomCharacter.h"
#include "CurrentField.h"
#include "DriftCharacter.h"
#include "FathomEngine.h"
#include "HarmonicAnalyzer.h"
#include "HarmonicTail.h"
#include "SpatialDucker.h"
#include "StereoField.h"
#include "VeilCharacter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amanita::dsp
{
enum class ReverbMode
{
    defaultMode = 0,
    bloom,
    drift,
    veil,
    current,
    fathom,
    undertow,
    spume
};

// What the host says about its transport at the first frame of a block.
// Undertow keeps time by it; the other Characters do not read it.
struct HostTransport
{
    double quarterNotes = 0.0;   // position of the block's first frame
    double bpm = 120.0;          // tempo; read only when hasTempo
    bool playing = false;        // false: the position does not advance and is not used
    bool hasTempo = false;       // false: the host gave no tempo
};

struct ReverbParameters
{
    ReverbMode mode = ReverbMode::defaultMode;
    float mix = 0.35f;
    float decaySeconds = 5.0f;
    float size = 1.0f;
    float preDelayMs = 20.0f;
    float lowCutHz = 80.0f;
    float highDampingHz = 9000.0f;
    float evolution = 0.35f;
    float width = 1.0f;
    float ducking = 0.0f;
    float harmony = 0.0f;
    HarmonicTail::PitchClassWeights harmonyPitchClasses {};
    float harmonyConfidence = 0.0f;
    bool autoHarmony = false;
    bool freeze = false;
    bool monoSafeStereo = false;
};

class FDNReverb
{
public:
    static constexpr std::size_t numDelayLines = StereoField::numDelayLines;
    // Keeps the shortest line above 4.34 ms, so Decay=30 s remains below the
    // normal 0.999 feedback ceiling at every supported sample rate.
    static constexpr float minimumSizeScale = 0.15f;
    static constexpr float maximumSizeScale = 2.0f;

    void prepare(double sampleRate, int maximumBlockSize);
    void reset() noexcept;

    void setParameters(const ReverbParameters& newParameters) noexcept;
    [[nodiscard]] const ReverbParameters& getParameters() const noexcept;

    // Seed of the voice phase of Fathom, in place of the engine's default one.
    // It takes effect at the next prepare() or reset().
    void setFathomVoiceSeed(std::uint64_t seed) noexcept;

    // Tempo and position of the host at the first frame of the next process()
    // call. Call it once in front of every block; without a call the reverb
    // behaves as under a stopped transport without a tempo.
    void setHostTransport(const HostTransport& transport) noexcept;

    void process(float* left, float* right, int numSamples) noexcept;
    void processSample(float& left, float& right) noexcept;

    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] const std::array<float, numDelayLines>& getNominalDelaySamples() const noexcept;
    [[nodiscard]] const HarmonicAnalysisFrame& getHarmonicAnalysisFrame() const noexcept;
    [[nodiscard]] const CurrentField::Frame& getCurrentFieldFrame() const noexcept;
    [[nodiscard]] float getCurrentFieldStrength() const noexcept;

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
    std::array<float, numDelayLines> dcGuardLowPassStates_ {};
    std::array<float, numDelayLines> lfoPhases_ {};
    std::array<float, numDelayLines> lfoIncrements_ {};
    std::array<LinearSmoother, numDelayLines> feedbackGains_;

    std::array<VariableDelay, 2> preDelayLines_;
    std::array<AllPass, 4> diffusersLeft_;
    std::array<AllPass, 4> diffusersRight_;
    BloomCharacter bloom_;
    CurrentField currentField_;
    DriftCharacter drift_;
    FathomEngine fathom_;
    FathomEngine::LevelStage fathomLevelStage_;
    StereoField fathomSubAnchor_;
    // Undertow: a second engine, which carries the Undertow layer, with outer
    // stages of its own.
    FathomEngine undertow_;
    FathomEngine::LevelStage undertowLevelStage_;
    StereoField undertowSubAnchor_;
    // Spume: a third engine, which carries the Spume layer, with outer stages
    // of its own.
    FathomEngine spume_;
    FathomEngine::LevelStage spumeLevelStage_;
    StereoField spumeSubAnchor_;
    HarmonicAnalyzer harmonicAnalyzer_;
    HarmonicTail harmonicTail_;
    SpatialDucker spatialDucker_;
    StereoField stereoField_;
    VeilCharacter veil_;

    LinearSmoother bloomAmount_;
    LinearSmoother currentAmount_;
    LinearSmoother driftAmount_;
    LinearSmoother fathomAmount_;
    LinearSmoother undertowAmount_;
    LinearSmoother spumeAmount_;
    // How much of the wet Fathom, Undertow and Spume hold together.
    LinearSmoother engineAmount_;
    LinearSmoother veilAmount_;
    LinearSmoother mix_;
    LinearSmoother size_;
    LinearSmoother preDelaySamples_;
    LinearSmoother lowCutCoefficient_;
    LinearSmoother dampingCoefficient_;
    LinearSmoother evolution_;
    LinearSmoother width_;
    LinearSmoother monoSafeStereoAmount_;
    LinearSmoother harmony_;
    LinearSmoother freeze_;
    float dcGuardCoefficient_ = 0.0f;
    float currentFieldStrength_ = 0.0f;
    bool fathomEngaged_ = false;
    bool undertowEngaged_ = false;
    bool spumeEngaged_ = false;
    HostTransport hostTransport_;
};
} // namespace amanita::dsp
