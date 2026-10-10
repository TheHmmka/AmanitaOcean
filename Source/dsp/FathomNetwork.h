#pragma once

#include "FathomEngineConstants.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amanita::dsp
{
// Values that move in straight lines of one common length and land exactly on
// their targets.
template <std::size_t count>
class FathomGlide
{
public:
    using Values = std::array<double, count>;

    void setLength(int samples) noexcept { length_ = std::max(1, samples); }

    void jumpTo(const Values& values) noexcept
    {
        current_ = values;
        target_ = values;
        remaining_ = 0;
    }

    // A glide towards the targets it already has goes on undisturbed.
    void glideTo(const Values& values) noexcept
    {
        if (values == target_)
            return;

        target_ = values;
        remaining_ = length_;
        for (std::size_t index = 0; index < count; ++index)
            step_[index] = (target_[index] - current_[index]) / length_;
    }

    [[nodiscard]] bool moving() const noexcept { return remaining_ > 0; }

    // The value at `index` behind the next advance(), or where it stands
    // while nothing moves.
    [[nodiscard]] double next(std::size_t index) const noexcept
    {
        if (remaining_ <= 0)
            return current_[index];
        return remaining_ == 1 ? target_[index] : current_[index] + step_[index];
    }

    void advance() noexcept
    {
        if (--remaining_ == 0)
        {
            current_ = target_;
            return;
        }
        for (std::size_t index = 0; index < count; ++index)
            current_[index] += step_[index];
    }

    [[nodiscard]] const Values& values() const noexcept { return current_; }

private:
    Values current_ {};
    Values target_ {};
    Values step_ {};
    int length_ = 1;
    int remaining_ = 0;
};

// The phase of the two voices of each output, in cycles: a ramp whose rate
// follows Macro plus, per output, a level that wanders between random targets.
// The level holds its start value up to its first knot and then moves from
// knot to knot along a raised cosine; knot j lies at a random place of cell j
// of the output's grid, counted from the first sample. Every random number is
// a function of the seed and the knot alone, the same on every platform.
class FathomVoicePhase
{
public:
    static constexpr std::size_t outputCount = static_cast<std::size_t>(fathom::groupCount);

    struct Knot
    {
        double seconds;
        double level;
    };

    // Rate of the ramp in cycles per second.
    [[nodiscard]] static double rate(double macro) noexcept;

    // A new instance of the random level, at block 0 with no ramp.
    void reset(std::uint64_t seed) noexcept;
    // On to the next block of the voices; the ramp rises at the rate of `macro`.
    void advance(double macro) noexcept;
    // Phase of the first voice of an output at the current block.
    [[nodiscard]] double phase(std::size_t output) const noexcept;

    // The level of an output at a time since the first sample, and the knots
    // it moves between: knot 0 is the start, knot j >= 1 lies in cell j.
    [[nodiscard]] double level(std::size_t output, double seconds) const noexcept;
    [[nodiscard]] Knot knot(std::size_t output, std::int64_t index) const noexcept;

private:
    std::array<std::uint64_t, outputCount> key_ {};
    std::array<double, outputCount> startLevel_ {};
    std::uint64_t block_ = 0;
    double ramp_ = 0.0;
};

// The 44.1 kHz core of the Fathom engine: input equaliser, the Tide comb of each
// input, two groups of sixteen modulated lines closed through one mixing
// matrix and loop filter, and the two voices of each output. Sample k of its
// clocks is the k-th call of process() or idle() after reset().
class FathomNetwork
{
public:
    static constexpr std::size_t groupCount = static_cast<std::size_t>(fathom::groupCount);
    static constexpr std::size_t lineCount = static_cast<std::size_t>(fathom::lineCount);

    struct Settings
    {
        float decaySeconds = 4.0f;
        float sizeScale = 1.0f;
        float macro = 0.0f;
        float lowCutHz = 20.0f;
        float highDampingHz = 20000.0f;
        bool freeze = false;
    };

    // Ocean's loop filters leave the circuit at these positions.
    static constexpr float neutralLowCutHz = 20.0f;
    static constexpr float neutralHighDampingHz = 20000.0f;

    void prepare(std::uint64_t voiceSeed);
    // Clears every signal, returns the clocks to sample 0, starts a new
    // instance of the voice phase and takes the settings as they are.
    void reset(std::uint64_t voiceSeed) noexcept;
    // Settings glide to their new values while the network sounds; a silent
    // network takes them at once.
    void setSettings(const Settings& settings) noexcept;
    // Phase of the first voice of the left and of the right output at the
    // start of each block, in place of the network's own; see
    // FathomEngine::setVoicePhaseForTesting.
    void prescribeVoicePhase(const double* left, const double* right, std::size_t blockCount) noexcept;

    void process(double left, double right, double& outputLeft, double& outputRight) noexcept;
    // The share of their input the lines take in the sample process() computes
    // next: one, none while the loop is held, and between the two the hold's
    // glide of 50 ms. The comb of the Tide layer takes its input by it, and so
    // does a layer in front of the network, so that what is played under the
    // hold is nowhere when the hold ends.
    [[nodiscard]] double nextInputShare() const noexcept;
    // Clears every signal and takes the settings as they are; the clocks keep
    // their place.
    void silence() noexcept;
    // One sample of time with no input and no output: the clocks advance, and
    // a network that still sounds falls silent first.
    void idle() noexcept;

private:
    static constexpr std::size_t allLines = groupCount * lineCount;
    // Power of two above the longest read: 2 * 7589 samples, the modulation
    // depth and the second sample of the interpolation.
    static constexpr std::size_t lineCapacity = 16384;

    // Power of two above the longest delay of a comb and the two samples of
    // its read.
    static constexpr std::size_t combCapacity = 1024;
    static_assert(fathom::combDelayMidSamples + fathom::combDelaySwingSamples + 2.0f
                  < combCapacity);

    // The comb of one input: a delay read through a first-order all-pass and
    // fed back, inverted, through a high-pass and a low-pass.
    struct Comb
    {
        // The delayed path for one input sample; `slot` is where the line
        // takes this sample.
        [[nodiscard]] double process(double sample, float delaySamples, std::size_t slot) noexcept;
        void clear() noexcept;

        std::array<double, combCapacity> line {};
        double read = 0.0;
        double highPassInput = 0.0;
        double highPass = 0.0;
        double lowPassInput = 0.0;
        double lowPass = 0.0;
    };

    // A voice: a two-pole low-pass in trapezoidal state-variable form with a
    // gain behind it. At rest its cut-off and Q are fixed and its gain is 1.
    struct Voice
    {
        void setCutoff(double cutoffHz, double q) noexcept;
        [[nodiscard]] double process(double sample) noexcept;

        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 0.0;
        double state1 = 0.0;
        double state2 = 0.0;
        // Q as its smoothing has it, and the gain of the voice at full depth.
        double quality = fathom::voiceRestQ;
        double window = 1.0;
    };

    // Indices of the loop-shaping glide.
    enum Shaping : std::size_t
    {
        hold,
        lowCutAmount,
        lowCutCoefficient,
        dampingAmount,
        dampingCoefficient,
        shapingCount
    };

    using LineArray = std::array<double, lineCount>;

    void applySettings(bool atOnce) noexcept;
    void updateLengths() noexcept;
    void updateMacro() noexcept;
    void advanceGlides() noexcept;
    void updateVoices() noexcept;
    void restVoices() noexcept;
    void advanceOscillators(std::size_t group, std::array<float, lineCount>& phases) noexcept;
    void readLines(std::size_t group, LineArray& reads) noexcept;
    void filterLoop(std::size_t group, LineArray& signal) noexcept;
    void shapeLoop(std::size_t group, const LineArray& mixed, LineArray& signal) noexcept;
    [[nodiscard]] double equalise(std::size_t channel, double sample) noexcept;

    Settings settings_;
    bool prepared_ = false;
    bool silent_ = true;
    bool shaped_ = false;

    // Decay and Size: the length of each line, the gain of one pass through it
    // and the output tap weight of each line of a group.
    FathomGlide<allLines> length_;
    FathomGlide<allLines> attenuation_;
    FathomGlide<lineCount> tapWeight_;
    // Freeze and Ocean's loop filters.
    FathomGlide<shapingCount> shaping_;

    // Macro, and what follows it sample by sample: whether the Tide layer is
    // in the circuit, the shares of the plain and of the combed input, and
    // the depth of the voices' gain and Q.
    FathomGlide<1> macro_;
    bool tide_ = false;
    double plainShare_ = 1.0;
    double combShare_ = 0.0;
    double voiceDepth_ = 0.0;

    // Line lengths and oscillator accumulators, single precision as in the
    // reference. A length is whole at rest and passes through the fractions in
    // between while Size glides.
    std::array<float, allLines> lineLength_ {};
    std::array<float, allLines> oscillatorPhase_ {};

    // Line memory, one ring per line. `written_` counts the samples a ring has
    // held since it was last cleared, so clearing costs nothing.
    std::vector<float> lines_;
    std::uint64_t sampleCount_ = 0;
    int written_ = 0;

    std::array<std::array<std::array<double, 2>, fathom::equaliser.size()>, groupCount>
        equaliserState_ {};
    std::array<std::array<double, fathom::lineWriteDelaySamples>, groupCount> lineInput_ {};
    std::size_t lineInputIndex_ = 0;

    // Loop filter: [group][section][state][line].
    std::array<std::array<std::array<LineArray, 2>, fathom::loopKernel.size()>, groupCount>
        kernelState_ {};
    std::array<LineArray, groupCount> lowCutState_ {};
    std::array<LineArray, groupCount> dampingState_ {};

    std::array<Comb, groupCount> combs_ {};
    std::array<std::array<Voice, 2>, groupCount> voices_ {};
    bool voicesAtRest_ = true;
    FathomVoicePhase voicePhase_;
    std::array<const double*, groupCount> prescribedPhase_ {};
    std::size_t prescribedBlocks_ = 0;
};
} // namespace amanita::dsp
