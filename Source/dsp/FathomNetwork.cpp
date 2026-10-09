#include "FathomExactArithmetic.h"

#include "FathomNetwork.h"

#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double decaySizeGlideSeconds = 0.25;
constexpr double shapingGlideSeconds = 0.05;
constexpr double macroGlideSeconds = 0.2;
// Seconds between two settings of the voices.
constexpr double voiceBlockSeconds = static_cast<double>(fathom::voiceBlockSamples)
                                   / fathom::internalRate;
// A voice whose Q is this close to the resting one is at rest again.
constexpr double restingQualityTolerance = 1.0e-9;
// Bounds of what a line may hold. The upper one lies far above any signal and
// only keeps a fault from spreading; below the lower one, 600 dB under full
// scale, a line holds zero, well before single precision runs out of normal
// numbers.
constexpr double lineLimit = 1.0e6;
constexpr double lineFloor = 1.0e-30;

// Each quadrant of the reference's sine lags the true one by the error of the
// single-precision pi / 2 it reduces its argument with.
constexpr double quadrantLag = fathom::oscillatorHalfPiSingle - fathom::oscillatorHalfPi;

// Odd curve through (-1, -1), (0, 0) and (1, 1), exponential on each half.
[[nodiscard]] double sCurve(double position, double shape) noexcept
{
    const auto half = std::expm1(shape * std::abs(position)) / std::expm1(shape);
    return position < 0.0 ? -half : half;
}

[[nodiscard]] double onePoleCoefficient(double cutoffHz) noexcept
{
    const auto safeCutoff = std::clamp(cutoffHz, 1.0, 0.45 * fathom::internalRate);
    return std::exp(-2.0 * pi * safeCutoff / fathom::internalRate);
}

// What a line stores: single precision and inside its bounds.
[[nodiscard]] float stored(double sample) noexcept
{
    if (!std::isfinite(sample) || std::abs(sample) < lineFloor)
        return 0.0f;
    return static_cast<float>(std::clamp(sample, -lineLimit, lineLimit));
}

// What a first-order filter keeps of a value: nothing once it is under the
// floor of the lines, so a state that only decays reaches zero instead of the
// denormal range.
[[nodiscard]] double settled(double value) noexcept
{
    return std::abs(value) < lineFloor ? 0.0 : value;
}

// The same for the two states of a second-order filter, which settle together.
// One alone at zero would leave the other to a recursion that is not the
// filter's and that keeps it sounding at the floor.
void settle(double& first, double& second) noexcept
{
    const auto quiet = std::abs(first) < lineFloor && std::abs(second) < lineFloor;
    first = quiet ? 0.0 : first;
    second = quiet ? 0.0 : second;
}

// Delay of the comb of an input at a sample, in samples: a triangle of 200 s
// around its middle, the right input a quarter period ahead. The triangle is
// rounded to single precision, and so are its product with the swing and the
// sum with the middle.
[[nodiscard]] float combDelay(std::uint64_t sample, std::size_t channel) noexcept
{
    auto position = (static_cast<double>(sample) - fathom::combOriginSamples)
                        / fathom::combPeriodSamples
                  + fathom::combRightInputLead * static_cast<double>(channel);
    position -= std::floor(position);
    const auto triangle = position < 0.25 ? 4.0 * position
                        : position < 0.75 ? 2.0 - 4.0 * position
                                          : 4.0 * position - 4.0;
    const auto swing = fathom::combDelaySwingSamples * static_cast<float>(triangle);
    return fathom::combDelayMidSamples + swing;
}

// Curve through (0, 0) and (1, 1) that rises fastest at its start.
[[nodiscard]] double bent(double position) noexcept
{
    return (1.0 - std::exp(-fathom::voiceCurveBend * position))
         / (1.0 - std::exp(-fathom::voiceCurveBend));
}

// Q a voice at full depth moves towards: straight lines between knots along
// the cut-off, each knot `macroBend` of the way from its value at Macro 0 to
// that at Macro 100 %.
[[nodiscard]] double qualityAt(double cutoffHz, double macroBend) noexcept
{
    const auto value = [macroBend](const fathom::QualityKnot& knot)
    {
        return knot.atMacro0 + (knot.atMacro100 - knot.atMacro0) * macroBend;
    };

    std::size_t upper = 1;
    while (upper + 1 < fathom::voiceQualityKnots.size()
           && cutoffHz > fathom::voiceQualityKnots[upper].cutoffHz)
        ++upper;
    const auto& low = fathom::voiceQualityKnots[upper - 1];
    const auto& high = fathom::voiceQualityKnots[upper];
    const auto along = (cutoffHz - low.cutoffHz) / (high.cutoffHz - low.cutoffHz);
    return value(low) + (value(high) - value(low)) * along;
}

// Number `index` of the SplitMix64 sequence that starts at `key`. Taken by its
// index, a number depends on nothing drawn before it.
[[nodiscard]] std::uint64_t randomBits(std::uint64_t key, std::uint64_t index) noexcept
{
    auto bits = key + (index + 1) * 0x9e3779b97f4a7c15ULL;
    bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
    bits = (bits ^ (bits >> 27)) * 0x94d049bb133111ebULL;
    return bits ^ (bits >> 31);
}

// Uniform in [0, 1): the upper 53 bits as a fraction, which a double holds exactly.
[[nodiscard]] double unitInterval(std::uint64_t bits) noexcept
{
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

// The sixteen feedback signals of a group from its sixteen line outputs: the
// Sylvester Hadamard matrix of order 16 with its rows reversed, times 1/4.
void mixLines(std::array<double, FathomNetwork::lineCount>& values) noexcept
{
    constexpr auto count = FathomNetwork::lineCount;
    for (std::size_t stride = 1; stride < count; stride *= 2)
    {
        for (std::size_t block = 0; block < count; block += stride * 2)
        {
            for (std::size_t offset = 0; offset < stride; ++offset)
            {
                const auto first = values[block + offset];
                const auto second = values[block + offset + stride];
                values[block + offset] = first + second;
                values[block + offset + stride] = first - second;
            }
        }
    }

    for (std::size_t line = 0; line < count / 2; ++line)
    {
        const auto low = fathom::feedbackScale * values[count - 1 - line];
        values[count - 1 - line] = fathom::feedbackScale * values[line];
        values[line] = low;
    }
}
} // namespace

double FathomVoicePhase::rate(double macro) noexcept
{
    return fathom::phaseRateAtMacro0
         + (fathom::phaseRateAtMacro100 - fathom::phaseRateAtMacro0)
               * (std::expm1(fathom::phaseRateShape * macro) / std::expm1(fathom::phaseRateShape));
}

void FathomVoicePhase::reset(std::uint64_t seed) noexcept
{
    // The two start levels are tied: more often than not both lie in the same
    // half of their ranges.
    const auto rightHigh = unitInterval(randomBits(seed, outputCount)) < 0.5;
    const auto sameHalf = unitInterval(randomBits(seed, outputCount + 1))
                        < fathom::phaseStartSameHalf;
    for (std::size_t output = 0; output < outputCount; ++output)
    {
        key_[output] = randomBits(seed, output);
        const auto high = output == 0 ? rightHigh == sameHalf : rightHigh;
        const auto place = 0.5 * ((high ? 1.0 : 0.0) + unitInterval(randomBits(key_[output], 0)));
        startLevel_[output] = fathom::phaseLevelLow[output]
                            + (fathom::phaseLevelHigh[output] - fathom::phaseLevelLow[output])
                                  * place;
    }
    block_ = 0;
    ramp_ = 0.0;
}

void FathomVoicePhase::advance(double macro) noexcept
{
    ramp_ += rate(macro) * voiceBlockSeconds;
    ++block_;
}

double FathomVoicePhase::phase(std::size_t output) const noexcept
{
    const auto seconds = static_cast<double>(block_ * fathom::voiceBlockSamples)
                       / fathom::internalRate;
    return level(output, seconds) + ramp_;
}

double FathomVoicePhase::level(std::size_t output, double seconds) const noexcept
{
    // The knot at or before this time is that of its cell or the one before.
    auto index = static_cast<std::int64_t>(std::floor(seconds * fathom::phaseGridHz[output]));
    if (index < 1 || seconds < knot(output, index).seconds)
        --index;
    if (index < 1)
        return startLevel_[output];

    const auto from = knot(output, index);
    const auto to = knot(output, index + 1);
    const auto length = to.seconds - from.seconds;
    const auto along = length > 0.0 ? std::min((seconds - from.seconds) / length, 1.0) : 1.0;
    return from.level + (to.level - from.level) * (0.5 - 0.5 * std::cos(pi * along));
}

FathomVoicePhase::Knot FathomVoicePhase::knot(std::size_t output, std::int64_t index) const noexcept
{
    if (index < 1)
        return { 0.0, startLevel_[output] };

    // Numbers 2j - 1 and 2j of an output belong to knot j: its place in its
    // cell and its level. Knot 1 ends the hold of the start value.
    const auto number = 2 * static_cast<std::uint64_t>(index);
    const auto cells = static_cast<double>(index) + unitInterval(randomBits(key_[output], number - 1));
    const auto level = index == 1
        ? startLevel_[output]
        : fathom::phaseLevelLow[output]
              + (fathom::phaseLevelHigh[output] - fathom::phaseLevelLow[output])
                    * unitInterval(randomBits(key_[output], number));
    return { cells / fathom::phaseGridHz[output], level };
}

double FathomNetwork::Comb::process(double sample, float delaySamples, std::size_t slot) noexcept
{
    constexpr auto mask = combCapacity - 1;
    const auto whole = static_cast<std::size_t>(delaySamples);
    const auto fraction = static_cast<double>(delaySamples) - static_cast<double>(whole);
    const auto newer = line[(slot - whole) & mask];
    const auto older = line[(slot - whole - 1) & mask];
    read = settled(older + (1.0 - fraction) / (1.0 + fraction) * (newer - read));
    highPass = settled(fathom::combHighPassGain * (read - highPassInput)
                       + fathom::combHighPassPole * highPass);
    highPassInput = read;
    const auto delayed = settled(fathom::combLowPassGain * (highPass + lowPassInput)
                                 + fathom::combLowPassPole * lowPass);
    lowPassInput = highPass;
    lowPass = delayed;
    line[slot & mask] = settled(sample + fathom::combFeedback * delayed);
    return delayed;
}

void FathomNetwork::Comb::clear() noexcept
{
    line.fill(0.0);
    read = 0.0;
    highPassInput = 0.0;
    highPass = 0.0;
    lowPassInput = 0.0;
    lowPass = 0.0;
}

void FathomNetwork::Voice::setCutoff(double cutoffHz, double q) noexcept
{
    const auto g = std::tan(pi * cutoffHz / fathom::internalRate);
    a1 = 1.0 / (1.0 + g * (g + 1.0 / q));
    a2 = g * a1;
    a3 = g * a2;
}

double FathomNetwork::Voice::process(double sample) noexcept
{
    const auto v3 = sample - state2;
    const auto v1 = a1 * state1 + a2 * v3;
    const auto v2 = state2 + a2 * state1 + a3 * v3;
    state1 = 2.0 * v1 - state1;
    state2 = 2.0 * v2 - state2;
    settle(state1, state2);
    return v2;
}

void FathomNetwork::prepare(std::uint64_t voiceSeed)
{
    lines_.assign(allLines * lineCapacity, 0.0f);
    length_.setLength(static_cast<int>(std::lround(decaySizeGlideSeconds * fathom::internalRate)));
    attenuation_.setLength(static_cast<int>(std::lround(decaySizeGlideSeconds * fathom::internalRate)));
    tapWeight_.setLength(static_cast<int>(std::lround(decaySizeGlideSeconds * fathom::internalRate)));
    shaping_.setLength(static_cast<int>(std::lround(shapingGlideSeconds * fathom::internalRate)));
    macro_.setLength(static_cast<int>(std::lround(macroGlideSeconds * fathom::internalRate)));
    prepared_ = true;
    reset(voiceSeed);
}

void FathomNetwork::reset(std::uint64_t voiceSeed) noexcept
{
    if (!prepared_)
        return;

    sampleCount_ = 0;
    for (std::size_t group = 0; group < groupCount; ++group)
        for (std::size_t line = 0; line < lineCount; ++line)
            oscillatorPhase_[group * lineCount + line] = fathom::oscillatorStartPhase[group][line];
    voicePhase_.reset(voiceSeed);
    restVoices();
    silence();
}

void FathomNetwork::setSettings(const Settings& settings) noexcept
{
    settings_ = settings;
    if (prepared_)
        applySettings(silent_);
}

void FathomNetwork::prescribeVoicePhase(const double* left, const double* right,
                                        std::size_t blockCount) noexcept
{
    const auto given = left != nullptr && right != nullptr;
    prescribedPhase_ = { given ? left : nullptr, given ? right : nullptr };
    prescribedBlocks_ = given ? blockCount : 0;
    updateMacro();
}

void FathomNetwork::silence() noexcept
{
    written_ = 0;
    equaliserState_ = {};
    lineInput_ = {};
    kernelState_ = {};
    lowCutState_ = {};
    dampingState_ = {};
    for (auto& comb : combs_)
        comb.clear();
    for (auto& output : voices_)
    {
        for (auto& voice : output)
        {
            voice.state1 = 0.0;
            voice.state2 = 0.0;
        }
    }
    silent_ = true;
    applySettings(true);
}

void FathomNetwork::applySettings(bool atOnce) noexcept
{
    const auto decay = static_cast<double>(settings_.decaySeconds);
    const auto size = static_cast<double>(settings_.sizeScale);

    // Whole line lengths in the reference's arithmetic, product, sum and floor
    // in single precision, and 60 dB per Decay seconds of the unrounded length.
    FathomGlide<allLines>::Values length {};
    FathomGlide<allLines>::Values attenuation {};
    for (std::size_t group = 0; group < groupCount; ++group)
    {
        for (std::size_t line = 0; line < lineCount; ++line)
        {
            const auto scaled = static_cast<float>(fathom::linePrimes[group][line]) * settings_.sizeScale;
            length[group * lineCount + line] = static_cast<double>(std::floor(scaled + 0.5f));
            attenuation[group * lineCount + line] = std::pow(
                10.0,
                -3.0 * fathom::linePrimes[group][line] * size / (fathom::internalRate * decay));
        }
    }

    // The tap weights of line 1 and line 16 move with Decay between the low end
    // of the reference's range and a knee, and rest outside it. Lines 1 to 9
    // lie on one S-curve between the weight of line 1 and 1, lines 9 to 16 on
    // another between 1 and the weight of line 16.
    const auto position = (std::clamp(decay, fathom::tapDecayLowSeconds,
                                      fathom::tapDecayKneeSeconds)
                           - fathom::tapDecayLowSeconds)
                        / (fathom::tapDecayKneeSeconds - fathom::tapDecayLowSeconds);
    const auto first = fathom::tapFirstAtLow
                     + (fathom::tapFirstAtKnee - fathom::tapFirstAtLow) * position;
    const auto last = fathom::tapLastAtLow
                    + (fathom::tapLastAtKnee - fathom::tapLastAtLow)
                          * (std::expm1(fathom::tapLastDecayShape * position)
                             / std::expm1(fathom::tapLastDecayShape));
    FathomGlide<lineCount>::Values tapWeight {};
    for (std::size_t line = 0; line < lineCount; ++line)
    {
        const auto number = static_cast<double>(line + 1);
        tapWeight[line] = number <= 9.0
            ? (first + 1.0) / 2.0
                  + (1.0 - first) / 2.0 * sCurve((number - 5.0) / 4.0, fathom::tapEarlyShape)
            : (1.0 + last) / 2.0
                  + (last - 1.0) / 2.0 * sCurve((number - 12.5) / 3.5, fathom::tapLateShape);
    }

    FathomGlide<shapingCount>::Values shaping {};
    shaping[hold] = settings_.freeze ? 1.0 : 0.0;
    shaping[lowCutAmount] = settings_.lowCutHz > neutralLowCutHz ? 1.0 : 0.0;
    shaping[lowCutCoefficient] = onePoleCoefficient(static_cast<double>(settings_.lowCutHz));
    shaping[dampingAmount] = settings_.highDampingHz < neutralHighDampingHz ? 1.0 : 0.0;
    shaping[dampingCoefficient] = onePoleCoefficient(static_cast<double>(settings_.highDampingHz));

    const auto macro = static_cast<double>(settings_.macro);
    if (atOnce)
    {
        length_.jumpTo(length);
        attenuation_.jumpTo(attenuation);
        tapWeight_.jumpTo(tapWeight);
        shaping_.jumpTo(shaping);
        macro_.jumpTo({ macro });
        updateLengths();
        updateMacro();
        shaped_ = shaping[hold] > 0.0 || shaping[lowCutAmount] > 0.0 || shaping[dampingAmount] > 0.0;
        return;
    }

    length_.glideTo(length);
    attenuation_.glideTo(attenuation);
    tapWeight_.glideTo(tapWeight);
    shaping_.glideTo(shaping);
    macro_.glideTo({ macro });
}

// The lengths the lines are read at. Between two settings of Size each line
// moves in a straight line from the whole length of the one to that of the
// other, so the read sweeps through the samples in between instead of jumping
// over them.
void FathomNetwork::updateLengths() noexcept
{
    const auto& length = length_.values();
    for (std::size_t line = 0; line < allLines; ++line)
        lineLength_[line] = static_cast<float>(length[line]);
}

// What follows Macro sample by sample. At Macro 0 the Tide layer is out of
// the circuit, unless a prescribed phase keeps it in.
void FathomNetwork::updateMacro() noexcept
{
    const auto macro = macro_.values()[0];
    tide_ = macro > 0.0 || prescribedPhase_[0] != nullptr;
    plainShare_ = std::cos(0.5 * pi * macro);
    combShare_ = std::sin(0.5 * pi * macro);
    voiceDepth_ = std::min(1.0, fathom::voiceAmountPerMacro * macro / fathom::voiceFullDepthAmount);
}

void FathomNetwork::advanceGlides() noexcept
{
    if (macro_.moving())
    {
        macro_.advance();
        updateMacro();
    }
    if (length_.moving())
    {
        length_.advance();
        updateLengths();
    }
    if (attenuation_.moving())
        attenuation_.advance();
    if (tapWeight_.moving())
        tapWeight_.advance();
    if (shaping_.moving())
    {
        shaping_.advance();
        const auto& shaping = shaping_.values();
        const auto shaped = shaping[hold] > 0.0 || shaping[lowCutAmount] > 0.0
                         || shaping[dampingAmount] > 0.0;
        // Out of the circuit, Ocean's filters start from rest when they return.
        if (shaped_ && !shaped)
        {
            lowCutState_ = {};
            dampingState_ = {};
        }
        shaped_ = shaped;
    }
}

// The voices at the start of a block. Their gain at full depth follows the
// phase at every Macro. Cut-off and Q follow their laws while the Tide layer is
// in the circuit, and afterwards until Q has come back to rest.
void FathomNetwork::updateVoices() noexcept
{
    const auto macro = macro_.values()[0];
    const auto block = static_cast<std::size_t>(sampleCount_ / fathom::voiceBlockSamples);
    std::array<std::array<double, 2>, groupCount> phase;
    for (std::size_t group = 0; group < groupCount; ++group)
    {
        const auto first = block < prescribedBlocks_ ? prescribedPhase_[group][block]
                                                     : voicePhase_.phase(group);
        phase[group] = { first, first + fathom::voiceSecondOffsetCycles };
        for (std::size_t voice = 0; voice < phase[group].size(); ++voice)
        {
            const auto sine = std::sin(pi * phase[group][voice]);
            voices_[group][voice].window = fathom::voiceGainTop * sine * sine;
        }
    }
    voicePhase_.advance(macro);
    if (!tide_ && voicesAtRest_)
        return;

    // The cut-off falls from its resting value along the cycle, the further
    // the higher Macro is, and returns at once when a new cycle starts. Q
    // follows its target with a lag, from the target itself at the first block.
    const auto reach = (fathom::voiceRestCutoffHz - fathom::voiceCutoffEndHz)
                     * bent(fathom::voiceAmountPerMacro * macro);
    const auto macroBend = bent(macro);
    auto resting = !tide_;
    for (std::size_t group = 0; group < groupCount; ++group)
    {
        for (std::size_t voice = 0; voice < phase[group].size(); ++voice)
        {
            auto& moved = voices_[group][voice];
            const auto cycle = phase[group][voice] - std::floor(phase[group][voice]);
            const auto cutoff = fathom::voiceRestCutoffHz - reach * bent(cycle);
            const auto target = fathom::voiceRestQ
                              + voiceDepth_ * (qualityAt(cutoff, macroBend) - fathom::voiceRestQ);
            moved.quality = sampleCount_ == 0
                ? target
                : moved.quality + fathom::voiceQualitySmoothing * (target - moved.quality);
            moved.setCutoff(cutoff, moved.quality);
            resting = resting && std::abs(moved.quality - fathom::voiceRestQ) <= restingQualityTolerance;
        }
    }
    voicesAtRest_ = false;
    if (resting)
        restVoices();
}

void FathomNetwork::restVoices() noexcept
{
    for (auto& output : voices_)
    {
        for (auto& voice : output)
        {
            voice.quality = fathom::voiceRestQ;
            voice.setCutoff(fathom::voiceRestCutoffHz, fathom::voiceRestQ);
        }
    }
    voicesAtRest_ = true;
}

// One step of the sixteen accumulators of a group. `phases` receives the phase
// each line uses in this sample; the accumulator moves on for the next one,
// with at most one subtraction of two pi.
void FathomNetwork::advanceOscillators(std::size_t group, std::array<float, lineCount>& phases) noexcept
{
    auto* accumulator = oscillatorPhase_.data() + group * lineCount;
    for (std::size_t line = 0; line < lineCount; ++line)
    {
        auto phase = accumulator[line];
        if (phase >= fathom::oscillatorTwoPi)
            phase -= fathom::oscillatorTwoPi;
        accumulator[line] = phase + fathom::oscillatorIncrement;
        phases[line] = phase;
    }
}

// Every line of a group read at its modulated length with linear interpolation.
void FathomNetwork::readLines(std::size_t group, LineArray& reads) noexcept
{
    std::array<float, lineCount> phases;
    advanceOscillators(group, phases);

    constexpr auto mask = lineCapacity - 1;
    const auto newest = static_cast<std::size_t>(sampleCount_);
    for (std::size_t line = 0; line < lineCount; ++line)
    {
        // The sine is rounded to single precision, and so are its product with
        // the depth and the sum with the length of the line.
        const auto phase = static_cast<double>(phases[line]);
        const auto quadrant = std::nearbyint(phase / fathom::oscillatorHalfPi);
        const auto sine = static_cast<float>(std::sin(phase - quadrant * quadrantLag));
        const auto modulation = fathom::oscillatorDepthSamples * sine;
        const auto length = lineLength_[group * lineCount + line] + modulation;

        const auto whole = static_cast<int>(length);
        const auto fraction = static_cast<double>(length) - whole;
        const auto* ring = lines_.data() + (group * lineCount + line) * lineCapacity;
        const auto back = static_cast<std::size_t>(whole);
        const auto newer = whole <= written_ ? ring[(newest - back) & mask] : 0.0f;
        const auto older = whole < written_ ? ring[(newest - back - 1) & mask] : 0.0f;
        reads[line] = (1.0 - fraction) * static_cast<double>(newer)
                    + fraction * static_cast<double>(older);
    }
}

double FathomNetwork::equalise(std::size_t channel, double sample) noexcept
{
    for (std::size_t index = 0; index < fathom::equaliser.size(); ++index)
    {
        const auto& section = fathom::equaliser[index];
        auto& state = equaliserState_[channel][index];
        const auto output = section.b0 * sample + state[0];
        state[0] = section.b1 * sample - section.a1 * output + state[1];
        state[1] = section.b2 * sample - section.a2 * output;
        settle(state[0], state[1]);
        sample = output;
    }
    return sample;
}

// The loop filter on the sixteen feedback signals of a group, each with its own state.
void FathomNetwork::filterLoop(std::size_t group, LineArray& signal) noexcept
{
    for (std::size_t index = 0; index < fathom::loopKernel.size(); ++index)
    {
        const auto& section = fathom::loopKernel[index];
        auto& state0 = kernelState_[group][index][0];
        auto& state1 = kernelState_[group][index][1];
        for (std::size_t line = 0; line < lineCount; ++line)
        {
            const auto input = signal[line];
            const auto output = section.b0 * input + state0[line];
            state0[line] = section.b1 * input - section.a1 * output + state1[line];
            state1[line] = section.b2 * input - section.a2 * output;
            settle(state0[line], state1[line]);
            signal[line] = output;
        }
    }
}

// Ocean's own part of the loop, behind the loop filter: the one-pole low cut
// and damping of FDNReverb per line, each faded in by its amount, and the
// hold, which fades the whole filtered path out in favour of the unfiltered
// mix.
void FathomNetwork::shapeLoop(std::size_t group, const LineArray& mixed, LineArray& signal) noexcept
{
    const auto& shaping = shaping_.values();
    const auto keep = 1.0 - shaping[hold];
    auto& lowCut = lowCutState_[group];
    auto& damping = dampingState_[group];
    for (std::size_t line = 0; line < lineCount; ++line)
    {
        auto sample = signal[line];
        lowCut[line] = settled(shaping[lowCutCoefficient] * lowCut[line]
                               + (1.0 - shaping[lowCutCoefficient]) * sample);
        sample -= shaping[lowCutAmount] * lowCut[line];
        damping[line] = settled(shaping[dampingCoefficient] * damping[line]
                                + (1.0 - shaping[dampingCoefficient]) * sample);
        sample += shaping[dampingAmount] * (damping[line] - sample);
        signal[line] = mixed[line] + keep * (sample - mixed[line]);
    }
}

void FathomNetwork::process(double left, double right, double& outputLeft, double& outputRight) noexcept
{
    silent_ = false;
    advanceGlides();
    if (sampleCount_ % fathom::voiceBlockSamples == 0)
        updateVoices();

    // Each equalised input passes its comb at every Macro, so that the comb
    // has its history when Macro rises. The lines take the equalised input,
    // or its mix with the combed one, a fixed number of samples later.
    std::array<double, groupCount> lineInput {};
    const std::array<double, groupCount> equalised { equalise(0, left), equalise(1, right) };
    for (std::size_t channel = 0; channel < groupCount; ++channel)
    {
        const auto combed = combs_[channel].process(equalised[channel],
                                                    combDelay(sampleCount_, channel),
                                                    static_cast<std::size_t>(sampleCount_));
        lineInput[channel] = lineInput_[channel][lineInputIndex_];
        lineInput_[channel][lineInputIndex_] = tide_
            ? plainShare_ * equalised[channel] + combShare_ * combed
            : equalised[channel];
    }
    if (++lineInputIndex_ == lineInput_[0].size())
        lineInputIndex_ = 0;

    const auto& tapWeight = tapWeight_.values();
    const auto heldShare = shaping_.values()[hold];
    const auto inputShare = 1.0 - heldShare;
    const auto slot = static_cast<std::size_t>(sampleCount_) & (lineCapacity - 1);
    std::array<double, groupCount> output {};

    for (std::size_t group = 0; group < groupCount; ++group)
    {
        LineArray reads;
        readLines(group, reads);

        // The attenuated read of a line is its output tap and, unless the loop
        // is held, its contribution to the feedback. The first lines of a
        // group feed one voice of its output, the others the second. In the
        // circuit, the Tide layer gives each voice its gain: 1 at no depth,
        // the voice's window at full depth.
        const auto* attenuation = attenuation_.values().data() + group * lineCount;
        LineArray attenuated;
        for (std::size_t line = 0; line < lineCount; ++line)
            attenuated[line] = attenuation[line] * reads[line];

        constexpr auto split = static_cast<std::size_t>(fathom::voiceLineCount);
        auto early = 0.0;
        auto late = 0.0;
        for (std::size_t line = 0; line < split; ++line)
            early += tapWeight[line] * attenuated[line];
        for (std::size_t line = split; line < lineCount; ++line)
            late += tapWeight[line] * attenuated[line];
        auto& voices = voices_[group];
        if (tide_)
        {
            const auto plain = 1.0 - voiceDepth_;
            output[group] = (plain + voiceDepth_ * voices[0].window) * voices[0].process(early)
                          + (plain + voiceDepth_ * voices[1].window) * voices[1].process(late);
        }
        else
        {
            output[group] = voices[0].process(early) + voices[1].process(late);
        }

        const auto own = lineInput[group];
        const auto other = lineInput[1 - group];
        auto* ring = lines_.data() + group * lineCount * lineCapacity + slot;
        if (!shaped_)
        {
            mixLines(attenuated);
            filterLoop(group, attenuated);
            for (std::size_t line = 0; line < lineCount; ++line)
                ring[line * lineCapacity] = stored(fathom::lineOwnGain[line] * own
                                                   + fathom::lineCrossGain[line] * other
                                                   + attenuated[line]);
            continue;
        }

        // Held, a line loses nothing in a pass and takes no input.
        LineArray mixed;
        for (std::size_t line = 0; line < lineCount; ++line)
            mixed[line] = (attenuation[line] + heldShare * (1.0 - attenuation[line])) * reads[line];
        mixLines(mixed);
        auto shaped = mixed;
        filterLoop(group, shaped);
        shapeLoop(group, mixed, shaped);
        for (std::size_t line = 0; line < lineCount; ++line)
            ring[line * lineCapacity] = stored(inputShare * (fathom::lineOwnGain[line] * own
                                                             + fathom::lineCrossGain[line] * other)
                                               + shaped[line]);
    }

    ++sampleCount_;
    if (written_ < static_cast<int>(lineCapacity))
        ++written_;
    outputLeft = output[0];
    outputRight = output[1];
}

void FathomNetwork::idle() noexcept
{
    if (!silent_)
        silence();
    if (sampleCount_ % fathom::voiceBlockSamples == 0)
        updateVoices();

    std::array<float, lineCount> phases;
    for (std::size_t group = 0; group < groupCount; ++group)
        advanceOscillators(group, phases);
    ++sampleCount_;
}
} // namespace amanita::dsp
