#include "FathomExactArithmetic.h"

#include "FathomEngine.h"
#include "FathomConverter.h"
#include "FathomEngineConstants.h"
#include "FathomNetwork.h"
#include "SpumeLayer.h"
#include "UndertowConstants.h"
#include "UndertowLayer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace amanita::dsp
{
namespace
{
constexpr int minimumHostRate = 22050;
constexpr int maximumHostRate = 384000;
constexpr int fallbackHostRate = 48000;

// Ocean's Decay and Size reach below the reference's ranges; its Low Cut and
// High Damping have no counterpart there.
constexpr float minimumDecaySeconds = 0.2f;
constexpr float minimumSizeScale = 0.15f;
constexpr float maximumLowCutHz = 1000.0f;
constexpr float minimumHighDampingHz = 1000.0f;

constexpr double preDelayFadeSeconds = 0.05;
// Bound of an input sample, 36 dB above full scale.
constexpr float inputLimit = 64.0f;
// A smaller number counts as zero, long before it would turn denormal.
constexpr float silenceFloor = 1.0e-30f;
// Below this input peak the level stage's curve is zero whatever the knee
// reads in decibels, so the logarithm is not taken.
constexpr float levelStageQuietPeak = 0.5f;

[[nodiscard]] float clampFinite(float value, float minimum, float maximum, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

[[nodiscard]] bool sameBits(float first, float second) noexcept
{
    return std::bit_cast<std::uint32_t>(first) == std::bit_cast<std::uint32_t>(second);
}

[[nodiscard]] bool sameParameters(const FathomEngine::Parameters& first,
                                  const FathomEngine::Parameters& second) noexcept
{
    return sameBits(first.decaySeconds, second.decaySeconds)
        && sameBits(first.sizeScale, second.sizeScale)
        && sameBits(first.preDelaySeconds, second.preDelaySeconds)
        && sameBits(first.macro, second.macro)
        && sameBits(first.lowCutHz, second.lowCutHz)
        && sameBits(first.highDampingHz, second.highDampingHz)
        && first.freeze == second.freeze;
}

// The host rate as a whole number of hertz inside the range the engine supports.
[[nodiscard]] int hostRateOf(double sampleRate) noexcept
{
    if (!std::isfinite(sampleRate))
        return fallbackHostRate;
    return static_cast<int>(std::lround(std::clamp(
        sampleRate, static_cast<double>(minimumHostRate), static_cast<double>(maximumHostRate))));
}

[[nodiscard]] float sanitiseInput(float sample) noexcept
{
    return std::isfinite(sample) ? std::clamp(sample, -inputLimit, inputLimit) : 0.0f;
}

[[nodiscard]] float outputSample(double sample) noexcept
{
    const auto single = static_cast<float>(sample);
    return std::isfinite(single) && std::abs(single) >= silenceFloor ? single : 0.0f;
}

// The reference's pre-delay in whole host frames: one frame less than the time
// holds, never fewer than none. The time in milliseconds, its product with the
// rate and the quotient are single precision.
[[nodiscard]] int preDelayFrames(float seconds, int hostRate) noexcept
{
    const auto milliseconds = static_cast<float>(1000.0 * static_cast<double>(seconds));
    const auto product = milliseconds * static_cast<float>(hostRate);
    return std::max(0, static_cast<int>(std::floor(product / 1000.0f)) - 1);
}

// A delay of whole host frames in front of the input converter. A new delay
// fades in over a second tap while the first fades out.
class PreDelay
{
public:
    void prepare(int maximumFrames, int fadeFrames)
    {
        capacity_ = maximumFrames + 1;
        fadeFrames_ = std::max(1, fadeFrames);
        line_.assign(2 * static_cast<std::size_t>(capacity_), 0.0f);
        write_ = 0;
        target_ = 0;
        clear();
    }

    // Forgets the signal. The line is not touched: `held_` counts the frames
    // that may be read.
    void clear() noexcept
    {
        held_ = 0;
        fadePosition_ = 0;
        current_ = target_;
    }

    void setFrames(int frames, bool atOnce) noexcept
    {
        target_ = std::clamp(frames, 0, capacity_ - 1);
        if (atOnce)
        {
            current_ = target_;
            fadePosition_ = 0;
        }
    }

    [[nodiscard]] FathomEngine::Frame process(float left, float right) noexcept
    {
        line_[2 * static_cast<std::size_t>(write_)] = left;
        line_[2 * static_cast<std::size_t>(write_) + 1] = right;
        held_ = std::min(held_ + 1, capacity_);

        auto output = tap(current_);
        if (fadePosition_ == 0 && target_ != current_)
        {
            next_ = target_;
            fadePosition_ = 1;
        }
        if (fadePosition_ > 0)
        {
            const auto incoming = tap(next_);
            const auto share = static_cast<float>(fadePosition_) / static_cast<float>(fadeFrames_);
            output.left += share * (incoming.left - output.left);
            output.right += share * (incoming.right - output.right);
            if (++fadePosition_ > fadeFrames_)
            {
                current_ = next_;
                fadePosition_ = 0;
            }
        }

        if (++write_ == capacity_)
            write_ = 0;
        return output;
    }

private:
    [[nodiscard]] FathomEngine::Frame tap(int frames) const noexcept
    {
        if (frames >= held_)
            return {};
        auto index = write_ - frames;
        if (index < 0)
            index += capacity_;
        return { line_[2 * static_cast<std::size_t>(index)],
                 line_[2 * static_cast<std::size_t>(index) + 1] };
    }

    std::vector<float> line_;
    int capacity_ = 1;
    int write_ = 0;
    int held_ = 0;
    int current_ = 0;
    int next_ = 0;
    int target_ = 0;
    int fadeFrames_ = 1;
    int fadePosition_ = 0;
};
} // namespace

struct FathomEngine::State
{
    void applyParameters() noexcept
    {
        FathomNetwork::Settings settings;
        settings.decaySeconds = parameters.decaySeconds;
        settings.sizeScale = parameters.sizeScale;
        // Under the Undertow and the Spume layer the network's own Macro
        // rests: Tide stays out of the circuit and Macro moves the layer.
        settings.macro = undertowLayer || spumeLayer ? 0.0f : parameters.macro;
        settings.lowCutHz = parameters.lowCutHz;
        settings.highDampingHz = parameters.highDampingHz;
        settings.freeze = parameters.freeze;
        network.setSettings(settings);
        if (undertowLayer)
            undertow.setMacro(static_cast<double>(parameters.macro), silent);
        if (spumeLayer)
            spume.setMacro(static_cast<double>(parameters.macro), silent);
        preDelay.setFrames(preDelayFrames(parameters.preDelaySeconds, hostRate), silent);
    }

    // The networks fall silent; every clock keeps its place.
    void silence() noexcept
    {
        preDelay.clear();
        toInternal.clear();
        toHost.clear();
        network.silence();
        if (undertowLayer)
            undertow.silence();
        if (spumeLayer)
            spume.silence();
        silent = true;
    }

    // One internal sample of the core: under the Undertow layer the input
    // takes the layer's voices first, under the Spume layer it is crossfaded
    // with its diffused copy. A layer at rest adds nothing, and the input
    // passes as it is.
    //
    // Ocean's own: held, the network takes no input, and neither does the
    // layer in front of it. The layer's input goes by the very share the
    // network gives what it writes into its lines in this sample, through the
    // same glide, so nothing that is played under Freeze waits in a layer to
    // come back when Freeze ends.
    void processCore(double left, double right, double& wetLeft, double& wetRight) noexcept
    {
        if (undertowLayer)
        {
            auto addedLeft = 0.0;
            auto addedRight = 0.0;
            undertow.process(left, right, network.nextInputShare(), addedLeft, addedRight);
            if (!undertow.atRest())
            {
                left += addedLeft;
                right += addedRight;
            }
        }
        else if (spumeLayer)
        {
            spume.process(left, right, network.nextInputShare());
        }
        network.process(left, right, wetLeft, wetRight);
        ++coreSamples;
    }

    void idleCore() noexcept
    {
        if (undertowLayer)
            undertow.idle();
        else if (spumeLayer)
            spume.idle();
        network.idle();
        ++coreSamples;
    }

    Parameters parameters;
    std::uint64_t voiceSeed = defaultVoiceSeed;
    Layer layer = Layer::tide;
    ClockOrigins origins;
    bool undertowLayer = false;
    bool spumeLayer = false;
    // Internal samples in front of the first frame (a test hook), and those
    // the core has computed or passed since reset().
    std::int64_t firstCoreSample = 0;
    std::int64_t coreSamples = 0;
    bool prepared = false;
    bool silent = true;

    int hostRate = fallbackHostRate;
    FathomRateLattice lattice;
    PreDelay preDelay;
    FathomConverter toInternal;
    FathomConverter toHost;
    FathomNetwork network;
    UndertowLayer undertow;
    SpumeLayer spume;
};

FathomEngine::FathomEngine() : state_(std::make_unique<State>()) {}
FathomEngine::~FathomEngine() = default;

void FathomEngine::prepare(double sampleRate)
{
    auto& state = *state_;
    state.hostRate = hostRateOf(sampleRate);
    state.lattice = FathomRateLattice::at(state.hostRate);
    const auto& lattice = state.lattice;

    state.preDelay.prepare(
        preDelayFrames(fathom::referenceMaximumPreDelaySeconds, state.hostRate),
        static_cast<int>(std::lround(preDelayFadeSeconds * state.hostRate)));

    if (lattice.converts())
    {
        // The engine returns the reference's wet output a reported latency
        // early, so its output converter reads that much later in the core.
        const auto engineOutputDelay = lattice.outputDelay
                                     - std::int64_t { lattice.latencyFrames } * lattice.hostStep;
        state.toHost.prepare(lattice.internalStep, lattice.hostStep, engineOutputDelay,
                             lattice.outputClockSign, 0);
        // An internal sample is computed when the output first needs it. That
        // is later than its own taps arrive, by fewer host frames than this.
        const auto slack = (lattice.inputDelay + engineOutputDelay
                            - std::int64_t { lattice.internalStep } * state.toHost.wingTaps())
                         / lattice.hostStep + 3;
        state.toInternal.prepare(lattice.hostStep, lattice.internalStep, lattice.inputDelay,
                                 lattice.inputClockSign, static_cast<int>(slack));
    }

    state.undertowLayer = state.layer == Layer::undertow;
    state.spumeLayer = state.layer == Layer::spume;
    if (state.undertowLayer)
        state.undertow.prepare(lattice, state.hostRate);
    if (state.spumeLayer)
        state.spume.prepare();

    state.network.prepare(state.voiceSeed);
    state.prepared = true;
    reset();
}

void FathomEngine::reset() noexcept
{
    auto& state = *state_;
    if (!state.prepared)
        return;

    state.silence();
    state.toInternal.reset();
    state.toHost.reset();
    state.network.reset(state.voiceSeed);
    state.firstCoreSample = 0;
    state.coreSamples = 0;
    if (state.undertowLayer || state.spumeLayer)
    {
        if (state.undertowLayer)
        {
            state.undertow.reset();
            state.firstCoreSample = state.undertow.firstSample();
        }
        else
        {
            // Internal samples count from the same moment as host frames, so
            // the frames in front of the first are whole lattice periods.
            state.firstCoreSample = std::max<std::int64_t>(0, state.origins.firstFrame)
                                  / state.lattice.internalStep * state.lattice.hostStep;
            state.spume.setFirstSample(state.firstCoreSample);
            state.spume.reset();
        }
        // Test hook: the oscillators of the network were at their start phases
        // some samples in front of the first frame. The network passes that
        // time; a long one is shortened by whole periods of the accumulators.
        auto steps = std::max<std::int64_t>(0, state.firstCoreSample - state.origins.oscillators);
        if (steps > undertow::oscillatorSettleSteps + undertow::oscillatorPeriodSteps)
            steps = undertow::oscillatorSettleSteps
                  + (steps - undertow::oscillatorSettleSteps) % undertow::oscillatorPeriodSteps;
        for (std::int64_t step = 0; step < steps; ++step)
            state.network.idle();
    }
    state.applyParameters();
}

void FathomEngine::setVoiceSeed(std::uint64_t seed) noexcept
{
    state_->voiceSeed = seed;
}

void FathomEngine::setVoicePhaseForTesting(const double* left, const double* right,
                                           std::size_t blockCount) noexcept
{
    state_->network.prescribeVoicePhase(left, right, blockCount);
}

void FathomEngine::setLayer(Layer layer) noexcept
{
    state_->layer = layer;
}

void FathomEngine::setTransport(const Transport& transport) noexcept
{
    auto& state = *state_;
    if (!state.prepared || !state.undertowLayer)
        return;

    UndertowLayer::Transport forwarded;
    forwarded.quarterNotes = transport.quarterNotes;
    forwarded.bpm = transport.bpm;
    forwarded.playing = transport.playing;
    forwarded.hasTempo = transport.hasTempo;
    state.undertow.setTransport(forwarded);
}

void FathomEngine::setReferenceArithmetic(bool reference) noexcept
{
    state_->undertow.setReferenceArithmetic(reference);
}

void FathomEngine::setClockOriginsForTesting(const ClockOrigins& origins) noexcept
{
    auto& state = *state_;
    state.origins = origins;
    UndertowLayer::ClockOrigins forwarded;
    forwarded.firstFrame = origins.firstFrame;
    forwarded.phasors = origins.phasors;
    forwarded.freeRun = origins.freeRun;
    state.undertow.setClockOrigins(forwarded);
}

std::int64_t FathomEngine::nextCoreSampleForTesting() const noexcept
{
    return state_->firstCoreSample + state_->coreSamples;
}

void FathomEngine::setParameters(const Parameters& parameters) noexcept
{
    auto& state = *state_;
    auto next = state.parameters;
    next.decaySeconds = clampFinite(parameters.decaySeconds, minimumDecaySeconds,
                                    fathom::referenceMaximumDecaySeconds, next.decaySeconds);
    next.sizeScale = clampFinite(parameters.sizeScale, minimumSizeScale,
                                 fathom::referenceMaximumSizeScale, next.sizeScale);
    next.preDelaySeconds = clampFinite(parameters.preDelaySeconds, 0.0f,
                                       fathom::referenceMaximumPreDelaySeconds,
                                       next.preDelaySeconds);
    next.macro = clampFinite(parameters.macro, 0.0f, 1.0f, next.macro);
    next.lowCutHz = clampFinite(parameters.lowCutHz, FathomNetwork::neutralLowCutHz,
                                maximumLowCutHz, next.lowCutHz);
    next.highDampingHz = clampFinite(parameters.highDampingHz, minimumHighDampingHz,
                                     FathomNetwork::neutralHighDampingHz, next.highDampingHz);
    next.freeze = parameters.freeze;
    if (sameParameters(next, state.parameters))
        return;

    state.parameters = next;
    if (state.prepared)
        state.applyParameters();
}

FathomEngine::Frame FathomEngine::processSample(float left, float right) noexcept
{
    auto& state = *state_;
    if (!state.prepared)
        return {};

    state.silent = false;
    if (state.undertowLayer)
        state.undertow.hostFrame();
    const auto delayed = state.preDelay.process(sanitiseInput(left), sanitiseInput(right));
    auto wetLeft = 0.0;
    auto wetRight = 0.0;
    if (!state.lattice.converts())
    {
        state.processCore(static_cast<double>(delayed.left), static_cast<double>(delayed.right),
                          wetLeft, wetRight);
        return { outputSample(wetLeft), outputSample(wetRight) };
    }

    // The core runs on its own clock: as many internal samples per host frame
    // as the output converter needs to have received.
    state.toInternal.write(static_cast<double>(delayed.left), static_cast<double>(delayed.right));
    while (state.toHost.written() <= state.toHost.lastSourceNeeded())
    {
        auto coreLeft = 0.0;
        auto coreRight = 0.0;
        state.toInternal.read(coreLeft, coreRight);
        state.processCore(coreLeft, coreRight, wetLeft, wetRight);
        state.toHost.write(wetLeft, wetRight);
    }
    state.toHost.read(wetLeft, wetRight);
    return { outputSample(wetLeft), outputSample(wetRight) };
}

void FathomEngine::advanceIdle() noexcept
{
    auto& state = *state_;
    if (!state.prepared)
        return;

    if (!state.silent)
        state.silence();
    if (state.undertowLayer)
        state.undertow.hostFrame();
    if (!state.lattice.converts())
    {
        state.idleCore();
        return;
    }

    state.toInternal.skipSource();
    while (state.toHost.written() <= state.toHost.lastSourceNeeded())
    {
        state.toInternal.skipTarget();
        state.idleCore();
        state.toHost.skipSource();
    }
    state.toHost.skipTarget();
}

FathomEngine::Frame FathomEngine::applyWidth(Frame wet, float widthScale) noexcept
{
    const auto travel = clampFinite(widthScale, 0.0f, 2.0f, 1.0f);
    // At 100 % both gains are one and the wet is the engine's own. The sums
    // below would round the smaller of two channels that lie far apart.
    if (sameBits(travel, 1.0f))
        return wet;

    const auto scale = static_cast<double>(travel);
    const auto midGain = std::sqrt(2.0 / (1.0 + scale));
    const auto sideGain = scale * midGain;
    const auto mid = 0.5 * (static_cast<double>(wet.left) + static_cast<double>(wet.right));
    const auto side = 0.5 * (static_cast<double>(wet.left) - static_cast<double>(wet.right));
    return { static_cast<float>(midGain * mid + sideGain * side),
             static_cast<float>(midGain * mid - sideGain * side) };
}

void FathomEngine::mixGains(float mix, float& dryGain, float& wetGain) noexcept
{
    const auto amount = static_cast<double>(clampFinite(mix, 0.0f, 1.0f, 0.0f));
    dryGain = static_cast<float>(std::min(1.0, 2.0 * (1.0 - amount)));
    wetGain = static_cast<float>(std::min(1.0, 2.0 * amount));
}

float FathomEngine::mix(float dryGain, float dry, float wetGain, float wet) noexcept
{
    const auto dryShare = dryGain * dry;
    const auto wetShare = wetGain * wet;
    return dryShare + wetShare;
}

float FathomEngine::clip(float sample) noexcept
{
    if (std::isnan(sample))
        return 0.0f;

    const auto size = std::abs(static_cast<double>(sample));
    if (size <= fathom::clipThreshold)
        return sample;

    // A quadratic knee from the threshold to the ceiling, flat above.
    const auto knee = size - (size - fathom::clipThreshold) * (size - fathom::clipThreshold)
                                 / (4.0 * (fathom::clipCeiling - fathom::clipThreshold));
    const auto limited = size >= 2.0 * fathom::clipCeiling - fathom::clipThreshold
        ? fathom::clipCeiling
        : knee;
    return static_cast<float>(std::copysign(limited, static_cast<double>(sample)));
}

void FathomEngine::LevelStage::prepare(double sampleRate) noexcept
{
    const auto rate = static_cast<double>(hostRateOf(sampleRate));
    attackCoefficient_ = static_cast<float>(
        std::exp(-1.0 / (fathom::levelStageAttackSeconds * rate)));
    releaseCoefficient_ = static_cast<float>(
        std::exp(-1.0 / (fathom::levelStageReleaseSeconds * rate)));
    reset();
}

void FathomEngine::LevelStage::reset() noexcept
{
    reductionDb_ = 0.0f;
}

FathomEngine::Frame FathomEngine::LevelStage::process(float dryLeft, float dryRight,
                                                      Frame wet) noexcept
{
    // Static curve of a compressor with its threshold at 0 dBFS, keyed by the
    // larger input magnitude: nothing below the knee, a parabola through it,
    // a straight line above.
    const auto key = std::max(std::isfinite(dryLeft) ? std::abs(dryLeft) : 0.0f,
                              std::isfinite(dryRight) ? std::abs(dryRight) : 0.0f);
    auto target = 0.0f;
    if (key > levelStageQuietPeak)
    {
        const auto level = 20.0 * std::log10(static_cast<double>(key));
        const auto half = fathom::levelStageKneeDb / 2.0;
        const auto intoKnee = level + half;
        if (level >= half)
            target = static_cast<float>(-fathom::levelStageSlope * level);
        else if (intoKnee > 0.0)
            target = static_cast<float>(-fathom::levelStageSlope * (intoKnee * intoKnee)
                                        / (2.0 * fathom::levelStageKneeDb));
    }

    // The reduction follows the curve in decibels, fast while it grows and
    // slowly while it recovers. Each step is rounded to single precision.
    const auto coefficient = target < reductionDb_ ? attackCoefficient_ : releaseCoefficient_;
    const auto distance = reductionDb_ - target;
    const auto remainder = coefficient * distance;
    reductionDb_ = target + remainder;
    if (std::abs(reductionDb_) < silenceFloor)
        reductionDb_ = 0.0f;
    if (!(reductionDb_ < 0.0f))
        return wet;

    const auto gain = std::pow(10.0, static_cast<double>(reductionDb_) / 20.0);
    return { static_cast<float>(gain * static_cast<double>(wet.left)),
             static_cast<float>(gain * static_cast<double>(wet.right)) };
}
} // namespace amanita::dsp
