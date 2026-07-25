#include "HarmonicAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace amanita::dsp
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float a4FrequencyHz = 440.0f;
constexpr int firstMidiNote = 36; // C2
constexpr float analysisQ = 22.0f;
constexpr float highPassFrequencyHz = 54.0f;
constexpr float lowPassFrequencyHz = 2400.0f;
constexpr float activityFloorPower = 6.3095734e-8f; // -72 dBFS RMS
constexpr float activityFullPower = 1.5848932e-5f;  // -48 dBFS RMS
constexpr std::array<float, HarmonicAnalyzer::octaveCount> octaveWeights {
    0.45f, 0.80f, 1.00f, 0.90f, 0.65f
};
struct HarmonicPartial
{
    int semitones;
    float maximumRelativeAmplitude;
};

// Octaves are deliberately absent: they reinforce the same pitch class and
// are useful evidence. These non-octave partials are removed low-to-high
// before chroma folding so a rich monophonic source cannot masquerade as a
// chord made from its own overtone series.
constexpr std::array<HarmonicPartial, 12> nonOctavePartials {{
    { 19, 0.50f }, // h3
    { 28, 0.30f }, // h5
    { 31, 0.24f }, // h6
    { 34, 0.20f }, // h7
    { 38, 0.16f }, // h9
    { 40, 0.14f }, // h10
    { 42, 0.12f }, // h11
    { 43, 0.11f }, // h12
    { 44, 0.10f }, // h13
    { 46, 0.09f }, // h14
    { 47, 0.085f }, // h15
    { 49, 0.075f }  // h17
}};
constexpr std::array<int, 20> harmonicSeriesOffsets {
    0, 12, 19, 24, 28, 31, 34, 36, 38, 40,
    42, 43, 44, 46, 47, 48, 49, 50, 51, 52
};

[[nodiscard]] float midiNoteFrequency(int midiNote) noexcept
{
    return a4FrequencyHz
         * std::exp2(static_cast<float>(midiNote - 69) / 12.0f);
}

[[nodiscard]] float onePoleMemory(double sampleRate, float seconds) noexcept
{
    return static_cast<float>(std::exp(-1.0 / (sampleRate * seconds)));
}

[[nodiscard]] float frequencyMemory(double sampleRate, float frequencyHz) noexcept
{
    return static_cast<float>(std::exp(-2.0 * pi * frequencyHz
                                      / static_cast<float>(sampleRate)));
}
} // namespace

void HarmonicAnalyzer::BandPass::prepare(double sampleRate,
                                         float frequencyHz,
                                         float q) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? static_cast<float>(sampleRate)
        : 12000.0f;
    const auto safeFrequency = std::clamp(frequencyHz, 20.0f,
                                          safeSampleRate * 0.45f);
    const auto safeQ = std::clamp(q, 1.0f, 60.0f);
    const auto g = std::tan(pi * safeFrequency / safeSampleRate);
    k_ = 1.0f / safeQ;
    a1_ = 1.0f / (1.0f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
    reset();
}

void HarmonicAnalyzer::BandPass::reset() noexcept
{
    integrator1_ = 0.0f;
    integrator2_ = 0.0f;
}

float HarmonicAnalyzer::BandPass::process(float sample) noexcept
{
    const auto v3 = sample - integrator2_;
    const auto v1 = a1_ * integrator1_ + a2_ * v3;
    const auto v2 = integrator2_ + a2_ * integrator1_ + a3_ * v3;
    const auto nextIntegrator1 = 2.0f * v1 - integrator1_;
    const auto nextIntegrator2 = 2.0f * v2 - integrator2_;
    const auto output = k_ * v1;

    if (!std::isfinite(output)
        || !std::isfinite(nextIntegrator1)
        || !std::isfinite(nextIntegrator2))
    {
        reset();
        return 0.0f;
    }

    integrator1_ = std::abs(nextIntegrator1) < 1.0e-20f
        ? 0.0f
        : std::clamp(nextIntegrator1, -64.0f, 64.0f);
    integrator2_ = std::abs(nextIntegrator2) < 1.0e-20f
        ? 0.0f
        : std::clamp(nextIntegrator2, -64.0f, 64.0f);
    return std::clamp(output, -4.0f, 4.0f);
}

void HarmonicAnalyzer::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) && sampleRate > 1000.0
        ? sampleRate
        : 48000.0;
    decimationFactor_ = sampleRate_ > 66000.0 ? 8 : 4;
    analysisSampleRate_ = sampleRate_ / static_cast<double>(decimationFactor_);
    analysisHopSamples_ = std::max(
        1, static_cast<int>(std::lround(analysisSampleRate_ * 0.0055)));

    for (std::size_t index = 0; index < modeCount; ++index)
    {
        const auto frequency = midiNoteFrequency(
            firstMidiNote + static_cast<int>(index));
        filtersLeft_[index].prepare(analysisSampleRate_, frequency, analysisQ);
        filtersRight_[index].prepare(analysisSampleRate_, frequency, analysisQ);
    }

    highPassMemory_ = frequencyMemory(sampleRate_, highPassFrequencyHz);
    lowPassMemory_ = frequencyMemory(sampleRate_, lowPassFrequencyHz);
    fastAttackMemory_ = onePoleMemory(analysisSampleRate_, 0.020f);
    fastReleaseMemory_ = onePoleMemory(analysisSampleRate_, 0.080f);
    sustainedAttackMemory_ = onePoleMemory(analysisSampleRate_, 0.110f);
    sustainedReleaseMemory_ = onePoleMemory(analysisSampleRate_, 0.350f);
    confidenceAttackMemory_ = onePoleMemory(
        analysisSampleRate_ / static_cast<double>(analysisHopSamples_), 0.120f);
    confidenceReleaseMemory_ = onePoleMemory(
        analysisSampleRate_ / static_cast<double>(analysisHopSamples_), 0.080f);
    profileMemory_ = onePoleMemory(
        analysisSampleRate_ / static_cast<double>(analysisHopSamples_), 0.250f);
    onsetHoldMemory_ = onePoleMemory(
        analysisSampleRate_ / static_cast<double>(analysisHopSamples_), 0.240f);
    prepared_ = true;
    reset();
}

void HarmonicAnalyzer::reset() noexcept
{
    for (auto& filter : filtersLeft_)
        filter.reset();
    for (auto& filter : filtersRight_)
        filter.reset();
    fastModeEnergy_.fill(0.0f);
    sustainedModeEnergy_.fill(0.0f);
    stableProfile_.fill(0.0f);
    frame_ = {};
    frame_.transientReliability = 1.0f;
    highPassStatesLeft_.fill(0.0f);
    highPassStatesRight_.fill(0.0f);
    lowPassStatesLeft_.fill(0.0f);
    lowPassStatesRight_.fill(0.0f);
    fastBroadbandEnergy_ = 0.0f;
    sustainedBroadbandEnergy_ = 0.0f;
    onsetHold_ = 0.0f;
    decimationCounter_ = 0;
    hopCounter_ = 0;
}

bool HarmonicAnalyzer::processSample(float dryLeft, float dryRight) noexcept
{
    if (!prepared_)
        return false;

    const auto conditionedLeft = conditionSample(
        sanitise(dryLeft), highPassStatesLeft_, lowPassStatesLeft_);
    const auto conditionedRight = conditionSample(
        sanitise(dryRight), highPassStatesRight_, lowPassStatesRight_);

    ++decimationCounter_;
    if (decimationCounter_ < decimationFactor_)
        return false;
    decimationCounter_ = 0;

    const auto broadbandPower = 0.5f
        * (conditionedLeft * conditionedLeft
           + conditionedRight * conditionedRight);
    fastBroadbandEnergy_ = asymmetricEnvelope(
        broadbandPower, fastBroadbandEnergy_,
        fastAttackMemory_, fastReleaseMemory_);
    sustainedBroadbandEnergy_ = asymmetricEnvelope(
        broadbandPower, sustainedBroadbandEnergy_,
        sustainedAttackMemory_, sustainedReleaseMemory_);

    for (std::size_t index = 0; index < modeCount; ++index)
    {
        const auto filteredLeft = filtersLeft_[index].process(conditionedLeft);
        const auto filteredRight = filtersRight_[index].process(conditionedRight);
        const auto power = 0.5f
            * (filteredLeft * filteredLeft + filteredRight * filteredRight);
        fastModeEnergy_[index] = asymmetricEnvelope(
            power, fastModeEnergy_[index],
            fastAttackMemory_, fastReleaseMemory_);
        sustainedModeEnergy_[index] = asymmetricEnvelope(
            power, sustainedModeEnergy_[index],
            sustainedAttackMemory_, sustainedReleaseMemory_);
    }

    ++hopCounter_;
    if (hopCounter_ < analysisHopSamples_)
        return false;
    hopCounter_ = 0;
    publishFrame();
    return true;
}

const HarmonicAnalysisFrame& HarmonicAnalyzer::getFrame() const noexcept
{
    return frame_;
}

double HarmonicAnalyzer::getAnalysisSampleRate() const noexcept
{
    return analysisSampleRate_;
}

float HarmonicAnalyzer::sanitise(float value, float limit) noexcept
{
    if (!std::isfinite(value) || std::abs(value) < 1.0e-20f)
        return 0.0f;
    return std::clamp(value, -limit, limit);
}

float HarmonicAnalyzer::smoothStep(float value) noexcept
{
    const auto x = std::clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float HarmonicAnalyzer::asymmetricEnvelope(float input,
                                           float current,
                                           float attackMemory,
                                           float releaseMemory) noexcept
{
    const auto safeInput = std::isfinite(input)
        ? std::clamp(input, 0.0f, 16.0f)
        : 0.0f;
    const auto memory = safeInput > current ? attackMemory : releaseMemory;
    const auto next = memory * current + (1.0f - memory) * safeInput;
    if (!std::isfinite(next) || next < 1.0e-30f)
        return 0.0f;
    return std::clamp(next, 0.0f, 16.0f);
}

float HarmonicAnalyzer::conditionSample(
    float sample,
    std::array<float, 2>& highPassStates,
    std::array<float, 4>& lowPassStates) noexcept
{
    auto conditioned = sample;
    for (auto& state : highPassStates)
    {
        state = highPassMemory_ * state + (1.0f - highPassMemory_) * conditioned;
        state = sanitise(state);
        conditioned -= state;
    }
    for (auto& state : lowPassStates)
    {
        state = lowPassMemory_ * state + (1.0f - lowPassMemory_) * conditioned;
        state = sanitise(state);
        conditioned = state;
    }
    return sanitise(conditioned);
}

void HarmonicAnalyzer::publishFrame() noexcept
{
    std::array<float, modeCount> salience {};
    auto totalSalience = 0.0f;
    auto strongestMode = 0.0f;
    for (std::size_t index = 0; index < modeCount; ++index)
    {
        const auto sustainedCap = 1.7f * sustainedModeEnergy_[index];
        salience[index] = std::sqrt(std::max(
            0.0f, std::min(fastModeEnergy_[index], sustainedCap)));
        totalSalience += salience[index];
        strongestMode = std::max(strongestMode, salience[index]);
    }

    auto maximumHarmonicCoverage = 0.0f;
    if (totalSalience > 1.0e-12f)
    {
        for (std::size_t fundamental = 0;
             fundamental < modeCount;
             ++fundamental)
        {
            if (salience[fundamental] < 0.015f * strongestMode)
                continue;
            auto explained = 0.0f;
            for (const auto offset : harmonicSeriesOffsets)
            {
                const auto partial = fundamental
                                   + static_cast<std::size_t>(offset);
                if (partial < modeCount)
                    explained += salience[partial];
            }
            maximumHarmonicCoverage = std::max(
                maximumHarmonicCoverage, explained / totalSalience);
        }
    }

    // Explain a plausible harmonic series before folding the filter bank into
    // pitch classes. Only genuinely independent residual energy receives a
    // full chroma vote. Keeping a small raw component makes the sieve tolerant
    // of unusual oscillator spectra and close voicings.
    auto independentSalience = salience;
    for (std::size_t fundamental = 0; fundamental < modeCount; ++fundamental)
    {
        const auto evidence = independentSalience[fundamental];
        if (evidence <= 1.0e-12f)
            continue;
        for (const auto partial : nonOctavePartials)
        {
            const auto child = fundamental
                             + static_cast<std::size_t>(partial.semitones);
            if (child >= modeCount)
                continue;
            // Spectral magnitude alone cannot prove whether a component at an
            // overtone frequency belongs to one rich oscillator or to a real
            // open-voicing note. Preserve most of the observed component in
            // the map and let the independent confidence gates express that
            // ambiguity instead of deleting a genuine chord tone.
            const auto genuineToneFloor = 0.75f * salience[child];
            independentSalience[child] = std::max(
                genuineToneFloor,
                std::max(0.0f,
                         independentSalience[child]
                            - partial.maximumRelativeAmplitude * evidence));
        }
    }

    std::array<float, octaveCount> octaveTotals {};
    auto strongestOctave = 0.0f;
    for (std::size_t octave = 0; octave < octaveCount; ++octave)
    {
        for (std::size_t pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
        {
            const auto index = octave * pitchClassCount + pitchClass;
            octaveTotals[octave] += 0.88f * independentSalience[index]
                                  + 0.12f * salience[index];
        }
        strongestOctave = std::max(strongestOctave, octaveTotals[octave]);
    }

    std::array<float, pitchClassCount> chroma {};
    auto supportVotes = 0.0f;
    auto upperSupportVotes = 0.0f;
    for (std::size_t octave = 0; octave < octaveCount; ++octave)
    {
        const auto relativeEnergy = strongestOctave > 1.0e-12f
            ? octaveTotals[octave] / strongestOctave
            : 0.0f;
        const auto reliability = smoothStep(
            (relativeEnergy - 0.04f) / 0.56f);
        const auto vote = octaveWeights[octave] * reliability;
        supportVotes += reliability;
        if (octave > 0)
            upperSupportVotes += reliability;
        if (octaveTotals[octave] <= 1.0e-12f || vote <= 0.0f)
            continue;

        for (std::size_t pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
        {
            const auto index = octave * pitchClassCount + pitchClass;
            const auto independent = 0.88f * independentSalience[index]
                                   + 0.12f * salience[index];
            chroma[pitchClass] += vote * independent;
        }
    }

    auto chromaSum = 0.0f;
    for (const auto value : chroma)
        chromaSum += value;
    if (chromaSum <= 1.0e-12f)
    {
        frame_.pitchClassWeights.fill(0.0f);
        frame_.confidence = 0.0f;
        frame_.activity = 0.0f;
        frame_.transientReliability = 1.0f;
        stableProfile_.fill(0.0f);
        return;
    }

    std::array<float, pitchClassCount> sortedChroma = chroma;
    std::sort(sortedChroma.begin(), sortedChroma.end());
    const auto median = 0.5f * (sortedChroma[5] + sortedChroma[6]);
    const auto noiseFloor = 0.72f * median;
    auto maximumAboveFloor = 0.0f;
    for (const auto value : chroma)
        maximumAboveFloor = std::max(maximumAboveFloor,
                                     std::max(0.0f, value - noiseFloor));

    for (std::size_t pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
    {
        const auto ratio = maximumAboveFloor > 1.0e-12f
            ? std::max(0.0f, chroma[pitchClass] - noiseFloor)
                / maximumAboveFloor
            : 0.0f;
        const auto softWeight = smoothStep(
            (std::sqrt(ratio) - 0.18f) / 0.72f);
        frame_.pitchClassWeights[pitchClass] = softWeight * softWeight;
    }

    auto tonalWeightSum = 0.0f;
    for (const auto weight : frame_.pitchClassWeights)
        tonalWeightSum += weight;
    auto entropy = 0.0f;
    std::array<float, pitchClassCount> probabilities {};
    for (std::size_t pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
    {
        const auto probability = tonalWeightSum > 1.0e-12f
            ? frame_.pitchClassWeights[pitchClass] / tonalWeightSum
            : 1.0f / static_cast<float>(pitchClassCount);
        probabilities[pitchClass] = probability;
        if (probability > 1.0e-12f)
            entropy -= probability * std::log(probability);
    }
    entropy /= std::log(static_cast<float>(pitchClassCount));
    const auto entropyTone = smoothStep(
        ((1.0f - entropy) - 0.08f) / 0.32f);

    auto sortedProbabilities = probabilities;
    std::sort(sortedProbabilities.begin(), sortedProbabilities.end(),
              std::greater<float>());
    const auto topFourRatio = sortedProbabilities[0] + sortedProbabilities[1]
                            + sortedProbabilities[2] + sortedProbabilities[3];
    const auto structure = smoothStep((topFourRatio - 0.38f) / 0.35f);
    auto probabilityEnergy = 0.0f;
    for (const auto probability : probabilities)
        probabilityEnergy += probability * probability;
    const auto effectivePitchCount = probabilityEnergy > 1.0e-12f
        ? 1.0f / probabilityEnergy
        : 0.0f;

    // A stable monophonic source is useful weak evidence, but it must not claim
    // the same certainty as a chord. Conversely, a very crowded field usually
    // means unrelated stereo material or inharmonic/transient energy.
    const auto pitchDiversity = 0.20f + 0.80f * smoothStep(
        (effectivePitchCount - 1.25f) / 1.25f);
    const auto densityReliability = 1.0f - 0.82f * smoothStep(
        (effectivePitchCount - 4.10f) / 1.90f);

    auto consonantPairEnergy = 0.0f;
    auto pairEnergy = 0.0f;
    constexpr std::array<float, 7> intervalConsonance {
        1.0f, 0.05f, 0.35f, 0.85f, 0.90f, 0.80f, 0.15f
    };
    for (std::size_t first = 0; first < pitchClassCount; ++first)
    {
        for (std::size_t second = first + 1;
             second < pitchClassCount;
             ++second)
        {
            const auto pair = probabilities[first] * probabilities[second];
            const auto distance = static_cast<int>(second - first);
            const auto interval = std::min(distance, 12 - distance);
            pairEnergy += pair;
            consonantPairEnergy += pair
                * intervalConsonance[static_cast<std::size_t>(interval)];
        }
    }
    const auto meanConsonance = pairEnergy > 1.0e-12f
        ? consonantPairEnergy / pairEnergy
        : 1.0f;
    const auto consonanceReliability = pitchDiversity < 0.34f
        ? 1.0f
        : 0.08f + 0.92f * smoothStep(
            (meanConsonance - 0.36f) / 0.40f);
    const auto harmonicSeriesAmount = smoothStep(
        (maximumHarmonicCoverage - 0.40f) / 0.30f)
        * smoothStep((effectivePitchCount - 1.25f) / 0.90f);
    const auto harmonicSeriesReliability = 1.0f
        - 0.88f * harmonicSeriesAmount;

    auto profileEnergy = 0.0f;
    auto stableEnergy = 0.0f;
    auto correlation = 0.0f;
    for (std::size_t pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
    {
        profileEnergy += probabilities[pitchClass] * probabilities[pitchClass];
        stableEnergy += stableProfile_[pitchClass] * stableProfile_[pitchClass];
        correlation += probabilities[pitchClass] * stableProfile_[pitchClass];
    }
    const auto stability = stableEnergy > 1.0e-12f && profileEnergy > 1.0e-12f
        ? std::clamp(correlation / std::sqrt(profileEnergy * stableEnergy),
                     0.0f, 1.0f)
        : 1.0f;
    for (std::size_t pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
    {
        stableProfile_[pitchClass] = profileMemory_ * stableProfile_[pitchClass]
                                   + (1.0f - profileMemory_)
                                       * probabilities[pitchClass];
    }

    const auto activityPower = std::min(fastBroadbandEnergy_,
                                        1.7f * sustainedBroadbandEnergy_);
    frame_.activity = smoothStep(
        (activityPower - activityFloorPower)
        / (activityFullPower - activityFloorPower));
    const auto transientRatio = fastBroadbandEnergy_
        / std::max(sustainedBroadbandEnergy_, 1.0e-12f);
    const auto onset = smoothStep((transientRatio - 1.25f) / 2.40f);
    onsetHold_ = std::max(onset, onsetHoldMemory_ * onsetHold_);
    if (!std::isfinite(onsetHold_) || onsetHold_ < 1.0e-20f)
        onsetHold_ = 0.0f;
    frame_.transientReliability = 1.0f - 0.96f * smoothStep(onsetHold_);
    const auto multiOctaveSupport = 0.35f + 0.65f * smoothStep(
        (supportVotes - 0.8f) / 1.4f);
    const auto lowRegisterReliability = 0.25f + 0.75f * smoothStep(
        (upperSupportVotes - 0.10f) / 0.90f);
    const auto stabilityFactor = 0.10f + 0.90f * smoothStep(
        (stability - 0.52f) / 0.42f);
    const auto rawConfidence = frame_.activity
        * std::sqrt(entropyTone * structure)
        * frame_.transientReliability
        * multiOctaveSupport
        * lowRegisterReliability
        * pitchDiversity
        * densityReliability
        * consonanceReliability
        * harmonicSeriesReliability
        * stabilityFactor;
    const auto chordConfidenceTarget = smoothStep(
        (rawConfidence - 0.08f) / 0.52f);
    const auto monophonicEvidence = smoothStep(
        (sortedProbabilities[0] - 0.45f) / 0.25f)
        * (1.0f - smoothStep((effectivePitchCount - 3.0f) / 2.0f));
    const auto monophonicConfidenceTarget = 0.10f
        * frame_.activity
        * frame_.transientReliability
        * stabilityFactor
        * monophonicEvidence;
    const auto confidenceTarget = std::max(chordConfidenceTarget,
                                           monophonicConfidenceTarget);
    const auto confidenceMemory = confidenceTarget > frame_.confidence
        ? confidenceAttackMemory_
        : confidenceReleaseMemory_;
    frame_.confidence = confidenceMemory * frame_.confidence
                      + (1.0f - confidenceMemory) * confidenceTarget;
    if (!std::isfinite(frame_.confidence)
        || (confidenceTarget <= 0.0f && frame_.confidence < 1.0e-5f))
        frame_.confidence = 0.0f;
    frame_.confidence = std::clamp(frame_.confidence, 0.0f, 1.0f);
    if (frame_.activity <= 0.02f && frame_.confidence <= 0.02f)
        frame_.pitchClassWeights.fill(0.0f);
}
} // namespace amanita::dsp
