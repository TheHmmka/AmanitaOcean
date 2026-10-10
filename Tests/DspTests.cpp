#include "dsp/BloomCharacter.h"
#include "dsp/CharacterExcitationNormalizer.h"
#include "dsp/CurrentField.h"
#include "dsp/FDNReverb.h"
#include "dsp/DriftCharacter.h"
#include "dsp/FathomConverter.h"
#include "dsp/FathomEngine.h"
#include "dsp/FathomNetwork.h"
#include "dsp/HarmonicTail.h"
#include "dsp/LateralDecay.h"
#include "dsp/SpatialDucker.h"
#include "dsp/SpumeLayer.h"
#include "dsp/StereoField.h"
#include "dsp/UndertowLayer.h"
#include "dsp/VeilCharacter.h"

#include "FathomGoldenVectors.h"
#include "SpumeGoldenVectors.h"
#include "UndertowGoldenVectors.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cfenv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
std::atomic<bool> countAllocations { false };
std::atomic<std::size_t> allocationCount { 0 };

void noteAllocation() noexcept
{
    if (countAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
}

void* allocateAligned(std::size_t size, std::size_t alignment) noexcept
{
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* memory = nullptr;
    return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
#endif
}

void freeAligned(void* memory) noexcept
{
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
} // namespace

void* operator new(std::size_t size)
{
    noteAllocation();
    if (auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new(size, std::nothrow);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    noteAllocation();
    if (auto* memory = allocateAligned(size, static_cast<std::size_t>(alignment)))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void* operator new(std::size_t size,
                   std::align_val_t alignment,
                   const std::nothrow_t&) noexcept
{
    noteAllocation();
    return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size,
                     std::align_val_t alignment,
                     const std::nothrow_t&) noexcept
{
    return ::operator new(size, alignment, std::nothrow);
}

void operator delete(void* memory, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    freeAligned(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    freeAligned(memory);
}

namespace
{
using amanita::dsp::BloomCharacter;
using amanita::dsp::CharacterExcitationNormalizer;
using amanita::dsp::CurrentField;
using amanita::dsp::FDNReverb;
using amanita::dsp::DriftCharacter;
using amanita::dsp::FathomConverter;
using amanita::dsp::FathomEngine;
using amanita::dsp::FathomRateLattice;
using amanita::dsp::FathomVoicePhase;
using amanita::dsp::HarmonicAnalyzer;
using amanita::dsp::HarmonicAnalysisFrame;
using amanita::dsp::HarmonicTail;
using amanita::dsp::LateralDecay;

using amanita::dsp::ReverbMode;
using amanita::dsp::ReverbParameters;
using amanita::dsp::SpatialDucker;
using amanita::dsp::StereoField;
using amanita::dsp::VeilCharacter;

[[nodiscard]] bool isPrime(int value)
{
    if (value < 2)
        return false;
    for (auto divisor = 2; divisor <= value / divisor; ++divisor)
        if (value % divisor == 0)
            return false;
    return true;
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct StereoRender
{
    std::vector<float> left;
    std::vector<float> right;
};

[[nodiscard]] HarmonicTail::PitchClassWeights harmonyWeights(
    std::initializer_list<std::size_t> pitchClasses)
{
    HarmonicTail::PitchClassWeights weights {};
    for (const auto pitchClass : pitchClasses)
        if (pitchClass < weights.size())
            weights[pitchClass] = 1.0f;
    return weights;
}

[[nodiscard]] double midiFrequency(int midiNote)
{
    return 440.0 * std::exp2(static_cast<double>(midiNote - 69) / 12.0);
}

[[nodiscard]] StereoRender renderImpulse(const ReverbParameters& parameters,
                                         double sampleRate,
                                         int sampleCount,
                                         int warmupSamples = 0)
{
    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 512);

    for (auto sample = 0; sample < warmupSamples; ++sample)
    {
        auto left = 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
    }

    StereoRender render {
        std::vector<float>(static_cast<std::size_t>(sampleCount), 0.0f),
        std::vector<float>(static_cast<std::size_t>(sampleCount), 0.0f)
    };
    render.left[0] = 1.0f;
    reverb.process(render.left.data(), render.right.data(), sampleCount);
    return render;
}

void testVeilDisperserKernel()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    std::array<double, sampleRates.size()> leftCentroidsMs {};
    std::array<double, sampleRates.size()> rightCentroidsMs {};

    for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
    {
        const auto sampleRate = sampleRates[rateIndex];
        const auto sampleCount = static_cast<int>(sampleRate);
        VeilCharacter veil;
        VeilCharacter repeat;
        veil.prepare(sampleRate);
        repeat.prepare(sampleRate);

        std::vector<double> stereoEnergy(static_cast<std::size_t>(sampleCount), 0.0);
        auto leftEnergy = 0.0;
        auto rightEnergy = 0.0;
        auto crossEnergy = 0.0;
        auto leftMoment = 0.0;
        auto rightMoment = 0.0;
        auto peak = 0.0f;
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            const auto input = sample == 0 ? 1.0f : 0.0f;
            const auto output = veil.processExcitation(input, input);
            const auto repeated = repeat.processExcitation(input, input);
            require(std::bit_cast<std::uint32_t>(output.left)
                        == std::bit_cast<std::uint32_t>(repeated.left)
                        && std::bit_cast<std::uint32_t>(output.right)
                            == std::bit_cast<std::uint32_t>(repeated.right),
                    "Veil disperser is not deterministic");
            require(std::isfinite(output.left) && std::isfinite(output.right),
                    "Veil disperser produced NaN/Inf");

            const auto leftSquared = static_cast<double>(output.left) * output.left;
            const auto rightSquared = static_cast<double>(output.right) * output.right;
            stereoEnergy[static_cast<std::size_t>(sample)] = leftSquared + rightSquared;
            leftEnergy += leftSquared;
            rightEnergy += rightSquared;
            crossEnergy += static_cast<double>(output.left) * output.right;
            leftMoment += static_cast<double>(sample) * leftSquared;
            rightMoment += static_cast<double>(sample) * rightSquared;
            peak = std::max({ peak, std::abs(output.left), std::abs(output.right) });

            if (sample == 0)
                require(std::abs(output.left) < 0.04f && std::abs(output.right) < 0.04f,
                        "Veil same-sample impulse is not sufficiently softened");
        }

        require(leftEnergy > 0.995 && leftEnergy < 1.005
                    && rightEnergy > 0.995 && rightEnergy < 1.005,
                "Veil all-pass cascade does not preserve impulse energy");
        const auto correlation = crossEnergy / std::sqrt(leftEnergy * rightEnergy);
        require(std::abs(correlation) < 0.20,
                "Veil left/right dispersers are insufficiently decorrelated");
        require(peak < 0.35f, "Veil disperser impulse crest is too high");

        leftCentroidsMs[rateIndex] = 1000.0 * leftMoment / leftEnergy / sampleRate;
        rightCentroidsMs[rateIndex] = 1000.0 * rightMoment / rightEnergy / sampleRate;
        require(leftCentroidsMs[rateIndex] > 30.0 && leftCentroidsMs[rateIndex] < 42.0
                    && rightCentroidsMs[rateIndex] > 30.0
                    && rightCentroidsMs[rateIndex] < 42.0,
                "Veil disperser energy centroid is outside the intended cloud window");

        const auto totalStereoEnergy = leftEnergy + rightEnergy;
        auto cumulativeEnergy = 0.0;
        auto percentile95Sample = 0;
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            cumulativeEnergy += stereoEnergy[static_cast<std::size_t>(sample)];
            if (cumulativeEnergy >= 0.95 * totalStereoEnergy)
            {
                percentile95Sample = sample;
                break;
            }
        }
        const auto percentile95Ms = 1000.0 * percentile95Sample / sampleRate;
        require(percentile95Ms > 45.0 && percentile95Ms < 80.0,
                "Veil disperser cloud is outside its intended p95 duration");
    }

    const auto [minimumLeft, maximumLeft] = std::minmax_element(leftCentroidsMs.begin(),
                                                                leftCentroidsMs.end());
    const auto [minimumRight, maximumRight] = std::minmax_element(rightCentroidsMs.begin(),
                                                                  rightCentroidsMs.end());
    std::cout << "[METRIC] Veil kernel centroid range: L=" << *minimumLeft << ".."
              << *maximumLeft << " ms, R=" << *minimumRight << ".."
              << *maximumRight << " ms\n";
    require(*maximumLeft - *minimumLeft < 1.0 && *maximumRight - *minimumRight < 1.0,
            "Veil timing changes audibly across sample rates");
}

void testVeilImpulseSofteningAndEnergy()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = static_cast<int>(sampleRate * 4.0);
    constexpr auto shapeWindowSamples = static_cast<int>(sampleRate * 0.120);
    constexpr auto leadingWindowSamples = static_cast<int>(sampleRate * 0.012);

    ReverbParameters parameters;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 3.0f;
    parameters.size = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.width = 1.0f;

    parameters.evolution = 0.0f;
    parameters.mode = ReverbMode::defaultMode;
    const auto lowDefaultRender = renderImpulse(parameters, sampleRate, sampleCount);
    parameters.mode = ReverbMode::veil;
    const auto lowVeilRender = renderImpulse(parameters, sampleRate, sampleCount);

    parameters.evolution = 1.0f;
    parameters.mode = ReverbMode::defaultMode;
    const auto defaultRender = renderImpulse(parameters, sampleRate, sampleCount);
    parameters.mode = ReverbMode::veil;
    const auto veilRender = renderImpulse(parameters, sampleRate, sampleCount);
    const auto veilRepeat = renderImpulse(parameters, sampleRate, sampleCount);

    struct ShapeMetrics
    {
        int onset = -1;
        double leadingFraction = 0.0;
        double crest = 0.0;
        double centroidMs = 0.0;
        double totalEnergy = 0.0;
        double lateEnergy = 0.0;
        float peak = 0.0f;
    };

    const auto analyse = [&] (const StereoRender& render)
    {
        ShapeMetrics metrics;
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            const auto index = static_cast<std::size_t>(sample);
            require(std::isfinite(render.left[index]) && std::isfinite(render.right[index]),
                    "Veil impulse comparison produced NaN/Inf");
            metrics.peak = std::max({ metrics.peak,
                                      std::abs(render.left[index]),
                                      std::abs(render.right[index]) });
            const auto energy = static_cast<double>(render.left[index]) * render.left[index]
                              + static_cast<double>(render.right[index]) * render.right[index];
            metrics.totalEnergy += energy;
            if (metrics.onset < 0
                && std::max(std::abs(render.left[index]), std::abs(render.right[index]))
                       > 1.0e-8f)
                metrics.onset = sample;
        }
        require(metrics.onset >= 0, "Veil impulse comparison is silent");

        auto leadingEnergy = 0.0;
        auto shapeEnergy = 0.0;
        auto shapeMoment = 0.0;
        auto maximumSampleEnergy = 0.0;
        const auto shapeEnd = std::min(sampleCount, metrics.onset + shapeWindowSamples);
        for (auto sample = metrics.onset; sample < shapeEnd; ++sample)
        {
            const auto index = static_cast<std::size_t>(sample);
            const auto energy = static_cast<double>(render.left[index]) * render.left[index]
                              + static_cast<double>(render.right[index]) * render.right[index];
            shapeEnergy += energy;
            shapeMoment += static_cast<double>(sample - metrics.onset) * energy;
            maximumSampleEnergy = std::max(maximumSampleEnergy, energy);
            if (sample < metrics.onset + leadingWindowSamples)
                leadingEnergy += energy;
        }
        metrics.leadingFraction = leadingEnergy / (shapeEnergy + 1.0e-30);
        metrics.crest = std::sqrt(maximumSampleEnergy
                                  / (shapeEnergy / static_cast<double>(shapeEnd - metrics.onset)
                                     + 1.0e-30));
        metrics.centroidMs = 1000.0 * shapeMoment / (shapeEnergy + 1.0e-30) / sampleRate;

        const auto lateStart = metrics.onset + static_cast<int>(sampleRate * 0.25);
        const auto lateEnd = std::min(sampleCount,
                                      metrics.onset + static_cast<int>(sampleRate * 2.5));
        for (auto sample = lateStart; sample < lateEnd; ++sample)
        {
            const auto index = static_cast<std::size_t>(sample);
            metrics.lateEnergy += static_cast<double>(render.left[index]) * render.left[index]
                                + static_cast<double>(render.right[index]) * render.right[index];
        }
        return metrics;
    };

    const auto lowDefaultShape = analyse(lowDefaultRender);
    const auto lowVeilShape = analyse(lowVeilRender);
    const auto defaultShape = analyse(defaultRender);
    const auto veilShape = analyse(veilRender);
    auto differenceEnergy = 0.0;
    auto referenceEnergy = 0.0;
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        require(std::bit_cast<std::uint32_t>(veilRender.left[index])
                    == std::bit_cast<std::uint32_t>(veilRepeat.left[index])
                    && std::bit_cast<std::uint32_t>(veilRender.right[index])
                        == std::bit_cast<std::uint32_t>(veilRepeat.right[index]),
                "Veil impulse render is not deterministic");
        const auto differenceLeft = static_cast<double>(veilRender.left[index])
                                  - defaultRender.left[index];
        const auto differenceRight = static_cast<double>(veilRender.right[index])
                                   - defaultRender.right[index];
        differenceEnergy += differenceLeft * differenceLeft + differenceRight * differenceRight;
        referenceEnergy += 0.5
                         * (static_cast<double>(veilRender.left[index]) * veilRender.left[index]
                            + static_cast<double>(veilRender.right[index]) * veilRender.right[index]
                            + static_cast<double>(defaultRender.left[index])
                                  * defaultRender.left[index]
                            + static_cast<double>(defaultRender.right[index])
                                  * defaultRender.right[index]);
    }

    const auto totalEnergyRatio = veilShape.totalEnergy / defaultShape.totalEnergy;
    const auto lateEnergyRatio = veilShape.lateEnergy / defaultShape.lateEnergy;
    const auto normalisedDifference = std::sqrt(differenceEnergy / referenceEnergy);
    std::cout << "[METRIC] Veil Evolution impulse: low leading Default="
              << lowDefaultShape.leadingFraction << " Veil=" << lowVeilShape.leadingFraction
              << ", low centroid Default=" << lowDefaultShape.centroidMs
              << " ms Veil=" << lowVeilShape.centroidMs
              << "; high leading Default="
              << defaultShape.leadingFraction << " Veil=" << veilShape.leadingFraction
              << ", crest Default=" << defaultShape.crest << " Veil=" << veilShape.crest
              << ", centroid Default=" << defaultShape.centroidMs
              << " ms Veil=" << veilShape.centroidMs
              << " ms, total ratio=" << totalEnergyRatio
              << ", late ratio=" << lateEnergyRatio
              << ", NRMS=" << normalisedDifference << '\n';

    require(lowVeilShape.leadingFraction <= lowDefaultShape.leadingFraction * 0.98
                && lowVeilShape.leadingFraction >= lowDefaultShape.leadingFraction * 0.85,
            "Low-Evolution Veil is either inaudible or too strong");
    require(lowVeilShape.centroidMs >= lowDefaultShape.centroidMs + 0.10
                && lowVeilShape.centroidMs <= lowDefaultShape.centroidMs + 2.5,
            "Low-Evolution Veil does not remain a subtle transient cloud");
    require(lowVeilShape.totalEnergy >= lowDefaultShape.totalEnergy * 0.85
                && lowVeilShape.totalEnergy <= lowDefaultShape.totalEnergy * 1.10,
            "Low-Evolution Veil changes impulse energy excessively");
    require(std::abs(veilShape.onset - defaultShape.onset)
                <= static_cast<int>(sampleRate * 0.0015),
            "Veil behaves like an unintended pre-delay");
    require(veilShape.leadingFraction <= defaultShape.leadingFraction * 0.80,
            "Veil does not sufficiently redistribute leading transient energy");
    require(veilShape.crest <= defaultShape.crest * 0.90,
            "Veil does not sufficiently reduce the early impulse crest");
    require(veilShape.centroidMs >= defaultShape.centroidMs + 3.0,
            "Veil does not move the attack energy centroid later");
    require(totalEnergyRatio >= 0.63 && totalEnergyRatio <= 1.58,
            "Veil changes total impulse energy excessively");
    require(lateEnergyRatio >= 0.50 && lateEnergyRatio <= 1.80,
            "Veil changes late reverb energy excessively");
    require(normalisedDifference > 0.10, "Veil is too similar to Default");
    require(veilShape.peak < 4.0f, "Veil impulse exceeded the safety range");
}

void testCharacterExcitationNormalisation()
{
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };
    constexpr std::array<float, 5> amounts { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    auto minimumBloomDb = 100.0;
    auto maximumBloomDb = -100.0;
    auto minimumVeilDb = 100.0;
    auto maximumVeilDb = -100.0;

    require(std::abs(CharacterExcitationNormalizer::gain(0.0f, 0.0f) - 1.0f)
                < 1.0e-7f,
            "Character excitation normalisation changes Default/Drift");
    require(std::abs(CharacterExcitationNormalizer::gain(1.0f, 0.0f)
                     - 1.1175711f)
                < 1.0e-5f,
            "Bloom endpoint normalisation gain changed unexpectedly");
    require(CharacterExcitationNormalizer::gain(0.0f, 0.5f) > 1.17f,
            "Veil midpoint is not perceptually compensated");

    for (const auto sampleRate : sampleRates)
    {
        for (const auto amount : amounts)
        {
            BloomCharacter bloom;
            VeilCharacter veil;
            bloom.prepare(sampleRate);
            veil.prepare(sampleRate);

            const auto warmupSamples = static_cast<int>(sampleRate);
            const auto measurementSamples = static_cast<int>(sampleRate * 2.0);
            const auto totalSamples = warmupSamples + measurementSamples;
            std::uint32_t leftNoise = 0x243f6a88u;
            std::uint32_t rightNoise = 0x85a308d3u;
            auto inputEnergy = 0.0;
            auto bloomEnergy = 0.0;
            auto veilEnergy = 0.0;
            const auto bloomGain = CharacterExcitationNormalizer::gain(amount, 0.0f);
            const auto veilGain = CharacterExcitationNormalizer::gain(0.0f, amount);

            for (auto sample = 0; sample < totalSamples; ++sample)
            {
                leftNoise = leftNoise * 1664525u + 1013904223u;
                rightNoise = rightNoise * 22695477u + 1u;
                const auto left = 0.05f
                    * static_cast<float>(static_cast<std::int32_t>(leftNoise))
                    / static_cast<float>(std::numeric_limits<std::int32_t>::max());
                const auto right = 0.05f
                    * static_cast<float>(static_cast<std::int32_t>(rightNoise))
                    / static_cast<float>(std::numeric_limits<std::int32_t>::max());
                const auto bloomFrame = bloom.processExcitation(left, right);
                const auto veilFrame = veil.processExcitation(left, right);
                const auto normalisedBloomLeft = bloomGain
                    * (left + amount * (bloomFrame.left - left));
                const auto normalisedBloomRight = bloomGain
                    * (right + amount * (bloomFrame.right - right));
                const auto normalisedVeilLeft = veilGain
                    * (left + amount * (veilFrame.left - left));
                const auto normalisedVeilRight = veilGain
                    * (right + amount * (veilFrame.right - right));

                require(std::isfinite(normalisedBloomLeft)
                            && std::isfinite(normalisedBloomRight)
                            && std::isfinite(normalisedVeilLeft)
                            && std::isfinite(normalisedVeilRight),
                        "Character excitation normalisation produced NaN/Inf");
                if (sample >= warmupSamples)
                {
                    inputEnergy += static_cast<double>(left) * left
                                 + static_cast<double>(right) * right;
                    bloomEnergy += static_cast<double>(normalisedBloomLeft)
                                       * normalisedBloomLeft
                                 + static_cast<double>(normalisedBloomRight)
                                       * normalisedBloomRight;
                    veilEnergy += static_cast<double>(normalisedVeilLeft)
                                      * normalisedVeilLeft
                                + static_cast<double>(normalisedVeilRight)
                                      * normalisedVeilRight;
                }
            }

            const auto bloomDb = 10.0 * std::log10(
                bloomEnergy / std::max(inputEnergy, 1.0e-20));
            const auto veilDb = 10.0 * std::log10(
                veilEnergy / std::max(inputEnergy, 1.0e-20));
            minimumBloomDb = std::min(minimumBloomDb, bloomDb);
            maximumBloomDb = std::max(maximumBloomDb, bloomDb);
            minimumVeilDb = std::min(minimumVeilDb, veilDb);
            maximumVeilDb = std::max(maximumVeilDb, veilDb);
            require(bloomDb >= -1.10 && bloomDb <= 0.20,
                    "Bloom excitation loudness is not energy matched");
            require(veilDb >= -1.60 && veilDb <= 0.20,
                    "Veil excitation loudness left the conservative matched range");
        }
    }

    for (const auto bloomAmount : { -1.0f, 0.0f, 0.5f, 1.0f, 2.0f })
    {
        for (const auto veilAmount : { -1.0f, 0.0f, 0.5f, 1.0f, 2.0f })
        {
            const auto gain = CharacterExcitationNormalizer::gain(
                bloomAmount, veilAmount);
            require(std::isfinite(gain)
                        && gain >= 0.75f
                        && gain <= CharacterExcitationNormalizer::maximumGain,
                    "Character excitation gain is invalid outside its nominal range");
        }
    }

    std::cout << "[METRIC] Character excitation match: Bloom="
              << minimumBloomDb << ".." << maximumBloomDb
              << " dB, Veil=" << minimumVeilDb << ".." << maximumVeilDb
              << " dB\n";
}

void testCharacterEvolutionLoudnessNormalisation()
{
    constexpr auto sampleRate = 48000.0;
    constexpr std::array<float, 5> evolutions { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    constexpr std::array modes { ReverbMode::bloom, ReverbMode::veil };

    for (const auto mode : modes)
    {
        std::array<double, evolutions.size()> wetRms {};
        for (std::size_t evolutionIndex = 0;
             evolutionIndex < evolutions.size();
             ++evolutionIndex)
        {
            ReverbParameters parameters;
            parameters.mode = mode;
            parameters.mix = 1.0f;
            parameters.decaySeconds = 3.0f;
            parameters.size = 1.0f;
            parameters.preDelayMs = 0.0f;
            parameters.lowCutHz = 20.0f;
            parameters.highDampingHz = 20000.0f;
            parameters.evolution = evolutions[evolutionIndex];
            parameters.width = 0.0f;
            parameters.ducking = 0.0f;
            parameters.harmony = 0.0f;

            FDNReverb reverb;
            reverb.setParameters(parameters);
            reverb.prepare(sampleRate, 512);

            const auto warmupSamples = static_cast<int>(sampleRate * 7.0);
            const auto measurementSamples = static_cast<int>(sampleRate * 2.0);
            const auto totalSamples = warmupSamples + measurementSamples;
            std::uint32_t leftNoise = 0x9e3779b9u;
            std::uint32_t rightNoise = 0x7f4a7c15u;
            auto outputEnergy = 0.0;
            auto peak = 0.0f;
            for (auto sample = 0; sample < totalSamples; ++sample)
            {
                leftNoise = leftNoise * 1664525u + 1013904223u;
                rightNoise = rightNoise * 22695477u + 1u;
                auto left = 0.025f
                    * static_cast<float>(static_cast<std::int32_t>(leftNoise))
                    / static_cast<float>(std::numeric_limits<std::int32_t>::max());
                auto right = 0.025f
                    * static_cast<float>(static_cast<std::int32_t>(rightNoise))
                    / static_cast<float>(std::numeric_limits<std::int32_t>::max());
                reverb.processSample(left, right);
                require(std::isfinite(left) && std::isfinite(right),
                        "Character loudness render produced NaN/Inf");
                peak = std::max({ peak, std::abs(left), std::abs(right) });
                if (sample >= warmupSamples)
                    outputEnergy += static_cast<double>(left) * left
                                  + static_cast<double>(right) * right;
            }

            wetRms[evolutionIndex] = std::sqrt(
                outputEnergy / (2.0 * static_cast<double>(measurementSamples)));
            require(wetRms[evolutionIndex] > 1.0e-7,
                    "Character loudness render is silent");
            require(peak < 4.0f,
                    "Character loudness render exceeded the safety range");
        }

        const auto [minimum, maximum] = std::minmax_element(
            wetRms.begin(), wetRms.end());
        const auto spreadDb = 20.0 * std::log10(*maximum / *minimum);
        std::cout << "[METRIC] Character Evolution wet RMS mode="
                  << static_cast<int>(mode) << ": "
                  << wetRms[0] << '/' << wetRms[1] << '/'
                  << wetRms[2] << '/' << wetRms[3] << '/'
                  << wetRms[4] << ", spread=" << spreadDb << " dB\n";
        require(spreadDb <= 1.0,
                "Evolution changes sustained Character loudness excessively");
    }
}

void testCurrentFieldGeometryAndRateSafety()
{
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };
    std::array<float, CurrentField::numLines> referenceDelay {};
    auto hasReference = false;
    auto maximumPitchCents = 0.0;

    for (const auto sampleRate : sampleRates)
    {
        CurrentField field;
        field.prepare(sampleRate);
        CurrentField::Frame previous;
        const auto sampleCount = static_cast<int>(sampleRate);
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            const auto frame = field.next(true);
            auto delaySum = 0.0f;
            auto dampingSum = 0.0f;
            auto delayDampingDot = 0.0f;
            for (std::size_t index = 0; index < CurrentField::numLines; ++index)
            {
                require(std::isfinite(frame.delay[index])
                            && std::isfinite(frame.damping[index])
                            && std::isfinite(frame.stereo[index]),
                        "Current Field produced NaN/Inf");
                require(std::abs(frame.delay[index]) <= 1.001f
                            && std::abs(frame.damping[index]) <= 1.001f
                            && std::abs(frame.stereo[index]) <= 1.001f,
                        "Current Field projection escaped its unit bound");
                delaySum += frame.delay[index];
                dampingSum += frame.damping[index];
                delayDampingDot += frame.delay[index] * frame.damping[index];

                if (sample > 0)
                {
                    const auto delayStep = CurrentField::maximumDelaySeconds
                        * static_cast<float>(sampleRate)
                        * (frame.delay[index] - previous.delay[index]);
                    const auto playbackRatio = std::max(0.5, 1.0 - delayStep);
                    maximumPitchCents = std::max(
                        maximumPitchCents,
                        std::abs(1200.0 * std::log2(playbackRatio)));
                }
            }
            require(std::abs(delaySum) <= 2.0e-5f
                        && std::abs(dampingSum) <= 2.0e-5f,
                    "Current Field lost its zero-mean geometry");
            require(std::abs(delayDampingDot) <= 2.0e-5f,
                    "Current delay and damping fields are not orthogonal");
            previous = frame;
        }

        if (!hasReference)
        {
            referenceDelay = previous.delay;
            hasReference = true;
        }
        else
        {
            for (std::size_t index = 0; index < CurrentField::numLines; ++index)
                require(std::abs(previous.delay[index] - referenceDelay[index])
                            <= 2.0e-5f,
                        "Current Field physical rate changes with sample rate");
        }
    }
    require(maximumPitchCents < 0.10,
            "Current Field delay slope can create audible chorus pitch");

    CurrentField inactive;
    CurrentField resetReference;
    inactive.prepare(48000.0);
    resetReference.prepare(48000.0);
    for (auto sample = 0; sample < 48000; ++sample)
    {
        const auto frame = inactive.next(false);
        for (const auto value : frame.delay)
            require(value == 0.0f,
                    "Inactive Current Field does not remain a zero-cost bypass");
    }
    const auto advancedFrame = inactive.next(true);
    const auto resetFrame = resetReference.next(true);
    auto inactiveAdvanceDifference = 0.0f;
    for (std::size_t index = 0; index < CurrentField::numLines; ++index)
        inactiveAdvanceDifference += std::abs(advancedFrame.delay[index]
                                               - resetFrame.delay[index]);
    require(inactiveAdvanceDifference > 0.01f,
            "Inactive Current Field phase restarts when the mode is entered");

    CurrentField coherence;
    coherence.prepare(48000.0);
    std::array<double, CurrentField::numLines * CurrentField::numLines> covariance {};
    std::array<float, CurrentField::numLines> previousSample {};
    constexpr auto stride = 64;
    const auto coherenceSamples = 48000 * 20;
    auto observations = 0;
    for (auto sample = 0; sample < coherenceSamples; ++sample)
    {
        const auto frame = coherence.next(true);
        if ((sample + 1) % stride != 0)
            continue;

        std::array<float, CurrentField::numLines> derivative {};
        for (std::size_t index = 0; index < CurrentField::numLines; ++index)
        {
            derivative[index] = frame.delay[index] - previousSample[index];
            previousSample[index] = frame.delay[index];
        }
        if (observations++ == 0)
            continue;

        for (std::size_t row = 0; row < CurrentField::numLines; ++row)
            for (std::size_t column = 0; column < CurrentField::numLines; ++column)
                covariance[row * CurrentField::numLines + column]
                    += static_cast<double>(derivative[row]) * derivative[column];
    }

    auto trace = 0.0;
    auto squaredFrobenius = 0.0;
    for (std::size_t row = 0; row < CurrentField::numLines; ++row)
    {
        trace += covariance[row * CurrentField::numLines + row];
        for (std::size_t column = 0; column < CurrentField::numLines; ++column)
        {
            const auto value = covariance[row * CurrentField::numLines + column];
            squaredFrobenius += value * value;
        }
    }
    require(trace > 1.0e-15 && squaredFrobenius > 1.0e-30,
            "Current Field trajectory did not move");
    const auto effectiveRank = trace * trace / squaredFrobenius;
    std::cout << "[METRIC] Current Field effective rank=" << effectiveRank
              << ", maximum pitch deviation=" << maximumPitchCents
              << " cent\n";
    require(effectiveRank <= 2.05,
            "Current Field behaves like independent per-line LFOs");

    const auto position = coherence.getLastFrame().stereo;
    auto openMidMovement = 0.0f;
    auto openSideMovement = 0.0f;
    auto monoSafeMidMovement = 0.0f;
    auto monoSafeSideMovement = 0.0f;
    for (std::size_t line = 0; line < CurrentField::numLines; ++line)
    {
        std::array<float, CurrentField::numLines> impulse {};
        impulse[line] = 1.0f;
        const auto legacyBase = StereoField::decodeLegacy(impulse);
        const auto legacyZero = StereoField::decodeCurrentLegacy(
            impulse, position, 0.0f);
        require(std::bit_cast<std::uint32_t>(legacyBase.left)
                    == std::bit_cast<std::uint32_t>(legacyZero.left)
                    && std::bit_cast<std::uint32_t>(legacyBase.right)
                    == std::bit_cast<std::uint32_t>(legacyZero.right),
                "Current open decoder changes its zero-depth endpoint");
        const auto legacyMoved = StereoField::decodeCurrentLegacy(
            impulse, position, 1.0f);
        const auto legacyEnergy = legacyMoved.left * legacyMoved.left
                                + legacyMoved.right * legacyMoved.right;
        require(std::abs(legacyEnergy - 0.25f) <= 2.0e-6f,
                "Current open decoder does not preserve per-line energy");
        openMidMovement += std::abs(
            0.5f * (legacyMoved.left + legacyMoved.right)
            - 0.5f * (legacyBase.left + legacyBase.right));
        openSideMovement += std::abs(
            0.5f * (legacyMoved.left - legacyMoved.right)
            - 0.5f * (legacyBase.left - legacyBase.right));

        const auto monoBase = StereoField::decode(impulse);
        const auto monoZero = StereoField::decodeCurrentMonoSafe(
            impulse, position, 0.0f);
        require(std::bit_cast<std::uint32_t>(monoBase.left)
                    == std::bit_cast<std::uint32_t>(monoZero.left)
                    && std::bit_cast<std::uint32_t>(monoBase.right)
                    == std::bit_cast<std::uint32_t>(monoZero.right),
                "Current mono-safe decoder changes its zero-depth endpoint");
        const auto monoMoved = StereoField::decodeCurrentMonoSafe(
            impulse, position, 1.0f);
        const auto monoEnergy = monoMoved.left * monoMoved.left
                              + monoMoved.right * monoMoved.right;
        require(std::abs(monoEnergy - 0.25f) <= 2.0e-6f,
                "Current mono-safe decoder does not preserve per-line energy");
        require(monoMoved.left * monoMoved.right >= -1.0e-7f
                    && std::abs(monoMoved.left + monoMoved.right) > 0.05f,
                "Current mono-safe decoder lets a line cancel in mono");
        monoSafeMidMovement += std::abs(
            0.5f * (monoMoved.left + monoMoved.right)
            - 0.5f * (monoBase.left + monoBase.right));
        monoSafeSideMovement += std::abs(
            0.5f * (monoMoved.left - monoMoved.right)
            - 0.5f * (monoBase.left - monoBase.right));
    }
    require(openMidMovement > 0.01f && openSideMovement > 0.01f,
            "Current open stereo field moves only Mid or only Side");
    require(monoSafeMidMovement > 0.01f && monoSafeSideMovement > 0.01f,
            "Current mono-safe stereo field moves only Mid or only Side");
}

void testFeedbackMatrix()
{
    std::uint32_t state = 0x12345678u;
    for (auto iteration = 0; iteration < 2048; ++iteration)
    {
        std::array<float, FDNReverb::numDelayLines> values {};
        auto inputNorm = 0.0;
        for (auto& value : values)
        {
            state = state * 1664525u + 1013904223u;
            value = static_cast<float>(static_cast<std::int32_t>(state))
                  / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            inputNorm += static_cast<double>(value) * value;
        }

        FDNReverb::applyFeedbackMatrix(values);
        auto outputNorm = 0.0;
        for (const auto value : values)
            outputNorm += static_cast<double>(value) * value;

        require(std::abs(inputNorm - outputNorm) < 1.0e-5,
                "Hadamard feedback matrix does not preserve energy");
    }
}

void testLateralDecayProfile()
{
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };
    constexpr std::array<float, 3> sizes { 0.15f, 1.0f, 2.0f };
    constexpr std::array<float, 3> decays { 0.2f, 5.0f, 30.0f };
    constexpr std::array<float, 3> widths { 0.0f, 1.0f, 2.0f };
    constexpr std::array<float, 3> evolutions { 0.0f, 0.5f, 1.0f };

    require(std::abs(LateralDecay::decayTimeScale(1, 1.0f, 1.0f) - 1.07f)
                < 1.0e-6f,
            "Width=100% does not produce the intended subtle lateral RT60 extension");
    require(std::abs(LateralDecay::decayTimeScale(1, 2.0f, 1.0f) - 1.14f)
                < 1.0e-6f,
            "Width=200% does not produce the intended maximum lateral RT60 extension");

    for (std::size_t line = 0; line < LateralDecay::numDelayLines; ++line)
    {
        require(LateralDecay::decayTimeScale(line, 0.0f, 1.0f) == 1.0f
                    && LateralDecay::decayTimeScale(line, 2.0f, 0.0f) == 1.0f,
                "Lateral Decay is not neutral at zero Width or Evolution");
        if (LateralDecay::isLateralLine(line))
            require(LateralDecay::decayTimeScale(line, 2.0f, 1.0f) > 1.0f,
                    "A lateral FDN line was not extended");
        else
            require(LateralDecay::decayTimeScale(line, 2.0f, 1.0f) == 1.0f,
                    "Lateral Decay changed a mono-core FDN line");
    }

    for (const auto sampleRate : sampleRates)
    {
        FDNReverb geometry;
        geometry.prepare(sampleRate, 512);
        const auto& delaySamples = geometry.getNominalDelaySamples();

        for (const auto size : sizes)
        {
            for (const auto decay : decays)
            {
                for (const auto width : widths)
                {
                    for (const auto evolution : evolutions)
                    {
                        for (std::size_t line = 0;
                             line < LateralDecay::numDelayLines;
                             ++line)
                        {
                            const auto delaySeconds = delaySamples[line] * size
                                                    / static_cast<float>(sampleRate);
                            const auto baseGain = LateralDecay::feedbackGain(
                                delaySeconds, decay, line, 0.0f, 0.0f);
                            const auto gain = LateralDecay::feedbackGain(
                                delaySeconds, decay, line, width, evolution);
                            require(std::isfinite(gain)
                                        && gain >= 0.0f
                                        && gain <= LateralDecay::maximumFeedbackGain,
                                    "Lateral Decay produced an invalid feedback gain");
                            if (LateralDecay::isLateralLine(line))
                                require(gain + 1.0e-7f >= baseGain,
                                        "Lateral Decay shortened a lateral FDN line");
                            else
                                require(std::bit_cast<std::uint32_t>(gain)
                                            == std::bit_cast<std::uint32_t>(baseGain),
                                        "Lateral Decay changed a mono-core feedback gain");
                        }
                    }
                }
            }
        }
    }

    constexpr float referenceDelaySeconds = 0.047f;
    constexpr float referenceDecaySeconds = 5.0f;
    const auto centreGain = LateralDecay::feedbackGain(
        referenceDelaySeconds, referenceDecaySeconds, 0, 2.0f, 1.0f);
    const auto lateralGain = LateralDecay::feedbackGain(
        referenceDelaySeconds, referenceDecaySeconds, 1, 2.0f, 1.0f);
    const auto centreRt60 = std::log(0.001f) * referenceDelaySeconds
                          / std::log(centreGain);
    const auto lateralRt60 = std::log(0.001f) * referenceDelaySeconds
                           / std::log(lateralGain);
    require(std::abs(centreRt60 - referenceDecaySeconds) < 1.0e-3f,
            "Lateral Decay changed the centre RT60");
    require(std::abs(lateralRt60
                     - referenceDecaySeconds
                           * (1.0f + LateralDecay::maximumLateralExtension))
                < 1.0e-3f,
            "Lateral Decay feedback gain does not encode its requested RT60");
}

void testMonoSafeStereoField()
{
    auto leftEnergy = 0.0;
    auto rightEnergy = 0.0;
    auto crossEnergy = 0.0;
    auto maximumSideSpan = 0.0;
    for (std::size_t line = 0; line < StereoField::numDelayLines; ++line)
    {
        std::array<float, StereoField::numDelayLines> basis {};
        basis[line] = 1.0f;
        const auto decoded = StereoField::decode(basis);
        const auto linePower = static_cast<double>(decoded.left) * decoded.left
                             + static_cast<double>(decoded.right) * decoded.right;
        const auto monoPowerRatio = std::pow(
            static_cast<double>(decoded.left + decoded.right), 2.0)
                                  / (2.0 * linePower);
        const auto sideSpan = std::abs(
            static_cast<double>(decoded.left - decoded.right))
                            / std::sqrt(linePower);

        require(std::abs(linePower - 0.25) < 1.0e-6,
                "Stereo decoder does not use equal-power line placement");
        require(decoded.left * decoded.right >= -1.0e-8f,
                "Stereo decoder does not use a shared L/R polarity");
        require(monoPowerRatio >= 0.49,
                "An FDN line disappears or loses excessive energy in mono");

        leftEnergy += static_cast<double>(decoded.left) * decoded.left;
        rightEnergy += static_cast<double>(decoded.right) * decoded.right;
        crossEnergy += static_cast<double>(decoded.left) * decoded.right;
        maximumSideSpan = std::max(maximumSideSpan, sideSpan);
    }

    require(std::abs(leftEnergy - 1.0) < 1.0e-6
                && std::abs(rightEnergy - 1.0) < 1.0e-6,
            "Stereo decoder changed the output-row energy");
    require(crossEnergy >= 0.10 && crossEnergy <= 0.20,
            "Stereo decoder is either excessively correlated or insufficiently mono-safe");
    require(maximumSideSpan >= 0.70,
            "Stereo decoder collapsed the delay lines toward the centre");

    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };
    std::array<double, sampleRates.size()> lowSideRatios {};
    std::array<double, sampleRates.size()> highSideRatios {};
    for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
    {
        const auto sampleRate = sampleRates[rateIndex];
        const auto measureSideRatio = [sampleRate] (double frequency)
        {
            StereoField field;
            field.prepare(sampleRate);
            const auto sampleCount = static_cast<int>(sampleRate * 1.5);
            const auto measurementStart = static_cast<int>(sampleRate * 0.5);
            auto inputEnergy = 0.0;
            auto outputEnergy = 0.0;
            auto monoPeak = 0.0f;
            for (auto sample = 0; sample < sampleCount; ++sample)
            {
                const auto phase = 2.0 * 3.14159265358979323846
                                 * frequency * static_cast<double>(sample)
                                 / sampleRate;
                const auto side = static_cast<float>(std::sin(phase));
                const auto output = field.applyWidth(side, -side, 1.0f);
                monoPeak = std::max(
                    monoPeak, std::abs(0.5f * (output.left + output.right)));
                if (sample >= measurementStart)
                {
                    const auto outputSide = 0.5 * static_cast<double>(
                        output.left - output.right);
                    inputEnergy += static_cast<double>(side) * side;
                    outputEnergy += outputSide * outputSide;
                }
            }

            require(monoPeak < 1.0e-7f,
                    "Sub Anchor changed the Mid/mono signal");
            return std::sqrt(outputEnergy / std::max(inputEnergy, 1.0e-20));
        };

        lowSideRatios[rateIndex] = measureSideRatio(60.0);
        highSideRatios[rateIndex] = measureSideRatio(2000.0);
        require(lowSideRatios[rateIndex] >= 0.82
                    && lowSideRatios[rateIndex] <= 0.90,
                "Sub Anchor does not gently narrow the low Side");
        require(highSideRatios[rateIndex] >= 0.99
                    && highSideRatios[rateIndex] <= 1.01,
                "Sub Anchor changes the high-frequency Side");

        StereoField field;
        field.prepare(sampleRate);
        const auto mid = field.applyWidth(0.3f, 0.3f, 2.0f);
        require(std::abs(mid.left - 0.3f) < 1.0e-7f
                    && std::abs(mid.right - 0.3f) < 1.0e-7f,
                "Sub Anchor or Width changed a pure Mid signal");
        const auto mono = field.applyWidth(0.4f, -0.2f, 0.0f);
        require(std::abs(mono.left - mono.right) < 1.0e-7f,
                "Width=0 is not mono after Sub Anchor");
    }

    const auto [minimumLow, maximumLow] = std::minmax_element(
        lowSideRatios.begin(), lowSideRatios.end());
    const auto [minimumHigh, maximumHigh] = std::minmax_element(
        highSideRatios.begin(), highSideRatios.end());
    require(*maximumLow - *minimumLow < 0.002
                && *maximumHigh - *minimumHigh < 0.002,
            "Sub Anchor response changes across sample rates");
    std::cout << "[METRIC] Stereo Field covariance=" << crossEnergy
              << ", 60 Hz Side=" << *minimumLow << ".." << *maximumLow
              << ", 2 kHz Side=" << *minimumHigh << ".." << *maximumHigh
              << '\n';
}

void testStereoFieldVoicingSwitch()
{
    constexpr float inverseSqrtEight = 0.35355339059327376220f;
    constexpr std::array<float, StereoField::numDelayLines> expectedLeft {
        1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f
    };
    constexpr std::array<float, StereoField::numDelayLines> expectedRight {
        1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f
    };

    for (std::size_t line = 0; line < StereoField::numDelayLines; ++line)
    {
        std::array<float, StereoField::numDelayLines> basis {};
        basis[line] = 1.0f;
        const auto legacy = StereoField::decodeLegacy(basis);
        require(std::abs(legacy.left - expectedLeft[line] * inverseSqrtEight)
                    < 1.0e-7f
                    && std::abs(legacy.right
                                - expectedRight[line] * inverseSqrtEight)
                           < 1.0e-7f,
                "Open stereo decoder does not reproduce the original sign matrix");
    }

    const auto mono = StereoField::applyLegacyWidth(0.6f, -0.2f, 0.0f);
    const auto normal = StereoField::applyLegacyWidth(0.6f, -0.2f, 1.0f);
    const auto wide = StereoField::applyLegacyWidth(0.6f, -0.2f, 2.0f);
    require(std::abs(mono.left - 0.2f) < 1.0e-7f
                && std::abs(mono.right - 0.2f) < 1.0e-7f
                && std::abs(normal.left - 0.6f) < 1.0e-7f
                && std::abs(normal.right + 0.2f) < 1.0e-7f
                && std::abs(wide.left - 1.0f) < 1.0e-7f
                && std::abs(wide.right + 0.6f) < 1.0e-7f,
            "Open stereo Width does not reproduce the original M/S path");

    constexpr auto sampleRate = 48000.0;
    constexpr auto totalSamples = 57600;
    constexpr auto toggleSample = 24000;
    constexpr auto postSettleSample = toggleSample + 7200;

    ReverbParameters newParameters;
    newParameters.mix = 1.0f;
    newParameters.decaySeconds = 8.0f;
    newParameters.size = 1.0f;
    newParameters.preDelayMs = 0.0f;
    newParameters.lowCutHz = 20.0f;
    newParameters.highDampingHz = 20000.0f;
    newParameters.evolution = 0.5f;
    newParameters.width = 1.0f;
    newParameters.ducking = 0.0f;
    newParameters.harmony = 0.0f;
    newParameters.autoHarmony = false;
    newParameters.monoSafeStereo = true;

    auto legacyParameters = newParameters;
    legacyParameters.monoSafeStereo = false;

    FDNReverb alwaysNew;
    FDNReverb alwaysLegacy;
    FDNReverb switched;
    alwaysNew.setParameters(newParameters);
    alwaysLegacy.setParameters(legacyParameters);
    switched.setParameters(legacyParameters);
    alwaysNew.prepare(sampleRate, 512);
    alwaysLegacy.prepare(sampleRate, 512);
    switched.prepare(sampleRate, 512);

    auto preErrorEnergy = 0.0;
    auto preReferenceEnergy = 0.0;
    auto postErrorEnergy = 0.0;
    auto postReferenceEnergy = 0.0;
    auto firstTransitionFraction = 0.0;
    auto peak = 0.0f;
    std::uint32_t noiseState = 0x51e2e0u;

    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        if (sample == toggleSample)
            switched.setParameters(newParameters);

        noiseState = noiseState * 1664525u + 1013904223u;
        const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                         / static_cast<float>(
                               std::numeric_limits<std::int32_t>::max());
        const auto excitationActive = sample < 9600;
        const auto inputLeft = excitationActive
            ? (sample == 0 ? 0.8f : 0.012f * noise)
            : 0.0f;
        const auto inputRight = excitationActive
            ? (sample == 0 ? -0.35f : -0.009f * noise)
            : 0.0f;

        auto newLeft = inputLeft;
        auto newRight = inputRight;
        auto legacyLeft = inputLeft;
        auto legacyRight = inputRight;
        auto switchedLeft = inputLeft;
        auto switchedRight = inputRight;
        alwaysNew.processSample(newLeft, newRight);
        alwaysLegacy.processSample(legacyLeft, legacyRight);
        switched.processSample(switchedLeft, switchedRight);

        require(std::isfinite(switchedLeft) && std::isfinite(switchedRight),
                "Mono Safe transition produced NaN/Inf");
        peak = std::max({ peak, std::abs(switchedLeft), std::abs(switchedRight) });

        if (sample < toggleSample)
        {
            const auto leftError = static_cast<double>(switchedLeft - legacyLeft);
            const auto rightError = static_cast<double>(switchedRight - legacyRight);
            preErrorEnergy += leftError * leftError + rightError * rightError;
            preReferenceEnergy += static_cast<double>(legacyLeft) * legacyLeft
                                + static_cast<double>(legacyRight) * legacyRight;
        }
        else if (sample >= postSettleSample)
        {
            const auto leftError = static_cast<double>(switchedLeft - newLeft);
            const auto rightError = static_cast<double>(switchedRight - newRight);
            postErrorEnergy += leftError * leftError + rightError * rightError;
            postReferenceEnergy += static_cast<double>(newLeft) * newLeft
                                 + static_cast<double>(newRight) * newRight;
        }

        if (sample == toggleSample)
        {
            const auto firstDistance = std::hypot(
                static_cast<double>(switchedLeft - legacyLeft),
                static_cast<double>(switchedRight - legacyRight));
            const auto fullDistance = std::hypot(
                static_cast<double>(newLeft - legacyLeft),
                static_cast<double>(newRight - legacyRight));
            require(fullDistance > 1.0e-8,
                    "Mono Safe test has no decoder contrast at the switch");
            firstTransitionFraction = firstDistance / fullDistance;
        }
    }

    const auto preNormalisedError = std::sqrt(
        preErrorEnergy / std::max(preReferenceEnergy, 1.0e-20));
    const auto postNormalisedError = std::sqrt(
        postErrorEnergy / std::max(postReferenceEnergy, 1.0e-20));
    require(preNormalisedError < 1.0e-7,
            "Mono Safe Off differs from the open stereo path");
    require(firstTransitionFraction < 0.02,
            "Mono Safe switch changes the tail abruptly");
    require(postNormalisedError < 1.0e-5,
            "Mono Safe switch does not settle on the protected stereo path");
    require(peak < 4.0f,
            "Mono Safe transition exceeded the safety range");

    std::cout << "[METRIC] Stereo Field voicing first fraction="
              << firstTransitionFraction
              << ", legacy NRMS=" << preNormalisedError
              << ", new-settled NRMS=" << postNormalisedError << '\n';
}

void testCurrentMonoSafeVoicingSwitch()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto totalSamples = 57600;
    constexpr auto toggleSample = 24000;
    constexpr auto postSettleSample = toggleSample + 7200;

    ReverbParameters protectedParameters;
    protectedParameters.mode = ReverbMode::current;
    protectedParameters.mix = 1.0f;
    protectedParameters.decaySeconds = 8.0f;
    protectedParameters.size = 1.0f;
    protectedParameters.preDelayMs = 0.0f;
    protectedParameters.lowCutHz = 20.0f;
    protectedParameters.highDampingHz = 20000.0f;
    protectedParameters.evolution = 1.0f;
    protectedParameters.width = 1.4f;
    protectedParameters.ducking = 0.0f;
    protectedParameters.harmony = 0.0f;
    protectedParameters.autoHarmony = false;
    protectedParameters.monoSafeStereo = true;

    auto openParameters = protectedParameters;
    openParameters.monoSafeStereo = false;

    FDNReverb alwaysProtected;
    FDNReverb alwaysOpen;
    FDNReverb switched;
    alwaysProtected.setParameters(protectedParameters);
    alwaysOpen.setParameters(openParameters);
    switched.setParameters(openParameters);
    alwaysProtected.prepare(sampleRate, 512);
    alwaysOpen.prepare(sampleRate, 512);
    switched.prepare(sampleRate, 512);

    auto preErrorEnergy = 0.0;
    auto preReferenceEnergy = 0.0;
    auto postErrorEnergy = 0.0;
    auto postReferenceEnergy = 0.0;
    auto firstTransitionFraction = 0.0;
    auto preSwitchPeak = 1.0e-3f;
    auto maximumResidualDerivative = 0.0f;
    auto previousResidualLeft = 0.0f;
    auto previousResidualRight = 0.0f;
    auto peak = 0.0f;
    std::uint32_t noiseState = 0x7ac31du;

    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        if (sample == toggleSample)
            switched.setParameters(protectedParameters);

        noiseState = noiseState * 1664525u + 1013904223u;
        const auto noise = static_cast<float>(
            static_cast<std::int32_t>(noiseState))
            / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        const auto excitationActive = sample < 9600;
        const auto inputLeft = excitationActive
            ? (sample == 0 ? 0.8f : 0.012f * noise)
            : 0.0f;
        const auto inputRight = excitationActive
            ? (sample == 0 ? -0.35f : -0.009f * noise)
            : 0.0f;

        auto protectedLeft = inputLeft;
        auto protectedRight = inputRight;
        auto openLeft = inputLeft;
        auto openRight = inputRight;
        auto switchedLeft = inputLeft;
        auto switchedRight = inputRight;
        alwaysProtected.processSample(protectedLeft, protectedRight);
        alwaysOpen.processSample(openLeft, openRight);
        switched.processSample(switchedLeft, switchedRight);

        require(std::isfinite(switchedLeft) && std::isfinite(switchedRight),
                "Current Mono Safe transition produced NaN/Inf");
        peak = std::max({ peak, std::abs(switchedLeft),
                         std::abs(switchedRight) });

        if (sample < toggleSample)
        {
            const auto leftError =
                static_cast<double>(switchedLeft - openLeft);
            const auto rightError =
                static_cast<double>(switchedRight - openRight);
            preErrorEnergy += leftError * leftError + rightError * rightError;
            preReferenceEnergy += static_cast<double>(openLeft) * openLeft
                                + static_cast<double>(openRight) * openRight;
            if (sample >= toggleSample - 1024)
                preSwitchPeak = std::max(
                    preSwitchPeak,
                    std::max(std::abs(openLeft), std::abs(openRight)));
        }
        else
        {
            const auto residualLeft = switchedLeft - openLeft;
            const auto residualRight = switchedRight - openRight;
            if (sample == toggleSample)
            {
                const auto firstDistance = std::hypot(
                    static_cast<double>(residualLeft),
                    static_cast<double>(residualRight));
                const auto fullDistance = std::hypot(
                    static_cast<double>(protectedLeft - openLeft),
                    static_cast<double>(protectedRight - openRight));
                require(fullDistance > 1.0e-8,
                        "Current Mono Safe test has no decoder contrast");
                firstTransitionFraction = firstDistance / fullDistance;
            }
            // Inspect the onset of the 30 ms morph. Once the crossfade is
            // established, the residual legitimately contains the moving
            // difference between two decorrelated stereo decoders.
            if (sample < toggleSample + 256)
            {
                maximumResidualDerivative = std::max({
                    maximumResidualDerivative,
                    std::abs(residualLeft - previousResidualLeft),
                    std::abs(residualRight - previousResidualRight)
                });
            }
            if (sample >= postSettleSample)
            {
                const auto leftError =
                    static_cast<double>(switchedLeft - protectedLeft);
                const auto rightError =
                    static_cast<double>(switchedRight - protectedRight);
                postErrorEnergy += leftError * leftError
                                 + rightError * rightError;
                postReferenceEnergy
                    += static_cast<double>(protectedLeft) * protectedLeft
                     + static_cast<double>(protectedRight) * protectedRight;
            }
            previousResidualLeft = residualLeft;
            previousResidualRight = residualRight;
        }
    }

    const auto preNormalisedError = std::sqrt(
        preErrorEnergy / std::max(preReferenceEnergy, 1.0e-20));
    const auto postNormalisedError = std::sqrt(
        postErrorEnergy / std::max(postReferenceEnergy, 1.0e-20));
    const auto derivativeLimit = std::max(2.0e-4f, 0.11f * preSwitchPeak);
    require(preNormalisedError < 1.0e-7,
            "Current Mono Safe Off differs before switching");
    require(firstTransitionFraction < 0.02,
            "Current Mono Safe switch changes the tail immediately");
    require(maximumResidualDerivative <= derivativeLimit,
            "Current Mono Safe morph changes too abruptly: derivative="
                + std::to_string(maximumResidualDerivative)
                + " limit=" + std::to_string(derivativeLimit));
    require(postNormalisedError < 1.0e-5,
            "Current Mono Safe switch does not settle on its protected path");
    require(peak < 4.0f,
            "Current Mono Safe transition exceeded the safety range");

    std::cout << "[METRIC] Current Mono Safe switch: first fraction="
              << firstTransitionFraction
              << ", max residual derivative=" << maximumResidualDerivative
              << ", settled NRMS=" << postNormalisedError << '\n';
}

void testStereoFieldFdnIntegration()
{
    // Fathom is left out: this pins the FDN's shared-sign decoder and Lateral
    // Decay.
    constexpr std::array modes {
        ReverbMode::defaultMode,
        ReverbMode::bloom,
        ReverbMode::drift,
        ReverbMode::veil,
        ReverbMode::current
    };
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };

    for (const auto mode : modes)
    {
        for (const auto sampleRate : sampleRates)
        {
            ReverbParameters parameters;
            parameters.mode = mode;
            parameters.mix = 1.0f;
            parameters.decaySeconds = 5.0f;
            parameters.size = 1.0f;
            parameters.preDelayMs = 0.0f;
            parameters.lowCutHz = 20.0f;
            parameters.highDampingHz = 20000.0f;
            parameters.evolution = 1.0f;
            parameters.ducking = 0.0f;
            parameters.harmony = 0.0f;
            parameters.monoSafeStereo = true;

            std::array<FDNReverb, 3> reverbs;
            constexpr std::array<float, 3> widths { 0.0f, 1.0f, 2.0f };
            for (std::size_t index = 0; index < reverbs.size(); ++index)
            {
                parameters.width = widths[index];
                reverbs[index].setParameters(parameters);
                reverbs[index].prepare(sampleRate, 512);
            }

            const auto sampleCount = static_cast<int>(sampleRate * 1.5);
            const auto excitationSamples = static_cast<int>(sampleRate * 0.20);
            const auto measurementStart = static_cast<int>(sampleRate * 0.30);
            std::array<double, reverbs.size()> midEnergy {};
            std::array<double, reverbs.size()> sideEnergy {};
            auto peak = 0.0f;
            auto widthZeroDifferencePeak = 0.0f;
            std::uint32_t noiseState = 0x51deca7u;

            for (auto sample = 0; sample < sampleCount; ++sample)
            {
                noiseState = noiseState * 1664525u + 1013904223u;
                const auto noise = static_cast<float>(
                    static_cast<std::int32_t>(noiseState))
                                 / static_cast<float>(
                                     std::numeric_limits<std::int32_t>::max());
                const auto active = sample < excitationSamples;
                const auto dryLeft = active
                    ? (sample == 0 ? 0.8f : 0.018f * noise)
                    : 0.0f;
                const auto dryRight = active
                    ? (sample == 0 ? -0.35f : -0.013f * noise)
                    : 0.0f;
                std::array<float, reverbs.size()> left {};
                std::array<float, reverbs.size()> right {};
                for (std::size_t index = 0; index < reverbs.size(); ++index)
                {
                    left[index] = dryLeft;
                    right[index] = dryRight;
                    reverbs[index].processSample(left[index], right[index]);
                    require(std::isfinite(left[index])
                                && std::isfinite(right[index]),
                            "Stereo Field FDN integration produced NaN/Inf");
                    peak = std::max(
                        { peak, std::abs(left[index]), std::abs(right[index]) });
                }

                widthZeroDifferencePeak = std::max(
                    widthZeroDifferencePeak, std::abs(left[0] - right[0]));
                if (sample >= measurementStart)
                {
                    for (std::size_t index = 0; index < reverbs.size(); ++index)
                    {
                        const auto mid = 0.5 * static_cast<double>(
                            left[index] + right[index]);
                        const auto side = 0.5 * static_cast<double>(
                            left[index] - right[index]);
                        midEnergy[index] += 2.0 * mid * mid;
                        sideEnergy[index] += 2.0 * side * side;
                    }
                }
            }

            require(peak < 4.0f,
                    "Stereo Field FDN integration exceeded the safety range");
            require(widthZeroDifferencePeak < 1.0e-6f,
                    "Width=0 does not produce a mono wet tail");
            require(midEnergy[0] > 1.0e-10,
                    "Stereo Field FDN integration tail is silent");
            for (std::size_t index = 1; index < midEnergy.size(); ++index)
            {
                const auto midEnergyRatio = midEnergy[index]
                                          / std::max(midEnergy[0], 1.0e-20);
                require(midEnergyRatio >= 0.85 && midEnergyRatio <= 1.20,
                        "Lateral Decay collapsed or over-amplified the FDN mono core");
            }

            const auto normalMonoRatio = midEnergy[1]
                / std::max(midEnergy[1] + sideEnergy[1], 1.0e-20);
            const auto wideMonoRatio = midEnergy[2]
                / std::max(midEnergy[2] + sideEnergy[2], 1.0e-20);
            const auto normalSideRatio = sideEnergy[1]
                / std::max(midEnergy[1] + sideEnergy[1], 1.0e-20);
            require(normalMonoRatio >= 0.25,
                    "Width=100% loses excessive FDN energy in mono");
            require(wideMonoRatio >= 0.125,
                    "Width=200% loses excessive FDN energy in mono");
            require(normalSideRatio >= 0.01,
                    "Mono-safe decoder collapsed the FDN tail toward mono");

            std::cout << "[METRIC] Stereo Field mode="
                      << static_cast<int>(mode)
                      << " rate=" << sampleRate
                      << " mono fold 100%="
                      << 10.0 * std::log10(normalMonoRatio)
                      << " dB 200%="
                      << 10.0 * std::log10(wideMonoRatio)
                      << " dB, Mid energy 100/200%="
                      << midEnergy[1] / std::max(midEnergy[0], 1.0e-20)
                      << '/'
                      << midEnergy[2] / std::max(midEnergy[0], 1.0e-20)
                      << '\n';
        }
    }
}

void testLateralDecayFdnTail()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = static_cast<int>(sampleRate * 6.5);
    constexpr auto earlyStart = static_cast<int>(sampleRate * 0.75);
    constexpr auto earlyEnd = static_cast<int>(sampleRate * 1.75);
    constexpr auto lateStart = static_cast<int>(sampleRate * 4.75);
    constexpr auto lateEnd = static_cast<int>(sampleRate * 5.75);

    struct TailMetrics
    {
        double earlyMid = 0.0;
        double earlySide = 0.0;
        double lateMid = 0.0;
        double lateSide = 0.0;
        float peak = 0.0f;
    };

    const auto measure = [] (float evolution, float width)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::defaultMode;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 8.0f;
        parameters.size = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = evolution;
        parameters.width = width;
        parameters.ducking = 0.0f;
        parameters.harmony = 0.0f;
        parameters.monoSafeStereo = false;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);
        TailMetrics metrics;
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            auto left = sample == 0 ? 1.0f : 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Lateral Decay FDN tail produced NaN/Inf");
            metrics.peak = std::max(
                { metrics.peak, std::abs(left), std::abs(right) });

            const auto mid = 0.5 * static_cast<double>(left + right);
            const auto side = 0.5 * static_cast<double>(left - right);
            if (sample >= earlyStart && sample < earlyEnd)
            {
                metrics.earlyMid += mid * mid;
                metrics.earlySide += side * side;
            }
            if (sample >= lateStart && sample < lateEnd)
            {
                metrics.lateMid += mid * mid;
                metrics.lateSide += side * side;
            }
        }
        return metrics;
    };

    const auto neutral = measure(0.0f, 2.0f);
    const auto monoReference = measure(1.0f, 0.0f);
    const auto lateral = measure(1.0f, 2.0f);
    for (const auto* metrics : { &neutral, &lateral })
    {
        require(metrics->earlyMid > 1.0e-12
                    && metrics->earlySide > 1.0e-12
                    && metrics->lateMid > 1.0e-16
                    && metrics->lateSide > 1.0e-16,
                "Lateral Decay FDN measurement window is silent");
        require(metrics->peak < 4.0f,
                "Lateral Decay FDN tail exceeded the safety range");
    }
    require(monoReference.earlyMid > 1.0e-12
                && monoReference.lateMid > 1.0e-16
                && monoReference.peak < 4.0f,
            "Lateral Decay mono-core reference is silent or unstable");

    const auto retentionContrast = [] (const TailMetrics& metrics)
    {
        const auto midRetention = metrics.lateMid / metrics.earlyMid;
        const auto sideRetention = metrics.lateSide / metrics.earlySide;
        return sideRetention / std::max(midRetention, 1.0e-20);
    };
    const auto neutralContrast = retentionContrast(neutral);
    const auto lateralContrast = retentionContrast(lateral);
    const auto monoCoreRatio = lateral.lateMid / monoReference.lateMid;
    std::cout << "[METRIC] Lateral Decay retention contrast neutral/active="
              << neutralContrast << '/' << lateralContrast
              << ", late mono-core ratio=" << monoCoreRatio << '\n';

    require(lateralContrast >= neutralContrast * 1.03,
            "Width/Evolution coupling does not retain the Side relative to the mono core");
    require(monoCoreRatio >= 0.80 && monoCoreRatio <= 2.00,
            "Lateral Decay collapsed or over-amplified the late mono core");
}

void testDriftSuperpositionLinearity()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto totalSamples = 144000;
    constexpr auto measurementStart = 48000;
    constexpr auto twoPi = 6.28318530717958647692;
    constexpr std::array<float, 3> evolutionValues { 0.0f, 0.5f, 1.0f };
    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 10.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 80.0f;
    parameters.highDampingHz = 9000.0f;

    for (const auto evolution : evolutionValues)
    {
        parameters.evolution = evolution;
        FDNReverb first;
        FDNReverb second;
        FDNReverb summed;
        for (auto* reverb : { &first, &second, &summed })
        {
            reverb->setParameters(parameters);
            reverb->prepare(sampleRate, 64);
        }

        auto errorEnergy = 0.0;
        auto referenceEnergy = 0.0;
        for (auto sample = 0; sample < totalSamples; ++sample)
        {
            const auto time = static_cast<double>(sample) / sampleRate;
            auto firstLeft = static_cast<float>(0.035 * std::sin(twoPi * 1000.0 * time));
            auto firstRight = 0.71f * firstLeft;
            auto secondLeft = static_cast<float>(0.027 * std::sin(
                twoPi * 4500.0 * time + 0.31));
            auto secondRight = -0.63f * secondLeft;
            auto summedLeft = firstLeft + secondLeft;
            auto summedRight = firstRight + secondRight;
            first.processSample(firstLeft, firstRight);
            second.processSample(secondLeft, secondRight);
            summed.processSample(summedLeft, summedRight);

            require(std::isfinite(summedLeft) && std::isfinite(summedRight),
                    "Drift superposition render produced NaN/Inf");
            if (sample >= measurementStart)
            {
                const auto leftError = static_cast<double>(summedLeft - firstLeft - secondLeft);
                const auto rightError = static_cast<double>(summedRight - firstRight - secondRight);
                errorEnergy += leftError * leftError + rightError * rightError;
                referenceEnergy += static_cast<double>(summedLeft) * summedLeft
                                 + static_cast<double>(summedRight) * summedRight;
            }
        }

        require(referenceEnergy > 1.0e-12, "Drift superposition reference became silent");
        const auto normalisedError = std::sqrt(errorEnergy / referenceEnergy);
        std::cout << "[METRIC] Drift superposition Evolution=" << evolution
                  << " NRMS=" << normalisedError << '\n';
        require(normalisedError <= 2.0e-4,
                "Drift is signal-dependent/nonlinear at Evolution="
                    + std::to_string(evolution) + ": superposition NRMS="
                    + std::to_string(normalisedError));
    }
}

void testDriftCharacterIdentityAndSubBypass()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<float, 2> subFrequencies { 55.0f, 80.0f };
    constexpr float inverseSqrtEight = 0.35355339059327376220f;
    constexpr float twoPi = 6.28318530717958647692f;
    constexpr std::array<std::array<float, DriftCharacter::numFeedbackLines>, 2> axes {{
        { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f },
        { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f }
    }};

    for (const auto sampleRate : sampleRates)
    {
        DriftCharacter identity;
        identity.prepare(sampleRate);
        std::uint32_t identityState = 0x3c6ef372u;
        for (auto iteration = 0; iteration < 4096; ++iteration)
        {
            std::array<float, DriftCharacter::numFeedbackLines> feedback {};
            for (auto& value : feedback)
            {
                identityState = identityState * 1664525u + 1013904223u;
                value = 0.75f
                      * static_cast<float>(static_cast<std::int32_t>(identityState))
                      / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            }
            const auto original = feedback;
            identity.processFeedback(feedback, 0.0f, 1.0f);
            for (std::size_t index = 0; index < feedback.size(); ++index)
                require(std::bit_cast<std::uint32_t>(feedback[index])
                            == std::bit_cast<std::uint32_t>(original[index]),
                        "Unified Drift amount=0 is not bit-transparent");

            identity.processFeedback(feedback, 1.0f, 1.0f, false);
            for (std::size_t index = 0; index < feedback.size(); ++index)
                require(std::bit_cast<std::uint32_t>(feedback[index])
                            == std::bit_cast<std::uint32_t>(original[index]),
                        "Inactive unified Drift changed the feedback output");
        }

        for (const auto frequency : subFrequencies)
        {
            for (const auto& axis : axes)
            {
                DriftCharacter drift;
                drift.prepare(sampleRate);
                const auto coversFullSlowCycle = sampleRate == 48000.0;
                const auto totalSamples = static_cast<int>(
                    sampleRate * (coversFullSlowCycle ? 70.0 : 2.0));
                const auto measurementStart = static_cast<int>(
                    sampleRate * (coversFullSlowCycle ? 1.0 : 0.5));
                const auto measurementWindow = coversFullSlowCycle
                    ? static_cast<int>(sampleRate)
                    : totalSamples - measurementStart;
                auto inputEnergy = 0.0;
                auto outputEnergy = 0.0;
                auto minimumGainDb = std::numeric_limits<double>::infinity();
                auto maximumGainDb = -std::numeric_limits<double>::infinity();
                for (auto sample = 0; sample < totalSamples; ++sample)
                {
                    const auto tone = std::sin(twoPi * frequency
                                               * static_cast<float>(sample)
                                               / static_cast<float>(sampleRate));
                    std::array<float, DriftCharacter::numFeedbackLines> feedback {};
                    for (std::size_t index = 0; index < feedback.size(); ++index)
                        feedback[index] = inverseSqrtEight * axis[index] * tone;
                    if (sample >= measurementStart)
                        inputEnergy += static_cast<double>(tone) * tone;
                    drift.processFeedback(feedback, 1.0f, 1.0f);
                    if (sample >= measurementStart)
                        for (const auto value : feedback)
                            outputEnergy += static_cast<double>(value) * value;

                    const auto measuredSamples = sample + 1 - measurementStart;
                    if (sample >= measurementStart
                        && measuredSamples % measurementWindow == 0)
                    {
                        const auto gainDb = 10.0 * std::log10(
                            (outputEnergy + 1.0e-30) / (inputEnergy + 1.0e-30));
                        minimumGainDb = std::min(minimumGainDb, gainDb);
                        maximumGainDb = std::max(maximumGainDb, gainDb);
                        inputEnergy = 0.0;
                        outputEnergy = 0.0;
                    }
                }
                require(maximumGainDb <= 0.05 && minimumGainDb >= -0.50,
                        "Drift 2 sub bypass changed " + std::to_string(frequency)
                            + " Hz excessively at " + std::to_string(sampleRate)
                            + " Hz sample rate: min=" + std::to_string(minimumGainDb)
                            + " dB max=" + std::to_string(maximumGainDb) + " dB");
            }
        }
    }
}

void driftRegressionFft(std::vector<std::complex<double>>& values)
{
    const auto size = values.size();
    for (std::size_t index = 1, reversed = 0; index < size; ++index)
    {
        auto bit = size >> 1;
        while ((reversed & bit) != 0)
        {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed)
            std::swap(values[index], values[reversed]);
    }

    constexpr auto twoPi = 6.28318530717958647692;
    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const auto step = std::polar(1.0, -twoPi / static_cast<double>(length));
        for (std::size_t offset = 0; offset < size; offset += length)
        {
            auto phase = std::complex<double>(1.0, 0.0);
            for (std::size_t index = 0; index < length / 2; ++index)
            {
                const auto even = values[offset + index];
                const auto odd = values[offset + index + length / 2] * phase;
                values[offset + index] = even + odd;
                values[offset + index + length / 2] = even - odd;
                phase *= step;
            }
        }
    }
}

[[nodiscard]] double driftHighBandFraction(const StereoRender& render,
                                           double sampleRate,
                                           std::size_t startSample)
{
    constexpr std::size_t fftSize = 65536;
    constexpr auto twoPi = 6.28318530717958647692;
    auto totalEnergy = 0.0;
    auto highBandEnergy = 0.0;
    for (const auto* channel : { &render.left, &render.right })
    {
        std::vector<std::complex<double>> spectrum(fftSize);
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            const auto window = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(index) / static_cast<double>(fftSize - 1));
            spectrum[index] = static_cast<double>((*channel)[startSample + index]) * window;
        }
        driftRegressionFft(spectrum);
        for (std::size_t bin = 1; bin <= fftSize / 2; ++bin)
        {
            const auto frequency = static_cast<double>(bin) * sampleRate / fftSize;
            const auto energy = std::norm(spectrum[bin]);
            totalEnergy += energy;
            if (frequency >= 6000.0 && frequency < 10000.0)
                highBandEnergy += energy;
        }
    }
    return highBandEnergy / (totalEnergy + 1.0e-300);
}

class DriftVocalSource
{
public:
    DriftVocalSource()
    {
        for (std::size_t index = 0; index < weights_.size(); ++index)
        {
            const auto harmonic = static_cast<double>(index + 1);
            const auto frequency = 200.0 * harmonic;
            const auto formant1 = std::exp(-0.5 * std::pow((frequency - 760.0) / 190.0, 2.0));
            const auto formant2 = std::exp(-0.5 * std::pow((frequency - 1180.0) / 260.0, 2.0));
            const auto formant3 = std::exp(-0.5 * std::pow((frequency - 2850.0) / 480.0, 2.0));
            weights_[index] = (0.08 + 0.95 * formant1
                                   + 0.75 * formant2 + 0.55 * formant3) / harmonic;
            weightSum_ += weights_[index];
        }
    }

    [[nodiscard]] float sample(int index, int length, double sampleRate) const noexcept
    {
        if (index >= length)
            return 0.0f;
        constexpr auto twoPi = 6.28318530717958647692;
        const auto time = static_cast<double>(index) / sampleRate;
        const auto attack = std::min(1.0, time / 0.05);
        const auto remaining = static_cast<double>(length - index) / sampleRate;
        const auto envelope = attack * std::min(1.0, remaining / 0.15);
        auto voice = 0.0;
        for (std::size_t harmonicIndex = 0; harmonicIndex < weights_.size(); ++harmonicIndex)
        {
            const auto harmonic = static_cast<double>(harmonicIndex + 1);
            voice += weights_[harmonicIndex] * std::sin(
                twoPi * 200.0 * harmonic * time + 0.17 * harmonic);
        }
        return static_cast<float>(0.42 * envelope * voice / weightSum_);
    }

private:
    std::array<double, 29> weights_ {};
    double weightSum_ = 0.0;
};

void testDriftBandLimitedVocalTail()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto excitationSamples = 192000;
    constexpr auto totalSamples = 288000;
    constexpr std::array<float, 2> evolutionValues { 0.30f, 1.0f };
    constexpr std::size_t analysisStart = excitationSamples + 9600;
    const DriftVocalSource vocal;

    for (const auto evolution : evolutionValues)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::drift;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 15.0f;
        parameters.size = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 80.0f;
        parameters.highDampingHz = 9000.0f;
        parameters.evolution = evolution;
        parameters.width = 1.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);
        StereoRender render {
            std::vector<float>(totalSamples, 0.0f),
            std::vector<float>(totalSamples, 0.0f)
        };
        for (auto sample = 0; sample < totalSamples; ++sample)
        {
            const auto input = vocal.sample(sample, excitationSamples, sampleRate);
            auto left = input;
            auto right = input;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Band-limited Drift vocal render produced NaN/Inf");
            render.left[static_cast<std::size_t>(sample)] = left;
            render.right[static_cast<std::size_t>(sample)] = right;
        }

        const auto highBandFraction = driftHighBandFraction(
            render, sampleRate, analysisStart);
        const auto highBandDb = 10.0 * std::log10(highBandFraction + 1.0e-300);
        std::cout << "[METRIC] Drift band-limited vocal Evolution=" << evolution
                  << " 6-10 kHz fraction=" << highBandDb << " dB\n";
        require(highBandFraction <= 1.0e-6,
                "Drift generated excessive 6-10 kHz energy from a <=5.8 kHz vocal input: "
                    + std::to_string(highBandDb) + " dB");
    }
}

void testDriftFreezeLinearityAndBandLimitedTail()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto excitationSamples = 192000;
    constexpr auto frozenSamples = 384000;
    constexpr auto rampSettledSample = 12000;
    constexpr auto twoPi = 6.28318530717958647692;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 15.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 80.0f;
    parameters.highDampingHz = 9000.0f;
    parameters.evolution = 1.0f;

    std::array<FDNReverb, 3> superposition;
    for (auto& reverb : superposition)
    {
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 64);
    }
    for (auto sample = 0; sample < excitationSamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto firstLeft = static_cast<float>(0.08 * std::sin(twoPi * 1000.0 * time));
        const auto firstRight = 0.71f * firstLeft;
        const auto secondLeft = static_cast<float>(0.065 * std::sin(
            twoPi * 4500.0 * time + 0.31));
        const auto secondRight = -0.63f * secondLeft;
        std::array<float, 3> left { firstLeft, secondLeft, firstLeft + secondLeft };
        std::array<float, 3> right { firstRight, secondRight, firstRight + secondRight };
        for (std::size_t index = 0; index < superposition.size(); ++index)
            superposition[index].processSample(left[index], right[index]);
    }
    parameters.freeze = true;
    for (auto& reverb : superposition)
        reverb.setParameters(parameters);
    auto errorEnergy = 0.0;
    auto referenceEnergy = 0.0;
    for (auto sample = 0; sample < frozenSamples / 2; ++sample)
    {
        std::array<float, 3> left {};
        std::array<float, 3> right {};
        for (std::size_t index = 0; index < superposition.size(); ++index)
            superposition[index].processSample(left[index], right[index]);
        if (sample >= rampSettledSample)
        {
            const auto leftError = static_cast<double>(left[2] - left[0] - left[1]);
            const auto rightError = static_cast<double>(right[2] - right[0] - right[1]);
            errorEnergy += leftError * leftError + rightError * rightError;
            referenceEnergy += static_cast<double>(left[2]) * left[2]
                             + static_cast<double>(right[2]) * right[2];
        }
    }
    require(referenceEnergy > 1.0e-12, "Drift Freeze superposition reference became silent");
    const auto normalisedError = std::sqrt(errorEnergy / referenceEnergy);
    require(normalisedError <= 2.0e-4,
            "Fully engaged Drift Freeze is signal-dependent/nonlinear: NRMS="
                + std::to_string(normalisedError));

    parameters.freeze = false;
    FDNReverb vocalReverb;
    vocalReverb.setParameters(parameters);
    vocalReverb.prepare(sampleRate, 512);
    StereoRender vocalRender {
        std::vector<float>(excitationSamples + frozenSamples, 0.0f),
        std::vector<float>(excitationSamples + frozenSamples, 0.0f)
    };
    const DriftVocalSource vocal;
    auto peak = 0.0f;
    auto earlyEnergy = 0.0;
    auto lateEnergy = 0.0;
    for (auto sample = 0; sample < excitationSamples + frozenSamples; ++sample)
    {
        if (sample == excitationSamples)
        {
            parameters.freeze = true;
            vocalReverb.setParameters(parameters);
        }
        auto left = vocal.sample(sample, excitationSamples, sampleRate);
        auto right = left;
        vocalReverb.processSample(left, right);
        require(std::isfinite(left) && std::isfinite(right),
                "Drift Freeze vocal tail produced NaN/Inf");
        peak = std::max({ peak, std::abs(left), std::abs(right) });
        vocalRender.left[static_cast<std::size_t>(sample)] = left;
        vocalRender.right[static_cast<std::size_t>(sample)] = right;
        const auto frozenOffset = sample - excitationSamples;
        const auto energy = static_cast<double>(left) * left
                          + static_cast<double>(right) * right;
        if (frozenOffset >= 24000 && frozenOffset < 72000)
            earlyEnergy += energy;
        if (frozenOffset >= 288000 && frozenOffset < 336000)
            lateEnergy += energy;
    }

    const auto highBandFraction = driftHighBandFraction(
        vocalRender, sampleRate, excitationSamples + 24000);
    const auto highBandDb = 10.0 * std::log10(highBandFraction + 1.0e-300);
    const auto lateEarlyRatio = lateEnergy / (earlyEnergy + 1.0e-300);
    std::cout << "[METRIC] Drift Freeze: superposition NRMS=" << normalisedError
              << ", vocal 6-10 kHz=" << highBandDb
              << " dB, late/early=" << lateEarlyRatio << '\n';
    require(highBandFraction <= 1.0e-6,
            "Drift Freeze generated excessive 6-10 kHz vocal energy: "
                + std::to_string(highBandDb) + " dB");
    require(earlyEnergy > 1.0e-10, "Drift Freeze vocal tail became silent");
    require(lateEarlyRatio >= 0.20,
            "Drift Freeze vocal tail decays too quickly: late/early="
                + std::to_string(lateEarlyRatio));
    require(lateEnergy <= earlyEnergy * 1.25 + 1.0e-12,
            "Drift Freeze vocal tail grows over time");
    require(peak < 4.0f, "Drift Freeze vocal tail exceeded the safety range");
}

void testDelayGeometryAndSampleRates()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    std::array<double, sampleRates.size()> onsetSeconds {};

    for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
    {
        ReverbParameters parameters;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 3.0f;
        parameters.size = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 0.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRates[rateIndex], 512);

        const auto& delays = reverb.getNominalDelaySamples();
        for (std::size_t index = 0; index < delays.size(); ++index)
        {
            const auto integerDelay = static_cast<int>(std::lround(delays[index]));
            require(isPrime(integerDelay), "Nominal FDN delay is not prime");
            require(delays[index] / sampleRates[rateIndex] > 0.029,
                    "Nominal FDN delay is unexpectedly short");
            require(delays[index] / sampleRates[rateIndex] < 0.073,
                    "Nominal FDN delay is unexpectedly long");
            for (std::size_t other = index + 1; other < delays.size(); ++other)
                require(std::lround(delays[index]) != std::lround(delays[other]),
                        "Nominal FDN delays are not distinct");
        }

        const auto sampleCount = static_cast<int>(sampleRates[rateIndex] * 0.1);
        auto firstWetSample = -1;
        for (auto sample = 0; sample < sampleCount; ++sample)
        {
            auto left = sample == 0 ? 1.0f : 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Non-finite sample in sample-rate impulse test");
            if (firstWetSample < 0 && std::max(std::abs(left), std::abs(right)) > 1.0e-8f)
                firstWetSample = sample;
        }

        require(firstWetSample >= 0, "Impulse response is silent");
        onsetSeconds[rateIndex] = firstWetSample / sampleRates[rateIndex];
    }

    const auto [minimum, maximum] = std::minmax_element(onsetSeconds.begin(), onsetSeconds.end());
    require(*maximum - *minimum < 0.001,
            "Impulse onset changes by more than 1 ms across sample rates");
}

void testMinimumSizeSampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    // Fathom is left out: the onset window is the FDN's shortest line at minimum
    // Size.
    constexpr std::array modes {
        ReverbMode::defaultMode,
        ReverbMode::bloom,
        ReverbMode::drift,
        ReverbMode::veil,
        ReverbMode::current
    };

    for (const auto mode : modes)
    {
        std::array<double, sampleRates.size()> onsetSeconds {};
        for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
        {
            const auto sampleRate = sampleRates[rateIndex];
            ReverbParameters parameters;
            parameters.mode = mode;
            parameters.mix = 1.0f;
            parameters.decaySeconds = 3.0f;
            parameters.size = 0.15f;
            parameters.preDelayMs = 0.0f;
            parameters.lowCutHz = 20.0f;
            parameters.highDampingHz = 20000.0f;
            parameters.evolution = 1.0f;
            parameters.width = 1.0f;
            parameters.ducking = 0.0f;
            parameters.harmony = 0.0f;

            const auto sampleCount = static_cast<int>(sampleRate * 0.25);
            const auto render = renderImpulse(parameters, sampleRate, sampleCount);
            auto firstWetSample = -1;
            auto peak = 0.0f;
            auto energy = 0.0;
            for (auto sample = 0; sample < sampleCount; ++sample)
            {
                const auto index = static_cast<std::size_t>(sample);
                const auto left = render.left[index];
                const auto right = render.right[index];
                require(std::isfinite(left) && std::isfinite(right),
                        "Minimum Size produced NaN/Inf");
                const auto magnitude = std::max(std::abs(left), std::abs(right));
                if (firstWetSample < 0 && magnitude > 1.0e-8f)
                    firstWetSample = sample;
                peak = std::max(peak, magnitude);
                energy += static_cast<double>(left) * left
                        + static_cast<double>(right) * right;
            }

            require(firstWetSample >= 0, "Minimum Size impulse response is silent");
            onsetSeconds[rateIndex] = firstWetSample / sampleRate;
            require(onsetSeconds[rateIndex] >= 0.0025
                        && onsetSeconds[rateIndex] <= 0.0065,
                    "Minimum Size onset left the safe near-immediate range");
            require(energy > 1.0e-7, "Minimum Size tail has no energy");
            require(peak < 4.0f, "Minimum Size exceeded the safety range");
        }

        const auto [minimum, maximum] = std::minmax_element(
            onsetSeconds.begin(), onsetSeconds.end());
        require(*maximum - *minimum < 0.0005,
                "Minimum Size onset changes across sample rates");
    }

    ReverbParameters belowMinimum;
    belowMinimum.mix = 1.0f;
    belowMinimum.decaySeconds = 3.0f;
    belowMinimum.size = 0.0f;
    belowMinimum.preDelayMs = 0.0f;
    belowMinimum.lowCutHz = 20.0f;
    belowMinimum.highDampingHz = 20000.0f;
    belowMinimum.evolution = 1.0f;
    auto atMinimum = belowMinimum;
    atMinimum.size = 0.15f;
    const auto clampedRender = renderImpulse(belowMinimum, 48000.0, 12000);
    const auto minimumRender = renderImpulse(atMinimum, 48000.0, 12000);
    for (std::size_t sample = 0; sample < clampedRender.left.size(); ++sample)
    {
        require(std::bit_cast<std::uint32_t>(clampedRender.left[sample])
                    == std::bit_cast<std::uint32_t>(minimumRender.left[sample])
                    && std::bit_cast<std::uint32_t>(clampedRender.right[sample])
                        == std::bit_cast<std::uint32_t>(minimumRender.right[sample]),
                "Size values below the safe floor are not clamped deterministically");
    }
}

void testNaturalDecayExcitation()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto measurementSeconds = 4.0;
    constexpr std::array<float, 4> decaySeconds { 5.0f, 10.0f, 20.0f, 30.0f };
    std::array<double, decaySeconds.size()> wetRms {};
    std::array<double, decaySeconds.size()> earlyDirectEnergy {};

    for (std::size_t decayIndex = 0; decayIndex < decaySeconds.size(); ++decayIndex)
    {
        ReverbParameters parameters;
        parameters.mix = 1.0f;
        parameters.decaySeconds = decaySeconds[decayIndex];
        parameters.size = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 0.0f;
        parameters.width = 1.0f;
        parameters.ducking = 0.0f;
        parameters.harmony = 0.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        const auto& nominalDelays = reverb.getNominalDelaySamples();
        const auto firstDirectSample = static_cast<int>(
            std::lround(nominalDelays[0]));
        const auto secondDirectSample = static_cast<int>(
            std::lround(nominalDelays[1]));
        require(secondDirectSample > firstDirectSample,
                "Decay onset measurement has invalid delay geometry");
        for (auto sample = 0; sample < secondDirectSample; ++sample)
        {
            auto left = sample == 0 ? 1.0f : 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Decay onset measurement produced NaN/Inf");
            if (sample >= firstDirectSample)
                earlyDirectEnergy[decayIndex]
                    += static_cast<double>(left) * left
                     + static_cast<double>(right) * right;
        }
        require(earlyDirectEnergy[decayIndex] > 1.0e-12,
                "Decay onset measurement is silent");

        // The sustained-noise measurement starts from a clean network.
        reverb.reset();
        const auto warmupSamples = static_cast<int>(
            std::ceil(sampleRate * static_cast<double>(decaySeconds[decayIndex]) * 1.5));
        const auto measurementSamples = static_cast<int>(sampleRate * measurementSeconds);
        const auto totalSamples = warmupSamples + measurementSamples;
        std::uint32_t leftNoise = 0x12345678u;
        std::uint32_t rightNoise = 0x9abcdef0u;
        auto outputEnergy = 0.0;
        auto peak = 0.0f;

        for (auto sample = 0; sample < totalSamples; ++sample)
        {
            leftNoise = 1664525u * leftNoise + 1013904223u;
            rightNoise = 22695477u * rightNoise + 1u;
            auto left = (static_cast<float>(leftNoise >> 8) / 8388607.5f - 1.0f) * 0.05f;
            auto right = (static_cast<float>(rightNoise >> 8) / 8388607.5f - 1.0f) * 0.05f;
            reverb.processSample(left, right);

            require(std::isfinite(left) && std::isfinite(right),
                    "Natural Decay excitation produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            if (sample >= warmupSamples)
                outputEnergy += static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
        }

        wetRms[decayIndex] = std::sqrt(
            outputEnergy / (2.0 * static_cast<double>(measurementSamples)));
        require(wetRms[decayIndex] > 1.0e-6,
                "Natural Decay excitation measurement is silent");
        require(peak < 4.0f,
                "Natural Decay excitation exceeded the safety range");
    }

    const auto [minimum, maximum] = std::minmax_element(wetRms.begin(), wetRms.end());
    const auto spreadDb = 20.0 * std::log10(*maximum / *minimum);
    std::array<double, decaySeconds.size()> earlyDirectDb {};
    for (std::size_t index = 0; index < decaySeconds.size(); ++index)
        earlyDirectDb[index] = 10.0 * std::log10(
            earlyDirectEnergy[index] / earlyDirectEnergy[0]);
    std::cout << "[METRIC] Decay excitation wet RMS: 5/10/20/30 s="
              << wetRms[0] << '/' << wetRms[1] << '/'
              << wetRms[2] << '/' << wetRms[3]
              << ", spread=" << spreadDb
              << " dB; early direct=" << earlyDirectDb[0] << '/'
              << earlyDirectDb[1] << '/' << earlyDirectDb[2] << '/'
              << earlyDirectDb[3] << " dB\n";

    for (std::size_t index = 1; index < earlyDirectDb.size(); ++index)
    {
        require(std::abs(earlyDirectDb[index]) <= 0.01,
                "Decay changes the direct Wet excitation level");
        require(wetRms[index] >= wetRms[index - 1] * 0.999,
                "Sustained Wet energy unexpectedly falls as Decay rises");
    }

    const auto sustainedThirtyToFiveDb = 20.0 * std::log10(
        wetRms.back() / wetRms.front());
    require(sustainedThirtyToFiveDb >= 3.0
                && sustainedThirtyToFiveDb <= 4.3,
            "Natural FDN energy does not grow as expected with Decay");

    // Fathom is left out: the onset window is read from getNominalDelaySamples().
    constexpr std::array modes {
        ReverbMode::defaultMode,
        ReverbMode::bloom,
        ReverbMode::drift,
        ReverbMode::veil,
        ReverbMode::current
    };
    constexpr std::array sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    auto minimumOnsetRatioDb = std::numeric_limits<double>::max();
    auto maximumOnsetRatioDb = std::numeric_limits<double>::lowest();

    for (const auto mode : modes)
    {
        for (const auto rate : sampleRates)
        {
            const auto measureOnsetEnergy = [mode, rate](float decay)
            {
                ReverbParameters onsetParameters;
                onsetParameters.mode = mode;
                onsetParameters.mix = 1.0f;
                onsetParameters.decaySeconds = decay;
                onsetParameters.size = 1.0f;
                onsetParameters.preDelayMs = 0.0f;
                onsetParameters.lowCutHz = 20.0f;
                onsetParameters.highDampingHz = 20000.0f;
                onsetParameters.evolution = 1.0f;
                onsetParameters.width = 1.0f;
                onsetParameters.ducking = 0.0f;
                onsetParameters.harmony = 0.0f;

                FDNReverb onsetReverb;
                onsetReverb.setParameters(onsetParameters);
                onsetReverb.prepare(rate, 512);
                const auto& delays = onsetReverb.getNominalDelaySamples();
                const auto firstReturn = static_cast<int>(
                    std::floor(*std::min_element(delays.begin(), delays.end())));
                const auto secondReturn = static_cast<int>(
                    std::floor(*std::next(delays.begin())));
                auto energy = 0.0;
                for (auto sample = 0; sample < secondReturn; ++sample)
                {
                    auto left = sample == 0 ? 1.0f : 0.0f;
                    auto right = 0.0f;
                    onsetReverb.processSample(left, right);
                    if (sample >= firstReturn)
                        energy += static_cast<double>(left) * left
                                + static_cast<double>(right) * right;
                }
                return energy;
            };

            const auto shortEnergy = measureOnsetEnergy(5.0f);
            const auto longEnergy = measureOnsetEnergy(30.0f);
            require(shortEnergy > 1.0e-12 && longEnergy > 1.0e-12,
                    "Mode/rate Decay onset measurement is silent");
            const auto ratioDb = 10.0 * std::log10(longEnergy / shortEnergy);
            minimumOnsetRatioDb = std::min(minimumOnsetRatioDb, ratioDb);
            maximumOnsetRatioDb = std::max(maximumOnsetRatioDb, ratioDb);
            require(std::abs(ratioDb) <= 0.01,
                    "Decay changes onset level for a Character/sample rate");
        }
    }

    require(maximumOnsetRatioDb - minimumOnsetRatioDb <= 0.01,
            "Decay onset level is not Character/sample-rate independent");
}

void testImpulseDecayAndFiniteOutput()
{
    constexpr auto sampleRate = 48000.0;
    ReverbParameters parameters;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 2.0f;
    parameters.preDelayMs = 15.0f;
    parameters.lowCutHz = 40.0f;
    parameters.highDampingHz = 8000.0f;
    parameters.evolution = 0.25f;

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 127);

    double earlyEnergy = 0.0;
    double lateEnergy = 0.0;
    auto peak = 0.0f;
    const auto sampleCount = static_cast<int>(sampleRate * 7.0);

    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        require(std::isfinite(left) && std::isfinite(right), "Impulse response contains NaN/Inf");
        peak = std::max({ peak, std::abs(left), std::abs(right) });

        const auto energy = static_cast<double>(left) * left + static_cast<double>(right) * right;
        if (sample >= static_cast<int>(sampleRate * 0.25)
            && sample < static_cast<int>(sampleRate * 1.25))
            earlyEnergy += energy;
        if (sample >= static_cast<int>(sampleRate * 5.5)
            && sample < static_cast<int>(sampleRate * 6.5))
            lateEnergy += energy;
    }

    require(earlyEnergy > 1.0e-8, "Impulse response has no measurable wet energy");
    require(lateEnergy < earlyEnergy * 0.01, "Impulse response does not decay sufficiently");
    require(peak < 4.0f, "Impulse response exceeded safety range");
}

void testFeedbackFreezeAndBadInputs()
{
    constexpr auto sampleRate = 96000.0;
    ReverbParameters parameters;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 30.0f;
    parameters.size = 2.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 2.0f;

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 512);

    for (auto sample = 0; sample < static_cast<int>(sampleRate * 0.5); ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = sample == 0 ? -0.5f : 0.0f;
        reverb.processSample(left, right);
    }

    parameters.freeze = true;
    reverb.setParameters(parameters);

    double firstWindowEnergy = 0.0;
    double lastWindowEnergy = 0.0;
    auto peak = 0.0f;
    const auto frozenSamples = static_cast<int>(sampleRate * 12.0);
    for (auto sample = 0; sample < frozenSamples; ++sample)
    {
        auto left = 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        require(std::isfinite(left) && std::isfinite(right), "Freeze produced NaN/Inf");
        peak = std::max({ peak, std::abs(left), std::abs(right) });
        const auto energy = static_cast<double>(left) * left + static_cast<double>(right) * right;
        if (sample >= static_cast<int>(sampleRate)
            && sample < static_cast<int>(sampleRate * 2.0))
            firstWindowEnergy += energy;
        if (sample >= static_cast<int>(sampleRate * 11.0))
            lastWindowEnergy += energy;
    }

    require(lastWindowEnergy <= firstWindowEnergy * 1.2 + 1.0e-12,
            "Freeze feedback energy grows over time");
    require(peak < 4.0f, "Freeze exceeded safety range");

    auto badLeft = std::numeric_limits<float>::quiet_NaN();
    auto badRight = std::numeric_limits<float>::infinity();
    reverb.processSample(badLeft, badRight);
    require(std::isfinite(badLeft) && std::isfinite(badRight),
            "Bad input was not sanitised");

    for (auto sample = 0; sample < 20000; ++sample)
    {
        auto left = 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        require(std::isfinite(left) && std::isfinite(right),
                "Bad input contaminated future feedback state");
    }
}

void testIndependentDcGuardDuringFreeze()
{
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };

    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 0.0f;
        parameters.width = 1.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 256);

        const auto excitationSamples = static_cast<int>(sampleRate * 2.0);
        auto excitationEnergy = 0.0;
        for (auto sample = 0; sample < excitationSamples; ++sample)
        {
            auto left = 0.16f;
            auto right = -0.11f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "DC Guard excitation produced NaN/Inf");
            if (sample >= static_cast<int>(sampleRate * 1.5))
                excitationEnergy += static_cast<double>(left) * left
                                  + static_cast<double>(right) * right;
        }

        parameters.freeze = true;
        reverb.setParameters(parameters);
        const auto frozenSamples = static_cast<int>(sampleRate * 4.0);
        auto lateSumLeft = 0.0;
        auto lateSumRight = 0.0;
        auto lateCount = 0;
        auto peak = 0.0f;
        for (auto sample = 0; sample < frozenSamples; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "DC Guard Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            if (sample >= static_cast<int>(sampleRate * 3.0))
            {
                lateSumLeft += left;
                lateSumRight += right;
                ++lateCount;
            }
        }

        const auto lateMeanLeft = lateSumLeft / lateCount;
        const auto lateMeanRight = lateSumRight / lateCount;
        const auto lateDc = std::hypot(lateMeanLeft, lateMeanRight);
        const auto inputDc = std::hypot(0.16, -0.11);
        const auto attenuationDb = 20.0 * std::log10(
            (lateDc + 1.0e-15) / inputDc);

        std::cout << "[METRIC] DC Guard " << sampleRate
                  << " Hz: frozen late=" << lateDc
                  << ", attenuation=" << attenuationDb << " dB\n";
        require(excitationEnergy > 1.0e-8,
                "DC Guard test excitation became silent");
        require(attenuationDb <= -55.0,
                "DC Guard does not remove frozen DC sufficiently");
        require(peak < 4.0f,
                "DC Guard Freeze exceeded the safety range");
    }
}

void testParameterJumpsAndBlockSegmentation()
{
    constexpr auto sampleRate = 48000.0;
    constexpr float twoPi = 6.28318530717958647692f;
    ReverbParameters parameters;
    parameters.mix = 0.25f;
    parameters.decaySeconds = 2.0f;
    parameters.size = 0.15f;
    parameters.preDelayMs = 0.0f;
    parameters.evolution = 0.0f;

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 512);

    auto phase = 0.0f;
    auto previousLeft = 0.0f;
    for (auto sample = 0; sample < 48000; ++sample)
    {
        auto left = 0.05f * std::sin(phase);
        auto right = 0.05f * std::sin(phase * 1.007f);
        phase += 0.01f;
        reverb.processSample(left, right);
        previousLeft = left;
    }

    parameters.mix = 1.0f;
    parameters.decaySeconds = 30.0f;
    parameters.size = 2.0f;
    parameters.preDelayMs = 250.0f;
    parameters.lowCutHz = 1000.0f;
    parameters.highDampingHz = 1000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 2.0f;
    parameters.ducking = 1.0f;
    parameters.freeze = true;
    reverb.setParameters(parameters);

    auto left = 0.05f * std::sin(phase);
    auto right = 0.05f * std::sin(phase * 1.007f);
    reverb.processSample(left, right);
    require(std::abs(left - previousLeft) < 0.1f,
            "Simultaneous parameter jump caused a discontinuity");

    for (auto sample = 0; sample < 96000; ++sample)
    {
        auto automationLeft = 0.03f * std::sin(phase);
        auto automationRight = -automationLeft;
        phase += 0.01f;
        reverb.processSample(automationLeft, automationRight);
        require(std::isfinite(automationLeft) && std::isfinite(automationRight),
                "Parameter automation produced NaN/Inf");
        require(std::max(std::abs(automationLeft), std::abs(automationRight)) < 4.0f,
                "Parameter automation exceeded safety range");
    }

    constexpr auto comparisonSamples = 24000;
    std::vector<float> singleLeft(comparisonSamples, 0.0f);
    std::vector<float> singleRight(comparisonSamples, 0.0f);
    std::vector<float> blockLeft(comparisonSamples, 0.0f);
    std::vector<float> blockRight(comparisonSamples, 0.0f);
    for (auto sample = 0; sample < comparisonSamples; ++sample)
    {
        const auto time = static_cast<float>(sample) / static_cast<float>(sampleRate);
        const auto leftInput = 0.18f * std::sin(twoPi * 173.0f * time)
                             + 0.07f * std::sin(twoPi * 997.0f * time);
        const auto rightInput = 0.14f * std::sin(twoPi * 211.0f * time + 0.37f)
                              + 0.05f * std::sin(twoPi * 1409.0f * time);
        singleLeft[static_cast<std::size_t>(sample)]
            = blockLeft[static_cast<std::size_t>(sample)] = leftInput;
        singleRight[static_cast<std::size_t>(sample)]
            = blockRight[static_cast<std::size_t>(sample)] = rightInput;
    }
    singleLeft[0] = blockLeft[0] = 1.0f;

    parameters = {};
    parameters.mix = 1.0f;
    parameters.preDelayMs = 7.0f;
    parameters.ducking = 0.73f;

    FDNReverb singleSample;
    FDNReverb blockBased;
    singleSample.setParameters(parameters);
    blockBased.setParameters(parameters);
    singleSample.prepare(sampleRate, 1);
    blockBased.prepare(sampleRate, 127);

    for (auto sample = 0; sample < comparisonSamples; ++sample)
        singleSample.process(singleLeft.data() + sample, singleRight.data() + sample, 1);

    for (auto offset = 0; offset < comparisonSamples; offset += 127)
    {
        const auto blockSize = std::min(127, comparisonSamples - offset);
        blockBased.process(blockLeft.data() + offset, blockRight.data() + offset, blockSize);
    }

    for (std::size_t sample = 0; sample < static_cast<std::size_t>(comparisonSamples); ++sample)
    {
        require(std::abs(singleLeft[sample] - blockLeft[sample]) <= 1.0e-7f
                    && std::abs(singleRight[sample] - blockRight[sample]) <= 1.0e-7f,
                "DSP result depends on process block segmentation");
    }
}

void testBloomSampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    FDNReverb reverb;

    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::bloom;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 1.0f;
        parameters.width = 2.0f;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        std::uint32_t noiseState = 0x81f42a7du;
        auto peak = 0.0f;
        const auto excitationSamples = static_cast<int>(sampleRate * 0.5);
        for (auto sample = 0; sample < excitationSamples; ++sample)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                             / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            auto left = (sample == 0 ? 1.0f : 0.0f) + 0.01f * noise;
            auto right = (sample == 0 ? -0.35f : 0.0f) - 0.007f * noise;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Bloom excitation produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        parameters.freeze = true;
        reverb.setParameters(parameters);
        double firstWindowEnergy = 0.0;
        double lastWindowEnergy = 0.0;
        const auto frozenSamples = static_cast<int>(sampleRate * 6.0);
        for (auto sample = 0; sample < frozenSamples; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Bloom Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            const auto energy = static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
            if (sample >= static_cast<int>(sampleRate)
                && sample < static_cast<int>(sampleRate * 2.0))
                firstWindowEnergy += energy;
            if (sample >= static_cast<int>(sampleRate * 5.0))
                lastWindowEnergy += energy;
        }

        require(firstWindowEnergy > 1.0e-10, "Bloom Freeze tail became silent");
        require(lastWindowEnergy <= firstWindowEnergy * 1.25 + 1.0e-12,
                "Bloom Freeze feedback energy grows over time");
        require(peak < 4.0f, "Bloom stress test exceeded safety range");
    }

    ReverbParameters invalid;
    invalid.mode = static_cast<ReverbMode>(99);
    reverb.setParameters(invalid);
    require(reverb.getParameters().mode == ReverbMode::defaultMode,
            "Unknown mode did not fall back to Default");
}

void testBloomBlockInvarianceAndModeSwitching()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto comparisonSamples = 48000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::bloom;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 6.0f;
    parameters.preDelayMs = 11.0f;
    parameters.evolution = 0.8f;

    std::vector<float> singleLeft(comparisonSamples, 0.0f);
    std::vector<float> singleRight(comparisonSamples, 0.0f);
    std::vector<float> blockLeft(comparisonSamples, 0.0f);
    std::vector<float> blockRight(comparisonSamples, 0.0f);
    singleLeft[0] = blockLeft[0] = 1.0f;

    FDNReverb singleSample;
    FDNReverb blockBased;
    singleSample.setParameters(parameters);
    blockBased.setParameters(parameters);
    singleSample.prepare(sampleRate, 1);
    blockBased.prepare(sampleRate, 127);

    for (auto sample = 0; sample < comparisonSamples; ++sample)
        singleSample.process(singleLeft.data() + sample, singleRight.data() + sample, 1);

    for (auto offset = 0; offset < comparisonSamples; offset += 127)
    {
        const auto blockSize = std::min(127, comparisonSamples - offset);
        blockBased.process(blockLeft.data() + offset, blockRight.data() + offset, blockSize);
    }

    for (std::size_t sample = 0; sample < static_cast<std::size_t>(comparisonSamples); ++sample)
    {
        require(std::abs(singleLeft[sample] - blockLeft[sample]) <= 1.0e-7f
                    && std::abs(singleRight[sample] - blockRight[sample]) <= 1.0e-7f,
                "Bloom result depends on process block segmentation");
    }

    parameters.mode = ReverbMode::defaultMode;
    parameters.mix = 0.7f;
    parameters.preDelayMs = 0.0f;
    FDNReverb control;
    FDNReverb switched;
    control.setParameters(parameters);
    switched.setParameters(parameters);
    control.prepare(sampleRate, 64);
    switched.prepare(sampleRate, 64);

    constexpr auto switchSample = 36000;
    auto preSwitchPeak = 1.0e-3f;
    auto previousResidualLeft = 0.0f;
    auto previousResidualRight = 0.0f;
    auto firstResidual = 0.0f;
    auto maximumResidualDerivative = 0.0f;
    for (auto sample = 0; sample < 72000; ++sample)
    {
        const auto input = 0.04f * std::sin(0.013f * static_cast<float>(sample))
                         + (sample == 0 ? 0.8f : 0.0f);
        auto controlLeft = input;
        auto controlRight = -0.37f * input;
        auto switchedLeft = controlLeft;
        auto switchedRight = controlRight;

        if (sample == switchSample)
        {
            parameters.mode = ReverbMode::bloom;
            switched.setParameters(parameters);
        }

        control.processSample(controlLeft, controlRight);
        switched.processSample(switchedLeft, switchedRight);

        if (sample < switchSample)
        {
            require(std::bit_cast<std::uint32_t>(controlLeft)
                        == std::bit_cast<std::uint32_t>(switchedLeft)
                        && std::bit_cast<std::uint32_t>(controlRight)
                        == std::bit_cast<std::uint32_t>(switchedRight),
                    "Bloom differs before the mode switch");
            if (sample >= switchSample - 1024)
                preSwitchPeak = std::max(preSwitchPeak,
                                         std::max(std::abs(controlLeft), std::abs(controlRight)));
        }
        else
        {
            const auto residualLeft = switchedLeft - controlLeft;
            const auto residualRight = switchedRight - controlRight;
            if (sample == switchSample)
                firstResidual = std::max(std::abs(residualLeft), std::abs(residualRight));
            if (sample < switchSample + static_cast<int>(sampleRate * 0.20))
            {
                maximumResidualDerivative = std::max({
                    maximumResidualDerivative,
                    std::abs(residualLeft - previousResidualLeft),
                    std::abs(residualRight - previousResidualRight)
                });
            }
            previousResidualLeft = residualLeft;
            previousResidualRight = residualRight;
        }
    }

    require(firstResidual <= std::max(1.0e-5f, 0.02f * preSwitchPeak),
            "Default to Bloom switch has an immediate discontinuity");
    require(maximumResidualDerivative <= std::max(2.0e-4f, 0.10f * preSwitchPeak),
            "Default to Bloom morph changes too abruptly");

    parameters.mode = ReverbMode::bloom;
    FDNReverb bloomControl;
    FDNReverb bloomToDefault;
    bloomControl.setParameters(parameters);
    bloomToDefault.setParameters(parameters);
    bloomControl.prepare(sampleRate, 64);
    bloomToDefault.prepare(sampleRate, 64);
    preSwitchPeak = 1.0e-3f;
    previousResidualLeft = 0.0f;
    previousResidualRight = 0.0f;
    firstResidual = 0.0f;
    maximumResidualDerivative = 0.0f;
    for (auto sample = 0; sample < 72000; ++sample)
    {
        const auto input = 0.035f * std::sin(0.011f * static_cast<float>(sample))
                         + (sample == 0 ? 0.7f : 0.0f);
        auto controlLeft = input;
        auto controlRight = -0.41f * input;
        auto switchedLeft = controlLeft;
        auto switchedRight = controlRight;

        if (sample == switchSample)
        {
            parameters.mode = ReverbMode::defaultMode;
            bloomToDefault.setParameters(parameters);
        }

        bloomControl.processSample(controlLeft, controlRight);
        bloomToDefault.processSample(switchedLeft, switchedRight);

        if (sample < switchSample)
        {
            require(std::bit_cast<std::uint32_t>(controlLeft)
                        == std::bit_cast<std::uint32_t>(switchedLeft)
                        && std::bit_cast<std::uint32_t>(controlRight)
                        == std::bit_cast<std::uint32_t>(switchedRight),
                    "Bloom differs before switching back to Default");
            if (sample >= switchSample - 1024)
                preSwitchPeak = std::max(preSwitchPeak,
                                         std::max(std::abs(controlLeft), std::abs(controlRight)));
        }
        else
        {
            const auto residualLeft = switchedLeft - controlLeft;
            const auto residualRight = switchedRight - controlRight;
            if (sample == switchSample)
                firstResidual = std::max(std::abs(residualLeft), std::abs(residualRight));
            if (sample < switchSample + static_cast<int>(sampleRate * 0.20))
            {
                maximumResidualDerivative = std::max({
                    maximumResidualDerivative,
                    std::abs(residualLeft - previousResidualLeft),
                    std::abs(residualRight - previousResidualRight)
                });
            }
            previousResidualLeft = residualLeft;
            previousResidualRight = residualRight;
        }
    }

    require(firstResidual <= std::max(1.0e-5f, 0.02f * preSwitchPeak),
            "Bloom to Default switch has an immediate discontinuity");
    require(maximumResidualDerivative <= std::max(2.0e-4f, 0.10f * preSwitchPeak),
            "Bloom to Default morph changes too abruptly");

    parameters.mode = ReverbMode::defaultMode;
    switched.setParameters(parameters);
    for (auto sample = 0; sample < 60000; ++sample)
    {
        if (sample % 113 == 0)
        {
            parameters.mode = parameters.mode == ReverbMode::defaultMode
                ? ReverbMode::bloom
                : ReverbMode::defaultMode;
            switched.setParameters(parameters);
        }
        auto left = 0.02f * std::sin(0.017f * static_cast<float>(sample));
        auto right = -left;
        switched.processSample(left, right);
        require(std::isfinite(left) && std::isfinite(right),
                "Repeated mode switches produced NaN/Inf");
        require(std::max(std::abs(left), std::abs(right)) < 4.0f,
                "Repeated mode switches exceeded safety range");
    }
}

void testBloomStereoEvolutionAndDryPath()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 144000;
    ReverbParameters bloomParameters;
    bloomParameters.mode = ReverbMode::bloom;
    bloomParameters.mix = 1.0f;
    bloomParameters.decaySeconds = 5.0f;
    bloomParameters.preDelayMs = 0.0f;
    bloomParameters.evolution = 0.75f;
    bloomParameters.width = 1.0f;

    const auto bloom = renderImpulse(bloomParameters, sampleRate, sampleCount);
    auto defaultParameters = bloomParameters;
    defaultParameters.mode = ReverbMode::defaultMode;
    const auto defaultRender = renderImpulse(defaultParameters, sampleRate, sampleCount);

    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    double crossEnergy = 0.0;
    double sideEnergy = 0.0;
    double differenceEnergy = 0.0;
    double bloomEnergy = 0.0;
    const auto start = static_cast<int>(sampleRate * 0.25);
    const auto end = static_cast<int>(sampleRate * 2.5);
    for (auto sample = start; sample < end; ++sample)
    {
        const auto left = static_cast<double>(bloom.left[static_cast<std::size_t>(sample)]);
        const auto right = static_cast<double>(bloom.right[static_cast<std::size_t>(sample)]);
        const auto defaultLeft = static_cast<double>(
            defaultRender.left[static_cast<std::size_t>(sample)]);
        const auto defaultRight = static_cast<double>(
            defaultRender.right[static_cast<std::size_t>(sample)]);
        leftEnergy += left * left;
        rightEnergy += right * right;
        crossEnergy += left * right;
        const auto side = 0.5 * (left - right);
        sideEnergy += side * side;
        differenceEnergy += (left - defaultLeft) * (left - defaultLeft)
                          + (right - defaultRight) * (right - defaultRight);
        bloomEnergy += left * left + right * right;
    }

    require(leftEnergy > 1.0e-10 && rightEnergy > 1.0e-10,
            "Bloom stereo impulse response is silent");
    require(std::max(leftEnergy, rightEnergy) / std::min(leftEnergy, rightEnergy) < 4.0,
            "Bloom stereo energy balance exceeds 6 dB");
    const auto correlation = crossEnergy / std::sqrt(leftEnergy * rightEnergy);
    require(std::abs(correlation) < 0.85, "Bloom left/right tail is insufficiently decorrelated");
    require(sideEnergy / bloomEnergy > 0.05, "Bloom tail has insufficient side energy");
    require(std::sqrt(differenceEnergy / bloomEnergy) > 0.10,
            "Bloom impulse response is too similar to Default");

    const auto evolved = renderImpulse(bloomParameters, sampleRate, sampleCount,
                                       static_cast<int>(sampleRate * 2.731));
    const auto evolvedRepeat = renderImpulse(bloomParameters, sampleRate, sampleCount,
                                             static_cast<int>(sampleRate * 2.731));
    double initialEnergy = 0.0;
    double evolvedEnergy = 0.0;
    double evolutionCross = 0.0;
    double evolutionDifference = 0.0;
    const auto evolutionStart = static_cast<int>(sampleRate * 0.2);
    const auto evolutionEnd = static_cast<int>(sampleRate * 2.0);
    for (auto sample = evolutionStart; sample < evolutionEnd; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const auto initial = static_cast<double>(bloom.left[index] + bloom.right[index]);
        const auto moved = static_cast<double>(evolved.left[index] + evolved.right[index]);
        initialEnergy += initial * initial;
        evolvedEnergy += moved * moved;
        evolutionCross += initial * moved;
        evolutionDifference += (initial - moved) * (initial - moved);
        require(std::abs(evolved.left[index] - evolvedRepeat.left[index]) <= 1.0e-7f
                    && std::abs(evolved.right[index] - evolvedRepeat.right[index]) <= 1.0e-7f,
                "Bloom evolution is not deterministic");
    }

    const auto evolutionCorrelation = evolutionCross / std::sqrt(initialEnergy * evolvedEnergy);
    require(evolutionCorrelation < 0.995,
            "Bloom tail does not evolve enough over time: correlation="
                + std::to_string(evolutionCorrelation));
    require(std::sqrt(evolutionDifference / initialEnergy) > 0.05,
            "Bloom evolution difference is too small");

    bloomParameters.mix = 0.0f;
    FDNReverb dryPath;
    dryPath.setParameters(bloomParameters);
    dryPath.prepare(sampleRate, 64);
    for (auto sample = 0; sample < 10000; ++sample)
    {
        const auto expectedLeft = 0.1f * std::sin(0.01f * static_cast<float>(sample));
        const auto expectedRight = -0.7f * expectedLeft;
        auto left = expectedLeft;
        auto right = expectedRight;
        dryPath.processSample(left, right);
        require(std::abs(left - expectedLeft) <= 1.0e-8f
                    && std::abs(right - expectedRight) <= 1.0e-8f,
                "Bloom changes the dry path at Mix=0");
    }
}

void requireSmoothModeSwitch(ReverbMode fromMode,
                             ReverbMode toMode,
                             const std::string& label)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto switchSample = 30000;
    ReverbParameters parameters;
    parameters.mode = fromMode;
    parameters.mix = 0.7f;
    parameters.decaySeconds = 8.0f;
    parameters.preDelayMs = 0.0f;
    parameters.highDampingHz = 16000.0f;
    parameters.evolution = 0.8f;

    FDNReverb control;
    FDNReverb switched;
    control.setParameters(parameters);
    switched.setParameters(parameters);
    control.prepare(sampleRate, 64);
    switched.prepare(sampleRate, 64);
    const auto firstDelayReturn = static_cast<int>(std::ceil(
        *std::min_element(switched.getNominalDelaySamples().begin(),
                          switched.getNominalDelaySamples().end())
        * parameters.size));
    const auto returnedRmsStart = switchSample + firstDelayReturn;
    const auto returnedRmsEnd = returnedRmsStart + static_cast<int>(sampleRate * 0.35);

    auto preSwitchPeak = 1.0e-3f;
    auto previousResidualLeft = 0.0f;
    auto previousResidualRight = 0.0f;
    auto firstResidual = 0.0f;
    auto maximumResidualDerivative = 0.0f;
    double returnedDifferenceEnergy = 0.0;
    double returnedReferenceEnergy = 0.0;
    for (auto sample = 0; sample < 72000; ++sample)
    {
        const auto input = 0.04f * std::sin(0.013f * static_cast<float>(sample))
                         + (sample == 0 ? 0.8f : 0.0f);
        auto controlLeft = input;
        auto controlRight = -0.37f * input;
        auto switchedLeft = controlLeft;
        auto switchedRight = controlRight;

        if (sample == switchSample)
        {
            parameters.mode = toMode;
            switched.setParameters(parameters);
        }

        control.processSample(controlLeft, controlRight);
        switched.processSample(switchedLeft, switchedRight);

        if (sample < switchSample)
        {
            require(std::bit_cast<std::uint32_t>(controlLeft)
                        == std::bit_cast<std::uint32_t>(switchedLeft)
                        && std::bit_cast<std::uint32_t>(controlRight)
                        == std::bit_cast<std::uint32_t>(switchedRight),
                    label + " differs before switching");
            if (sample >= switchSample - 1024)
                preSwitchPeak = std::max(preSwitchPeak,
                                         std::max(std::abs(controlLeft), std::abs(controlRight)));
        }
        else
        {
            const auto residualLeft = switchedLeft - controlLeft;
            const auto residualRight = switchedRight - controlRight;
            if (sample == switchSample)
                firstResidual = std::max(std::abs(residualLeft), std::abs(residualRight));
            if (sample < switchSample + static_cast<int>(sampleRate * 0.20))
            {
                maximumResidualDerivative = std::max({
                    maximumResidualDerivative,
                    std::abs(residualLeft - previousResidualLeft),
                    std::abs(residualRight - previousResidualRight)
                });
            }
            if (sample >= returnedRmsStart && sample < returnedRmsEnd)
            {
                returnedDifferenceEnergy += static_cast<double>(residualLeft) * residualLeft
                                          + static_cast<double>(residualRight) * residualRight;
                returnedReferenceEnergy += 0.5
                    * (static_cast<double>(controlLeft) * controlLeft
                       + static_cast<double>(controlRight) * controlRight
                       + static_cast<double>(switchedLeft) * switchedLeft
                       + static_cast<double>(switchedRight) * switchedRight);
            }
            previousResidualLeft = residualLeft;
            previousResidualRight = residualRight;
        }
    }

    require(firstResidual <= std::max(1.0e-5f, 0.02f * preSwitchPeak),
            label + " has an immediate discontinuity");
    // This guards an abrupt edge, not the intended 200-ms morph itself. Use a
    // small level-relative margin so a deliberate Wet gain policy change does
    // not invalidate an otherwise identical smoothing trajectory.
    const auto derivativeLimit = std::max(2.0e-4f, 0.12f * preSwitchPeak);
    std::cout << "[METRIC] " << label << ": first residual=" << firstResidual
              << ", max residual derivative=" << maximumResidualDerivative
              << ", derivative limit=" << derivativeLimit << '\n';
    require(maximumResidualDerivative <= derivativeLimit,
            label + " morph changes too abruptly: derivative="
                + std::to_string(maximumResidualDerivative)
                + " limit=" + std::to_string(derivativeLimit));
    require(returnedReferenceEnergy > 1.0e-12,
            label + " post-return reference became silent");
    const auto normalisedReturnedRms = std::sqrt(
        returnedDifferenceEnergy / returnedReferenceEnergy);
    require(normalisedReturnedRms > 0.01,
            label + " has no measurable DSP effect after the first delay return: RMS="
                + std::to_string(normalisedReturnedRms));
}

void requireSmoothEvolutionSwitch(ReverbMode mode,
                                  float fromEvolution,
                                  float toEvolution,
                                  const std::string& label)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto switchSample = 30000;
    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.evolution = fromEvolution;
    parameters.mix = 0.7f;
    parameters.decaySeconds = 8.0f;
    parameters.preDelayMs = 0.0f;
    parameters.highDampingHz = 16000.0f;

    FDNReverb control;
    FDNReverb switched;
    control.setParameters(parameters);
    switched.setParameters(parameters);
    control.prepare(sampleRate, 64);
    switched.prepare(sampleRate, 64);
    const auto firstDelayReturn = static_cast<int>(std::ceil(
        *std::min_element(switched.getNominalDelaySamples().begin(),
                          switched.getNominalDelaySamples().end())
        * parameters.size));
    const auto returnedRmsStart = switchSample + firstDelayReturn;
    const auto returnedRmsEnd = returnedRmsStart + static_cast<int>(sampleRate * 0.35);

    auto preSwitchPeak = 1.0e-3f;
    auto previousResidualLeft = 0.0f;
    auto previousResidualRight = 0.0f;
    auto firstResidual = 0.0f;
    auto maximumResidualDerivative = 0.0f;
    double returnedDifferenceEnergy = 0.0;
    double returnedReferenceEnergy = 0.0;
    for (auto sample = 0; sample < 72000; ++sample)
    {
        const auto input = 0.04f * std::sin(0.013f * static_cast<float>(sample))
                         + (sample == 0 ? 0.8f : 0.0f);
        auto controlLeft = input;
        auto controlRight = -0.37f * input;
        auto switchedLeft = controlLeft;
        auto switchedRight = controlRight;

        if (sample == switchSample)
        {
            parameters.evolution = toEvolution;
            switched.setParameters(parameters);
        }

        control.processSample(controlLeft, controlRight);
        switched.processSample(switchedLeft, switchedRight);
        if (sample < switchSample)
        {
            require(std::bit_cast<std::uint32_t>(controlLeft)
                        == std::bit_cast<std::uint32_t>(switchedLeft)
                        && std::bit_cast<std::uint32_t>(controlRight)
                        == std::bit_cast<std::uint32_t>(switchedRight),
                    label + " differs before switching");
            if (sample >= switchSample - 1024)
                preSwitchPeak = std::max(preSwitchPeak,
                                         std::max(std::abs(controlLeft),
                                                  std::abs(controlRight)));
        }
        else
        {
            const auto residualLeft = switchedLeft - controlLeft;
            const auto residualRight = switchedRight - controlRight;
            if (sample == switchSample)
                firstResidual = std::max(std::abs(residualLeft), std::abs(residualRight));
            if (sample < switchSample + static_cast<int>(sampleRate * 0.20))
            {
                maximumResidualDerivative = std::max({
                    maximumResidualDerivative,
                    std::abs(residualLeft - previousResidualLeft),
                    std::abs(residualRight - previousResidualRight)
                });
            }
            if (sample >= returnedRmsStart && sample < returnedRmsEnd)
            {
                returnedDifferenceEnergy += static_cast<double>(residualLeft) * residualLeft
                                          + static_cast<double>(residualRight) * residualRight;
                returnedReferenceEnergy += 0.5
                    * (static_cast<double>(controlLeft) * controlLeft
                       + static_cast<double>(controlRight) * controlRight
                       + static_cast<double>(switchedLeft) * switchedLeft
                       + static_cast<double>(switchedRight) * switchedRight);
            }
            previousResidualLeft = residualLeft;
            previousResidualRight = residualRight;
        }
    }

    require(firstResidual <= std::max(1.0e-5f, 0.02f * preSwitchPeak),
            label + " has an immediate discontinuity");
    // Evolution uses the same 200-ms morph policy as Character. Keep this
    // threshold relative to the deliberately louder natural Wet reference.
    const auto derivativeLimit = std::max(2.0e-4f, 0.125f * preSwitchPeak);
    std::cout << "[METRIC] " << label << ": first residual=" << firstResidual
              << ", max residual derivative=" << maximumResidualDerivative
              << ", derivative limit=" << derivativeLimit << '\n';
    require(maximumResidualDerivative <= derivativeLimit,
            label + " morph changes too abruptly");
    require(returnedReferenceEnergy > 1.0e-12,
            label + " post-return reference became silent");
    const auto normalisedReturnedRms = std::sqrt(
        returnedDifferenceEnergy / returnedReferenceEnergy);
    require(normalisedReturnedRms > 0.01,
            label + " has no measurable effect after first delay return: RMS="
                + std::to_string(normalisedReturnedRms));
}

void testCurrentFdnMinimumMovementAndSwitching()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 192000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::defaultMode;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 5.0f;
    parameters.size = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.evolution = 0.0f;
    parameters.width = 1.0f;
    parameters.ducking = 0.0f;
    parameters.harmony = 0.0f;

    const auto defaultLow = renderImpulse(parameters, sampleRate, sampleCount);
    parameters.mode = ReverbMode::current;
    const auto currentLow = renderImpulse(parameters, sampleRate, sampleCount);
    auto lowReferenceEnergy = 0.0;
    auto lowCurrentEnergy = 0.0;
    auto lowDifferenceEnergy = 0.0;
    const auto lowAnalysisStart = static_cast<int>(sampleRate * 0.10);
    const auto lowAnalysisEnd = static_cast<int>(sampleRate * 3.5);
    for (auto sample = lowAnalysisStart; sample < lowAnalysisEnd; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const auto defaultLeft = static_cast<double>(defaultLow.left[index]);
        const auto defaultRight = static_cast<double>(defaultLow.right[index]);
        const auto currentLeft = static_cast<double>(currentLow.left[index]);
        const auto currentRight = static_cast<double>(currentLow.right[index]);
        lowReferenceEnergy += defaultLeft * defaultLeft
                            + defaultRight * defaultRight;
        lowCurrentEnergy += currentLeft * currentLeft + currentRight * currentRight;
        lowDifferenceEnergy += (currentLeft - defaultLeft)
                                   * (currentLeft - defaultLeft)
                             + (currentRight - defaultRight)
                                   * (currentRight - defaultRight);
    }
    require(lowReferenceEnergy > 1.0e-12 && lowCurrentEnergy > 1.0e-12,
            "Current low-Evolution comparison became silent");
    const auto lowNormalisedDifference = std::sqrt(
        lowDifferenceEnergy / lowReferenceEnergy);
    const auto lowEnergyRatio = lowCurrentEnergy / lowReferenceEnergy;
    std::cout << "[METRIC] Current minimum field NRMS="
              << lowNormalisedDifference
              << ", energy ratio=" << lowEnergyRatio << '\n';
    require(lowNormalisedDifference >= 0.005
                && lowNormalisedDifference <= 1.20,
            "Current minimum field is either identical to Default or unstable");
    require(lowEnergyRatio >= 0.85 && lowEnergyRatio <= 1.15,
            "Current minimum field changes tail energy excessively");

    parameters.evolution = 1.0f;
    const auto currentHigh = renderImpulse(parameters, sampleRate, sampleCount);
    auto referenceEnergy = 0.0;
    auto differenceEnergy = 0.0;
    auto currentEnergy = 0.0;
    auto peak = 0.0f;
    const auto analysisStart = static_cast<int>(sampleRate * 0.10);
    const auto analysisEnd = static_cast<int>(sampleRate * 3.5);
    for (auto sample = analysisStart; sample < analysisEnd; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        const auto lowLeft = static_cast<double>(currentLow.left[index]);
        const auto lowRight = static_cast<double>(currentLow.right[index]);
        const auto highLeft = static_cast<double>(currentHigh.left[index]);
        const auto highRight = static_cast<double>(currentHigh.right[index]);
        referenceEnergy += lowLeft * lowLeft + lowRight * lowRight;
        currentEnergy += highLeft * highLeft + highRight * highRight;
        differenceEnergy += (highLeft - lowLeft) * (highLeft - lowLeft)
                          + (highRight - lowRight) * (highRight - lowRight);
        require(std::isfinite(highLeft) && std::isfinite(highRight),
                "Current high-Evolution render produced NaN/Inf");
        peak = std::max({ peak,
                          static_cast<float>(std::abs(highLeft)),
                          static_cast<float>(std::abs(highRight)) });
    }
    require(referenceEnergy > 1.0e-12 && currentEnergy > 1.0e-12,
            "Current comparison became silent");
    const auto normalisedDifference = std::sqrt(
        differenceEnergy / referenceEnergy);
    const auto energyRatio = currentEnergy / referenceEnergy;
    std::cout << "[METRIC] Current FDN NRMS=" << normalisedDifference
              << ", energy ratio=" << energyRatio
              << ", peak=" << peak << '\n';
    require(normalisedDifference >= 0.08,
            "Current high Evolution is not audibly distinct");
    require(energyRatio >= 0.50 && energyRatio <= 2.0,
            "Current changes total tail energy excessively");
    require(peak < 4.0f,
            "Current high Evolution exceeded the safety range");

    FDNReverb singleSample;
    FDNReverb blockBased;
    singleSample.setParameters(parameters);
    blockBased.setParameters(parameters);
    singleSample.prepare(sampleRate, 1);
    blockBased.prepare(sampleRate, 127);
    std::vector<float> singleLeft(48000, 0.0f);
    std::vector<float> singleRight(48000, 0.0f);
    std::vector<float> blockLeft(48000, 0.0f);
    std::vector<float> blockRight(48000, 0.0f);
    singleLeft[0] = blockLeft[0] = 1.0f;
    for (auto sample = 0; sample < 48000; ++sample)
        singleSample.process(singleLeft.data() + sample,
                             singleRight.data() + sample, 1);
    for (auto offset = 0; offset < 48000; offset += 127)
    {
        const auto blockSize = std::min(127, 48000 - offset);
        blockBased.process(blockLeft.data() + offset,
                           blockRight.data() + offset, blockSize);
    }
    for (auto sample = 0; sample < 48000; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        require(std::bit_cast<std::uint32_t>(singleLeft[index])
                    == std::bit_cast<std::uint32_t>(blockLeft[index])
                    && std::bit_cast<std::uint32_t>(singleRight[index])
                    == std::bit_cast<std::uint32_t>(blockRight[index]),
                "Current depends on process block segmentation");
    }

    requireSmoothModeSwitch(ReverbMode::defaultMode, ReverbMode::current,
                            "Default to Current switch");
    requireSmoothModeSwitch(ReverbMode::current, ReverbMode::defaultMode,
                            "Current to Default switch");
    requireSmoothModeSwitch(ReverbMode::drift, ReverbMode::current,
                            "Drift to Current switch");
    requireSmoothModeSwitch(ReverbMode::current, ReverbMode::veil,
                            "Current to Veil switch");
    requireSmoothEvolutionSwitch(ReverbMode::current, 0.0f, 1.0f,
                                 "Current low to high Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::current, 1.0f, 0.0f,
                                 "Current high to low Evolution");
}

void testCurrentSampleRatesFreezeAndStability()
{
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };
    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::current;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 1.0f;
        parameters.width = 2.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);
        std::uint32_t noiseState = 0xc001c0deu;
        auto peak = 0.0f;
        const auto excitationSamples = static_cast<int>(sampleRate * 0.5);
        for (auto sample = 0; sample < excitationSamples; ++sample)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto noise = static_cast<float>(
                static_cast<std::int32_t>(noiseState))
                / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            auto left = (sample == 0 ? 1.0f : 0.0f) + 0.01f * noise;
            auto right = (sample == 0 ? -0.31f : 0.0f) - 0.006f * noise;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Current excitation produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        parameters.freeze = true;
        reverb.setParameters(parameters);
        auto firstWindowEnergy = 0.0;
        auto lastWindowEnergy = 0.0;
        const auto frozenSamples = static_cast<int>(sampleRate * 8.0);
        for (auto sample = 0; sample < frozenSamples; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Current Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            const auto energy = static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
            if (sample >= static_cast<int>(sampleRate)
                && sample < static_cast<int>(sampleRate * 2.0))
                firstWindowEnergy += energy;
            if (sample >= static_cast<int>(sampleRate * 7.0))
                lastWindowEnergy += energy;
        }

        require(firstWindowEnergy > 1.0e-10,
                "Current Freeze tail became silent");
        const auto frozenEnergyRatio = lastWindowEnergy / firstWindowEnergy;
        std::cout << "[METRIC] Current Freeze " << sampleRate
                  << " Hz late/early=" << frozenEnergyRatio
                  << ", peak=" << peak << '\n';
        require(lastWindowEnergy >= firstWindowEnergy * 0.40,
                "Current Freeze tail collapsed unexpectedly");
        require(lastWindowEnergy <= firstWindowEnergy * 1.25 + 1.0e-12,
                "Current Freeze feedback energy grows over time");
        require(peak < 4.0f,
                "Current stress test exceeded the safety range");
    }
}

[[nodiscard]] bool sameBits(float first, float second) noexcept
{
    return std::bit_cast<std::uint32_t>(first) == std::bit_cast<std::uint32_t>(second);
}

[[nodiscard]] bool sameBits(double first, double second) noexcept
{
    return std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second);
}

[[nodiscard]] float fathomNoise(int frame, int channel) noexcept
{
    auto state = static_cast<std::uint32_t>(frame) * 2654435761u
               + static_cast<std::uint32_t>(channel) * 40503u + 0x0ebb5eedu;
    state ^= state >> 15;
    state *= 2246822519u;
    state ^= state >> 13;
    return static_cast<float>(static_cast<std::int32_t>(state)) / 2147483648.0f;
}

// Fathom with Ocean's own controls where they are out of its circuit, Width where
// the reference's law is the identity and Mix at 100 %, where the wet passes
// as it is.
[[nodiscard]] ReverbParameters fathomNeutralParameters()
{
    ReverbParameters parameters;
    parameters.mode = ReverbMode::fathom;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 4.0f;
    parameters.size = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 1.0f;
    parameters.ducking = 0.0f;
    parameters.harmony = 0.0f;
    parameters.autoHarmony = true;
    return parameters;
}

// A programme for the plug-in level tests of Fathom: four bursts per second of a
// C4 tone and noise on both sides, each with an onset of 2.5 ms that crosses
// the threshold of the reference's level stage (0.5629) at `drive` 1.
[[nodiscard]] FathomEngine::Frame fathomBursts(int frame, double sampleRate, float drive = 1.0f)
{
    const auto period = static_cast<int>(sampleRate * 0.25);
    const auto position = frame % period;
    if (position >= period / 2)
        return {};

    constexpr auto twoPi = 6.28318530717958647692;
    const auto tone = static_cast<float>(
        std::sin(twoPi * 261.6256 * static_cast<double>(frame) / sampleRate));
    const auto onset = position < period / 100 ? 0.45f : 0.0f;
    return { drive * (0.20f * tone + 0.10f * fathomNoise(frame, 20) + onset),
             drive * (-0.15f * tone - 0.08f * fathomNoise(frame, 21)) };
}

// What FDNReverb hands the engine for a set of Ocean's parameters.
[[nodiscard]] FathomEngine::Parameters fathomEngineParameters(
    const ReverbParameters& parameters) noexcept
{
    FathomEngine::Parameters engineParameters;
    engineParameters.decaySeconds = parameters.decaySeconds;
    engineParameters.sizeScale = parameters.size;
    engineParameters.preDelaySeconds = parameters.preDelayMs * 0.001f;
    engineParameters.macro = parameters.evolution;
    engineParameters.lowCutHz = parameters.lowCutHz;
    engineParameters.highDampingHz = parameters.highDampingHz;
    engineParameters.freeze = parameters.freeze;
    return engineParameters;
}

// How this build of the DSP forms the sum of Ocean's Mix, dry + mix (wet -
// dry): with the product fused into the addition, or rounded on its own
// first. Read from Default while its pre-delay holds the wet at exact silence,
// where the sum is dry + mix (0 - dry).
[[nodiscard]] bool oceanMixSumIsFused()
{
    static const auto fused = []
    {
        constexpr auto mix = 0.3f;
        ReverbParameters parameters;
        parameters.mix = mix;
        parameters.preDelayMs = 250.0f;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(48000.0, 512);

        auto fusedFrames = 0;
        auto separateFrames = 0;
        for (auto frame = 0; frame < 4000; ++frame)
        {
            const auto dry = fathomNoise(frame, 40);
            auto left = dry;
            auto right = dry;
            reverb.processSample(left, right);

            // The products pass through memory, so no build of this file
            // fuses them with the addition.
            volatile float product = mix * (0.0f - dry);
            const float separateSum = dry + product;
            const auto fusedSum = std::fma(mix, 0.0f - dry, dry);
            if (sameBits(fusedSum, separateSum))
                continue;
            fusedFrames += sameBits(left, fusedSum) ? 1 : 0;
            separateFrames += sameBits(left, separateSum) ? 1 : 0;
        }
        require(std::max(fusedFrames, separateFrames) > 100 && std::min(fusedFrames, separateFrames) == 0,
                "Ocean's Mix sum is neither the fused nor the separately rounded form of its expression");
        return fusedFrames > 0;
    }();
    return fused;
}

// Ocean's Mix of one channel as FDNReverb forms it for every Character: the
// dry signal, under the bound of the FDN's input, crossfaded linearly into
// the wet. At 100 % the wet of Fathom passes as it is.
[[nodiscard]] float oceanMix(float dry, float wet, float mix)
{
    if (mix >= 1.0f)
        return wet;

    const auto boundedDry = std::clamp(dry, -4.0f, 4.0f);
    volatile float difference = wet - boundedDry;
    const float roundedDifference = difference;
    if (oceanMixSumIsFused())
        return std::fma(mix, roundedDifference, boundedDry);

    volatile float product = mix * roundedDifference;
    return boundedDry + product;
}

// The chain Fathom must reduce to while Ocean's own controls are neutral: the
// engine with the parameters FDNReverb hands it, its level stage and the
// reference's Width law, Ocean's Mix, the reference's clipper and the output
// guard of a settled Fathom. Mono Safe adds the Sub Anchor behind the Width law.
struct FathomReferenceChain
{
    FathomReferenceChain(const ReverbParameters& parameters, double sampleRate,
                         std::uint64_t voiceSeed = FathomEngine::defaultVoiceSeed)
        : mix(parameters.mix), width(parameters.width)
    {
        engine.setParameters(fathomEngineParameters(parameters));
        engine.setVoiceSeed(voiceSeed);
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);
        subAnchor.prepare(sampleRate);
    }

    // The engine's wet through its level stage.
    [[nodiscard]] FathomEngine::Frame wet(float dryLeft, float dryRight) noexcept
    {
        return levelStage.process(dryLeft, dryRight, engine.processSample(dryLeft, dryRight));
    }

    // A frame of time while another Character is selected.
    void idle() noexcept
    {
        engine.advanceIdle();
    }

    [[nodiscard]] FathomEngine::Frame process(float dryLeft, float dryRight,
                                              bool monoSafe = false) noexcept
    {
        auto widened = FathomEngine::applyWidth(wet(dryLeft, dryRight), width);
        if (monoSafe)
        {
            const auto anchored = subAnchor.applyWidth(widened.left, widened.right, 1.0f);
            widened = { anchored.left, anchored.right };
        }
        widenedWet = widened;
        unclipped = { oceanMix(dryLeft, widened.left, mix), oceanMix(dryRight, widened.right, mix) };
        return { guard(FathomEngine::clip(unclipped.left)),
                 guard(FathomEngine::clip(unclipped.right)) };
    }

    [[nodiscard]] static float guard(float sample) noexcept
    {
        return std::abs(sample) < std::numeric_limits<float>::min()
            ? 0.0f : std::clamp(sample, -8.0f, 8.0f);
    }

    FathomEngine engine;
    FathomEngine::LevelStage levelStage;
    StereoField subAnchor;
    // The wet in front of the Mix and the sum in front of the clipper, for
    // the last frame.
    FathomEngine::Frame widenedWet;
    FathomEngine::Frame unclipped;
    float mix;
    float width;
};

void testFathomRoutingAtNeutralControls()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        for (const auto evolution : { 0.0f, 1.0f })
        {
            auto parameters = fathomNeutralParameters();
            parameters.evolution = evolution;
            parameters.preDelayMs = 12.3f;

            FDNReverb reverb;
            reverb.setParameters(parameters);
            reverb.prepare(sampleRate, 512);
            FathomReferenceChain chain(parameters, sampleRate);
            // Shows the reduction of the level stage on a wet signal of one.
            FathomEngine::LevelStage reduction;
            reduction.prepare(sampleRate);

            auto reducedFrames = 0;
            auto wetPeak = 0.0f;
            const auto frameCount = static_cast<int>(sampleRate * 0.75);
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                const auto input = fathomBursts(frame, sampleRate);
                const auto expected = chain.wet(input.left, input.right);
                if (reduction.process(input.left, input.right, { 1.0f, 1.0f }).left < 1.0f)
                    ++reducedFrames;
                wetPeak = std::max({ wetPeak, std::abs(expected.left), std::abs(expected.right) });

                auto left = input.left;
                auto right = input.right;
                reverb.processSample(left, right);
                require(sameBits(left, expected.left) && sameBits(right, expected.right),
                        "Fathom is not the engine's wet through its level stage at "
                            + std::to_string(static_cast<int>(sampleRate)) + " Hz, Evolution "
                            + std::to_string(evolution) + ", frame " + std::to_string(frame));
            }
            require(reducedFrames > frameCount / 4 && wetPeak > 0.01f,
                    "Fathom routing programme did not exercise the level stage and the engine");
        }
    }
}

// Low Cut and High Damping are the engine's own loop filters: the plug-in
// hands both over when it is prepared and whenever they move, and reset()
// returns Fathom and its outer stages to the state of a fresh instance.
void testFathomLoopControlsAndResetReachTheEngine()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto movedFrame = 14000;
    constexpr auto frameCount = 30000;

    auto engaged = fathomNeutralParameters();
    engaged.decaySeconds = 2.0f;
    engaged.lowCutHz = 300.0f;
    engaged.highDampingHz = 4000.0f;
    auto moved = engaged;
    moved.lowCutHz = 90.0f;
    moved.highDampingHz = 9000.0f;
    auto neutral = engaged;
    neutral.lowCutHz = 20.0f;
    neutral.highDampingHz = 20000.0f;

    struct LoopCase
    {
        const char* name;
        ReverbParameters atStart;
        ReverbParameters afterTheMove;
    };
    auto lowCutAlone = neutral;
    lowCutAlone.lowCutHz = engaged.lowCutHz;
    auto dampingAlone = neutral;
    dampingAlone.highDampingHz = engaged.highDampingHz;
    const std::array loopCases {
        LoopCase { "Low Cut and High Damping", engaged, moved },
        LoopCase { "Low Cut alone", lowCutAlone, neutral },
        LoopCase { "High Damping alone", dampingAlone, neutral }
    };

    for (const auto& loopCase : loopCases)
    {
        const auto label = std::string("Fathom with ") + loopCase.name;
        FDNReverb reverb;
        reverb.setParameters(loopCase.atStart);
        reverb.prepare(sampleRate, 512);
        FathomReferenceChain chain(loopCase.atStart, sampleRate);
        FathomReferenceChain open(neutral, sampleRate);

        double differenceEnergy = 0.0;
        double openEnergy = 0.0;
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            if (frame == movedFrame)
            {
                reverb.setParameters(loopCase.afterTheMove);
                chain.engine.setParameters(fathomEngineParameters(loopCase.afterTheMove));
            }
            const auto input = fathomBursts(frame, sampleRate);
            const auto expected = chain.process(input.left, input.right);
            const auto unfiltered = open.process(input.left, input.right);
            auto left = input.left;
            auto right = input.right;
            reverb.processSample(left, right);
            require(sameBits(left, expected.left) && sameBits(right, expected.right),
                    label + " is not the engine with the same loop filters at frame "
                        + std::to_string(frame));
            if (frame < movedFrame)
            {
                differenceEnergy += static_cast<double>(left - unfiltered.left) * (left - unfiltered.left)
                                  + static_cast<double>(right - unfiltered.right) * (right - unfiltered.right);
                openEnergy += static_cast<double>(unfiltered.left) * unfiltered.left
                            + static_cast<double>(unfiltered.right) * unfiltered.right;
            }
        }
        const auto effect = std::sqrt(differenceEnergy / std::max(openEnergy, 1.0e-300));
        std::cout << "[METRIC] " << label << ": NRMS against the open loop=" << effect << '\n';
        require(effect > 0.01, label + " leaves the tail as the open loop has it");
    }

    // reset() in a loud passage, with the level stage turned down, the engine
    // sounding and the Sub Anchor tracking: what follows is what a fresh
    // instance returns. The programme behind the reset stays under the level
    // stage, so a reduction that outlived the reset would show.
    auto parameters = fathomNeutralParameters();
    parameters.width = 1.5f;
    parameters.monoSafeStereo = true;
    FDNReverb used;
    used.setParameters(parameters);
    used.prepare(sampleRate, 512);
    FathomEngine::LevelStage reduction;
    reduction.prepare(sampleRate);
    // Just behind the onset of the second burst.
    constexpr auto resetFrame = 12200;
    auto gainAtReset = 1.0f;
    auto peakBeforeReset = 0.0f;
    for (auto frame = 0; frame < resetFrame; ++frame)
    {
        const auto input = fathomBursts(frame, sampleRate, 4.0f);
        gainAtReset = reduction.process(input.left, input.right, { 1.0f, 1.0f }).left;
        auto left = input.left;
        auto right = input.right;
        used.processSample(left, right);
        peakBeforeReset = std::max({ peakBeforeReset, std::abs(left), std::abs(right) });
    }
    std::cout << "[METRIC] Fathom at reset(): gain of the level stage=" << gainAtReset
              << ", wet peak before=" << peakBeforeReset << '\n';
    require(gainAtReset < 0.9f && peakBeforeReset > 0.05f,
            "Fathom was not sounding under a working level stage when it was reset");
    used.reset();

    FDNReverb fresh;
    fresh.setParameters(parameters);
    fresh.prepare(sampleRate, 512);
    auto peakAfterReset = 0.0f;
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        const auto input = fathomBursts(frame, sampleRate, 0.5f);
        auto left = input.left;
        auto right = input.right;
        used.processSample(left, right);
        auto freshLeft = input.left;
        auto freshRight = input.right;
        fresh.processSample(freshLeft, freshRight);
        require(sameBits(left, freshLeft) && sameBits(right, freshRight),
                "Fathom after reset() is not a fresh instance at frame " + std::to_string(frame));
        peakAfterReset = std::max({ peakAfterReset, std::abs(left), std::abs(right) });
    }
    require(peakAfterReset > 0.01f, "Fathom is silent after reset()");
}

void testFathomWidthMixAndClipperRoutes()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 30000;
    const auto threshold = static_cast<float>(std::pow(10.0, 8.0 / 20.0));
    const auto ceiling = static_cast<float>(std::pow(10.0, 12.0 / 20.0));

    struct RouteCase
    {
        const char* name;
        float width;
        float mix;
        float drive;
    };

    // Width at mono, between, and at the end of its travel, where Ocean's
    // 200 % is the reference's maximum; Mix at both ends, at the quarters, at
    // Ocean's default and at two more points where the rounding of the sum
    // shows. The driven cases reach 5.9 at the input, above the bound the
    // FDN's input observes: at Mix 100 % the wet alone enters the knee of the
    // clipper, below it the dry signal does, from the bound of 4 it enters
    // the Mix under.
    constexpr std::array routeCases {
        RouteCase { "Width 0 %", 0.0f, 1.0f, 1.0f },
        RouteCase { "Width 50 %", 0.5f, 1.0f, 1.0f },
        RouteCase { "Width 200 %", 2.0f, 1.0f, 1.0f },
        RouteCase { "Mix 0 %", 1.0f, 0.0f, 1.0f },
        RouteCase { "Mix 25 %", 1.0f, 0.25f, 1.0f },
        RouteCase { "Mix 30 %", 1.0f, 0.3f, 1.0f },
        RouteCase { "Mix 35 %", 1.0f, 0.35f, 1.0f },
        RouteCase { "Mix 50 %", 1.0f, 0.5f, 1.0f },
        RouteCase { "Mix 70 %", 1.0f, 0.7f, 1.0f },
        RouteCase { "Mix 75 %", 1.0f, 0.75f, 1.0f },
        RouteCase { "the clipper at Mix 0 %", 1.0f, 0.0f, 8.0f },
        RouteCase { "the clipper at Mix 20 %", 1.0f, 0.2f, 8.0f },
        RouteCase { "the clipper at Mix 100 % and Width 200 %", 2.0f, 1.0f, 8.0f }
    };

    std::cout << "[METRIC] Ocean's Mix sum in this build: the product "
              << (oceanMixSumIsFused() ? "fused into the addition" : "rounded on its own") << '\n';
    for (const auto& routeCase : routeCases)
    {
        const auto label = std::string("Fathom route at ") + routeCase.name;
        auto parameters = fathomNeutralParameters();
        parameters.width = routeCase.width;
        parameters.mix = routeCase.mix;
        parameters.preDelayMs = 7.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);
        FathomReferenceChain chain(parameters, sampleRate);

        const auto dryShare = 1.0 - static_cast<double>(routeCase.mix);
        const auto wetShare = static_cast<double>(routeCase.mix);
        auto inputPeak = 0.0f;
        auto outputPeak = 0.0f;
        auto untouchedFrames = 0;
        auto kneeFrames = 0;
        auto ceilingFrames = 0;
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            const auto input = fathomBursts(frame, sampleRate, routeCase.drive);
            const auto expected = chain.process(input.left, input.right);
            auto left = input.left;
            auto right = input.right;
            reverb.processSample(left, right);
            require(sameBits(left, expected.left) && sameBits(right, expected.right),
                    label + " is not Ocean's Mix of the engine's wet at frame "
                        + std::to_string(frame));

            inputPeak = std::max({ inputPeak, std::abs(input.left), std::abs(input.right) });
            outputPeak = std::max({ outputPeak, std::abs(left), std::abs(right) });
            const auto sum = std::abs(chain.unclipped.left);
            if (sum > 0.0f && sum <= threshold && sameBits(left, chain.unclipped.left))
            {
                ++untouchedFrames;
                // The linear law itself: a dry share of 1 - Mix, a wet share
                // of Mix, to the last place of single precision.
                const auto linear = dryShare * static_cast<double>(std::clamp(input.left, -4.0f, 4.0f))
                                  + wetShare * static_cast<double>(chain.widenedWet.left);
                require(std::abs(static_cast<double>(left) - linear) <= 5.0e-7,
                        label + " is not the linear Mix of dry and wet at frame "
                            + std::to_string(frame));
            }
            else if (sum > threshold && std::abs(left) < ceiling)
                ++kneeFrames;
            else if (sameBits(std::abs(left), ceiling))
                ++ceilingFrames;

            if (sameBits(routeCase.width, 0.0f))
                require(sameBits(left, right), label + " is not mono");
            if (sameBits(routeCase.mix, 0.0f) && routeCase.drive <= 1.0f)
                require(sameBits(left, input.left) && sameBits(right, input.right),
                        label + " is not the input itself");
        }

        require(untouchedFrames > 1000, label + " never left the sum as it is");
        if (routeCase.drive > 1.0f)
        {
            std::cout << "[METRIC] " << label << ": input peak=" << inputPeak
                      << ", output peak=" << outputPeak
                      << ", frames in the knee=" << kneeFrames
                      << ", at the ceiling=" << ceilingFrames << '\n';
            require(inputPeak > 5.0f, label + " did not drive the input above the FDN's bound");
            require(kneeFrames > 100, label + " did not reach the knee of the clipper");
            require(outputPeak <= ceiling, label + " went above +12 dBFS");
            // Without the wet the loudest sample is the dry signal at the
            // bound of the FDN's input, through the knee.
            if (sameBits(routeCase.mix, 0.0f))
                require(sameBits(outputPeak, FathomEngine::clip(4.0f)),
                        label + " does not pass the dry signal from the bound of the FDN's input "
                                "through the clipper");
        }
        else
        {
            require(kneeFrames == 0 && ceilingFrames == 0, label + " reached the clipper");
        }
    }
}

// Fathom leaves through the Mix of every Character, so its dry level is theirs.
// With the longest Pre-delay no wet arrives for a quarter of a second: until
// then Fathom, each other Character and a switch between the two in either
// direction return the same samples at every Mix, the dry signal times
// 1 - Mix.
void testFathomDryLevelIsThatOfEveryCharacter()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto switchFrame = 600;
    // The fade of a switch ends 9600 frames later; the first wet of either
    // network needs 11999 frames or more from its first input.
    constexpr auto frameCount = 11000;

    struct OtherCharacter
    {
        const char* name;
        ReverbMode mode;
    };
    constexpr std::array otherCharacters {
        OtherCharacter { "Default", ReverbMode::defaultMode },
        OtherCharacter { "Bloom", ReverbMode::bloom },
        OtherCharacter { "Drift", ReverbMode::drift },
        OtherCharacter { "Veil", ReverbMode::veil },
        OtherCharacter { "Current", ReverbMode::current }
    };

    for (const auto mix : { 0.0f, 0.2f, 0.35f, 0.5f, 0.75f, 1.0f })
    {
        for (const auto& other : otherCharacters)
        {
            const auto label = std::string("Dry level of Fathom and ") + other.name + " at Mix "
                             + std::to_string(mix);
            auto fathomParameters = fathomNeutralParameters();
            fathomParameters.mix = mix;
            fathomParameters.preDelayMs = 250.0f;
            auto otherParameters = fathomParameters;
            otherParameters.mode = other.mode;

            // Fathom, the other Character, and a switch each way.
            std::array<FDNReverb, 4> reverbs;
            const std::array startParameters { fathomParameters, otherParameters,
                                               otherParameters, fathomParameters };
            const std::array switchedParameters { fathomParameters, otherParameters,
                                                  fathomParameters, otherParameters };
            for (std::size_t index = 0; index < reverbs.size(); ++index)
            {
                reverbs[index].setParameters(startParameters[index]);
                reverbs[index].prepare(sampleRate, 512);
            }

            auto dryEnergy = 0.0;
            auto outputOnDry = 0.0;
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                if (frame == switchFrame)
                    for (std::size_t index = 0; index < reverbs.size(); ++index)
                        reverbs[index].setParameters(switchedParameters[index]);

                const auto input = fathomBursts(frame, sampleRate);
                std::array<FathomEngine::Frame, 4> outputs;
                for (std::size_t index = 0; index < reverbs.size(); ++index)
                {
                    outputs[index] = input;
                    reverbs[index].processSample(outputs[index].left, outputs[index].right);
                }
                for (std::size_t index = 1; index < outputs.size(); ++index)
                    require(sameBits(outputs[index].left, outputs[0].left)
                                && sameBits(outputs[index].right, outputs[0].right),
                            label + " differs at frame " + std::to_string(frame)
                                + (index == 1 ? " between the two Characters"
                                              : index == 2 ? " on the way into Fathom"
                                                           : " on the way out of Fathom"));

                dryEnergy += static_cast<double>(input.left) * input.left
                           + static_cast<double>(input.right) * input.right;
                outputOnDry += static_cast<double>(outputs[0].left) * input.left
                             + static_cast<double>(outputs[0].right) * input.right;
            }

            require(dryEnergy > 1.0, label + ": the programme is silent");
            require(std::abs(outputOnDry / dryEnergy - (1.0 - static_cast<double>(mix))) <= 1.0e-6,
                    label + " is not 1 - Mix: " + std::to_string(outputOnDry / dryEnergy));
        }
    }
}

// A voice seed handed to FDNReverb is the seed of its engine from the next
// prepare() or reset() on and stays until another is handed over. It reaches
// nothing but the voices: at Evolution 0 and in the other Characters every
// sample is the same for any seed.
void testFathomVoiceSeedThroughThePlugIn()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 24000;
    constexpr std::uint64_t firstSeed = 0x0123456789abcdefULL;
    constexpr std::uint64_t secondSeed = 0xfedcba9876543210ULL;

    const auto parameters = fathomNeutralParameters();
    const auto requireChain = [&](FDNReverb& reverb, FathomReferenceChain& chain, int firstFrame,
                                  const std::string& label)
    {
        for (auto frame = firstFrame; frame < firstFrame + frameCount; ++frame)
        {
            const auto input = fathomBursts(frame, sampleRate);
            const auto expected = chain.process(input.left, input.right);
            auto left = input.left;
            auto right = input.right;
            reverb.processSample(left, right);
            require(sameBits(left, expected.left) && sameBits(right, expected.right),
                    label + " at frame " + std::to_string(frame));
        }
    };

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.setFathomVoiceSeed(firstSeed);
    reverb.prepare(sampleRate, 512);
    FathomReferenceChain firstChain(parameters, sampleRate, firstSeed);
    FathomReferenceChain defaultChain(parameters, sampleRate);
    double differenceEnergy = 0.0;
    double defaultEnergy = 0.0;
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        const auto input = fathomBursts(frame, sampleRate);
        const auto expected = firstChain.process(input.left, input.right);
        const auto withDefaultSeed = defaultChain.process(input.left, input.right);
        auto left = input.left;
        auto right = input.right;
        reverb.processSample(left, right);
        require(sameBits(left, expected.left) && sameBits(right, expected.right),
                "A voice seed set before prepare() is not the seed of the engine at frame "
                    + std::to_string(frame));
        differenceEnergy += static_cast<double>(left - withDefaultSeed.left) * (left - withDefaultSeed.left)
                          + static_cast<double>(right - withDefaultSeed.right) * (right - withDefaultSeed.right);
        defaultEnergy += static_cast<double>(withDefaultSeed.left) * withDefaultSeed.left
                       + static_cast<double>(withDefaultSeed.right) * withDefaultSeed.right;
    }
    const auto seedEffect = std::sqrt(differenceEnergy / std::max(defaultEnergy, 1.0e-300));
    std::cout << "[METRIC] Fathom through FDNReverb with a voice seed of its own: NRMS against the default seed="
              << seedEffect << '\n';
    require(seedEffect > 0.01, "A voice seed handed to FDNReverb leaves Fathom as the default seed has it");

    reverb.setFathomVoiceSeed(secondSeed);
    requireChain(reverb, firstChain, frameCount, "A voice seed took effect before reset()");
    reverb.reset();
    FathomReferenceChain secondChain(parameters, sampleRate, secondSeed);
    requireChain(reverb, secondChain, 0, "A voice seed is not the seed of the engine after reset()");

    reverb.setFathomVoiceSeed(firstSeed);
    reverb.prepare(sampleRate, 512);
    FathomReferenceChain preparedChain(parameters, sampleRate, firstSeed);
    requireChain(reverb, preparedChain, 0, "A voice seed is not the seed of the engine after prepare()");
    reverb.reset();
    FathomReferenceChain keptChain(parameters, sampleRate, firstSeed);
    requireChain(reverb, keptChain, 0, "reset() did not start the phase of the same voice seed again");

    struct SeedlessCase
    {
        const char* name;
        ReverbMode mode;
        float evolution;
    };
    constexpr std::array seedlessCases {
        SeedlessCase { "Fathom at Evolution 0", ReverbMode::fathom, 0.0f },
        SeedlessCase { "Default", ReverbMode::defaultMode, 1.0f },
        SeedlessCase { "Bloom", ReverbMode::bloom, 1.0f },
        SeedlessCase { "Drift", ReverbMode::drift, 1.0f },
        SeedlessCase { "Veil", ReverbMode::veil, 1.0f },
        SeedlessCase { "Current", ReverbMode::current, 1.0f }
    };
    for (const auto& seedlessCase : seedlessCases)
    {
        auto seedlessParameters = parameters;
        seedlessParameters.mode = seedlessCase.mode;
        seedlessParameters.evolution = seedlessCase.evolution;
        std::array<FDNReverb, 2> seeded;
        seeded[0].setFathomVoiceSeed(firstSeed);
        seeded[1].setFathomVoiceSeed(secondSeed);
        for (auto& instance : seeded)
        {
            instance.setParameters(seedlessParameters);
            instance.prepare(sampleRate, 512);
        }

        auto peak = 0.0f;
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            const auto input = fathomBursts(frame, sampleRate);
            auto firstOutput = input;
            auto secondOutput = input;
            seeded[0].processSample(firstOutput.left, firstOutput.right);
            seeded[1].processSample(secondOutput.left, secondOutput.right);
            require(sameBits(firstOutput.left, secondOutput.left)
                        && sameBits(firstOutput.right, secondOutput.right),
                    std::string("The voice seed changes ") + seedlessCase.name + " at frame "
                        + std::to_string(frame));
            peak = std::max({ peak, std::abs(firstOutput.left), std::abs(firstOutput.right) });
        }
        require(peak > 0.01f, std::string(seedlessCase.name) + " was silent under the voice seed test");
    }
}

// Focus, Harmony and Mono Safe act on Fathom's wet behind the engine and its
// level stage. Each of them changes the output while it is engaged and is out
// of the circuit to the bit once it is back at its neutral position.
void testFathomOceanOwnControls()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto engagedFrames = 36000;
    // Longer than the slowest of the three smoothers, Harmony's 150 ms.
    constexpr auto settlingFrames = 12000;
    constexpr auto comparedFrames = 12000;

    struct OwnCase
    {
        const char* name;
        float focus;
        float harmony;
        bool monoSafe;
        double minimumEffect;
        double maximumEffect;
    };

    constexpr std::array ownCases {
        OwnCase { "Focus", 1.0f, 0.0f, false, 0.02, 0.5 },
        OwnCase { "Harmony", 0.0f, 1.0f, false, 0.02, 1.0 },
        OwnCase { "Mono Safe", 0.0f, 0.0f, true, 0.01, 0.5 }
    };

    for (const auto& ownCase : ownCases)
    {
        const auto label = std::string("Fathom with ") + ownCase.name;
        auto neutral = fathomNeutralParameters();
        neutral.autoHarmony = false;
        neutral.harmonyPitchClasses = harmonyWeights({ 0, 4, 7 });
        neutral.harmonyConfidence = 1.0f;
        auto engaged = neutral;
        engaged.ducking = ownCase.focus;
        engaged.harmony = ownCase.harmony;
        engaged.monoSafeStereo = ownCase.monoSafe;

        FDNReverb reverb;
        reverb.setParameters(engaged);
        reverb.prepare(sampleRate, 512);
        FathomReferenceChain chain(neutral, sampleRate);

        double differenceEnergy = 0.0;
        double referenceEnergy = 0.0;
        auto largestMidDifference = 0.0f;
        auto peak = 0.0f;
        for (auto frame = 0; frame < engagedFrames + settlingFrames + comparedFrames; ++frame)
        {
            if (frame == engagedFrames)
                reverb.setParameters(neutral);

            const auto input = fathomBursts(frame, sampleRate);
            const auto expected = chain.process(input.left, input.right);
            auto left = input.left;
            auto right = input.right;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right), label + " produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });

            if (frame < engagedFrames)
            {
                differenceEnergy += static_cast<double>(left - expected.left) * (left - expected.left)
                                  + static_cast<double>(right - expected.right) * (right - expected.right);
                referenceEnergy += static_cast<double>(expected.left) * expected.left
                                 + static_cast<double>(expected.right) * expected.right;
                largestMidDifference = std::max(
                    largestMidDifference,
                    std::abs(0.5f * (left + right) - 0.5f * (expected.left + expected.right)));
            }
            else if (frame >= engagedFrames + settlingFrames)
            {
                require(sameBits(left, expected.left) && sameBits(right, expected.right),
                        label + " back at neutral is not out of the circuit at frame "
                            + std::to_string(frame));
            }
        }

        const auto effect = std::sqrt(differenceEnergy / std::max(referenceEnergy, 1.0e-300));
        std::cout << "[METRIC] " << label << ": NRMS against the neutral chain=" << effect
                  << ", largest Mid difference=" << largestMidDifference
                  << ", peak=" << peak << '\n';
        require(effect >= ownCase.minimumEffect && effect <= ownCase.maximumEffect,
                label + " has no effect or an excessive one: NRMS=" + std::to_string(effect));
        require(peak < 4.0f, label + " exceeded the safety range");
        // Of Mono Safe only the Sub Anchor acts on Fathom, and that leaves Mid
        // alone.
        if (ownCase.monoSafe)
            require(largestMidDifference <= 1.0e-6f, label + " changed Mid");
    }

    // Mono Safe from the start is the Sub Anchor behind the reference's Width law.
    {
        auto parameters = fathomNeutralParameters();
        parameters.width = 1.5f;
        parameters.monoSafeStereo = true;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);
        FathomReferenceChain chain(parameters, sampleRate);
        for (auto frame = 0; frame < engagedFrames; ++frame)
        {
            const auto input = fathomBursts(frame, sampleRate);
            const auto expected = chain.process(input.left, input.right, true);
            auto left = input.left;
            auto right = input.right;
            reverb.processSample(left, right);
            require(std::abs(left - expected.left) <= 1.0e-6f
                        && std::abs(right - expected.right) <= 1.0e-6f,
                    "Fathom with Mono Safe is not the Sub Anchor behind the Width law at frame "
                        + std::to_string(frame));
        }
    }

    // Freeze holds Fathom's tail through the engine's own hold, whose level
    // follows the moving voices, and gives it back to the Decay afterwards.
    {
        auto parameters = fathomNeutralParameters();
        parameters.decaySeconds = 1.0f;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        const auto second = static_cast<int>(sampleRate);
        const auto energyOver = [&](int frames, bool excite)
        {
            double energy = 0.0;
            for (auto frame = 0; frame < frames; ++frame)
            {
                const auto input = excite ? fathomBursts(frame, sampleRate)
                                          : FathomEngine::Frame {};
                auto left = input.left;
                auto right = input.right;
                reverb.processSample(left, right);
                require(std::isfinite(left) && std::isfinite(right),
                        "Fathom Freeze in the plug-in produced NaN/Inf");
                energy += static_cast<double>(left) * left + static_cast<double>(right) * right;
            }
            return energy;
        };

        static_cast<void>(energyOver(second / 2, true));
        parameters.freeze = true;
        reverb.setParameters(parameters);
        static_cast<void>(energyOver(second, false));
        const auto earlyEnergy = energyOver(second, false);
        static_cast<void>(energyOver(3 * second, false));
        const auto lateEnergy = energyOver(second, false);
        parameters.freeze = false;
        reverb.setParameters(parameters);
        static_cast<void>(energyOver(2 * second, false));
        const auto releasedEnergy = energyOver(second, false);

        std::cout << "[METRIC] Fathom Freeze in the plug-in: held late/early="
                  << lateEnergy / earlyEnergy << ", two seconds after release="
                  << releasedEnergy / earlyEnergy << '\n';
        require(earlyEnergy > 1.0e-8, "Fathom Freeze in the plug-in holds nothing");
        require(lateEnergy >= 0.2 * earlyEnergy && lateEnergy <= 3.0 * earlyEnergy,
                "Fathom Freeze in the plug-in does not hold its tail");
        require(releasedEnergy <= 1.0e-4 * earlyEnergy,
                "Fathom tail does not decay after Freeze is released");
    }
}

void testFathomSampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::fathom;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 250.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 1.0f;
        parameters.width = 2.0f;

        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        std::uint32_t noiseState = 0x0ebb71deu;
        auto peak = 0.0f;
        const auto excitationSamples = static_cast<int>(sampleRate * 0.5);
        for (auto sample = 0; sample < excitationSamples; ++sample)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                             / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            auto left = (sample == 0 ? 1.0f : 0.0f) + 0.01f * noise;
            auto right = (sample == 0 ? -0.35f : 0.0f) - 0.007f * noise;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Fathom excitation produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        parameters.freeze = true;
        reverb.setParameters(parameters);
        const auto frozenSamples = static_cast<int>(sampleRate * 1.5);
        for (auto sample = 0; sample < frozenSamples; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Fathom Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        auto badLeft = std::numeric_limits<float>::quiet_NaN();
        auto badRight = std::numeric_limits<float>::infinity();
        reverb.processSample(badLeft, badRight);
        require(std::isfinite(badLeft) && std::isfinite(badRight),
                "Bad input was not sanitised in Fathom");
        for (auto sample = 0; sample < 10000; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Bad input contaminated Fathom's future state");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        require(peak < 4.0f, "Fathom stress test exceeded the safety range");
    }

    FDNReverb reverb;
    ReverbParameters lastKnown;
    lastKnown.mode = ReverbMode::fathom;
    reverb.setParameters(lastKnown);
    require(reverb.getParameters().mode == ReverbMode::fathom,
            "Fathom mode was not accepted");
    ReverbParameters pastTheEnd;
    pastTheEnd.mode = static_cast<ReverbMode>(static_cast<int>(ReverbMode::spume) + 1);
    reverb.setParameters(pastTheEnd);
    require(reverb.getParameters().mode == ReverbMode::defaultMode,
            "The value after the last mode did not fall back to Default");
}

// The amount of a Character over its 200-ms morph, as FDNReverb steps it.
class FathomMorphRamp
{
public:
    FathomMorphRamp(float from, float to, int frames) noexcept
        : current_(from), target_(to), step_((to - from) / static_cast<float>(frames)),
          remaining_(frames)
    {
    }

    [[nodiscard]] float next() noexcept
    {
        if (remaining_ <= 0)
            return current_;
        current_ = --remaining_ == 0 ? target_ : current_ + step_;
        return current_;
    }

private:
    float current_;
    float target_;
    float step_;
    int remaining_;
};

// A switch into or out of Fathom is a linear crossfade of 200 ms between two
// renders that exist on their own. One is the FDN switching between the other
// Character and Default, which is how it runs behind Fathom. The other is the
// Fathom chain: it starts from silence when Fathom is entered and goes on for
// the length of the fade when Fathom is left. With Ocean's own controls neutral
// and Width and Mix at 100 % the output is that crossfade sample by sample, so
// a switch can add neither a click nor a burst nor anything of an earlier
// visit.
void requireFathomCrossfade(ReverbMode otherMode, bool intoFathom, const std::string& label)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto switchSample = 30000;
    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    constexpr auto endSample = switchSample + morphSamples + 12000;

    auto parameters = fathomNeutralParameters();
    parameters.decaySeconds = 6.0f;
    parameters.evolution = 0.8f;
    auto switchedParameters = parameters;
    switchedParameters.mode = intoFathom ? otherMode : ReverbMode::fathom;
    auto fdnParameters = parameters;
    fdnParameters.mode = intoFathom ? otherMode : ReverbMode::defaultMode;

    FDNReverb switched;
    FDNReverb fdnSide;
    switched.setParameters(switchedParameters);
    fdnSide.setParameters(fdnParameters);
    switched.prepare(sampleRate, 64);
    fdnSide.prepare(sampleRate, 64);
    FathomReferenceChain fathomSide(parameters, sampleRate);
    FathomMorphRamp fathomAmount(intoFathom ? 0.0f : 1.0f, intoFathom ? 1.0f : 0.0f, morphSamples);

    auto largestDistance = 0.0f;
    auto peak = 1.0e-3f;
    double sideDifferenceEnergy = 0.0;
    double sideEnergy = 0.0;
    for (auto sample = 0; sample < endSample; ++sample)
    {
        if (sample == switchSample)
        {
            switchedParameters.mode = intoFathom ? ReverbMode::fathom : otherMode;
            switched.setParameters(switchedParameters);
            fdnParameters.mode = intoFathom ? ReverbMode::defaultMode : otherMode;
            fdnSide.setParameters(fdnParameters);
        }

        // Quiet enough for the tails of the earlier bursts to stay in view.
        const auto input = fathomBursts(sample, sampleRate, 0.5f);
        auto left = input.left;
        auto right = input.right;
        switched.processSample(left, right);
        auto fdnLeft = input.left;
        auto fdnRight = input.right;
        fdnSide.processSample(fdnLeft, fdnRight);
        auto fathom = FathomEngine::Frame {};
        if (intoFathom && sample < switchSample)
            fathomSide.idle();
        else
            fathom = fathomSide.process(input.left, input.right);

        if (sample < switchSample)
        {
            require(intoFathom ? sameBits(left, fdnLeft) && sameBits(right, fdnRight)
                               : sameBits(left, fathom.left) && sameBits(right, fathom.right),
                    label + " differs before the switch");
            continue;
        }

        const auto amount = fathomAmount.next();
        if (sample >= switchSample + morphSamples - 1)
        {
            require(intoFathom ? sameBits(left, fathom.left) && sameBits(right, fathom.right)
                               : sameBits(left, fdnLeft) && sameBits(right, fdnRight),
                    label + " is not the new Character alone after the crossfade, at sample "
                        + std::to_string(sample));
            continue;
        }

        largestDistance = std::max({
            largestDistance,
            std::abs(left - (fdnLeft + amount * (fathom.left - fdnLeft))),
            std::abs(right - (fdnRight + amount * (fathom.right - fdnRight)))
        });
        peak = std::max({ peak, std::abs(fdnLeft), std::abs(fdnRight),
                          std::abs(fathom.left), std::abs(fathom.right) });
        sideDifferenceEnergy += static_cast<double>(fathom.left - fdnLeft) * (fathom.left - fdnLeft)
                              + static_cast<double>(fathom.right - fdnRight)
                                    * (fathom.right - fdnRight);
        sideEnergy += 0.5 * (static_cast<double>(fathom.left) * fathom.left
                             + static_cast<double>(fathom.right) * fathom.right
                             + static_cast<double>(fdnLeft) * fdnLeft
                             + static_cast<double>(fdnRight) * fdnRight);
    }

    std::cout << "[METRIC] " << label << ": largest distance from the crossfade="
              << largestDistance << ", peak of its two sides=" << peak << '\n';
    // The blends of FDNReverb round in the last place of the larger side.
    require(largestDistance <= 4.0e-6f * peak,
            label + " is not a crossfade of the two Characters: distance="
                + std::to_string(largestDistance / peak) + " of the peak");
    require(sideEnergy > 1.0e-8 && sideDifferenceEnergy > 0.25 * sideEnergy,
            label + " crossfades two sides that do not differ");
}

// Fathom left for `framesAway` frames and selected again. A stay that outlasts
// the fade ends the engine's tail: Fathom then returns as an engine that has only
// kept time since it was prepared. A shorter stay never silences it, and the
// tail that was sounding goes on.
void requireFathomReturn(int framesAway, bool expectsRestart, const std::string& label)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto leaveSample = 30000;
    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    const auto returnSample = leaveSample + framesAway;
    const auto settledSample = returnSample + morphSamples;

    auto parameters = fathomNeutralParameters();
    parameters.decaySeconds = 30.0f;
    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 64);
    FathomReferenceChain chain(parameters, sampleRate);

    double tailEnergy = 0.0;
    for (auto sample = 0; sample < settledSample + 12000; ++sample)
    {
        if (sample == leaveSample || sample == returnSample)
        {
            parameters.mode = sample == leaveSample ? ReverbMode::defaultMode : ReverbMode::fathom;
            reverb.setParameters(parameters);
        }

        // A loud first visit, then silence until Fathom has settled again.
        const auto input = sample < leaveSample ? fathomBursts(sample, sampleRate)
                         : sample >= settledSample + 2400 ? fathomBursts(sample, sampleRate, 0.5f)
                                                          : FathomEngine::Frame {};
        auto left = input.left;
        auto right = input.right;
        reverb.processSample(left, right);
        auto expected = FathomEngine::Frame {};
        if (expectsRestart && sample < returnSample)
            chain.idle();
        else
            expected = chain.process(input.left, input.right);

        if (sample < settledSample)
            continue;
        require(sameBits(left, expected.left) && sameBits(right, expected.right),
                label + " is not the expected engine at sample " + std::to_string(sample));
        if (sample < settledSample + 2400)
            tailEnergy += static_cast<double>(left) * left + static_cast<double>(right) * right;
    }

    require(expectsRestart ? tailEnergy <= 0.0 : tailEnergy > 1.0e-6,
            label + (expectsRestart ? " kept a tail of the earlier visit"
                                    : " lost the tail that was sounding"));
}

void testFathomBlockInvarianceAndModeSwitching()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto comparisonSamples = 48000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::fathom;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 6.0f;
    parameters.preDelayMs = 11.0f;
    parameters.evolution = 0.8f;

    std::vector<float> singleLeft(comparisonSamples, 0.0f);
    std::vector<float> singleRight(comparisonSamples, 0.0f);
    std::vector<float> blockLeft(comparisonSamples, 0.0f);
    std::vector<float> blockRight(comparisonSamples, 0.0f);
    singleLeft[0] = blockLeft[0] = 1.0f;

    FDNReverb singleSample;
    FDNReverb blockBased;
    singleSample.setParameters(parameters);
    blockBased.setParameters(parameters);
    singleSample.prepare(sampleRate, 1);
    blockBased.prepare(sampleRate, 127);

    for (auto sample = 0; sample < comparisonSamples; ++sample)
        singleSample.process(singleLeft.data() + sample, singleRight.data() + sample, 1);
    for (auto offset = 0; offset < comparisonSamples; offset += 127)
    {
        const auto blockSize = std::min(127, comparisonSamples - offset);
        blockBased.process(blockLeft.data() + offset, blockRight.data() + offset, blockSize);
    }
    for (auto sample = 0; sample < comparisonSamples; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        require(std::bit_cast<std::uint32_t>(singleLeft[index])
                    == std::bit_cast<std::uint32_t>(blockLeft[index])
                    && std::bit_cast<std::uint32_t>(singleRight[index])
                    == std::bit_cast<std::uint32_t>(blockRight[index]),
                "Fathom depends on process block segmentation");
    }

    struct OtherCharacter
    {
        const char* name;
        ReverbMode mode;
    };

    constexpr std::array otherCharacters {
        OtherCharacter { "Default", ReverbMode::defaultMode },
        OtherCharacter { "Bloom", ReverbMode::bloom },
        OtherCharacter { "Drift", ReverbMode::drift },
        OtherCharacter { "Veil", ReverbMode::veil },
        OtherCharacter { "Current", ReverbMode::current }
    };
    for (const auto& other : otherCharacters)
    {
        requireFathomCrossfade(other.mode, true, std::string(other.name) + " to Fathom switch");
        requireFathomCrossfade(other.mode, false,
                               std::string("Fathom to ") + other.name + " switch");
    }

    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    requireFathomReturn(morphSamples, true, "Fathom selected again once its fade has ended");
    requireFathomReturn(3 * morphSamples, true, "Fathom selected again after a longer stay away");
    requireFathomReturn(morphSamples - 1, false,
                        "Fathom selected again one frame before its fade ends");
    requireFathomReturn(morphSamples / 2, false, "Fathom selected again in the middle of its fade");
}

// The campaign's null of a render against its reference: the energy of the
// difference over the energy of the reference, in decibels.
[[nodiscard]] double fathomNullDb(std::span<const float> candidate,
                                  std::span<const float> reference)
{
    double differenceEnergy = 0.0;
    double referenceEnergy = 0.0;
    for (std::size_t index = 0; index < reference.size(); ++index)
    {
        const auto difference = static_cast<double>(candidate[index])
                              - static_cast<double>(reference[index]);
        differenceEnergy += difference * difference;
        referenceEnergy += static_cast<double>(reference[index]) * reference[index];
    }
    return 10.0 * std::log10(std::max(differenceEnergy, 1.0e-300)
                             / std::max(referenceEnergy, 1.0e-300));
}

// A short programme for the engine's own tests: an impulse on each side, then
// a burst of noise.
[[nodiscard]] FathomEngine::Frame fathomProgramme(int frame) noexcept
{
    FathomEngine::Frame input;
    if (frame == 100)
        input.left = 0.5f;
    if (frame == 700)
        input.right = -0.4f;
    if (frame >= 1500 && frame < 2500)
    {
        input.left += 0.2f * fathomNoise(frame, 0);
        input.right += 0.2f * fathomNoise(frame, 1);
    }
    return input;
}

// The engine's wet output for the test programme, left and right interleaved.
[[nodiscard]] std::vector<float> renderFathomProgramme(FathomEngine& engine, int frameCount)
{
    std::vector<float> rendered;
    rendered.reserve(2 * static_cast<std::size_t>(frameCount));
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        const auto input = fathomProgramme(frame);
        const auto wet = engine.processSample(input.left, input.right);
        rendered.push_back(wet.left);
        rendered.push_back(wet.right);
    }
    return rendered;
}

void requireSameFathomRender(const std::vector<float>& rendered,
                             const std::vector<float>& expected,
                             const std::string& label)
{
    require(rendered.size() == expected.size(), label + ": render lengths differ");
    for (std::size_t index = 0; index < expected.size(); ++index)
        require(sameBits(rendered[index], expected[index]),
                label + ": output differs at frame " + std::to_string(index / 2));
}

// Blocks of the voices that a run of host frames reaches: the core never runs
// ahead of the host.
[[nodiscard]] std::size_t fathomVoiceBlocks(double sampleRate, int frames)
{
    namespace fathom = amanita::dsp::fathom;
    const auto internalSamples = static_cast<double>(frames) * fathom::internalRate / sampleRate;
    return static_cast<std::size_t>(internalSamples / fathom::voiceBlockSamples) + 2;
}

// The voice phase of a capture where the engine sets its voices, at the start
// of each block.
[[nodiscard]] std::vector<double> fathomCapturePhase(
    const amanita::dsp::fathomgolden::PhaseCurve& curve, std::size_t blockCount)
{
    namespace fathom = amanita::dsp::fathom;
    constexpr auto pi = 3.14159265358979323846;
    const auto knots = curve.knots;
    std::vector<double> phase(blockCount);
    for (std::size_t block = 0; block < blockCount; ++block)
    {
        const auto seconds = static_cast<double>(block * fathom::voiceBlockSamples) / fathom::internalRate;
        auto level = seconds < knots.front().seconds ? knots.front().cycles : knots.back().cycles;
        for (std::size_t knot = 0; knot + 1 < knots.size(); ++knot)
        {
            if (seconds >= knots[knot].seconds && seconds < knots[knot + 1].seconds)
            {
                const auto along = (seconds - knots[knot].seconds)
                                 / (knots[knot + 1].seconds - knots[knot].seconds);
                level = knots[knot].cycles
                      + (knots[knot + 1].cycles - knots[knot].cycles) * (0.5 - 0.5 * std::cos(pi * along));
            }
        }
        phase[block] = curve.cyclesPerSecond * seconds + level;
    }
    return phase;
}

void testFathomEngineGoldenVectors()
{
    namespace golden = amanita::dsp::fathomgolden;
    // The engine stores its signals in single precision and so sits about
    // 147 dB under the model; the model itself meets the reference at -99 to
    // -131 dB on these excerpts at Macro 0 and at -84 to -99 dB at Macro 100 %.
    constexpr auto nullLimitDb = -130.0;
    auto worstNullDb = -400.0;
    auto worstTideNullDb = -400.0;

    for (const auto& vector : golden::captureVectors)
    {
        auto frameCount = 0;
        for (const auto& excerpt : vector.excerpts)
            frameCount = std::max(frameCount,
                                  excerpt.firstFrame + static_cast<int>(excerpt.model.size() / 2));

        FathomEngine::Parameters parameters;
        parameters.decaySeconds = vector.decaySeconds;
        parameters.sizeScale = vector.sizeScale;
        parameters.preDelaySeconds = static_cast<float>(vector.preDelaySeconds);
        parameters.macro = vector.macro;
        FathomEngine engine;
        engine.setParameters(parameters);
        // Above Macro 0 the voices take the phase the capture had.
        std::array<std::vector<double>, 2> phase;
        if (vector.macro > 0.0f)
        {
            const auto blockCount = fathomVoiceBlocks(static_cast<double>(vector.hostRate),
                                                      vector.warmupFrames + frameCount);
            phase = { fathomCapturePhase(vector.phase[0], blockCount),
                      fathomCapturePhase(vector.phase[1], blockCount) };
            engine.setVoicePhaseForTesting(phase[0].data(), phase[1].data(), blockCount);
        }
        engine.prepare(static_cast<double>(vector.hostRate));
        for (auto frame = 0; frame < vector.warmupFrames; ++frame)
            engine.advanceIdle();

        const auto programmeFrames = static_cast<int>(vector.programme.size() / 2);
        std::vector<float> rendered;
        rendered.reserve(2 * static_cast<std::size_t>(frameCount));
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            FathomEngine::Frame input;
            for (const auto& impulse : vector.impulses)
                if (impulse.frame == frame)
                    (impulse.channel == 0 ? input.left : input.right) += impulse.amplitude;
            const auto programmeFrame = frame - vector.programmeFirstFrame;
            if (programmeFrame >= 0 && programmeFrame < programmeFrames)
            {
                input.left += vector.programme[2 * static_cast<std::size_t>(programmeFrame)];
                input.right += vector.programme[2 * static_cast<std::size_t>(programmeFrame) + 1];
            }
            const auto wet = engine.processSample(input.left, input.right);
            rendered.push_back(wet.left);
            rendered.push_back(wet.right);
        }

        for (const auto& excerpt : vector.excerpts)
        {
            const auto nullDb = fathomNullDb(
                std::span<const float>(rendered).subspan(
                    2 * static_cast<std::size_t>(excerpt.firstFrame), excerpt.model.size()),
                excerpt.model);
            auto& worst = vector.macro > 0.0f ? worstTideNullDb : worstNullDb;
            worst = std::max(worst, nullDb);
            require(nullDb <= nullLimitDb,
                    std::string("Fathom engine misses the golden vector ") + vector.name
                        + " from frame " + std::to_string(excerpt.firstFrame)
                        + ": null=" + std::to_string(nullDb) + " dB");
        }
    }

    std::cout << "[METRIC] Fathom golden vectors: worst null against the model="
              << worstNullDb << " dB at Macro 0, " << worstTideNullDb
              << " dB at Macro 100 %, limit=" << nullLimitDb << " dB\n";
    require(worstTideNullDb > -400.0, "Fathom golden vectors hold no capture above Macro 0");
}

void testFathomRateLatticeAndConverters()
{
    namespace golden = amanita::dsp::fathomgolden;

    for (const auto& timing : golden::rateTimings)
    {
        const auto lattice = FathomRateLattice::at(timing.hostRate);
        const auto label = "Fathom lattice at " + std::to_string(timing.hostRate) + " Hz: ";
        require(lattice.hostStep == timing.hostStep
                    && lattice.internalStep == timing.internalStep,
                label + "steps differ from the model");
        require(lattice.latencyFrames == timing.latencyFrames,
                label + "reported latency differs from the model");
        require(lattice.inputDelay == timing.inputDelay
                    && lattice.outputDelay == timing.outputDelay,
                label + "converter delays differ from the model");
        require(lattice.inputClockSign == timing.inputClockSign
                    && lattice.outputClockSign == timing.outputClockSign,
                label + "clock signs differ from the model");
        require(lattice.converts() == (timing.hostRate != 44100),
                label + "wrong choice of converting");
    }

    // 44 internal samples at the host rate, rounded to a multiple of four.
    constexpr std::array<std::array<int, 2>, 5> otherLatencies {{
        { 22050, 24 }, { 32000, 32 }, { 56000, 56 }, { 352800, 352 }, { 384000, 384 }
    }};
    for (const auto& [hostRate, latency] : otherLatencies)
        require(FathomRateLattice::at(hostRate).latencyFrames == latency,
                "Fathom reported latency rule fails at " + std::to_string(hostRate) + " Hz");

    // Clock signs away from the standard rates. At 56, 64 and 384 kHz the
    // measured pairs stand; at the other rates the signs are those of the
    // campaign's simulation of the reference's block clocks
    // (converters.clock_signs). The simulation alone gives the measured pairs
    // at 64 and 384 kHz and a high output clock at 56 kHz.
    constexpr std::array<std::array<int, 3>, 10> otherClockSigns {{
        { 22050, 0, 0 }, { 32000, -1, -1 }, { 44101, 1, -1 }, { 50000, -1, -1 },
        { 56000, -1, -1 }, { 64000, -1, -1 }, { 128000, -1, 1 }, { 352800, 0, 0 },
        { 383999, 1, -1 }, { 384000, 1, -1 }
    }};
    for (const auto& [hostRate, input, output] : otherClockSigns)
    {
        const auto lattice = FathomRateLattice::at(hostRate);
        require(lattice.inputClockSign == input && lattice.outputClockSign == output,
                "Fathom clock signs at " + std::to_string(hostRate) + " Hz are "
                    + std::to_string(lattice.inputClockSign) + " and "
                    + std::to_string(lattice.outputClockSign) + ", not the campaign's");
    }

    const auto& table = FathomConverter::kernelTable();
    double tableSum = 0.0;
    for (const auto entry : table)
        tableSum += static_cast<double>(entry);
    require(std::abs(tableSum - golden::converterTableSum) < 1.0e-9,
            "Fathom converter table does not sum to the model's");
    for (const auto& sample : golden::converterTableSamples)
        require(sameBits(table[static_cast<std::size_t>(sample.entry)], sample.value),
                "Fathom converter table differs at entry " + std::to_string(sample.entry));

    constexpr auto tolerance = 1.0e-12;
    const auto noiseFrames = static_cast<int>(std::size(golden::converterHostNoise));
    for (const auto& vector : golden::converterVectors)
    {
        const auto lattice = FathomRateLattice::at(vector.hostRate);
        const auto label = "Fathom converter at " + std::to_string(vector.hostRate) + " Hz: ";

        // Host frames to internal samples. The right channel carries the
        // inverted signal, so both channels are checked.
        FathomConverter toInternal;
        toInternal.prepare(lattice.hostStep, lattice.internalStep, lattice.inputDelay,
                           lattice.inputClockSign, 0);
        const auto internalCount = static_cast<int>(vector.toInternal.size());
        auto worst = 0.0;
        auto target = 0;
        for (auto frame = 0; target < vector.toInternalFirstSample + internalCount + 64; ++frame)
        {
            const auto noiseFrame = frame - vector.hostFirstFrame;
            const auto sample = noiseFrame >= 0 && noiseFrame < noiseFrames
                ? golden::converterHostNoise[noiseFrame] : 0.0;
            toInternal.write(sample, -sample);
            while (toInternal.lastSourceNeeded() < toInternal.written())
            {
                auto left = 0.0;
                auto right = 0.0;
                toInternal.read(left, right);
                const auto index = target - vector.toInternalFirstSample;
                const auto expected = index >= 0 && index < internalCount
                    ? vector.toInternal[static_cast<std::size_t>(index)] : 0.0;
                worst = std::max({ worst, std::abs(left - expected), std::abs(right + expected) });
                ++target;
            }
        }
        require(worst <= tolerance,
                label + "host to internal differs from the model by " + std::to_string(worst));

        // Internal samples to the reference's raw host frames.
        FathomConverter toHost;
        toHost.prepare(lattice.internalStep, lattice.hostStep, lattice.outputDelay,
                       lattice.outputClockSign, 0);
        const auto hostCount = static_cast<int>(vector.toHost.size());
        worst = 0.0;
        target = 0;
        for (auto sampleIndex = 0; target < vector.toHostFirstFrame + hostCount + 64; ++sampleIndex)
        {
            const auto noiseFrame = sampleIndex - vector.internalFirstSample;
            const auto sample = noiseFrame >= 0 && noiseFrame < noiseFrames
                ? golden::converterInternalNoise[noiseFrame] : 0.0;
            toHost.write(sample, -sample);
            while (toHost.lastSourceNeeded() < toHost.written())
            {
                auto left = 0.0;
                auto right = 0.0;
                toHost.read(left, right);
                const auto index = target - vector.toHostFirstFrame;
                const auto expected = index >= 0 && index < hostCount
                    ? vector.toHost[static_cast<std::size_t>(index)] : 0.0;
                worst = std::max({ worst, std::abs(left - expected), std::abs(right + expected) });
                ++target;
            }
        }
        require(worst <= tolerance,
                label + "internal to host differs from the model by " + std::to_string(worst));
    }
}

void testFathomEngineDeterminismAndClocks()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Fathom engine at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        // Long enough for every glide of the engine to end.
        const auto warmupFrames = static_cast<int>(sampleRate * 0.35);
        const auto programmeFrames = static_cast<int>(sampleRate * 0.25);
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 1.5f;
        parameters.sizeScale = 1.2f;
        parameters.preDelaySeconds = 0.004f;

        const auto idle = [](FathomEngine& engine, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
                engine.advanceIdle();
        };
        const auto excite = [](FathomEngine& engine, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
                static_cast<void>(engine.processSample(0.3f * fathomNoise(frame, 2),
                                                       0.3f * fathomNoise(frame, 3)));
        };

        FathomEngine fresh;
        fresh.setParameters(parameters);
        fresh.prepare(sampleRate);
        idle(fresh, warmupFrames);
        const auto expected = renderFathomProgramme(fresh, programmeFrames);
        auto peak = 0.0f;
        for (const auto sample : expected)
        {
            require(std::isfinite(sample), label + "the programme produced NaN/Inf");
            peak = std::max(peak, std::abs(sample));
        }
        require(peak > 1.0e-3f, label + "the programme left no wet signal");

        {
            FathomEngine second;
            second.setParameters(parameters);
            second.prepare(sampleRate);
            idle(second, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(second, programmeFrames), expected,
                                    label + "second instance");
        }
        {
            FathomEngine used;
            used.setParameters(parameters);
            used.prepare(sampleRate);
            excite(used, 6000);
            used.reset();
            idle(used, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(used, programmeFrames), expected,
                                    label + "after reset()");
        }
        {
            FathomEngine prepared;
            prepared.setParameters(parameters);
            prepared.prepare(sampleRate * 2.0);
            excite(prepared, 6000);
            prepared.prepare(sampleRate);
            idle(prepared, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(prepared, programmeFrames), expected,
                                    label + "after prepare() at another rate");
        }
        {
            FathomEngine silent;
            silent.setParameters(parameters);
            silent.prepare(sampleRate);
            for (auto frame = 0; frame < warmupFrames; ++frame)
            {
                const auto wet = silent.processSample(0.0f, 0.0f);
                require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                        label + "silence in did not give silence out");
            }
            requireSameFathomRender(renderFathomProgramme(silent, programmeFrames), expected,
                                    label + "processed silence against advanceIdle()");
        }
        {
            // Parameters the engine already has, and values that are no
            // numbers, must not disturb it.
            auto invalid = parameters;
            invalid.decaySeconds = std::numeric_limits<float>::quiet_NaN();
            invalid.sizeScale = std::numeric_limits<float>::infinity();
            invalid.preDelaySeconds = -std::numeric_limits<float>::infinity();
            invalid.macro = std::numeric_limits<float>::quiet_NaN();
            invalid.lowCutHz = std::numeric_limits<float>::quiet_NaN();
            invalid.highDampingHz = std::numeric_limits<float>::infinity();
            FathomEngine repeated;
            repeated.setParameters(parameters);
            repeated.prepare(sampleRate);
            idle(repeated, warmupFrames);
            std::vector<float> rendered;
            for (auto frame = 0; frame < programmeFrames; ++frame)
            {
                repeated.setParameters(frame % 2 == 0 ? parameters : invalid);
                const auto input = fathomProgramme(frame);
                const auto wet = repeated.processSample(input.left, input.right);
                rendered.push_back(wet.left);
                rendered.push_back(wet.right);
            }
            requireSameFathomRender(rendered, expected, label + "repeated and invalid setParameters()");
        }
        {
            // Every parameter arrives by its glide while the engine processes
            // silence; at rest the values must be exactly those of a fresh engine.
            FathomEngine::Parameters other;
            other.decaySeconds = 6.0f;
            other.sizeScale = 0.4f;
            other.preDelaySeconds = 0.020f;
            other.lowCutHz = 200.0f;
            other.highDampingHz = 5000.0f;
            other.freeze = true;
            FathomEngine glided;
            glided.setParameters(other);
            glided.prepare(sampleRate);
            for (auto frame = 0; frame < warmupFrames; ++frame)
            {
                if (frame == 300)
                    glided.setParameters(parameters);
                static_cast<void>(glided.processSample(0.0f, 0.0f));
            }
            requireSameFathomRender(renderFathomProgramme(glided, programmeFrames), expected,
                                    label + "parameters at rest after their glides");
        }

        // Left idle after sound, the engine starts from silence with its
        // clocks where a fresh instance has them, even after a single frame.
        for (const auto idleFrames : { 1, 2, 3000 })
        {
            constexpr auto soundFrames = 7000;
            FathomEngine reference;
            reference.setParameters(parameters);
            reference.prepare(sampleRate);
            idle(reference, soundFrames + idleFrames);
            const auto afterIdle = renderFathomProgramme(reference, programmeFrames);

            FathomEngine sounded;
            sounded.setParameters(parameters);
            sounded.prepare(sampleRate);
            excite(sounded, soundFrames);
            idle(sounded, idleFrames);
            requireSameFathomRender(renderFathomProgramme(sounded, programmeFrames), afterIdle,
                                    label + "after sound and " + std::to_string(idleFrames)
                                        + " idle frames");
        }
    }
}

void testFathomEngineSilenceHostileInputAndRates()
{
    // Every kind of rate ratio: none, whole, the common 160/147 family, rates
    // below the core's, the ends of the supported range, and a ratio with too
    // many branches to tabulate.
    constexpr std::array<double, 12> sampleRates {
        22050.0, 32000.0, 44100.0, 44101.0, 47952.0, 48000.0,
        88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0
    };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Fathom engine at " + std::to_string(static_cast<int>(sampleRate))
                         + " Hz ";
        FathomEngine engine;
        engine.prepare(sampleRate);
        for (auto frame = 0; frame < 3000; ++frame)
        {
            const auto wet = engine.processSample(0.0f, 0.0f);
            require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                    label + "turned silence into sound");
        }

        // An impulse on the left arrives at both outputs after the fixed delay
        // of the network and its shortest line, about 25 ms, and not before.
        // Above the core's rate one host frame is a shorter impulse; its
        // height makes up for that.
        const auto impulse = 0.5f * static_cast<float>(std::max(1.0, sampleRate / 44100.0));
        const auto responseFrames = static_cast<int>(sampleRate * 0.12);
        const auto earliestFrame = static_cast<int>(sampleRate * 0.020);
        auto firstFrame = -1;
        auto peak = 0.0f;
        for (auto frame = 0; frame < responseFrames; ++frame)
        {
            const auto wet = engine.processSample(frame == 0 ? impulse : 0.0f, 0.0f);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "produced NaN/Inf");
            const auto magnitude = std::max(std::abs(wet.left), std::abs(wet.right));
            if (firstFrame < 0 && magnitude > 1.0e-4f)
                firstFrame = frame;
            peak = std::max(peak, magnitude);
        }
        require(firstFrame >= earliestFrame && firstFrame < static_cast<int>(sampleRate * 0.030),
                label + "puts its first arrival at frame " + std::to_string(firstFrame));
        require(peak > 0.01f && peak < 0.2f,
                label + "has an implausible impulse response peak " + std::to_string(peak));
    }

    constexpr std::array<double, 4> hostileRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<float, 8> hostileSamples {
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
        -1.0e30f, std::numeric_limits<float>::denorm_min(), 1.0e-39f, -0.0f
    };
    for (const auto sampleRate : hostileRates)
    {
        const auto label = "Fathom engine at " + std::to_string(static_cast<int>(sampleRate))
                         + " Hz ";
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 30.0f;
        parameters.sizeScale = 2.0f;
        parameters.preDelaySeconds = 0.25f;
        FathomEngine engine;
        FathomEngine::LevelStage levelStage;
        engine.setParameters(parameters);
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);

        auto peak = 0.0f;
        const auto frames = static_cast<int>(sampleRate * 1.5);
        for (auto frame = 0; frame < frames; ++frame)
        {
            if (frame % 997 == 0)
            {
                // Parameters move through their extremes, with values no
                // parameter may take in between.
                const auto step = frame / 997;
                parameters.decaySeconds = step % 3 == 0 ? 0.2f : 60.0f;
                parameters.sizeScale = step % 2 == 0 ? 0.15f : 2.0f;
                parameters.preDelaySeconds = step % 5 == 0 ? 0.0f : 2.0f;
                parameters.lowCutHz = step % 4 == 0 ? 20.0f : 1000.0f;
                parameters.highDampingHz = step % 7 == 0 ? 20000.0f : 1000.0f;
                parameters.freeze = step % 6 == 5;
                engine.setParameters(parameters);
                auto invalid = parameters;
                invalid.decaySeconds = std::numeric_limits<float>::quiet_NaN();
                invalid.sizeScale = std::numeric_limits<float>::infinity();
                invalid.preDelaySeconds = -std::numeric_limits<float>::infinity();
                invalid.macro = std::numeric_limits<float>::quiet_NaN();
                invalid.lowCutHz = std::numeric_limits<float>::quiet_NaN();
                invalid.highDampingHz = std::numeric_limits<float>::quiet_NaN();
                engine.setParameters(invalid);
            }

            // Full-scale noise, with a sample that is no sample every so often.
            auto left = fathomNoise(frame, 4);
            auto right = fathomNoise(frame, 5);
            if (frame % 61 == 0)
                left = hostileSamples[static_cast<std::size_t>(frame / 61) % hostileSamples.size()];
            if (frame % 89 == 0)
                right = hostileSamples[static_cast<std::size_t>(frame / 89) % hostileSamples.size()];
            auto wet = levelStage.process(left, right, engine.processSample(left, right));
            wet = FathomEngine::applyWidth(wet, 2.0f);
            wet.left = FathomEngine::clip(wet.left);
            wet.right = FathomEngine::clip(wet.right);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "let hostile input through as NaN/Inf");
            peak = std::max({ peak, std::abs(wet.left), std::abs(wet.right) });
        }
        require(peak <= 3.99f, label + "left the clipper's range under hostile input");

        // Once the input is gone a short Decay brings the engine back to
        // exact silence.
        parameters.decaySeconds = 0.2f;
        parameters.sizeScale = 1.0f;
        parameters.preDelaySeconds = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.freeze = false;
        engine.setParameters(parameters);
        auto lastSound = -1;
        const auto tailFrames = static_cast<int>(sampleRate * 6.0);
        for (auto frame = 0; frame < tailFrames; ++frame)
        {
            const auto wet = engine.processSample(0.0f, 0.0f);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "kept NaN/Inf after hostile input");
            if (std::abs(wet.left) > 0.0f || std::abs(wet.right) > 0.0f)
                lastSound = frame;
        }
        require(lastSound < static_cast<int>(sampleRate * 5.0),
                label + "did not return to silence after hostile input");
    }
}

void testFathomEngineAllocatesOnlyInPrepare()
{
    constexpr std::array<double, 2> sampleRates { 48000.0, 44101.0 };
    for (const auto sampleRate : sampleRates)
    {
        FathomEngine engine;
        FathomEngine::LevelStage levelStage;
        FathomEngine::Parameters parameters;
        engine.setParameters(parameters);
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);
        const std::vector<double> prescribedPhase(300, 0.25);

        allocationCount.store(0, std::memory_order_relaxed);
        countAllocations.store(true, std::memory_order_relaxed);
        for (auto frame = 0; frame < 60000; ++frame)
        {
            if (frame % 211 == 0)
            {
                parameters.decaySeconds = parameters.decaySeconds > 1.0f ? 0.2f : 30.0f;
                parameters.sizeScale = parameters.sizeScale > 1.0f ? 0.15f : 2.0f;
                parameters.preDelaySeconds = parameters.preDelaySeconds > 0.1f ? 0.0f : 0.25f;
                parameters.macro = 1.0f - parameters.macro;
                parameters.lowCutHz = parameters.lowCutHz > 20.0f ? 20.0f : 400.0f;
                parameters.highDampingHz = parameters.highDampingHz < 20000.0f ? 20000.0f : 3000.0f;
                parameters.freeze = frame % 422 == 0;
                engine.setParameters(parameters);
                engine.setVoiceSeed(static_cast<std::uint64_t>(frame));
                const auto* phase = frame % 633 == 0 ? prescribedPhase.data() : nullptr;
                engine.setVoicePhaseForTesting(phase, phase, prescribedPhase.size());
            }
            if (frame % 17000 == 16000)
                engine.reset();
            if ((frame / 5000) % 3 == 2)
            {
                engine.advanceIdle();
                continue;
            }
            const auto left = 0.5f * fathomNoise(frame, 6);
            const auto right = 0.5f * fathomNoise(frame, 7);
            auto wet = levelStage.process(left, right, engine.processSample(left, right));
            wet = FathomEngine::applyWidth(wet, 1.3f);
            auto dryGain = 0.0f;
            auto wetGain = 0.0f;
            FathomEngine::mixGains(0.4f, dryGain, wetGain);
            static_cast<void>(
                FathomEngine::clip(FathomEngine::mix(dryGain, left, wetGain, wet.left)));
        }
        countAllocations.store(false, std::memory_order_relaxed);

        require(allocationCount.load(std::memory_order_relaxed) == 0,
                "Fathom engine allocated memory outside prepare()");
    }
}

void testFathomOuterLaws()
{
    namespace golden = amanita::dsp::fathomgolden;

    // Clipper: unity up to +8 dBFS, a knee, the ceiling of +12 dBFS.
    for (std::size_t index = 0; index < std::size(golden::clipInput); ++index)
    {
        const auto clipped = FathomEngine::clip(static_cast<float>(golden::clipInput[index]));
        require(std::abs(static_cast<double>(clipped) - golden::clipOutput[index]) <= 1.0e-6,
                "Fathom clipper differs from the model at input "
                    + std::to_string(golden::clipInput[index]));
    }
    const auto threshold = static_cast<float>(std::pow(10.0, 8.0 / 20.0));
    const auto ceiling = static_cast<float>(std::pow(10.0, 12.0 / 20.0));
    for (const auto sample : { 0.0f, 1.0e-30f, 0.5f, 1.0f, 2.5f, threshold })
        require(sameBits(FathomEngine::clip(sample), sample)
                    && sameBits(FathomEngine::clip(-sample), -sample),
                "Fathom clipper is not unity at " + std::to_string(sample));
    for (const auto sample : { 5.5f, 8.0f, 1.0e30f, std::numeric_limits<float>::infinity() })
        require(sameBits(FathomEngine::clip(sample), ceiling)
                    && sameBits(FathomEngine::clip(-sample), -ceiling),
                "Fathom clipper does not hold its ceiling at " + std::to_string(sample));
    require(sameBits(FathomEngine::clip(std::numeric_limits<float>::quiet_NaN()), 0.0f),
            "Fathom clipper passed NaN");
    auto previous = 0.0f;
    for (auto step = 0; step <= 7000; ++step)
    {
        const auto clipped = FathomEngine::clip(0.001f * static_cast<float>(step));
        require(clipped >= previous && clipped <= ceiling, "Fathom clipper is not monotonic");
        previous = clipped;
    }

    // Width: mid gain sqrt(2 / (1 + s)), side gain s times that.
    const auto frameCount = std::size(golden::widthInput) / 2;
    for (std::size_t scale = 0; scale < std::size(golden::widthScale); ++scale)
    {
        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            const auto widened = FathomEngine::applyWidth(
                { static_cast<float>(golden::widthInput[2 * frame]),
                  static_cast<float>(golden::widthInput[2 * frame + 1]) },
                static_cast<float>(golden::widthScale[scale]));
            const auto* expected = golden::widthOutput + 2 * (scale * frameCount + frame);
            require(std::abs(static_cast<double>(widened.left) - expected[0]) <= 1.0e-6
                        && std::abs(static_cast<double>(widened.right) - expected[1]) <= 1.0e-6,
                    "Fathom Width law differs from the model at scale "
                        + std::to_string(golden::widthScale[scale]));
        }
    }
    for (auto frame = 0; frame < 64; ++frame)
    {
        const FathomEngine::Frame wet { fathomNoise(frame, 8), 0.37f * fathomNoise(frame, 9) };
        const auto unchanged = FathomEngine::applyWidth(wet, 1.0f);
        require(sameBits(unchanged.left, wet.left) && sameBits(unchanged.right, wet.right),
                "Fathom Width law is not the identity at 100 %");
        const auto mono = FathomEngine::applyWidth(wet, 0.0f);
        require(sameBits(mono.left, mono.right)
                    && std::abs(mono.left - std::sqrt(0.5f) * (wet.left + wet.right)) <= 1.0e-6f,
                "Fathom Width law is not the scaled Mid at 0 %");
    }
    // The identity holds where one channel lies far below the other and the
    // sum of mid and side would round it.
    for (const auto wet : { FathomEngine::Frame { 1.0f, 1.0e-12f }, FathomEngine::Frame { 3.0e-11f, -0.5f } })
    {
        const auto unchanged = FathomEngine::applyWidth(wet, 1.0f);
        require(sameBits(unchanged.left, wet.left) && sameBits(unchanged.right, wet.right),
                "Fathom Width law rounds the quieter channel at 100 %");
    }
    const auto widest = FathomEngine::applyWidth({ 1.0f, -1.0f }, 2.0f);
    require(std::abs(widest.left - 2.0f * std::sqrt(2.0f / 3.0f)) <= 1.0e-6f
                && sameBits(widest.left, -widest.right),
            "Fathom Width law has the wrong Side gain at its maximum");

    // Mix: the dry stays at unity up to 50 %, the wet reaches unity there.
    for (std::size_t index = 0; index < std::size(golden::mixValue); ++index)
    {
        auto dryGain = -1.0f;
        auto wetGain = -1.0f;
        FathomEngine::mixGains(static_cast<float>(golden::mixValue[index]), dryGain, wetGain);
        require(std::abs(static_cast<double>(dryGain) - golden::mixDryAndWetGain[2 * index]) <= 1.0e-7
                    && std::abs(static_cast<double>(wetGain) - golden::mixDryAndWetGain[2 * index + 1]) <= 1.0e-7,
                "Fathom Mix law differs from the model at "
                    + std::to_string(golden::mixValue[index]));
    }
    constexpr std::array<std::array<float, 3>, 5> mixPoints {{
        { 0.0f, 1.0f, 0.0f }, { 0.25f, 1.0f, 0.5f }, { 0.5f, 1.0f, 1.0f },
        { 0.75f, 0.5f, 1.0f }, { 1.0f, 0.0f, 1.0f }
    }};
    for (const auto& point : mixPoints)
    {
        auto dryGain = -1.0f;
        auto wetGain = -1.0f;
        FathomEngine::mixGains(point[0], dryGain, wetGain);
        require(sameBits(dryGain, point[1]) && sameBits(wetGain, point[2]),
                "Fathom Mix law misses its defining point at " + std::to_string(point[0]));
    }

    // The Mix sum rounds each product before it adds. The products pass
    // through memory here, so no build of this file can fuse them with the
    // addition; a fused sum rounds once and differs in part of the frames.
    auto fusedSumsThatDiffer = 0;
    for (const auto mix : { 0.3f, 0.7f })
    {
        auto dryGain = 0.0f;
        auto wetGain = 0.0f;
        FathomEngine::mixGains(mix, dryGain, wetGain);
        for (auto frame = 0; frame < 4000; ++frame)
        {
            const auto dry = fathomNoise(frame, 30);
            const auto wet = fathomNoise(frame, 31);
            volatile float dryShare = dryGain * dry;
            volatile float wetShare = wetGain * wet;
            const float separate = dryShare + wetShare;
            require(sameBits(FathomEngine::mix(dryGain, dry, wetGain, wet), separate),
                    "Fathom Mix sum does not round each product on its own at Mix "
                        + std::to_string(mix) + ", frame " + std::to_string(frame));
            const float wetProduct = wetShare;
            if (!sameBits(std::fma(dryGain, dry, wetProduct), separate))
                ++fusedSumsThatDiffer;
        }
    }
    require(fusedSumsThatDiffer > 100, "Fathom Mix sum was not tried where a fused sum differs");

    // Level stage against the model's single-precision reduction, bit for bit.
    FathomEngine::LevelStage modelStage;
    modelStage.prepare(static_cast<double>(golden::levelStageHostRate));
    for (std::size_t frame = 0; frame < std::size(golden::levelStageReductionDb); ++frame)
    {
        const auto reductionDb = golden::levelStageReductionDb[frame];
        const auto gain = reductionDb < 0.0f
            ? std::pow(10.0, static_cast<double>(reductionDb) / 20.0) : 1.0;
        const auto wet = modelStage.process(golden::levelStageInput[2 * frame],
                                              golden::levelStageInput[2 * frame + 1],
                                              { 1.0f, -0.5f });
        require(sameBits(wet.left, static_cast<float>(gain))
                    && sameBits(wet.right, static_cast<float>(gain * -0.5)),
                "Fathom level stage differs from the model at frame " + std::to_string(frame));
    }

    constexpr auto sampleRate = 48000.0;
    constexpr auto kneeDb = 9.98306;
    const auto reductionAfter = [](FathomEngine::LevelStage& levelStage, float left, float right,
                                   int frames)
    {
        auto wet = FathomEngine::Frame { 1.0f, 1.0f };
        for (auto frame = 0; frame < frames; ++frame)
            wet = levelStage.process(left, right, { 1.0f, 1.0f });
        return 20.0 * std::log10(static_cast<double>(wet.left));
    };

    // Nothing happens to the wet while every input sample stays under the
    // knee, which starts half its width below 0 dBFS.
    FathomEngine::LevelStage stage;
    stage.prepare(sampleRate);
    const auto kneeStart = static_cast<float>(std::pow(10.0, -kneeDb / 40.0));
    for (auto frame = 0; frame < 20000; ++frame)
    {
        const FathomEngine::Frame wet { fathomNoise(frame, 10), fathomNoise(frame, 11) };
        const auto key = (kneeStart - 1.0e-6f) * fathomNoise(frame, 12);
        const auto staged = stage.process(key, -key, wet);
        require(sameBits(staged.left, wet.left) && sameBits(staged.right, wet.right),
                "Fathom level stage touched the wet below its knee");
    }
    require(reductionAfter(stage, kneeStart + 1.0e-3f, 0.0f, 48000) < -1.0e-7,
            "Fathom level stage did not start at its knee");

    // Static curve: 5/7 dB per dB above the knee, a parabola through it,
    // keyed by the larger magnitude of the two inputs. The single-precision
    // smoother comes to rest where its step rounds away, up to 2e-4 dB short
    // of the curve on the way down and 1e-3 dB on the way back.
    stage.reset();
    require(std::abs(reductionAfter(stage, 2.0f, 0.0f, 96000)
                     + (5.0 / 7.0) * 20.0 * std::log10(2.0)) < 2.0e-4,
            "Fathom level stage misses its slope at +6 dBFS");
    require(std::abs(reductionAfter(stage, 0.1f, -4.0f, 96000)
                     + (5.0 / 7.0) * 20.0 * std::log10(4.0)) < 2.0e-4,
            "Fathom level stage misses its slope at +12 dBFS on the right input");
    require(std::abs(reductionAfter(stage, -1.0f, 1.0f, 192000) + (5.0 / 7.0) * kneeDb / 8.0) < 1.0e-3,
            "Fathom level stage misses the middle of its knee at 0 dBFS");

    // 5 ms towards more reduction, 300 ms back.
    stage.reset();
    const auto targetDb = -(5.0 / 7.0) * 20.0 * std::log10(2.0);
    const auto attackedDb = reductionAfter(stage, 2.0f, 2.0f, static_cast<int>(sampleRate * 0.005));
    require(std::abs(attackedDb / targetDb - (1.0 - std::exp(-1.0))) < 1.0e-3,
            "Fathom level stage attack is not 5 ms");
    const auto settledDb = reductionAfter(stage, 2.0f, 2.0f, 96000);
    const auto releasedDb = reductionAfter(stage, 0.0f, 0.0f, static_cast<int>(sampleRate * 0.300));
    require(std::abs(releasedDb / settledDb - std::exp(-1.0)) < 1.0e-3,
            "Fathom level stage release is not 300 ms");

    // Input that is no number counts as silence, and reset() ends the reduction.
    static_cast<void>(reductionAfter(stage, 3.0f, 3.0f, 4800));
    stage.reset();
    for (const auto key : { std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity() })
    {
        const auto staged = stage.process(key, -key, { 0.25f, -0.75f });
        require(sameBits(staged.left, 0.25f) && sameBits(staged.right, -0.75f),
                "Fathom level stage reacted to a non-finite input or kept its state over reset()");
    }
}

// Energy of the engine's wet output over `frames` frames of silent input.
[[nodiscard]] double fathomSilentEnergy(FathomEngine& engine, int frames)
{
    double energy = 0.0;
    for (auto frame = 0; frame < frames; ++frame)
    {
        const auto wet = engine.processSample(0.0f, 0.0f);
        energy += static_cast<double>(wet.left) * wet.left + static_cast<double>(wet.right) * wet.right;
    }
    return energy;
}

void testFathomOceanLoopControls()
{
    constexpr auto sampleRate = 44100.0;
    constexpr auto twoPi = 6.28318530717958647692;
    FathomEngine::Parameters neutral;
    neutral.decaySeconds = 3.0f;
    neutral.lowCutHz = 20.0f;
    neutral.highDampingHz = 20000.0f;

    // At and beyond their neutral positions the two filters are out of the
    // circuit: the output is that of an engine that never had them.
    {
        const auto programmeFrames = 20000;
        FathomEngine plain;
        plain.setParameters(neutral);
        plain.prepare(sampleRate);
        const auto expected = renderFathomProgramme(plain, programmeFrames);

        auto beyond = neutral;
        beyond.lowCutHz = 5.0f;
        beyond.highDampingHz = 40000.0f;
        FathomEngine clamped;
        clamped.setParameters(beyond);
        clamped.prepare(sampleRate);
        requireSameFathomRender(renderFathomProgramme(clamped, programmeFrames), expected,
                                "Fathom loop filters beyond their neutral positions");

        // Engaged and released again before the programme, the filters leave
        // nothing behind once their fades have ended.
        const auto warmupFrames = static_cast<int>(sampleRate * 0.2);
        FathomEngine fresh;
        fresh.setParameters(neutral);
        fresh.prepare(sampleRate);
        for (auto frame = 0; frame < warmupFrames; ++frame)
            fresh.advanceIdle();
        const auto afterWarmup = renderFathomProgramme(fresh, programmeFrames);

        auto engaged = neutral;
        engaged.lowCutHz = 300.0f;
        engaged.highDampingHz = 4000.0f;
        FathomEngine released;
        released.setParameters(neutral);
        released.prepare(sampleRate);
        for (auto frame = 0; frame < warmupFrames; ++frame)
        {
            if (frame == 10)
                released.setParameters(engaged);
            if (frame == 3000)
                released.setParameters(neutral);
            static_cast<void>(released.processSample(0.0f, 0.0f));
        }
        requireSameFathomRender(renderFathomProgramme(released, programmeFrames), afterWarmup,
                                "Fathom loop filters after they were engaged and released");
    }

    // In the circuit they act on the recirculating sound only: the first pass
    // through the lines leaves before the loop filters, a later tail has gone
    // through them many times.
    const auto tailEnergy = [&](const FathomEngine::Parameters& parameters, double frequencyHz,
                                std::vector<float>& firstPass)
    {
        constexpr auto burstFrames = 8820;
        FathomEngine engine;
        engine.setParameters(parameters);
        engine.prepare(sampleRate);
        firstPass.clear();
        for (auto frame = 0; frame < burstFrames; ++frame)
        {
            const auto tone = 0.25f * static_cast<float>(
                std::sin(twoPi * frequencyHz * static_cast<double>(frame) / sampleRate));
            const auto wet = engine.processSample(frame == 0 ? 0.5f + tone : tone, tone);
            // No line is shorter than 1031 - 39 samples, so nothing has come
            // round a second time within 1900 frames.
            if (frame < 1900)
            {
                firstPass.push_back(wet.left);
                firstPass.push_back(wet.right);
            }
        }
        static_cast<void>(fathomSilentEnergy(engine, static_cast<int>(sampleRate * 0.5)));
        return fathomSilentEnergy(engine, static_cast<int>(sampleRate * 0.5));
    };

    std::vector<float> neutralFirstPass;
    std::vector<float> shapedFirstPass;
    auto lowCut = neutral;
    lowCut.lowCutHz = 500.0f;
    const auto neutralLowEnergy = tailEnergy(neutral, 70.0, neutralFirstPass);
    const auto cutLowEnergy = tailEnergy(lowCut, 70.0, shapedFirstPass);
    requireSameFathomRender(shapedFirstPass, neutralFirstPass, "Fathom Low Cut on the first pass");
    require(neutralLowEnergy > 1.0e-6, "Fathom tail of a 70 Hz burst is missing");
    std::cout << "[METRIC] Fathom Low Cut 500 Hz on a 70 Hz tail: "
              << 10.0 * std::log10(cutLowEnergy / neutralLowEnergy) << " dB\n";
    require(cutLowEnergy < 0.01 * neutralLowEnergy,
            "Fathom Low Cut does not remove low frequencies from the tail");

    auto damped = neutral;
    damped.highDampingHz = 1500.0f;
    const auto neutralHighEnergy = tailEnergy(neutral, 6000.0, neutralFirstPass);
    const auto dampedHighEnergy = tailEnergy(damped, 6000.0, shapedFirstPass);
    requireSameFathomRender(shapedFirstPass, neutralFirstPass,
                            "Fathom High Damping on the first pass");
    require(neutralHighEnergy > 1.0e-6, "Fathom tail of a 6 kHz burst is missing");
    std::cout << "[METRIC] Fathom High Damping 1.5 kHz on a 6 kHz tail: "
              << 10.0 * std::log10(dampedHighEnergy / neutralHighEnergy) << " dB\n";
    require(dampedHighEnergy < 0.01 * neutralHighEnergy,
            "Fathom High Damping does not remove high frequencies from the tail");

    // Each filter leaves the other end of the spectrum nearly alone. The low
    // cut is the input less its low-passed part, which also takes a fraction
    // of a decibel per pass from everything above it, as it does in FDNReverb.
    const auto cutHighEnergy = tailEnergy(lowCut, 6000.0, shapedFirstPass);
    const auto dampedLowEnergy = tailEnergy(damped, 70.0, shapedFirstPass);
    std::cout << "[METRIC] Fathom Low Cut 500 Hz on a 6 kHz tail: "
              << 10.0 * std::log10(cutHighEnergy / neutralHighEnergy)
              << " dB, High Damping 1.5 kHz on a 70 Hz tail: "
              << 10.0 * std::log10(dampedLowEnergy / neutralLowEnergy) << " dB\n";
    require(cutHighEnergy > 0.25 * neutralHighEnergy && dampedLowEnergy > 0.5 * neutralLowEnergy,
            "Fathom loop filters reach beyond their own end of the spectrum");
}

void testFathomFreezeHold()
{
    constexpr std::array<double, 2> sampleRates { 44100.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Fathom Freeze at " + std::to_string(static_cast<int>(sampleRate))
                         + " Hz ";
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 1.0f;
        parameters.lowCutHz = 250.0f;
        parameters.highDampingHz = 6000.0f;
        FathomEngine held;
        FathomEngine fed;
        FathomEngine released;
        for (auto* engine : { &held, &fed, &released })
        {
            engine->setParameters(parameters);
            engine->prepare(sampleRate);
        }

        // A band of noise below 1 kHz, so that the hold is judged on sound
        // the interpolated line reads wear down slowly.
        const auto burstFrames = static_cast<int>(sampleRate * 0.3);
        const auto smoothing = static_cast<float>(0.06 * 44100.0 / sampleRate);
        auto lowPassed = 0.0f;
        auto smoothed = 0.0f;
        for (auto frame = 0; frame < burstFrames; ++frame)
        {
            lowPassed += smoothing * (fathomNoise(frame, 13) - lowPassed);
            smoothed += smoothing * (lowPassed - smoothed);
            const auto input = 2.0f * (lowPassed - smoothed);
            for (auto* engine : { &held, &fed, &released })
                static_cast<void>(engine->processSample(input, -0.6f * input));
        }

        parameters.freeze = true;
        for (auto* engine : { &held, &fed, &released })
            engine->setParameters(parameters);
        const auto second = static_cast<int>(sampleRate);
        const auto fadeFrames = static_cast<int>(sampleRate * 0.1);
        static_cast<void>(fathomSilentEnergy(held, fadeFrames));
        static_cast<void>(fathomSilentEnergy(fed, fadeFrames));
        static_cast<void>(fathomSilentEnergy(released, fadeFrames));

        // Held, the engine takes no input: one that is fed full-scale noise
        // sounds like one that is fed silence, bit for bit.
        double firstEnergy = 0.0;
        double lastEnergy = 0.0;
        for (auto frame = 0; frame < 4 * second; ++frame)
        {
            const auto quiet = held.processSample(0.0f, 0.0f);
            const auto loud = fed.processSample(fathomNoise(frame, 14), fathomNoise(frame, 15));
            require(sameBits(quiet.left, loud.left) && sameBits(quiet.right, loud.right),
                    label + "lets input into the held loop");
            const auto energy = static_cast<double>(quiet.left) * quiet.left
                              + static_cast<double>(quiet.right) * quiet.right;
            if (frame < second)
                firstEnergy += energy;
            if (frame >= 3 * second)
                lastEnergy += energy;
        }
        const auto heldChangeDb = 10.0 * std::log10(lastEnergy / firstEnergy);
        std::cout << "[METRIC] " << label << "change over 3 s: " << heldChangeDb << " dB\n";
        require(firstEnergy > 1.0e-6, label + "holds nothing");
        // Decay 1 s alone would take 180 dB in that time. The hold is lossless
        // apart from what linear interpolation takes from the top.
        require(heldChangeDb > -3.0 && heldChangeDb < 0.5, label + "does not hold the tail");

        // Released, the tail takes up its Decay again and the input returns.
        parameters.freeze = false;
        released.setParameters(parameters);
        const auto releasedEarly = fathomSilentEnergy(released, second / 2);
        static_cast<void>(fathomSilentEnergy(released, second / 2));
        const auto releasedLate = fathomSilentEnergy(released, second / 2);
        require(releasedLate < 1.0e-4 * releasedEarly, label + "does not decay after release");
        auto inputEnergy = 0.0;
        for (auto frame = 0; frame < second / 2; ++frame)
        {
            const auto wet = released.processSample(0.3f * fathomNoise(frame, 16), 0.0f);
            inputEnergy += static_cast<double>(wet.left) * wet.left;
        }
        require(inputEnergy > 1.0e3 * releasedLate, label + "stays closed to the input after release");
    }
}

void testFathomParameterGlides()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto twoPi = 6.28318530717958647692;
    constexpr auto changeFrame = 72000;

    struct GlideCase
    {
        const char* name;
        FathomEngine::Parameters target;
        double glideSeconds;
        double toneHz;
    };
    FathomEngine::Parameters base;
    base.decaySeconds = 0.5f;
    base.preDelaySeconds = 0.010f;
    auto decay = base;
    decay.decaySeconds = 8.0f;
    auto size = base;
    size.sizeScale = 1.2f;
    auto preDelay = base;
    preDelay.preDelaySeconds = 0.035f;
    auto lowCut = base;
    lowCut.lowCutHz = 800.0f;
    auto damping = base;
    damping.highDampingHz = 1000.0f;
    auto freeze = base;
    freeze.freeze = true;
    const std::array glideCases {
        GlideCase { "Decay", decay, 0.25, 90.0 },
        GlideCase { "Size", size, 0.25, 90.0 },
        GlideCase { "Pre-delay", preDelay, 0.05, 90.0 },
        GlideCase { "Low Cut", lowCut, 0.05, 90.0 },
        GlideCase { "High Damping", damping, 0.05, 3000.0 },
        GlideCase { "Freeze", freeze, 0.05, 90.0 }
    };

    // Under a steady tone, an engine whose parameter changes is compared with
    // one that keeps it. A parameter that glides lets the two drift apart no
    // faster than its glide runs; one that stepped would open the whole
    // difference in a single frame, thousands of times that rate.
    constexpr auto driftLimit = 10.0;
    for (const auto& glideCase : glideCases)
    {
        FathomEngine kept;
        FathomEngine changed;
        kept.setParameters(base);
        changed.setParameters(base);
        kept.prepare(sampleRate);
        changed.prepare(sampleRate);

        const auto glideFrames = static_cast<int>(sampleRate * glideCase.glideSeconds);
        auto localPeak = 0.0;
        auto worstDrift = 0.0;
        auto lastDifference = 0.0;
        for (auto frame = 0; frame < changeFrame + glideFrames; ++frame)
        {
            if (frame == changeFrame)
                changed.setParameters(glideCase.target);
            const auto tone = 0.25f * static_cast<float>(
                std::sin(twoPi * glideCase.toneHz * static_cast<double>(frame) / sampleRate));
            const auto keptWet = kept.processSample(tone, -0.5f * tone);
            const auto changedWet = changed.processSample(tone, -0.5f * tone);
            if (frame < changeFrame)
            {
                require(sameBits(keptWet.left, changedWet.left)
                            && sameBits(keptWet.right, changedWet.right),
                        std::string("Fathom ") + glideCase.name + " differs before its change");
                continue;
            }
            localPeak = std::max({ localPeak,
                                   static_cast<double>(std::abs(keptWet.left)),
                                   static_cast<double>(std::abs(keptWet.right)) });
            lastDifference = static_cast<double>(std::max(std::abs(changedWet.left - keptWet.left),
                                                          std::abs(changedWet.right - keptWet.right)));
            const auto glided = static_cast<double>(frame - changeFrame + 1)
                              / static_cast<double>(glideFrames);
            worstDrift = std::max(worstDrift, lastDifference / glided);
        }

        std::cout << "[METRIC] Fathom " << glideCase.name << " glide: worst difference/glide="
                  << worstDrift / localPeak << " of the wet peak, limit=" << driftLimit
                  << ", difference at its end=" << lastDifference / localPeak << '\n';
        require(localPeak > 1.0e-3, std::string("Fathom ") + glideCase.name + " glide has no steady wet");
        require(worstDrift <= driftLimit * localPeak,
                std::string("Fathom ") + glideCase.name + " steps instead of gliding");
        require(lastDifference > 1.0e-3 * localPeak,
                std::string("Fathom ") + glideCase.name + " has no effect");
    }
}

// Share of the energy of a signal that lies more than a factor of 1.6 (0.68
// octave) from a tone, in decibels, over Hann windows of 4096 frames that
// overlap by half.
[[nodiscard]] double fathomOffToneDb(std::span<const float> signal, double toneHz,
                                     double sampleRate)
{
    constexpr std::size_t windowSize = 4096;
    constexpr auto twoPi = 6.28318530717958647692;
    auto totalEnergy = 0.0;
    auto offToneEnergy = 0.0;
    std::vector<std::complex<double>> spectrum(windowSize);
    for (std::size_t start = 0; start + windowSize <= signal.size(); start += windowSize / 2)
    {
        for (std::size_t index = 0; index < windowSize; ++index)
        {
            const auto window = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(index) / static_cast<double>(windowSize));
            spectrum[index] = static_cast<double>(signal[start + index]) * window;
        }
        driftRegressionFft(spectrum);
        for (std::size_t bin = 0; bin <= windowSize / 2; ++bin)
        {
            const auto frequency = static_cast<double>(bin) * sampleRate / windowSize;
            const auto energy = std::norm(spectrum[bin]);
            totalEnergy += energy;
            if (frequency < toneHz / 1.6 || frequency > toneHz * 1.6)
                offToneEnergy += energy;
        }
    }
    return 10.0 * std::log10(std::max(offToneEnergy, 1.0e-300) / std::max(totalEnergy, 1.0e-300));
}

// Size turned while a steady tone sounds. Each line moves from one whole
// length to the next through the fractions in between, so the turn bends the
// pitch of the tone and adds nothing else: what lies away from the tone stays
// far under it. A line that jumped over whole samples would put a click into
// the output at every jump, noise across the whole band 19 to 34 dB under
// the tone.
void testFathomSizeInMotion()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto twoPi = 6.28318530717958647692;
    constexpr auto blockFrames = 512;
    constexpr auto settleFrames = 3 * 48000;
    constexpr auto turnFrames = 2 * 48000;
    constexpr auto offToneLimitDb = -50.0;

    for (const auto macro : { 0.0f, 1.0f })
    {
        for (const auto toneHz : { 1000.0, 3000.0, 8000.0 })
        {
            const auto label = "Fathom Size turned under a "
                             + std::to_string(static_cast<int>(toneHz)) + " Hz tone at Macro "
                             + std::to_string(static_cast<int>(100.0f * macro)) + " %";
            FathomEngine::Parameters parameters;
            parameters.decaySeconds = 2.0f;
            parameters.macro = macro;
            FathomEngine engine;
            engine.setParameters(parameters);
            engine.prepare(sampleRate);

            std::vector<float> turned;
            turned.reserve(turnFrames);
            auto peak = 0.0f;
            for (auto frame = 0; frame < settleFrames + turnFrames; ++frame)
            {
                // The knob goes from 100 to 110 % at 5 % per second in its
                // steps of 0.1 %, handed over once per block.
                if (frame >= settleFrames && frame % blockFrames == 0)
                {
                    const auto steps = std::floor(
                        50.0 * static_cast<double>(frame - settleFrames) / sampleRate);
                    parameters.sizeScale = 1.0f + 0.001f * static_cast<float>(steps);
                    engine.setParameters(parameters);
                }
                const auto time = static_cast<double>(frame) / sampleRate;
                const auto wet = engine.processSample(
                    0.25f * static_cast<float>(std::sin(twoPi * toneHz * time)),
                    0.25f * static_cast<float>(std::sin(twoPi * toneHz * time + 1.0)));
                if (frame >= settleFrames)
                {
                    turned.push_back(wet.left);
                    peak = std::max(peak, std::abs(wet.left));
                }
            }

            const auto offToneDb = fathomOffToneDb(turned, toneHz, sampleRate);
            std::cout << "[METRIC] " << label << ": energy away from the tone=" << offToneDb
                      << " dB of all, limit=" << offToneLimitDb << " dB\n";
            require(peak > 1.0e-3f, label + " has no steady wet");
            require(offToneDb <= offToneLimitDb,
                    label + " adds noise away from the tone: " + std::to_string(offToneDb) + " dB");
        }
    }
}

// A tail that has died leaves the engine at rest: its output is exact silence
// and none of its filters goes on computing in the denormal range, which the
// underflow flag of the floating-point environment would show.
void testFathomEngineComesToRest()
{
    struct RestCase
    {
        double sampleRate;
        float macro;
        float lowCutHz;
        float highDampingHz;
    };
    constexpr std::array restCases {
        RestCase { 44100.0, 0.0f, 20.0f, 20000.0f },
        RestCase { 48000.0, 1.0f, 20.0f, 20000.0f },
        RestCase { 44100.0, 1.0f, 300.0f, 5000.0f }
    };

    for (const auto& restCase : restCases)
    {
        const auto label = "Fathom engine at "
                         + std::to_string(static_cast<int>(restCase.sampleRate))
                         + " Hz, Macro " + std::to_string(static_cast<int>(100.0f * restCase.macro))
                         + " %, Low Cut " + std::to_string(static_cast<int>(restCase.lowCutHz)) + " Hz";
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 0.5f;
        parameters.macro = restCase.macro;
        parameters.lowCutHz = restCase.lowCutHz;
        parameters.highDampingHz = restCase.highDampingHz;
        FathomEngine engine;
        engine.setParameters(parameters);
        engine.prepare(restCase.sampleRate);

        // A second of noise, then silence: Decay 0.5 s takes the tail under
        // the floor of the lines, 600 dB down, within five seconds.
        const auto second = static_cast<int>(restCase.sampleRate);
        auto peak = 0.0f;
        for (auto frame = 0; frame < second; ++frame)
        {
            const auto wet = engine.processSample(0.25f * fathomNoise(frame, 32), 0.25f * fathomNoise(frame, 33));
            peak = std::max({ peak, std::abs(wet.left), std::abs(wet.right) });
        }
        require(peak > 0.01f, label + " has no tail to die");
        static_cast<void>(fathomSilentEnergy(engine, 6 * second));

        std::feclearexcept(FE_UNDERFLOW);
        const auto restEnergy = fathomSilentEnergy(engine, second);
        const auto underflowed = std::fetestexcept(FE_UNDERFLOW) != 0;
        require(!(restEnergy > 0.0), label + " still sounds six seconds after its input");
        require(!underflowed, label + " computes in the denormal range after its tail has died");
    }
}

void testFathomVoicePhaseGenerator()
{
    namespace fathom = amanita::dsp::fathom;
    // The campaign's numbers (findings/tide_phase.md and tide_model.md): the
    // grid and the bounds of the level of each output, the share of
    // instances whose two start levels lie in the same half of their ranges,
    // and the rate of the phase as it was read at seven Macro values.
    constexpr std::array<double, 2> cellSeconds { 7.3529411764705882, 6.25 };
    constexpr std::array<double, 2> levelLow { 0.184, -0.007 };
    constexpr std::array<double, 2> levelHigh { 0.591, 0.433 };
    constexpr auto startSameHalf = 0.83;
    struct RateReading
    {
        double macro;
        double cyclesPerSecond;
    };
    constexpr std::array rateReadings {
        RateReading { 0.02, 0.0503272 }, RateReading { 0.05, 0.0504631 },
        RateReading { 0.10, 0.0506959 }, RateReading { 0.25, 0.0514399 },
        RateReading { 0.50, 0.0528473 }, RateReading { 0.75, 0.0544952 },
        RateReading { 1.00, 0.0564239 }
    };
    constexpr auto blockSeconds = static_cast<double>(fathom::voiceBlockSamples)
                                / fathom::internalRate;

    for (const auto& reading : rateReadings)
        require(std::abs(FathomVoicePhase::rate(reading.macro) - reading.cyclesPerSecond) <= 5.0e-7,
                "Fathom phase rate misses the campaign's reading at Macro "
                    + std::to_string(reading.macro));

    // The phase is the level plus a ramp that rises at the rate of the Macro
    // of each block: at one Macro a straight line, after a change the sum of
    // both stretches.
    {
        constexpr auto blocks = 60000;
        FathomVoicePhase steady;
        FathomVoicePhase moved;
        steady.reset(11);
        moved.reset(11);
        for (auto block = 0; block < blocks; ++block)
        {
            steady.advance(1.0);
            moved.advance(block < blocks / 2 ? 0.25 : 1.0);
        }
        const auto seconds = blocks * blockSeconds;
        for (std::size_t output = 0; output < FathomVoicePhase::outputCount; ++output)
        {
            const auto level = steady.level(output, seconds);
            require(std::abs(steady.phase(output) - level - 0.0564238 * seconds) <= 1.0e-9,
                    "Fathom phase does not rise at its rate at Macro 100 %");
            require(std::abs(moved.phase(output) - level
                             - 0.5 * (FathomVoicePhase::rate(0.25) + FathomVoicePhase::rate(1.0)) * seconds)
                        <= 1.0e-9,
                    "Fathom phase does not follow a Macro that moves");
        }
    }

    // One knot in every cell of an output's grid, each level inside the
    // bounds, the start held up to the first knot and a raised cosine from
    // knot to knot. Over many instances the places in the cells and the
    // levels are uniform and independent.
    constexpr auto instances = 200;
    constexpr auto knotsPerInstance = 40;
    for (std::size_t output = 0; output < FathomVoicePhase::outputCount; ++output)
    {
        const auto label = std::string("Fathom phase level of the ")
                         + (output == 0 ? "left" : "right") + " output ";
        const auto span = levelHigh[output] - levelLow[output];
        auto placeSum = 0.0;
        auto placeSquares = 0.0;
        auto placeProducts = 0.0;
        auto levelSum = 0.0;
        auto levelSquares = 0.0;
        auto levelProducts = 0.0;
        auto count = 0;
        for (auto instance = 0; instance < instances; ++instance)
        {
            FathomVoicePhase generator;
            generator.reset(static_cast<std::uint64_t>(instance));
            const auto start = generator.knot(output, 0);
            const auto first = generator.knot(output, 1);
            require(sameBits(start.seconds, 0.0) && sameBits(first.level, start.level),
                    label + "does not hold its start value up to the first knot");
            require(sameBits(generator.level(output, 0.0), start.level)
                        && sameBits(generator.level(output, 0.999 * first.seconds), start.level),
                    label + "moves before its first knot");

            auto previousPlace = 0.5;
            auto previousLevel = 0.5;
            for (auto index = 1; index <= knotsPerInstance; ++index)
            {
                const auto knot = generator.knot(output, index);
                const auto next = generator.knot(output, index + 1);
                const auto place = knot.seconds / cellSeconds[output] - index;
                require(place >= 0.0 && place < 1.0,
                        label + "has knot " + std::to_string(index) + " outside its cell");
                require(knot.level >= levelLow[output] && knot.level <= levelHigh[output],
                        label + "leaves its bounds at knot " + std::to_string(index));

                const auto length = next.seconds - knot.seconds;
                require(std::abs(generator.level(output, knot.seconds) - knot.level) <= 1.0e-12
                            && std::abs(generator.level(output, knot.seconds + 0.5 * length)
                                        - 0.5 * (knot.level + next.level)) <= 1.0e-9
                            && std::abs(generator.level(output, knot.seconds + 0.25 * length)
                                        - (knot.level + 0.14644660940672624 * (next.level - knot.level)))
                                   <= 1.0e-9,
                        label + "does not move along a raised cosine after knot "
                            + std::to_string(index));

                const auto normalisedLevel = (next.level - levelLow[output]) / span;
                placeSum += place;
                placeSquares += place * place;
                placeProducts += (place - 0.5) * (previousPlace - 0.5);
                levelSum += normalisedLevel;
                levelSquares += normalisedLevel * normalisedLevel;
                levelProducts += (normalisedLevel - 0.5) * (previousLevel - 0.5);
                previousPlace = place;
                previousLevel = normalisedLevel;
                ++count;
            }
        }

        const auto placeMean = placeSum / count;
        const auto placeVariance = placeSquares / count - placeMean * placeMean;
        const auto levelMean = levelSum / count;
        const auto levelVariance = levelSquares / count - levelMean * levelMean;
        std::cout << "[METRIC] " << label << "over " << count << " knots: place in the cell mean="
                  << placeMean << " variance=" << placeVariance << ", level mean=" << levelMean
                  << " variance=" << levelVariance << " (uniform: 0.5 and 0.0833), neighbour correlation="
                  << placeProducts / count / placeVariance << " and "
                  << levelProducts / count / levelVariance << '\n';
        require(std::abs(placeMean - 0.5) <= 0.012 && std::abs(placeVariance - 1.0 / 12.0) <= 0.004,
                label + "does not place its knots uniformly in their cells");
        require(std::abs(levelMean - 0.5) <= 0.012 && std::abs(levelVariance - 1.0 / 12.0) <= 0.004,
                label + "does not draw its targets uniformly between its bounds");
        require(std::abs(placeProducts / count / placeVariance) <= 0.04
                    && std::abs(levelProducts / count / levelVariance) <= 0.04,
                label + "ties a knot to the one before it");
    }

    // Start levels: each uniform over the range the campaign measured, the
    // two in the same half of their ranges as often as the campaign found
    // them. That holds for seeds that lie close together, for seeds spread
    // over all 64 bits, as an instance of the plug-in draws its own, and for
    // seeds that differ in their upper half alone.
    struct SeedFamily
    {
        const char* name;
        std::uint64_t (*seed)(std::uint64_t index);
    };
    constexpr std::array seedFamilies {
        SeedFamily { "seeds close together",
                     [](std::uint64_t index) -> std::uint64_t { return index * 0x9e3779b9ULL + 17; } },
        SeedFamily { "seeds over all 64 bits",
                     [](std::uint64_t index) -> std::uint64_t
                     { return (index + 1) * 6364136223846793005ULL + 1442695040888963407ULL; } },
        SeedFamily { "seeds that differ in their upper half",
                     [](std::uint64_t index) -> std::uint64_t { return (index << 32) | 0x45626245ULL; } }
    };
    for (const auto& family : seedFamilies)
    {
        constexpr auto seeds = 4000;
        const auto label = std::string("Fathom phase start levels of ") + family.name;
        std::array<double, 2> startSum {};
        std::array<double, 2> lowest { 1.0, 1.0 };
        std::array<double, 2> highest { 0.0, 0.0 };
        auto sameHalf = 0;
        auto rightHigh = 0;
        std::array<double, 2> previous {};
        auto distinct = 0;
        for (auto seed = 0; seed < seeds; ++seed)
        {
            FathomVoicePhase generator;
            generator.reset(family.seed(static_cast<std::uint64_t>(seed)));
            std::array<double, 2> place {};
            for (std::size_t output = 0; output < place.size(); ++output)
            {
                place[output] = (generator.knot(output, 0).level - levelLow[output])
                              / (levelHigh[output] - levelLow[output]);
                require(place[output] >= 0.0 && place[output] <= 1.0,
                        label + " leave the range the campaign measured");
                startSum[output] += place[output];
                lowest[output] = std::min(lowest[output], place[output]);
                highest[output] = std::max(highest[output], place[output]);
            }
            sameHalf += (place[0] >= 0.5) == (place[1] >= 0.5) ? 1 : 0;
            rightHigh += place[1] >= 0.5 ? 1 : 0;
            distinct += place != previous ? 1 : 0;
            previous = place;
        }
        const auto sameShare = static_cast<double>(sameHalf) / seeds;
        const auto cycles = [&](std::size_t output, double place)
        {
            return levelLow[output] + (levelHigh[output] - levelLow[output]) * place;
        };
        std::cout << "[METRIC] " << label << ", " << seeds << " of them: left "
                  << cycles(0, lowest[0]) << " to " << cycles(0, highest[0]) << " cycles (campaign "
                  << levelLow[0] << " to " << levelHigh[0] << "), right " << cycles(1, lowest[1])
                  << " to " << cycles(1, highest[1]) << " (campaign " << levelLow[1] << " to "
                  << levelHigh[1] << "), mean place left=" << startSum[0] / seeds << " right="
                  << startSum[1] / seeds << ", same half=" << sameShare << " (campaign "
                  << startSameHalf << ")\n";
        require(distinct == seeds, label + " are the same for two seeds");
        require(lowest[0] < 0.01 && lowest[1] < 0.01 && highest[0] > 0.99 && highest[1] > 0.99,
                label + " do not reach the ends of the range the campaign measured");
        require(std::abs(startSum[0] / seeds - 0.5) <= 0.02 && std::abs(startSum[1] / seeds - 0.5) <= 0.02
                    && std::abs(static_cast<double>(rightHigh) / seeds - 0.5) <= 0.035,
                label + " are not uniform over their ranges");
        require(std::abs(sameShare - startSameHalf) <= 0.025,
                label + " are not tied as the campaign found them");
    }

    // The same seed is the same instance; reset() starts it again.
    FathomVoicePhase first;
    FathomVoicePhase second;
    first.reset(0x1234abcdULL);
    second.reset(0x1234abcdULL);
    for (auto block = 0; block < 20000; ++block)
    {
        require(sameBits(first.phase(0), second.phase(0)) && sameBits(first.phase(1), second.phase(1)),
                "Fathom phase differs between two generators of one seed");
        first.advance(0.7);
        second.advance(0.7);
    }
    const auto late = first.phase(0);
    first.reset(0x1234abcdULL);
    second.reset(0x1234abceULL);
    require(sameBits(first.phase(0), first.knot(0, 0).level) && !sameBits(first.phase(0), late),
            "Fathom phase does not start again at reset()");
    require(!sameBits(first.phase(0), second.phase(0)) && !sameBits(first.phase(1), second.phase(1)),
            "Fathom phase is the same for neighbouring seeds");
}

void testFathomTideDeterminismAndSeeds()
{
    constexpr std::array<double, 2> sampleRates { 44100.0, 48000.0 };
    constexpr std::uint64_t otherSeed = 0x5eedf00dULL;

    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Fathom Tide layer at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        const auto warmupFrames = static_cast<int>(sampleRate * 0.35);
        const auto programmeFrames = static_cast<int>(sampleRate * 0.3);
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 1.5f;
        parameters.sizeScale = 1.2f;
        parameters.macro = 1.0f;

        const auto idle = [](FathomEngine& engine, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
                engine.advanceIdle();
        };
        const auto excite = [](FathomEngine& engine, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
                static_cast<void>(engine.processSample(0.3f * fathomNoise(frame, 17),
                                                       0.3f * fathomNoise(frame, 18)));
        };
        const auto renderWithSeed = [&](std::uint64_t seed)
        {
            FathomEngine engine;
            engine.setParameters(parameters);
            engine.setVoiceSeed(seed);
            engine.prepare(sampleRate);
            idle(engine, warmupFrames);
            return renderFathomProgramme(engine, programmeFrames);
        };

        const auto expected = renderWithSeed(FathomEngine::defaultVoiceSeed);
        auto peak = 0.0f;
        for (const auto sample : expected)
        {
            require(std::isfinite(sample), label + "the programme produced NaN/Inf");
            peak = std::max(peak, std::abs(sample));
        }
        require(peak > 1.0e-3f, label + "the programme left no wet signal");

        {
            // An engine that was given no seed has the default one.
            FathomEngine second;
            second.setParameters(parameters);
            second.prepare(sampleRate);
            idle(second, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(second, programmeFrames), expected,
                                    label + "second instance");
        }
        {
            FathomEngine used;
            used.setParameters(parameters);
            used.prepare(sampleRate);
            excite(used, 6000);
            used.reset();
            idle(used, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(used, programmeFrames), expected,
                                    label + "after reset()");
        }
        {
            FathomEngine silent;
            silent.setParameters(parameters);
            silent.prepare(sampleRate);
            for (auto frame = 0; frame < warmupFrames; ++frame)
            {
                const auto wet = silent.processSample(0.0f, 0.0f);
                require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                        label + "silence in did not give silence out");
            }
            requireSameFathomRender(renderFathomProgramme(silent, programmeFrames), expected,
                                    label + "processed silence against advanceIdle()");
        }
        // Left idle after sound, the engine starts from silence with the
        // comb, the voices and their phase where a fresh instance has them.
        for (const auto idleFrames : { 1, 43, 3000 })
        {
            constexpr auto soundFrames = 7000;
            FathomEngine reference;
            reference.setParameters(parameters);
            reference.prepare(sampleRate);
            idle(reference, soundFrames + idleFrames);
            const auto afterIdle = renderFathomProgramme(reference, programmeFrames);

            FathomEngine sounded;
            sounded.setParameters(parameters);
            sounded.prepare(sampleRate);
            excite(sounded, soundFrames);
            idle(sounded, idleFrames);
            requireSameFathomRender(renderFathomProgramme(sounded, programmeFrames), afterIdle,
                                    label + "after sound and " + std::to_string(idleFrames)
                                        + " idle frames");
        }

        // Another seed is another instance of the phase, from the next
        // reset() on.
        const auto other = renderWithSeed(otherSeed);
        const auto seedNullDb = fathomNullDb(other, expected);
        std::cout << "[METRIC] " << label << "null between two voice seeds: " << seedNullDb << " dB\n";
        require(seedNullDb > -40.0, label + "two voice seeds give the same render");
        {
            FathomEngine reseeded;
            reseeded.setParameters(parameters);
            reseeded.prepare(sampleRate);
            reseeded.setVoiceSeed(otherSeed);
            idle(reseeded, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(reseeded, programmeFrames), expected,
                                    label + "a new voice seed before reset()");
            reseeded.reset();
            idle(reseeded, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(reseeded, programmeFrames), other,
                                    label + "a new voice seed after reset()");
        }
        {
            // The engine's own phase is FathomVoicePhase: prescribed block by
            // block from a generator of the same seed, the render is the same.
            const auto blockCount = fathomVoiceBlocks(sampleRate, warmupFrames + programmeFrames);
            std::array<std::vector<double>, 2> phase { std::vector<double>(blockCount),
                                                       std::vector<double>(blockCount) };
            FathomVoicePhase generator;
            generator.reset(otherSeed);
            for (std::size_t block = 0; block < blockCount; ++block)
            {
                phase[0][block] = generator.phase(0);
                phase[1][block] = generator.phase(1);
                generator.advance(static_cast<double>(parameters.macro));
            }
            FathomEngine prescribed;
            prescribed.setParameters(parameters);
            prescribed.setVoicePhaseForTesting(phase[0].data(), phase[1].data(), blockCount);
            prescribed.prepare(sampleRate);
            idle(prescribed, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(prescribed, programmeFrames), other,
                                    label + "the phase of FathomVoicePhase prescribed from outside");
        }
    }
}

void testFathomTideOutOfCircuitAtMacroZero()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Fathom Tide layer at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        // Long enough for Macro to glide back and the Q of a voice to come to rest.
        const auto warmupFrames = static_cast<int>(sampleRate * 0.8);
        const auto programmeFrames = static_cast<int>(sampleRate * 0.3);
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 2.0f;
        parameters.sizeScale = 0.9f;
        parameters.preDelaySeconds = 0.003f;
        parameters.macro = 0.0f;

        const auto idle = [](FathomEngine& engine, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
                engine.advanceIdle();
        };

        // At Macro 0 the engine leaves the Tide layer out: the lines take
        // the equalised input and the voices rest.
        FathomEngine bypassed;
        bypassed.setParameters(parameters);
        bypassed.prepare(sampleRate);
        idle(bypassed, warmupFrames);
        const auto expected = renderFathomProgramme(bypassed, programmeFrames);

        {
            // A prescribed phase keeps the layer in the circuit. At Macro 0
            // its laws give no comb, the resting voice and a gain of 1
            // whatever the phase is, so the output is the same bits.
            const auto blockCount = fathomVoiceBlocks(sampleRate, warmupFrames + programmeFrames);
            std::array<std::vector<double>, 2> phase { std::vector<double>(blockCount),
                                                       std::vector<double>(blockCount) };
            for (std::size_t block = 0; block < blockCount; ++block)
            {
                const auto index = static_cast<int>(block);
                phase[0][block] = 0.3 + 5.6e-5 * index + 0.4 * static_cast<double>(fathomNoise(index, 19));
                phase[1][block] = 0.1 + 5.6e-5 * index + 0.4 * static_cast<double>(fathomNoise(index, 20));
            }
            FathomEngine inCircuit;
            inCircuit.setParameters(parameters);
            inCircuit.setVoicePhaseForTesting(phase[0].data(), phase[1].data(), blockCount);
            inCircuit.prepare(sampleRate);
            idle(inCircuit, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(inCircuit, programmeFrames), expected,
                                    label + "Macro 0 with the Tide layer in the circuit");
        }
        {
            // Raised and lowered again before the programme, Macro leaves
            // nothing behind once the voices have come back to rest.
            auto raised = parameters;
            raised.macro = 1.0f;
            FathomEngine released;
            released.setParameters(raised);
            released.prepare(sampleRate);
            idle(released, 100);
            released.setParameters(parameters);
            idle(released, warmupFrames - 100);
            requireSameFathomRender(renderFathomProgramme(released, programmeFrames), expected,
                                    label + "Macro 0 after Macro was raised and lowered");
        }
        {
            // The same with sound: while the engine processes, Macro glides
            // up and down, and the voices are back at rest when the input
            // returns.
            auto raised = parameters;
            raised.macro = 0.6f;
            FathomEngine glided;
            glided.setParameters(parameters);
            glided.prepare(sampleRate);
            const auto glideFrames = warmupFrames - 100;
            for (auto frame = 0; frame < glideFrames; ++frame)
            {
                if (frame == 10)
                    glided.setParameters(raised);
                if (frame == glideFrames / 8)
                    glided.setParameters(parameters);
                static_cast<void>(glided.processSample(0.0f, 0.0f));
            }
            idle(glided, 100);
            requireSameFathomRender(renderFathomProgramme(glided, programmeFrames), expected,
                                    label + "Macro 0 after Macro glided up and down");
        }
    }
}

void testFathomTideDepthBelowFullDepth()
{
    namespace fathom = amanita::dsp::fathom;
    constexpr auto sampleRate = 44100.0;
    constexpr auto pi = 3.14159265358979323846;
    constexpr auto warmupFrames = 8820;
    // An impulse on the left input reaches both outputs through the first
    // eight lines of each group, 1036 frames later at the earliest. For 195
    // frames more nothing else arrives: the comb's delayed path, the other
    // eight lines and the second passes all come later.
    constexpr auto arrivalFrame = 1036;
    constexpr auto windowFrames = arrivalFrame + 190;

    const auto impulseResponse = [&](FathomEngine& engine)
    {
        for (auto frame = 0; frame < warmupFrames; ++frame)
            engine.advanceIdle();
        std::vector<float> rendered;
        for (auto frame = 0; frame < windowFrames; ++frame)
        {
            const auto wet = engine.processSample(frame == 0 ? 0.5f : 0.0f, 0.0f);
            rendered.push_back(wet.left);
            rendered.push_back(wet.right);
        }
        return rendered;
    };

    FathomEngine atRest;
    atRest.prepare(sampleRate);
    const auto plain = impulseResponse(atRest);
    auto peak = 0.0;
    for (std::size_t index = 0; index < plain.size(); ++index)
    {
        require(index >= 2 * static_cast<std::size_t>(arrivalFrame) || !(std::abs(plain[index]) > 0.0f),
                "Fathom engine answers an impulse before its first line can");
        peak = std::max(peak, static_cast<double>(std::abs(plain[index])));
    }
    require(peak > 1.0e-3, "Fathom engine has no first arrival to judge the Tide depth on");

    // With its phase held at a whole number the first voice of an output
    // stays at its resting cut-off and its window is shut, so its gain is
    // what the depth leaves of 1: 1 - 0.88 Macro / 0.05, and nothing from
    // Macro 5.68 % up. The plain share of the input is cos(90 degrees x Macro).
    const auto blockCount = fathomVoiceBlocks(sampleRate, warmupFrames + windowFrames);
    const std::vector<double> wholePhase(blockCount, 0.0);
    for (const auto macro : { 0.01f, 0.03f, 0.05f, 0.06f })
    {
        FathomEngine::Parameters parameters;
        parameters.macro = macro;
        FathomEngine engine;
        engine.setParameters(parameters);
        engine.setVoicePhaseForTesting(wholePhase.data(), wholePhase.data(), blockCount);
        engine.prepare(sampleRate);
        const auto rendered = impulseResponse(engine);

        const auto amount = static_cast<double>(macro);
        const auto depth = std::min(1.0, fathom::voiceAmountPerMacro * amount / fathom::voiceFullDepthAmount);
        const auto expectedGain = (1.0 - depth) * std::cos(0.5 * pi * amount);
        auto worst = 0.0;
        for (std::size_t index = 0; index < plain.size(); ++index)
            worst = std::max(worst, std::abs(static_cast<double>(rendered[index])
                                             - expectedGain * static_cast<double>(plain[index])));
        std::cout << "[METRIC] Fathom first arrival at Macro " << 100.0f * macro << " %: gain "
                  << expectedGain << " of the Macro 0 one, largest deviation=" << worst / peak
                  << " of its peak\n";
        require(worst <= 1.0e-6 * peak,
                "Fathom Tide depth or plain share is off its law at Macro "
                    + std::to_string(macro));
    }
}

void testFathomTideHostileInputAndAutomation()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<float, 8> hostileSamples {
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
        -1.0e30f, std::numeric_limits<float>::denorm_min(), 1.0e-39f, -0.0f
    };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Fathom Tide layer at " + std::to_string(static_cast<int>(sampleRate)) + " Hz ";
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = 30.0f;
        parameters.sizeScale = 2.0f;
        parameters.macro = 1.0f;
        FathomEngine engine;
        FathomEngine::LevelStage levelStage;
        engine.setParameters(parameters);
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);

        // Macro runs from end to end and back every few milliseconds, first
        // in steps of 2 ms, then of single frames, while the other parameters
        // jump between their extremes and the input is full-scale noise with
        // samples that are no samples.
        const auto macroFrames = static_cast<int>(sampleRate * 0.002);
        const auto frames = static_cast<int>(sampleRate * 2.0);
        auto wetPeak = 0.0f;
        auto peak = 0.0f;
        for (auto frame = 0; frame < frames; ++frame)
        {
            const auto fast = frame >= frames / 2;
            if (fast || frame % macroFrames == 0)
            {
                const auto step = fast ? frame : frame / macroFrames;
                parameters.macro = step % 2 == 0 ? 1.0f : 0.0f;
                if (step % 7 == 3)
                    parameters.macro = 0.5f + 0.5f * fathomNoise(step, 21);
                engine.setParameters(parameters);
            }
            if (frame % 997 == 0)
            {
                const auto step = frame / 997;
                parameters.decaySeconds = step % 3 == 0 ? 0.2f : 60.0f;
                parameters.sizeScale = step % 2 == 0 ? 0.15f : 2.0f;
                parameters.preDelaySeconds = step % 5 == 0 ? 0.0f : 2.0f;
                parameters.lowCutHz = step % 4 == 0 ? 20.0f : 1000.0f;
                parameters.highDampingHz = step % 7 == 0 ? 20000.0f : 1000.0f;
                parameters.freeze = step % 6 == 5;
                auto invalid = parameters;
                invalid.macro = std::numeric_limits<float>::quiet_NaN();
                engine.setParameters(invalid);
            }

            auto left = fathomNoise(frame, 22);
            auto right = fathomNoise(frame, 23);
            if (frame % 61 == 0)
                left = hostileSamples[static_cast<std::size_t>(frame / 61) % hostileSamples.size()];
            if (frame % 89 == 0)
                right = hostileSamples[static_cast<std::size_t>(frame / 89) % hostileSamples.size()];
            auto wet = engine.processSample(left, right);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "let hostile input through as NaN/Inf");
            wetPeak = std::max({ wetPeak, std::abs(wet.left), std::abs(wet.right) });
            wet = FathomEngine::applyWidth(levelStage.process(left, right, wet), 2.0f);
            peak = std::max({ peak, std::abs(FathomEngine::clip(wet.left)),
                              std::abs(FathomEngine::clip(wet.right)) });
        }
        std::cout << "[METRIC] " << label << "wet peak under hostile input and Macro automation: "
                  << wetPeak << '\n';
        // The engine bounds an input sample at 64, and what it makes of the
        // input stays under that bound.
        require(wetPeak < 64.0f, label + "bursts under hostile input and Macro automation");
        require(peak <= 3.99f, label + "left the clipper's range under hostile input");

        // Once the input is gone a short Decay brings the engine back to
        // exact silence with the Tide layer in the circuit.
        parameters.decaySeconds = 0.2f;
        parameters.sizeScale = 1.0f;
        parameters.preDelaySeconds = 0.0f;
        parameters.macro = 1.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.freeze = false;
        engine.setParameters(parameters);
        auto lastSound = -1;
        const auto tailFrames = static_cast<int>(sampleRate * 6.0);
        for (auto frame = 0; frame < tailFrames; ++frame)
        {
            const auto wet = engine.processSample(0.0f, 0.0f);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "kept NaN/Inf after hostile input");
            if (std::abs(wet.left) > 0.0f || std::abs(wet.right) > 0.0f)
                lastSound = frame;
        }
        require(lastSound < static_cast<int>(sampleRate * 5.0),
                label + "did not return to silence after hostile input");
    }
}

void testFathomTideSmoothMotion()
{
    constexpr std::array<double, 2> sampleRates { 44100.0, 48000.0 };
    constexpr std::array<std::uint64_t, 3> seeds { FathomEngine::defaultVoiceSeed, 1, 2 };
    constexpr auto twoPi = 6.28318530717958647692;
    constexpr auto toneHz = 60.0;
    // The measure: the largest sample-to-sample step of the wet output while
    // a parameter moves, in units of what the signal itself explains. The
    // input is a steady tone, and a tone of its frequency at the peak level
    // of the output moves by no more than 2 pi f / fs of that peak per sample.
    // A parameter that stepped, a gain that moved once per block of the
    // voices or a line that jumped would show as many times that.
    constexpr auto stepLimit = 3.0;

    struct Motion
    {
        const char* name;
        FathomEngine::Parameters from;
        FathomEngine::Parameters to;
        // Above zero, the parameters go back and forth between the two sets
        // at this interval instead of changing once.
        double toggleSeconds;
    };
    FathomEngine::Parameters rest;
    rest.decaySeconds = 0.5f;
    FathomEngine::Parameters tide = rest;
    tide.macro = 1.0f;
    auto larger = tide;
    larger.sizeScale = 1.2f;
    auto longer = tide;
    longer.decaySeconds = 8.0f;
    auto together = tide;
    together.macro = 0.3f;
    together.sizeScale = 1.3f;
    together.decaySeconds = 4.0f;
    const std::array motions {
        Motion { "Macro 0 to 100 %", rest, tide, 0.0 },
        Motion { "Macro 100 % to 0", tide, rest, 0.0 },
        Motion { "Macro between 0 and 100 % every 3 ms", rest, tide, 0.003 },
        Motion { "Macro between 100 % and 0 every 20 ms", tide, rest, 0.02 },
        Motion { "Size at Macro 100 %", tide, larger, 0.0 },
        Motion { "Decay at Macro 100 %", tide, longer, 0.0 },
        Motion { "Macro, Size and Decay together", tide, together, 0.0 }
    };

    for (const auto& motion : motions)
    {
        auto worstSteady = 0.0;
        auto worstMoving = 0.0;
        for (const auto sampleRate : sampleRates)
        {
            const auto settleFrames = static_cast<int>(sampleRate * 1.5);
            const auto steadyFrames = static_cast<int>(sampleRate * 0.3);
            const auto movingFrames = static_cast<int>(sampleRate * 0.6);
            const auto toggleFrames = static_cast<int>(sampleRate * motion.toggleSeconds);
            const auto explained = twoPi * toneHz / sampleRate;
            for (const auto seed : seeds)
            {
                FathomEngine engine;
                engine.setParameters(motion.from);
                engine.setVoiceSeed(seed);
                engine.prepare(sampleRate);

                auto previous = FathomEngine::Frame {};
                auto peak = 0.0;
                auto steadyStep = 0.0;
                auto movingStep = 0.0;
                for (auto frame = 0; frame < settleFrames + movingFrames; ++frame)
                {
                    const auto moved = frame - settleFrames;
                    if (moved == 0)
                        engine.setParameters(motion.to);
                    else if (moved > 0 && toggleFrames > 0 && moved % toggleFrames == 0)
                        engine.setParameters((moved / toggleFrames) % 2 == 0 ? motion.to : motion.from);

                    const auto tone = 0.25f * static_cast<float>(
                        std::sin(twoPi * toneHz * static_cast<double>(frame) / sampleRate));
                    const auto wet = engine.processSample(tone, -0.5f * tone);
                    const auto step = static_cast<double>(std::max(std::abs(wet.left - previous.left),
                                                                   std::abs(wet.right - previous.right)));
                    previous = wet;
                    if (frame < settleFrames - steadyFrames)
                        continue;
                    peak = std::max({ peak, static_cast<double>(std::abs(wet.left)),
                                      static_cast<double>(std::abs(wet.right)) });
                    auto& largest = moved < 0 ? steadyStep : movingStep;
                    largest = std::max(largest, step);
                }
                require(peak > 1.0e-3, std::string("Fathom ") + motion.name + " has no steady wet");
                worstSteady = std::max(worstSteady, steadyStep / (explained * peak));
                worstMoving = std::max(worstMoving, movingStep / (explained * peak));
            }
        }

        std::cout << "[METRIC] Fathom " << motion.name << ": largest step while it moves=" << worstMoving
                  << ", before=" << worstSteady << " of what the signal explains, limit=" << stepLimit << '\n';
        require(worstMoving <= stepLimit,
                std::string("Fathom ") + motion.name + " puts a step into the output");
    }
}

void testVeilSampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 48000.0, 96000.0, 44100.0, 88200.0 };
    FDNReverb reverb;

    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::veil;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = 1.0f;
        parameters.width = 2.0f;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        std::uint32_t noiseState = 0x7ea10f5du;
        auto peak = 0.0f;
        const auto excitationSamples = static_cast<int>(sampleRate * 0.5);
        for (auto sample = 0; sample < excitationSamples; ++sample)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                             / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            auto left = (sample == 0 ? 1.0f : 0.0f) + 0.01f * noise;
            auto right = (sample == 0 ? -0.35f : 0.0f) - 0.007f * noise;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Veil excitation produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        parameters.freeze = true;
        reverb.setParameters(parameters);
        double firstWindowEnergy = 0.0;
        double lastWindowEnergy = 0.0;
        const auto frozenSamples = static_cast<int>(sampleRate * 6.0);
        for (auto sample = 0; sample < frozenSamples; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Veil Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            const auto energy = static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
            if (sample >= static_cast<int>(sampleRate)
                && sample < static_cast<int>(sampleRate * 2.0))
                firstWindowEnergy += energy;
            if (sample >= static_cast<int>(sampleRate * 5.0))
                lastWindowEnergy += energy;
        }

        require(firstWindowEnergy > 1.0e-10, "Veil Freeze tail became silent");
        require(lastWindowEnergy >= firstWindowEnergy * 0.50,
                "Veil Freeze tail collapsed unexpectedly");
        require(lastWindowEnergy <= firstWindowEnergy * 1.25 + 1.0e-12,
                "Veil Freeze feedback energy grows over time");
        require(peak < 4.0f, "Veil stress test exceeded the safety range");
    }
}

void testVeilBlockInvarianceAndModeSwitching()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto comparisonSamples = 48000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::veil;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 6.0f;
    parameters.preDelayMs = 11.0f;
    parameters.evolution = 0.8f;

    std::vector<float> singleLeft(comparisonSamples, 0.0f);
    std::vector<float> singleRight(comparisonSamples, 0.0f);
    std::vector<float> blockLeft(comparisonSamples, 0.0f);
    std::vector<float> blockRight(comparisonSamples, 0.0f);
    singleLeft[0] = blockLeft[0] = 1.0f;

    FDNReverb singleSample;
    FDNReverb blockBased;
    singleSample.setParameters(parameters);
    blockBased.setParameters(parameters);
    singleSample.prepare(sampleRate, 1);
    blockBased.prepare(sampleRate, 127);

    for (auto sample = 0; sample < comparisonSamples; ++sample)
        singleSample.process(singleLeft.data() + sample, singleRight.data() + sample, 1);
    for (auto offset = 0; offset < comparisonSamples; offset += 127)
    {
        const auto blockSize = std::min(127, comparisonSamples - offset);
        blockBased.process(blockLeft.data() + offset, blockRight.data() + offset, blockSize);
    }
    for (auto sample = 0; sample < comparisonSamples; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        require(std::abs(singleLeft[index] - blockLeft[index]) <= 1.0e-7f
                    && std::abs(singleRight[index] - blockRight[index]) <= 1.0e-7f,
                "Veil result depends on process block segmentation");
    }

    requireSmoothModeSwitch(ReverbMode::defaultMode, ReverbMode::veil,
                            "Default to Veil switch");
    requireSmoothModeSwitch(ReverbMode::veil, ReverbMode::defaultMode,
                            "Veil to Default switch");
    requireSmoothModeSwitch(ReverbMode::bloom, ReverbMode::veil,
                            "Bloom to Veil switch");
    requireSmoothModeSwitch(ReverbMode::veil, ReverbMode::bloom,
                            "Veil to Bloom switch");
    requireSmoothModeSwitch(ReverbMode::drift, ReverbMode::veil,
                            "Drift to Veil switch");
    requireSmoothModeSwitch(ReverbMode::veil, ReverbMode::drift,
                            "Veil to Drift switch");

    parameters.mix = 0.0f;
    FDNReverb dryPath;
    dryPath.setParameters(parameters);
    dryPath.prepare(sampleRate, 64);
    for (auto sample = 0; sample < 10000; ++sample)
    {
        const auto expectedLeft = 0.1f * std::sin(0.01f * static_cast<float>(sample));
        const auto expectedRight = -0.7f * expectedLeft;
        auto left = expectedLeft;
        auto right = expectedRight;
        dryPath.processSample(left, right);
        require(std::abs(left - expectedLeft) <= 1.0e-8f
                    && std::abs(right - expectedRight) <= 1.0e-8f,
                "Veil changes the dry path at Mix=0");
    }
}

void testDriftSampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<float, 2> evolutionAmounts { 0.0f, 1.0f };
    FDNReverb reverb;

    for (const auto sampleRate : sampleRates)
    {
      for (const auto evolution : evolutionAmounts)
      {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::drift;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.evolution = evolution;
        parameters.width = 2.0f;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        std::uint32_t noiseState = 0x74d51e3bu;
        auto peak = 0.0f;
        const auto excitationSamples = static_cast<int>(sampleRate * 0.5);
        for (auto sample = 0; sample < excitationSamples; ++sample)
        {
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                             / static_cast<float>(std::numeric_limits<std::int32_t>::max());
            auto left = (sample == 0 ? 1.0f : 0.0f) + 0.01f * noise;
            auto right = (sample == 0 ? -0.4f : 0.0f) - 0.008f * noise;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Drift excitation produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }

        parameters.freeze = true;
        reverb.setParameters(parameters);
        double firstWindowEnergy = 0.0;
        double lastWindowEnergy = 0.0;
        const auto frozenSamples = static_cast<int>(sampleRate * 6.0);
        for (auto sample = 0; sample < frozenSamples; ++sample)
        {
            auto left = 0.0f;
            auto right = 0.0f;
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Drift Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            const auto energy = static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
            if (sample >= static_cast<int>(sampleRate)
                && sample < static_cast<int>(sampleRate * 2.0))
                firstWindowEnergy += energy;
            if (sample >= static_cast<int>(sampleRate * 5.0))
                lastWindowEnergy += energy;
        }

        require(firstWindowEnergy > 1.0e-10, "Drift Freeze tail became silent");
        const auto sustainedEnergyRatio = lastWindowEnergy / firstWindowEnergy;
        require(lastWindowEnergy > 1.0e-12 && sustainedEnergyRatio >= 1.0e-4,
                "Drift Freeze tail collapsed instead of sustaining at "
                    + std::to_string(sampleRate) + " Hz, Evolution="
                    + std::to_string(evolution) + ": last/first="
                    + std::to_string(sustainedEnergyRatio));
        require(lastWindowEnergy <= firstWindowEnergy * 1.25 + 1.0e-12,
                "Drift Freeze feedback energy grows over time");
        require(peak < 4.0f, "Drift stress test exceeded safety range");
      }
    }
}

void testDriftBlockInvarianceAndModeSwitching()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto comparisonSamples = 48000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 6.0f;
    parameters.preDelayMs = 9.0f;
    parameters.highDampingHz = 18000.0f;
    parameters.evolution = 1.0f;

    std::vector<float> singleLeft(comparisonSamples, 0.0f);
    std::vector<float> singleRight(comparisonSamples, 0.0f);
    std::vector<float> blockLeft(comparisonSamples, 0.0f);
    std::vector<float> blockRight(comparisonSamples, 0.0f);
    singleLeft[0] = blockLeft[0] = 1.0f;

    FDNReverb singleSample;
    FDNReverb blockBased;
    singleSample.setParameters(parameters);
    blockBased.setParameters(parameters);
    singleSample.prepare(sampleRate, 1);
    blockBased.prepare(sampleRate, 127);
    for (auto sample = 0; sample < comparisonSamples; ++sample)
        singleSample.process(singleLeft.data() + sample, singleRight.data() + sample, 1);
    for (auto offset = 0; offset < comparisonSamples; offset += 127)
    {
        const auto blockSize = std::min(127, comparisonSamples - offset);
        blockBased.process(blockLeft.data() + offset, blockRight.data() + offset, blockSize);
    }
    for (std::size_t sample = 0; sample < static_cast<std::size_t>(comparisonSamples); ++sample)
    {
        require(std::abs(singleLeft[sample] - blockLeft[sample]) <= 1.0e-7f
                    && std::abs(singleRight[sample] - blockRight[sample]) <= 1.0e-7f,
                "Drift result depends on process block segmentation");
    }

    requireSmoothModeSwitch(ReverbMode::defaultMode, ReverbMode::drift,
                            "Default to Drift");
    requireSmoothModeSwitch(ReverbMode::drift, ReverbMode::defaultMode,
                            "Drift to Default");
    requireSmoothModeSwitch(ReverbMode::bloom, ReverbMode::drift,
                            "Bloom to Drift");
    requireSmoothModeSwitch(ReverbMode::drift, ReverbMode::bloom,
                            "Drift to Bloom");
    requireSmoothEvolutionSwitch(ReverbMode::defaultMode, 0.0f, 1.0f,
                                 "Default low to high Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::defaultMode, 1.0f, 0.0f,
                                 "Default high to low Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::bloom, 0.0f, 1.0f,
                                 "Bloom low to high Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::bloom, 1.0f, 0.0f,
                                 "Bloom high to low Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::drift, 0.0f, 1.0f,
                                 "Drift low to high Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::drift, 1.0f, 0.0f,
                                 "Drift high to low Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::veil, 0.0f, 1.0f,
                                 "Veil low to high Evolution");
    requireSmoothEvolutionSwitch(ReverbMode::veil, 1.0f, 0.0f,
                                 "Veil high to low Evolution");

    parameters = {};
    parameters.mix = 1.0f;
    parameters.decaySeconds = 10.0f;
    parameters.highDampingHz = 18000.0f;
    parameters.evolution = 1.0f;
    FDNReverb control;
    FDNReverb imprinted;
    control.setParameters(parameters);
    imprinted.setParameters(parameters);
    control.prepare(sampleRate, 64);
    imprinted.prepare(sampleRate, 64);
    double imprintDifference = 0.0;
    double controlEnergy = 0.0;
    for (auto sample = 0; sample < 240000; ++sample)
    {
        if (sample == 24000)
        {
            parameters.mode = ReverbMode::drift;
            imprinted.setParameters(parameters);
        }
        if (sample == 120000)
        {
            parameters.mode = ReverbMode::defaultMode;
            imprinted.setParameters(parameters);
        }
        auto controlLeft = sample == 0 ? 1.0f : 0.0f;
        auto controlRight = 0.0f;
        auto imprintedLeft = controlLeft;
        auto imprintedRight = controlRight;
        control.processSample(controlLeft, controlRight);
        imprinted.processSample(imprintedLeft, imprintedRight);
        if (sample >= 144000 && sample < 216000)
        {
            const auto differenceLeft = imprintedLeft - controlLeft;
            const auto differenceRight = imprintedRight - controlRight;
            imprintDifference += static_cast<double>(differenceLeft) * differenceLeft
                               + static_cast<double>(differenceRight) * differenceRight;
            controlEnergy += static_cast<double>(controlLeft) * controlLeft
                           + static_cast<double>(controlRight) * controlRight;
        }
    }
    require(controlEnergy > 1.0e-12, "Drift imprint control tail became silent");
    require(std::sqrt(imprintDifference / controlEnergy) > 0.01,
            "Drift does not leave a measurable in-loop spectral imprint");
}

using BandVector = std::array<double, 3>;
using BandTrajectory = std::array<BandVector, 4>;

[[nodiscard]] BandVector analyseBandFractions(const std::vector<float>& signal,
                                              double sampleRate,
                                              int startSample,
                                              int endSample)
{
    const auto lowCoefficient = std::exp(-6.28318530717958647692 * 350.0 / sampleRate);
    const auto midCoefficient = std::exp(-6.28318530717958647692 * 2500.0 / sampleRate);
    auto lowState = 0.0;
    auto midState = 0.0;
    BandVector energy {};
    for (auto sample = 0; sample < endSample; ++sample)
    {
        const auto input = static_cast<double>(signal[static_cast<std::size_t>(sample)]);
        lowState = lowCoefficient * lowState + (1.0 - lowCoefficient) * input;
        midState = midCoefficient * midState + (1.0 - midCoefficient) * input;
        if (sample >= startSample)
        {
            const auto bands = BandVector { lowState, midState - lowState, input - midState };
            for (std::size_t band = 0; band < energy.size(); ++band)
                energy[band] += bands[band] * bands[band];
        }
    }

    const auto total = energy[0] + energy[1] + energy[2] + 1.0e-30;
    for (auto& value : energy)
        value = 10.0 * std::log10((value + 1.0e-30) / total);
    return energy;
}

[[nodiscard]] double trajectoryMotion(const BandTrajectory& trajectory)
{
    BandVector mean {};
    for (const auto& point : trajectory)
        for (std::size_t band = 0; band < mean.size(); ++band)
            mean[band] += point[band] / static_cast<double>(trajectory.size());

    auto squared = 0.0;
    for (const auto& point : trajectory)
        for (std::size_t band = 0; band < mean.size(); ++band)
            squared += (point[band] - mean[band]) * (point[band] - mean[band]);
    return std::sqrt(squared / static_cast<double>(trajectory.size() * mean.size()));
}

[[nodiscard]] double stereoTrajectoryDifference(const BandTrajectory& left,
                                                 const BandTrajectory& right)
{
    BandTrajectory difference {};
    for (std::size_t point = 0; point < difference.size(); ++point)
        for (std::size_t band = 0; band < difference[point].size(); ++band)
            difference[point][band] = left[point][band] - right[point][band];
    return trajectoryMotion(difference);
}

void testDriftSpectralMotion()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 144000;
    constexpr std::array<double, 4> warmupSeconds { 0.0, 13.0, 29.0, 47.0 };
    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 30.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 1.0f;

    BandTrajectory driftLeft {};
    BandTrajectory driftRight {};
    BandTrajectory defaultLeft {};
    BandTrajectory defaultRight {};
    for (std::size_t point = 0; point < warmupSeconds.size(); ++point)
    {
        const auto warmup = static_cast<int>(sampleRate * warmupSeconds[point]);
        const auto drift = renderImpulse(parameters, sampleRate, sampleCount, warmup);
        driftLeft[point] = analyseBandFractions(drift.left, sampleRate, 12000, 132000);
        driftRight[point] = analyseBandFractions(drift.right, sampleRate, 12000, 132000);

        parameters.mode = ReverbMode::defaultMode;
        const auto defaultRender = renderImpulse(parameters, sampleRate, sampleCount, warmup);
        defaultLeft[point] = analyseBandFractions(defaultRender.left, sampleRate, 12000, 132000);
        defaultRight[point] = analyseBandFractions(defaultRender.right, sampleRate, 12000, 132000);
        parameters.mode = ReverbMode::drift;
    }

    const auto driftMotion = 0.5 * (trajectoryMotion(driftLeft) + trajectoryMotion(driftRight));
    const auto defaultMotion = 0.5
                             * (trajectoryMotion(defaultLeft) + trajectoryMotion(defaultRight));
    const auto driftStereoMotion = stereoTrajectoryDifference(driftLeft, driftRight);
    const auto defaultStereoMotion = stereoTrajectoryDifference(defaultLeft, defaultRight);
    require(driftMotion > defaultMotion + 0.05,
            "Drift spectral motion is not stronger than Default: Drift="
                + std::to_string(driftMotion) + " Default=" + std::to_string(defaultMotion));
    require(driftStereoMotion > std::max(0.20, defaultStereoMotion + 0.05),
            "Drift L/R spectral trajectories are insufficiently independent: Drift="
                + std::to_string(driftStereoMotion)
                + " Default=" + std::to_string(defaultStereoMotion));
}

constexpr std::size_t kickBassMeasureBeats = 64;
using MusicalBandVector = std::array<double, 3>;
using MusicalTrajectory = std::array<MusicalBandVector, kickBassMeasureBeats>;

class FourBandMeter
{
public:
    explicit FourBandMeter(double sampleRate)
        : lowCoefficient_(std::exp(-6.28318530717958647692 * 120.0 / sampleRate)),
          bodyCoefficient_(std::exp(-6.28318530717958647692 * 500.0 / sampleRate)),
          presenceCoefficient_(std::exp(-6.28318530717958647692 * 2500.0 / sampleRate))
    {
    }

    [[nodiscard]] std::array<double, 4> process(float sample) noexcept
    {
        const auto input = static_cast<double>(sample);
        lowState_ = lowCoefficient_ * lowState_ + (1.0 - lowCoefficient_) * input;
        bodyState_ = bodyCoefficient_ * bodyState_ + (1.0 - bodyCoefficient_) * input;
        presenceState_ = presenceCoefficient_ * presenceState_
                       + (1.0 - presenceCoefficient_) * input;
        return { lowState_, bodyState_ - lowState_,
                 presenceState_ - bodyState_, input - presenceState_ };
    }

private:
    double lowCoefficient_ = 0.0;
    double bodyCoefficient_ = 0.0;
    double presenceCoefficient_ = 0.0;
    double lowState_ = 0.0;
    double bodyState_ = 0.0;
    double presenceState_ = 0.0;
};

struct KickBassAnalysis
{
    std::array<std::array<double, 4>, kickBassMeasureBeats> leftEnergy {};
    std::array<std::array<double, 4>, kickBassMeasureBeats> rightEnergy {};
    std::array<double, kickBassMeasureBeats> subSideEnergy {};
};

[[nodiscard]] float kickBass190Sample(int sample, double sampleRate) noexcept
{
    constexpr double bpm = 190.0;
    constexpr double beatSeconds = 60.0 / bpm;
    constexpr double twoPi = 6.28318530717958647692;
    const auto time = static_cast<double>(sample) / sampleRate;
    const auto beatPosition = std::fmod(time, beatSeconds);

    auto output = 0.0;
    if (beatPosition < 0.130)
    {
        constexpr double baseFrequency = 48.0;
        constexpr double sweepFrequency = 115.0;
        constexpr double sweepSeconds = 0.018;
        const auto phase = twoPi
                         * (baseFrequency * beatPosition
                            + sweepFrequency * sweepSeconds
                                  * (1.0 - std::exp(-beatPosition / sweepSeconds)));
        output += 0.70 * std::exp(-beatPosition / 0.045) * std::sin(phase);
        output += 0.12 * std::exp(-beatPosition / 0.003)
                * std::cos(twoPi * 4500.0 * beatPosition);
    }

    constexpr std::array<double, 3> bassOffsets {
        beatSeconds * 0.25, beatSeconds * 0.50, beatSeconds * 0.75
    };
    for (const auto offset : bassOffsets)
    {
        const auto noteTime = beatPosition - offset;
        if (noteTime >= 0.0 && noteTime < 0.075)
        {
            const auto envelope = (1.0 - std::exp(-noteTime / 0.0015))
                                * std::exp(-noteTime / 0.045);
            output += 0.28 * envelope
                    * (std::sin(twoPi * 55.0 * noteTime)
                       + 0.32 * std::sin(twoPi * 110.0 * noteTime)
                       + 0.12 * std::sin(twoPi * 220.0 * noteTime));
        }
    }
    return static_cast<float>(std::clamp(output, -1.0, 1.0));
}

[[nodiscard]] float kickOnly190Sample(int sample, double sampleRate) noexcept
{
    constexpr double bpm = 190.0;
    constexpr double beatSeconds = 60.0 / bpm;
    constexpr double twoPi = 6.28318530717958647692;
    const auto time = static_cast<double>(sample) / sampleRate;
    const auto beatPosition = std::fmod(time, beatSeconds);

    auto output = 0.0;
    if (beatPosition < 0.130)
    {
        constexpr double baseFrequency = 48.0;
        constexpr double sweepFrequency = 115.0;
        constexpr double sweepSeconds = 0.018;
        const auto phase = twoPi
                         * (baseFrequency * beatPosition
                            + sweepFrequency * sweepSeconds
                                  * (1.0 - std::exp(-beatPosition / sweepSeconds)));
        output += 0.70 * std::exp(-beatPosition / 0.045) * std::sin(phase);
        output += 0.12 * std::exp(-beatPosition / 0.003)
                * std::cos(twoPi * 4500.0 * beatPosition);
    }
    return static_cast<float>(std::clamp(output, -1.0, 1.0));
}

[[nodiscard]] float bassOnly190Sample(int sample, double sampleRate) noexcept
{
    constexpr double bpm = 190.0;
    constexpr double beatSeconds = 60.0 / bpm;
    constexpr double twoPi = 6.28318530717958647692;
    const auto time = static_cast<double>(sample) / sampleRate;
    const auto beatPosition = std::fmod(time, beatSeconds);

    auto output = 0.0;
    constexpr std::array<double, 3> bassOffsets {
        beatSeconds * 0.25, beatSeconds * 0.50, beatSeconds * 0.75
    };
    for (const auto offset : bassOffsets)
    {
        const auto noteTime = beatPosition - offset;
        if (noteTime >= 0.0 && noteTime < 0.075)
        {
            const auto envelope = (1.0 - std::exp(-noteTime / 0.0015))
                                * std::exp(-noteTime / 0.045);
            output += 0.28 * envelope
                    * (std::sin(twoPi * 55.0 * noteTime)
                       + 0.32 * std::sin(twoPi * 110.0 * noteTime)
                       + 0.12 * std::sin(twoPi * 220.0 * noteTime));
        }
    }
    return static_cast<float>(std::clamp(output, -1.0, 1.0));
}

template <std::size_t numPoints>
[[nodiscard]] double detrendedTrajectoryMotion(
    const std::array<MusicalBandVector, numPoints>& trajectory)
{
    constexpr auto numBands = std::tuple_size_v<MusicalBandVector>;
    const auto centre = 0.5 * static_cast<double>(numPoints - 1);
    auto timeNorm = 0.0;
    for (std::size_t point = 0; point < numPoints; ++point)
    {
        const auto time = static_cast<double>(point) - centre;
        timeNorm += time * time;
    }

    MusicalBandVector mean {};
    MusicalBandVector slope {};
    for (std::size_t band = 0; band < numBands; ++band)
    {
        for (const auto& point : trajectory)
            mean[band] += point[band] / static_cast<double>(numPoints);
        for (std::size_t point = 0; point < numPoints; ++point)
            slope[band] += (static_cast<double>(point) - centre)
                         * (trajectory[point][band] - mean[band]) / timeNorm;
    }

    auto squared = 0.0;
    for (std::size_t point = 0; point < numPoints; ++point)
    {
        const auto time = static_cast<double>(point) - centre;
        for (std::size_t band = 0; band < numBands; ++band)
        {
            const auto residual = trajectory[point][band]
                                - mean[band] - slope[band] * time;
            squared += residual * residual;
        }
    }
    return std::sqrt(squared / static_cast<double>(numPoints * numBands));
}

template <std::size_t numPoints>
[[nodiscard]] double detrendedScalarMotion(const std::array<double, numPoints>& values)
{
    const auto centre = 0.5 * static_cast<double>(numPoints - 1);
    auto mean = 0.0;
    for (const auto value : values)
        mean += value / static_cast<double>(numPoints);
    auto timeNorm = 0.0;
    auto slope = 0.0;
    for (std::size_t point = 0; point < numPoints; ++point)
    {
        const auto time = static_cast<double>(point) - centre;
        timeNorm += time * time;
        slope += time * (values[point] - mean);
    }
    slope /= timeNorm;
    auto squared = 0.0;
    for (std::size_t point = 0; point < numPoints; ++point)
    {
        const auto residual = values[point] - mean
                            - slope * (static_cast<double>(point) - centre);
        squared += residual * residual;
    }
    return std::sqrt(squared / static_cast<double>(numPoints));
}

[[nodiscard]] MusicalTrajectory normalisedNonSubTrajectory(
    const std::array<std::array<double, 4>, kickBassMeasureBeats>& energy)
{
    MusicalTrajectory trajectory {};
    for (std::size_t beat = 0; beat < kickBassMeasureBeats; ++beat)
    {
        const auto total = energy[beat][1] + energy[beat][2] + energy[beat][3] + 1.0e-30;
        for (std::size_t band = 0; band < trajectory[beat].size(); ++band)
            trajectory[beat][band] = 10.0
                                   * std::log10((energy[beat][band + 1] + 1.0e-30) / total);
    }
    return trajectory;
}

[[nodiscard]] double musicalStereoMotion(const MusicalTrajectory& left,
                                          const MusicalTrajectory& right)
{
    MusicalTrajectory difference {};
    for (std::size_t beat = 0; beat < difference.size(); ++beat)
        for (std::size_t band = 0; band < difference[beat].size(); ++band)
            difference[beat][band] = left[beat][band] - right[beat][band];
    return detrendedTrajectoryMotion(difference);
}

[[nodiscard]] double meanValue(const std::array<double, kickBassMeasureBeats>& values)
{
    auto sum = 0.0;
    for (const auto value : values)
        sum += value;
    return sum / static_cast<double>(values.size());
}

[[nodiscard]] double percentile95(std::array<double, kickBassMeasureBeats> values)
{
    std::sort(values.begin(), values.end());
    const auto nearestRank = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(nearestRank, 1) - 1];
}

void testDriftEvolutionKickBass190()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto warmupBeats = 16;
    constexpr auto totalBeats = warmupBeats + static_cast<int>(kickBassMeasureBeats);
    constexpr auto beatSeconds = 60.0 / 190.0;
    const auto totalSamples = static_cast<int>(
        std::ceil(static_cast<double>(totalBeats) * beatSeconds * sampleRate));

    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 10.0f;
    parameters.size = 1.2f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 18000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 1.0f;

    FDNReverb lowEvolution;
    parameters.evolution = 0.0f;
    lowEvolution.setParameters(parameters);
    lowEvolution.prepare(sampleRate, 512);
    FDNReverb highEvolution;
    parameters.evolution = 1.0f;
    highEvolution.setParameters(parameters);
    highEvolution.prepare(sampleRate, 512);

    FourBandMeter lowEvolutionLeftMeter(sampleRate);
    FourBandMeter lowEvolutionRightMeter(sampleRate);
    FourBandMeter highEvolutionLeftMeter(sampleRate);
    FourBandMeter highEvolutionRightMeter(sampleRate);
    KickBassAnalysis lowEvolutionAnalysis;
    KickBassAnalysis highEvolutionAnalysis;
    auto differenceEnergy = 0.0;
    auto referenceEnergy = 0.0;

    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        const auto input = kickBass190Sample(sample, sampleRate);
        auto lowEvolutionLeft = input;
        auto lowEvolutionRight = input;
        auto highEvolutionLeft = input;
        auto highEvolutionRight = input;
        lowEvolution.processSample(lowEvolutionLeft, lowEvolutionRight);
        highEvolution.processSample(highEvolutionLeft, highEvolutionRight);
        require(std::isfinite(lowEvolutionLeft) && std::isfinite(lowEvolutionRight)
                    && std::isfinite(highEvolutionLeft) && std::isfinite(highEvolutionRight),
                "Kick+bass comparison produced NaN/Inf");

        const auto lowEvolutionLeftBands = lowEvolutionLeftMeter.process(lowEvolutionLeft);
        const auto lowEvolutionRightBands = lowEvolutionRightMeter.process(lowEvolutionRight);
        const auto highEvolutionLeftBands = highEvolutionLeftMeter.process(highEvolutionLeft);
        const auto highEvolutionRightBands = highEvolutionRightMeter.process(highEvolutionRight);
        const auto beat = static_cast<int>(std::floor(
            (static_cast<double>(sample) / sampleRate) / beatSeconds));
        if (beat < warmupBeats || beat >= totalBeats)
            continue;
        const auto measuredBeat = static_cast<std::size_t>(beat - warmupBeats);
        for (std::size_t band = 0; band < 4; ++band)
        {
            lowEvolutionAnalysis.leftEnergy[measuredBeat][band]
                += lowEvolutionLeftBands[band] * lowEvolutionLeftBands[band];
            lowEvolutionAnalysis.rightEnergy[measuredBeat][band]
                += lowEvolutionRightBands[band] * lowEvolutionRightBands[band];
            highEvolutionAnalysis.leftEnergy[measuredBeat][band]
                += highEvolutionLeftBands[band] * highEvolutionLeftBands[band];
            highEvolutionAnalysis.rightEnergy[measuredBeat][band]
                += highEvolutionRightBands[band] * highEvolutionRightBands[band];
        }
        const auto lowEvolutionSubSide = lowEvolutionLeftBands[0] - lowEvolutionRightBands[0];
        const auto highEvolutionSubSide = highEvolutionLeftBands[0] - highEvolutionRightBands[0];
        lowEvolutionAnalysis.subSideEnergy[measuredBeat]
            += 0.5 * lowEvolutionSubSide * lowEvolutionSubSide;
        highEvolutionAnalysis.subSideEnergy[measuredBeat]
            += 0.5 * highEvolutionSubSide * highEvolutionSubSide;

        const auto differenceLeft = highEvolutionLeft - lowEvolutionLeft;
        const auto differenceRight = highEvolutionRight - lowEvolutionRight;
        differenceEnergy += static_cast<double>(differenceLeft) * differenceLeft
                          + static_cast<double>(differenceRight) * differenceRight;
        referenceEnergy += 0.5
                         * (static_cast<double>(lowEvolutionLeft) * lowEvolutionLeft
                            + static_cast<double>(lowEvolutionRight) * lowEvolutionRight
                            + static_cast<double>(highEvolutionLeft) * highEvolutionLeft
                            + static_cast<double>(highEvolutionRight) * highEvolutionRight);
    }

    const auto lowEvolutionLeftTrajectory = normalisedNonSubTrajectory(lowEvolutionAnalysis.leftEnergy);
    const auto lowEvolutionRightTrajectory = normalisedNonSubTrajectory(lowEvolutionAnalysis.rightEnergy);
    const auto highEvolutionLeftTrajectory = normalisedNonSubTrajectory(highEvolutionAnalysis.leftEnergy);
    const auto highEvolutionRightTrajectory = normalisedNonSubTrajectory(highEvolutionAnalysis.rightEnergy);
    const auto lowEvolutionMotion = 0.5
                              * (detrendedTrajectoryMotion(lowEvolutionLeftTrajectory)
                                 + detrendedTrajectoryMotion(lowEvolutionRightTrajectory));
    const auto highEvolutionMotion = 0.5
                            * (detrendedTrajectoryMotion(highEvolutionLeftTrajectory)
                               + detrendedTrajectoryMotion(highEvolutionRightTrajectory));
    const auto lowEvolutionStereoMotion = musicalStereoMotion(lowEvolutionLeftTrajectory,
                                                           lowEvolutionRightTrajectory);
    const auto highEvolutionStereoMotion = musicalStereoMotion(highEvolutionLeftTrajectory,
                                                         highEvolutionRightTrajectory);
    const auto normalisedDifference = std::sqrt(differenceEnergy / referenceEnergy);

    std::array<double, kickBassMeasureBeats> lowEvolutionSubEnergy {};
    std::array<double, kickBassMeasureBeats> highEvolutionSubEnergy {};
    std::array<double, kickBassMeasureBeats> lowEvolutionSubDb {};
    std::array<double, kickBassMeasureBeats> highEvolutionSubDb {};
    auto lowEvolutionSubSideTotal = 0.0;
    auto highEvolutionSubSideTotal = 0.0;
    for (std::size_t beat = 0; beat < kickBassMeasureBeats; ++beat)
    {
        lowEvolutionSubEnergy[beat] = lowEvolutionAnalysis.leftEnergy[beat][0]
                                + lowEvolutionAnalysis.rightEnergy[beat][0];
        highEvolutionSubEnergy[beat] = highEvolutionAnalysis.leftEnergy[beat][0]
                              + highEvolutionAnalysis.rightEnergy[beat][0];
        lowEvolutionSubDb[beat] = 10.0 * std::log10(lowEvolutionSubEnergy[beat] + 1.0e-30);
        highEvolutionSubDb[beat] = 10.0 * std::log10(highEvolutionSubEnergy[beat] + 1.0e-30);
        lowEvolutionSubSideTotal += lowEvolutionAnalysis.subSideEnergy[beat];
        highEvolutionSubSideTotal += highEvolutionAnalysis.subSideEnergy[beat];
    }
    const auto lowEvolutionMeanSub = meanValue(lowEvolutionSubEnergy);
    const auto highEvolutionMeanSub = meanValue(highEvolutionSubEnergy);
    const auto meanSubRatio = highEvolutionMeanSub / lowEvolutionMeanSub;
    const auto p95SubRatio = percentile95(highEvolutionSubEnergy) / percentile95(lowEvolutionSubEnergy);
    const auto lowEvolutionSubMotion = detrendedScalarMotion(lowEvolutionSubDb);
    const auto highEvolutionSubMotion = detrendedScalarMotion(highEvolutionSubDb);
    const auto lowEvolutionSubSideFraction = lowEvolutionSubSideTotal / lowEvolutionMeanSub
                                       / static_cast<double>(kickBassMeasureBeats);
    const auto highEvolutionSubSideFraction = highEvolutionSubSideTotal / highEvolutionMeanSub
                                     / static_cast<double>(kickBassMeasureBeats);
    auto earlySub = 0.0;
    auto lateSub = 0.0;
    for (std::size_t beat = 0; beat < 16; ++beat)
    {
        earlySub += highEvolutionSubEnergy[beat];
        lateSub += highEvolutionSubEnergy[kickBassMeasureBeats - 16 + beat];
    }
    const auto lateEarlySubRatio = lateSub / earlySub;

    std::cout << "[METRIC] High Evolution kick+bass 190 BPM: motion Low Evolution="
              << lowEvolutionMotion << " High Evolution=" << highEvolutionMotion
              << ", stereo Low Evolution=" << lowEvolutionStereoMotion
              << " High Evolution=" << highEvolutionStereoMotion
              << ", NRMS=" << normalisedDifference
              << ", sub mean ratio=" << meanSubRatio
              << ", sub p95 ratio=" << p95SubRatio
              << ", sub motion Low Evolution=" << lowEvolutionSubMotion
              << " dB High Evolution=" << highEvolutionSubMotion
              << " dB, sub side Low Evolution=" << lowEvolutionSubSideFraction
              << " High Evolution=" << highEvolutionSubSideFraction
              << ", sub late/early=" << lateEarlySubRatio << '\n';

    require(normalisedDifference > 0.15,
            "High-Evolution Drift is too similar to Low Evolution on kick+bass");
    require(highEvolutionMotion > lowEvolutionMotion * 1.05,
            "High-Evolution Drift non-sub motion is not stronger than Low Evolution");
    require(highEvolutionStereoMotion > lowEvolutionStereoMotion * 1.04,
            "High-Evolution Drift stereo spectral motion is not stronger than Low Evolution");
    require(meanSubRatio >= 0.70 && meanSubRatio <= 1.05
                && p95SubRatio >= 0.70 && p95SubRatio <= 1.10,
            "High-Evolution Drift changes sub energy excessively");
    require(highEvolutionSubMotion <= 0.12,
            "High-Evolution Drift adds excessive slow sub modulation");
    require(lateEarlySubRatio <= 1.25,
            "High-Evolution Drift sub energy grows over the repeated pattern");
    require(highEvolutionSubSideFraction <= lowEvolutionSubSideFraction + 0.05,
            "High-Evolution Drift adds excessive stereo motion in the sub band");
}

void testVeilKickBass190()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto warmupBeats = 16;
    constexpr auto totalBeats = warmupBeats + static_cast<int>(kickBassMeasureBeats);
    constexpr auto beatSeconds = 60.0 / 190.0;
    const auto totalSamples = static_cast<int>(
        std::ceil(static_cast<double>(totalBeats) * beatSeconds * sampleRate));

    ReverbParameters parameters;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 6.0f;
    parameters.size = 1.2f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 18000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 1.0f;

    FDNReverb defaultReverb;
    parameters.mode = ReverbMode::defaultMode;
    defaultReverb.setParameters(parameters);
    defaultReverb.prepare(sampleRate, 512);
    FDNReverb veilReverb;
    parameters.mode = ReverbMode::veil;
    veilReverb.setParameters(parameters);
    veilReverb.prepare(sampleRate, 512);

    const auto returnOffsetSeconds = parameters.size
        * *std::min_element(defaultReverb.getNominalDelaySamples().begin(),
                            defaultReverb.getNominalDelaySamples().end())
        / sampleRate;
    FourBandMeter defaultLeftMeter(sampleRate);
    FourBandMeter defaultRightMeter(sampleRate);
    FourBandMeter veilLeftMeter(sampleRate);
    FourBandMeter veilRightMeter(sampleRate);

    std::array<double, kickBassMeasureBeats> defaultAttackHigh {};
    std::array<double, kickBassMeasureBeats> veilAttackHigh {};
    std::array<double, kickBassMeasureBeats> defaultCloudHigh {};
    std::array<double, kickBassMeasureBeats> veilCloudHigh {};
    std::array<double, kickBassMeasureBeats> defaultCloudNonSub {};
    std::array<double, kickBassMeasureBeats> veilCloudNonSub {};
    std::array<double, kickBassMeasureBeats> defaultHighPeak {};
    std::array<double, kickBassMeasureBeats> veilHighPeak {};
    std::array<double, kickBassMeasureBeats> defaultTotalEnergy {};
    std::array<double, kickBassMeasureBeats> veilTotalEnergy {};
    std::array<double, kickBassMeasureBeats> defaultSubEnergy {};
    std::array<double, kickBassMeasureBeats> veilSubEnergy {};
    auto differenceEnergy = 0.0;
    auto referenceEnergy = 0.0;

    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        const auto input = kickBass190Sample(sample, sampleRate);
        auto defaultLeft = input;
        auto defaultRight = input;
        auto veilLeft = input;
        auto veilRight = input;
        defaultReverb.processSample(defaultLeft, defaultRight);
        veilReverb.processSample(veilLeft, veilRight);
        require(std::isfinite(defaultLeft) && std::isfinite(defaultRight)
                    && std::isfinite(veilLeft) && std::isfinite(veilRight),
                "Veil kick+bass comparison produced NaN/Inf");

        const auto defaultLeftBands = defaultLeftMeter.process(defaultLeft);
        const auto defaultRightBands = defaultRightMeter.process(defaultRight);
        const auto veilLeftBands = veilLeftMeter.process(veilLeft);
        const auto veilRightBands = veilRightMeter.process(veilRight);
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto beat = static_cast<int>(std::floor(time / beatSeconds));
        if (beat < warmupBeats || beat >= totalBeats)
            continue;
        const auto measuredBeat = static_cast<std::size_t>(beat - warmupBeats);
        const auto phaseAfterReturn = time - static_cast<double>(beat) * beatSeconds
                                    - returnOffsetSeconds;

        auto defaultSampleEnergy = 0.0;
        auto veilSampleEnergy = 0.0;
        for (std::size_t band = 0; band < 4; ++band)
        {
            defaultSampleEnergy += defaultLeftBands[band] * defaultLeftBands[band]
                                 + defaultRightBands[band] * defaultRightBands[band];
            veilSampleEnergy += veilLeftBands[band] * veilLeftBands[band]
                              + veilRightBands[band] * veilRightBands[band];
        }
        defaultTotalEnergy[measuredBeat] += defaultSampleEnergy;
        veilTotalEnergy[measuredBeat] += veilSampleEnergy;
        defaultSubEnergy[measuredBeat] += defaultLeftBands[0] * defaultLeftBands[0]
                                       + defaultRightBands[0] * defaultRightBands[0];
        veilSubEnergy[measuredBeat] += veilLeftBands[0] * veilLeftBands[0]
                                    + veilRightBands[0] * veilRightBands[0];

        const auto defaultHighEnergy = defaultLeftBands[3] * defaultLeftBands[3]
                                     + defaultRightBands[3] * defaultRightBands[3];
        const auto veilHighEnergy = veilLeftBands[3] * veilLeftBands[3]
                                  + veilRightBands[3] * veilRightBands[3];
        if (phaseAfterReturn >= 0.0 && phaseAfterReturn < 0.012)
        {
            defaultAttackHigh[measuredBeat] += defaultHighEnergy;
            veilAttackHigh[measuredBeat] += veilHighEnergy;
            defaultHighPeak[measuredBeat] = std::max(
                defaultHighPeak[measuredBeat],
                std::max(std::abs(defaultLeftBands[3]), std::abs(defaultRightBands[3])));
            veilHighPeak[measuredBeat] = std::max(
                veilHighPeak[measuredBeat],
                std::max(std::abs(veilLeftBands[3]), std::abs(veilRightBands[3])));
        }
        else if (phaseAfterReturn >= 0.012 && phaseAfterReturn < 0.065)
        {
            defaultCloudHigh[measuredBeat] += defaultHighEnergy;
            veilCloudHigh[measuredBeat] += veilHighEnergy;
            for (std::size_t band = 1; band < 4; ++band)
            {
                defaultCloudNonSub[measuredBeat]
                    += defaultLeftBands[band] * defaultLeftBands[band]
                     + defaultRightBands[band] * defaultRightBands[band];
                veilCloudNonSub[measuredBeat]
                    += veilLeftBands[band] * veilLeftBands[band]
                     + veilRightBands[band] * veilRightBands[band];
            }
        }

        const auto differenceLeft = static_cast<double>(veilLeft) - defaultLeft;
        const auto differenceRight = static_cast<double>(veilRight) - defaultRight;
        differenceEnergy += differenceLeft * differenceLeft + differenceRight * differenceRight;
        referenceEnergy += 0.5
                         * (static_cast<double>(defaultLeft) * defaultLeft
                            + static_cast<double>(defaultRight) * defaultRight
                            + static_cast<double>(veilLeft) * veilLeft
                            + static_cast<double>(veilRight) * veilRight);
    }

    auto defaultAttackConcentration = 0.0;
    auto veilAttackConcentration = 0.0;
    for (std::size_t beat = 0; beat < kickBassMeasureBeats; ++beat)
    {
        defaultAttackConcentration += defaultAttackHigh[beat]
                                    / (defaultAttackHigh[beat]
                                       + defaultCloudHigh[beat] + 1.0e-30);
        veilAttackConcentration += veilAttackHigh[beat]
                                 / (veilAttackHigh[beat] + veilCloudHigh[beat] + 1.0e-30);
    }
    defaultAttackConcentration /= static_cast<double>(kickBassMeasureBeats);
    veilAttackConcentration /= static_cast<double>(kickBassMeasureBeats);

    const auto highPeakRatio = percentile95(veilHighPeak) / percentile95(defaultHighPeak);
    const auto cloudEnergyRatio = meanValue(veilCloudNonSub)
                                / meanValue(defaultCloudNonSub);
    const auto totalEnergyRatio = meanValue(veilTotalEnergy)
                                / meanValue(defaultTotalEnergy);
    const auto meanSubRatio = meanValue(veilSubEnergy) / meanValue(defaultSubEnergy);
    const auto p95SubRatio = percentile95(veilSubEnergy) / percentile95(defaultSubEnergy);
    const auto normalisedDifference = std::sqrt(differenceEnergy / referenceEnergy);
    auto earlyEnergy = 0.0;
    auto lateEnergy = 0.0;
    for (std::size_t beat = 0; beat < 16; ++beat)
    {
        earlyEnergy += veilTotalEnergy[beat];
        lateEnergy += veilTotalEnergy[kickBassMeasureBeats - 16 + beat];
    }
    const auto lateEarlyRatio = lateEnergy / earlyEnergy;

    std::cout << "[METRIC] Veil kick+bass 190 BPM: high peak ratio=" << highPeakRatio
              << ", concentration Default=" << defaultAttackConcentration
              << " Veil=" << veilAttackConcentration
              << ", cloud ratio=" << cloudEnergyRatio
              << ", total ratio=" << totalEnergyRatio
              << ", NRMS=" << normalisedDifference
              << ", sub mean ratio=" << meanSubRatio
              << ", sub p95 ratio=" << p95SubRatio
              << ", late/early=" << lateEarlyRatio << '\n';

    require(highPeakRatio <= 0.90,
            "Veil does not sufficiently soften kick high-frequency peaks");
    require(veilAttackConcentration <= defaultAttackConcentration * 0.85,
            "Veil does not redistribute kick attack energy into the cloud");
    require(cloudEnergyRatio >= 0.70 && cloudEnergyRatio <= 1.80,
            "Veil loses or amplifies excessive non-sub cloud energy");
    require(totalEnergyRatio >= 0.63 && totalEnergyRatio <= 1.58,
            "Veil changes repeated-pattern energy excessively");
    require(normalisedDifference > 0.10,
            "Veil is too similar to Default on kick+bass at 190 BPM");
    require(meanSubRatio >= 0.55 && meanSubRatio <= 1.25 && p95SubRatio <= 1.35,
            "Veil changes sub energy excessively");
    require(lateEarlyRatio <= 1.25,
            "Veil energy grows over the repeated kick+bass pattern");
}

void testPerceptualDuckerSpectralSelectivityAndSampleRates()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<double, 4> frequencies { 110.0, 630.0, 2200.0, 9000.0 };
    constexpr std::array<double, 4> phases { 0.17, 0.61, 1.13, 1.79 };
    constexpr auto twoPi = 6.28318530717958647692;
    std::array<double, sampleRates.size()> matchedReductionDb {};
    std::array<double, sampleRates.size()> totalLossDb {};

    for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
    {
        const auto sampleRate = sampleRates[rateIndex];
        SpatialDucker active;
        SpatialDucker bypassed;
        SpatialDucker quietConflict;
        active.prepare(sampleRate, 1.0f);
        bypassed.prepare(sampleRate, 0.0f);
        quietConflict.prepare(sampleRate, 1.0f);
        const auto highLevelBypass = bypassed.process(0.0f, 0.0f, 5.5f, -6.25f);
        require(std::bit_cast<std::uint32_t>(highLevelBypass.left)
                    == std::bit_cast<std::uint32_t>(5.5f)
                    && std::bit_cast<std::uint32_t>(highLevelBypass.right)
                        == std::bit_cast<std::uint32_t>(-6.25f),
                "Perceptual Ducking zero-percent bypass clamps high finite wet samples");

        const auto warmupSamples = static_cast<int>(sampleRate * 2.0);
        const auto measurementSamples = static_cast<int>(sampleRate);
        const auto totalSamples = warmupSamples + measurementSamples;
        std::array<double, frequencies.size()> referenceSin {};
        std::array<double, frequencies.size()> referenceCos {};
        std::array<double, frequencies.size()> activeSin {};
        std::array<double, frequencies.size()> activeCos {};
        auto quietReferenceSin = 0.0;
        auto quietReferenceCos = 0.0;
        auto quietActiveSin = 0.0;
        auto quietActiveCos = 0.0;
        auto referenceEnergy = 0.0;
        auto activeEnergy = 0.0;

        for (auto sample = 0; sample < totalSamples; ++sample)
        {
            const auto time = static_cast<double>(sample) / sampleRate;
            const auto dry = static_cast<float>(
                0.18 * std::sin(twoPi * frequencies[2] * time + 0.31));
            auto wet = 0.0f;
            for (std::size_t tone = 0; tone < frequencies.size(); ++tone)
                wet += static_cast<float>(
                    0.10 * std::sin(twoPi * frequencies[tone] * time + phases[tone]));
            const auto quietWet = static_cast<float>(
                0.0018 * std::sin(twoPi * frequencies[2] * time + phases[2]));

            const auto output = active.process(dry, dry, wet, wet);
            const auto reference = bypassed.process(dry, dry, wet, wet);
            const auto quietOutput = quietConflict.process(dry, dry, quietWet, quietWet);
            require(std::bit_cast<std::uint32_t>(reference.left)
                        == std::bit_cast<std::uint32_t>(wet)
                        && std::bit_cast<std::uint32_t>(reference.right)
                            == std::bit_cast<std::uint32_t>(wet),
                    "Perceptual Ducking at zero percent is not sample-exact");
            require(std::isfinite(output.left) && std::isfinite(output.right)
                        && std::isfinite(quietOutput.left),
                    "Perceptual Ducking produced NaN/Inf");

            if (sample < warmupSamples)
                continue;

            referenceEnergy += static_cast<double>(reference.left) * reference.left;
            activeEnergy += static_cast<double>(output.left) * output.left;
            for (std::size_t tone = 0; tone < frequencies.size(); ++tone)
            {
                const auto sine = std::sin(twoPi * frequencies[tone] * time);
                const auto cosine = std::cos(twoPi * frequencies[tone] * time);
                referenceSin[tone] += reference.left * sine;
                referenceCos[tone] += reference.left * cosine;
                activeSin[tone] += output.left * sine;
                activeCos[tone] += output.left * cosine;
            }
            const auto matchedSine = std::sin(twoPi * frequencies[2] * time);
            const auto matchedCosine = std::cos(twoPi * frequencies[2] * time);
            quietReferenceSin += quietWet * matchedSine;
            quietReferenceCos += quietWet * matchedCosine;
            quietActiveSin += quietOutput.left * matchedSine;
            quietActiveCos += quietOutput.left * matchedCosine;
        }

        std::array<double, frequencies.size()> bandReductionDb {};
        for (std::size_t tone = 0; tone < frequencies.size(); ++tone)
        {
            const auto referenceAmplitude = std::hypot(referenceSin[tone], referenceCos[tone]);
            const auto activeAmplitude = std::hypot(activeSin[tone], activeCos[tone]);
            bandReductionDb[tone] = 20.0 * std::log10(
                (referenceAmplitude + 1.0e-30) / (activeAmplitude + 1.0e-30));
        }
        matchedReductionDb[rateIndex] = bandReductionDb[2];
        totalLossDb[rateIndex] = 10.0 * std::log10(
            (referenceEnergy + 1.0e-30) / (activeEnergy + 1.0e-30));
        const auto quietReductionDb = 20.0 * std::log10(
            (std::hypot(quietReferenceSin, quietReferenceCos) + 1.0e-30)
            / (std::hypot(quietActiveSin, quietActiveCos) + 1.0e-30));

        std::cout << "[METRIC] Perceptual Ducking " << sampleRate
                  << " Hz: bands=" << bandReductionDb[0] << ',' << bandReductionDb[1]
                  << ',' << bandReductionDb[2] << ',' << bandReductionDb[3]
                  << " dB, total=" << totalLossDb[rateIndex]
                  << " dB, quiet conflict=" << quietReductionDb << " dB\n";

        require(bandReductionDb[2] >= 3.0 && bandReductionDb[2] <= 6.5,
                "Perceptual Ducking does not create a useful presence-band pocket");
        require(bandReductionDb[0] <= 1.25 && bandReductionDb[3] <= 1.25,
                "Perceptual Ducking changes distant wet bands excessively");
        require(bandReductionDb[1] <= 2.25,
                "Perceptual Ducking changes the neighbouring wet band excessively");
        require(totalLossDb[rateIndex] <= 3.0,
                "Perceptual Ducking collapses total wet loudness");
        require(bandReductionDb[2] - totalLossDb[rateIndex] >= 1.5,
                "Perceptual Ducking clarity comes mainly from full-band attenuation");
        require(quietReductionDb <= 0.75,
                "Perceptual Ducking cuts wet that cannot mask the dry source");
    }

    const auto [minimumMatched, maximumMatched] = std::minmax_element(
        matchedReductionDb.begin(), matchedReductionDb.end());
    const auto [minimumTotal, maximumTotal] = std::minmax_element(
        totalLossDb.begin(), totalLossDb.end());
    require(*maximumMatched - *minimumMatched <= 0.50,
            "Perceptual Ducking depth changes across sample rates");
    require(*maximumTotal - *minimumTotal <= 0.30,
            "Perceptual Ducking wet loudness changes across sample rates");
}

void testPerceptualDuckerAutomationAndAdaptiveStereo()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto twoPi = 6.28318530717958647692;

    SpatialDucker hardLeft;
    SpatialDucker hardRight;
    hardLeft.prepare(sampleRate, 1.0f);
    hardRight.prepare(sampleRate, 1.0f);
    const auto steadySamples = static_cast<int>(sampleRate * 2.0);
    const auto measurementStart = steadySamples - static_cast<int>(sampleRate * 0.5);
    auto wetEnergy = 0.0;
    auto hardLeftEnergy = 0.0;
    auto hardRightEnergy = 0.0;
    for (auto sample = 0; sample < steadySamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time + 0.21));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.83));
        const auto leftOutput = hardLeft.process(dry, 0.0f, wet, wet);
        const auto rightOutput = hardRight.process(0.0f, dry, wet, wet);
        require(std::bit_cast<std::uint32_t>(leftOutput.right)
                    == std::bit_cast<std::uint32_t>(wet)
                    && std::bit_cast<std::uint32_t>(rightOutput.left)
                        == std::bit_cast<std::uint32_t>(wet),
                "Hard-panned dry source changed the opposite wet channel");
        require(std::abs(leftOutput.left - rightOutput.right) <= 1.0e-6f,
                "Perceptual Ducking is not mirror-symmetric");
        if (sample >= measurementStart)
        {
            wetEnergy += static_cast<double>(wet) * wet;
            hardLeftEnergy += static_cast<double>(leftOutput.left) * leftOutput.left;
            hardRightEnergy += static_cast<double>(rightOutput.right) * rightOutput.right;
        }
    }
    const auto hardLeftReductionDb = 10.0 * std::log10(wetEnergy / hardLeftEnergy);
    const auto hardRightReductionDb = 10.0 * std::log10(wetEnergy / hardRightEnergy);
    require(hardLeftReductionDb >= 3.0 && hardLeftReductionDb <= 6.5,
            "Hard-left source does not open a useful local spectral pocket");
    require(std::abs(hardLeftReductionDb - hardRightReductionDb) <= 0.10,
            "Perceptual Ducking depth depends on pan direction");

    SpatialDucker transition;
    transition.prepare(sampleRate, 1.0f);
    for (auto sample = 0; sample < static_cast<int>(sampleRate); ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.7));
        (void) transition.process(dry, dry, wet, wet);
    }
    constexpr auto transitionWindowSamples = 960;
    auto transitionReferenceEnergy = 0.0;
    auto transitionErrorEnergy = 0.0;
    auto rightRecoverySample = -1;
    const auto transitionSamples = static_cast<int>(sampleRate * 0.5);
    for (auto sample = 0; sample < transitionSamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.7));
        const auto output = transition.process(dry, 0.0f, wet, wet);
        const auto error = static_cast<double>(output.right - wet);
        transitionReferenceEnergy += static_cast<double>(wet) * wet;
        transitionErrorEnergy += error * error;
        if ((sample + 1) % transitionWindowSamples == 0)
        {
            const auto normalisedError = std::sqrt(
                transitionErrorEnergy / (transitionReferenceEnergy + 1.0e-30));
            if (rightRecoverySample < 0 && normalisedError <= 0.02)
                rightRecoverySample = sample;
            transitionReferenceEnergy = 0.0;
            transitionErrorEnergy = 0.0;
        }
    }
    require(rightRecoverySample >= 0
                && static_cast<double>(rightRecoverySample) / sampleRate <= 0.30,
            "Centre-to-hard-left transition keeps shaping the right wet channel too long");

    SpatialDucker centred;
    centred.prepare(sampleRate, 1.0f);
    std::array<double, 2> referenceSin {};
    std::array<double, 2> referenceCos {};
    std::array<double, 2> outputSin {};
    std::array<double, 2> outputCos {};
    for (auto sample = 0; sample < steadySamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(
            0.16 * std::sin(twoPi * 2200.0 * time + 0.17)
            + 0.12 * std::sin(twoPi * 6500.0 * time + 0.57));
        const auto wetMid = static_cast<float>(
            0.10 * std::sin(twoPi * 2200.0 * time + 0.91));
        const auto wetSide = static_cast<float>(
            0.10 * std::sin(twoPi * 6500.0 * time + 1.31));
        const auto output = centred.process(dry, dry,
                                            wetMid + wetSide,
                                            wetMid - wetSide);
        if (sample < measurementStart)
            continue;
        const std::array reference { wetMid, wetSide };
        const std::array measured {
            0.5f * (output.left + output.right),
            0.5f * (output.left - output.right)
        };
        constexpr std::array frequencies { 2200.0, 6500.0 };
        for (std::size_t component = 0; component < frequencies.size(); ++component)
        {
            const auto sine = std::sin(twoPi * frequencies[component] * time);
            const auto cosine = std::cos(twoPi * frequencies[component] * time);
            referenceSin[component] += reference[component] * sine;
            referenceCos[component] += reference[component] * cosine;
            outputSin[component] += measured[component] * sine;
            outputCos[component] += measured[component] * cosine;
        }
    }
    std::array<double, 2> midSideReductionDb {};
    for (std::size_t component = 0; component < midSideReductionDb.size(); ++component)
        midSideReductionDb[component] = 20.0 * std::log10(
            std::hypot(referenceSin[component], referenceCos[component])
            / std::hypot(outputSin[component], outputCos[component]));
    require(midSideReductionDb[0] >= 3.0 && midSideReductionDb[0] <= 6.5,
            "Centred dry source does not clear the wet Mid presence band");
    require(midSideReductionDb[1] <= 1.0,
            "Centred dry source does not preserve the wet Side field");

    SpatialDucker macro;
    macro.prepare(sampleRate, 1.0f);
    for (auto sample = 0; sample < static_cast<int>(sampleRate); ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.7));
        (void) macro.process(dry, dry, wet, wet);
    }
    macro.setAmount(0.0f);
    auto earlyDifferenceEnergy = 0.0;
    auto exactBypassSamples = 0;
    const auto bypassSamples = static_cast<int>(sampleRate * 0.06);
    for (auto sample = 0; sample < bypassSamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.7));
        const auto output = macro.process(dry, dry, wet, wet);
        if (sample < static_cast<int>(sampleRate * 0.025))
        {
            const auto difference = static_cast<double>(output.left - wet);
            earlyDifferenceEnergy += difference * difference;
        }
        if (sample >= static_cast<int>(sampleRate * 0.055)
            && std::bit_cast<std::uint32_t>(output.left)
                == std::bit_cast<std::uint32_t>(wet)
            && std::bit_cast<std::uint32_t>(output.right)
                == std::bit_cast<std::uint32_t>(wet))
            ++exactBypassSamples;
    }
    require(earlyDifferenceEnergy > 1.0e-8,
            "Perceptual Ducking macro jumped directly to bypass");
    require(exactBypassSamples == static_cast<int>(sampleRate * 0.005),
            "Perceptual Ducking does not reach exact bypass after its 50-ms ramp");

    macro.setAmount(1.0f);
    auto reengagedDifferenceEnergy = 0.0;
    for (auto sample = 0; sample < bypassSamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.7));
        const auto output = macro.process(dry, dry, wet, wet);
        if (sample >= static_cast<int>(sampleRate * 0.055))
        {
            const auto difference = static_cast<double>(output.left - wet);
            reengagedDifferenceEnergy += difference * difference;
        }
    }
    require(reengagedDifferenceEnergy > 1.0e-7,
            "Re-engaged Perceptual Ducking did not use its warmed detector state");

    constexpr std::array<float, 5> amounts { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };
    std::array<SpatialDucker, amounts.size()> amountDucker;
    std::array<double, amounts.size()> amountEnergy {};
    for (std::size_t index = 0; index < amountDucker.size(); ++index)
        amountDucker[index].prepare(sampleRate, amounts[index]);
    for (auto sample = 0; sample < steadySamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto dry = static_cast<float>(0.18 * std::sin(twoPi * 2200.0 * time));
        const auto wet = static_cast<float>(0.10 * std::sin(twoPi * 2200.0 * time + 0.7));
        for (std::size_t index = 0; index < amountDucker.size(); ++index)
        {
            const auto output = amountDucker[index].process(dry, dry, wet, wet);
            if (sample >= measurementStart)
                amountEnergy[index] += static_cast<double>(output.left) * output.left;
        }
    }
    for (std::size_t index = 1; index < amountEnergy.size(); ++index)
        require(amountEnergy[index] < amountEnergy[index - 1],
                "Perceptual Ducking amount does not increase monotonically");

    SpatialDucker invalid;
    invalid.prepare(sampleRate, 0.0f);
    const auto invalidOutput = invalid.process(std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::infinity(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::infinity());
    require(invalidOutput.left == 0.0f && invalidOutput.right == 0.0f,
            "Invalid input contaminated bypassed Perceptual Ducking");

    std::cout << "[METRIC] Perceptual Ducking stereo: hard L/R="
              << hardLeftReductionDb << '/' << hardRightReductionDb
              << " dB, Mid/Side=" << midSideReductionDb[0] << '/'
              << midSideReductionDb[1] << " dB, right recovery="
              << 1000.0 * static_cast<double>(rightRecoverySample) / sampleRate
              << " ms\n";
}

void testPerceptualDuckingVocalClarityWithoutCollapse()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto phraseSamples = 57600;
    constexpr auto warmupSamples = 96000;
    constexpr auto measurementSamples = 192000;
    constexpr auto totalSamples = warmupSamples + measurementSamples;
    constexpr auto windowSamples = 2400;
    const DriftVocalSource vocal;

    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 8.0f;
    parameters.size = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 80.0f;
    parameters.highDampingHz = 9000.0f;
    parameters.evolution = 0.75f;
    parameters.width = 1.0f;
    parameters.ducking = 0.0f;

    FDNReverb wetGenerator;
    wetGenerator.setParameters(parameters);
    wetGenerator.prepare(sampleRate, 512);
    SpatialDucker ducker;
    ducker.prepare(sampleRate, 1.0f);
    FourBandMeter referenceLeftMeter(sampleRate);
    FourBandMeter referenceRightMeter(sampleRate);
    FourBandMeter duckedLeftMeter(sampleRate);
    FourBandMeter duckedRightMeter(sampleRate);
    std::array<double, 4> referenceBandEnergy {};
    std::array<double, 4> duckedBandEnergy {};
    auto referenceEnergy = 0.0;
    auto duckedEnergy = 0.0;
    auto windowReferenceEnergy = 0.0;
    auto windowDuckedEnergy = 0.0;
    auto maximumWindowLossDb = 0.0;
    auto peak = 0.0f;

    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        const auto dry = vocal.sample(sample % phraseSamples, phraseSamples, sampleRate);
        auto wetLeft = dry;
        auto wetRight = dry;
        wetGenerator.processSample(wetLeft, wetRight);
        const auto ducked = ducker.process(dry, dry, wetLeft, wetRight);
        require(std::isfinite(ducked.left) && std::isfinite(ducked.right),
                "Perceptual vocal Ducking produced NaN/Inf");
        peak = std::max({ peak, std::abs(ducked.left), std::abs(ducked.right) });
        if (sample < warmupSamples)
            continue;

        const auto referenceLeftBands = referenceLeftMeter.process(wetLeft);
        const auto referenceRightBands = referenceRightMeter.process(wetRight);
        const auto duckedLeftBands = duckedLeftMeter.process(ducked.left);
        const auto duckedRightBands = duckedRightMeter.process(ducked.right);
        for (std::size_t band = 0; band < referenceBandEnergy.size(); ++band)
        {
            referenceBandEnergy[band] += referenceLeftBands[band] * referenceLeftBands[band]
                                       + referenceRightBands[band] * referenceRightBands[band];
            duckedBandEnergy[band] += duckedLeftBands[band] * duckedLeftBands[band]
                                    + duckedRightBands[band] * duckedRightBands[band];
        }
        const auto referenceSampleEnergy = static_cast<double>(wetLeft) * wetLeft
                                         + static_cast<double>(wetRight) * wetRight;
        const auto duckedSampleEnergy = static_cast<double>(ducked.left) * ducked.left
                                      + static_cast<double>(ducked.right) * ducked.right;
        referenceEnergy += referenceSampleEnergy;
        duckedEnergy += duckedSampleEnergy;
        windowReferenceEnergy += referenceSampleEnergy;
        windowDuckedEnergy += duckedSampleEnergy;
        const auto measuredSample = sample - warmupSamples + 1;
        if (measuredSample % windowSamples == 0)
        {
            if (windowReferenceEnergy > 1.0e-12)
                maximumWindowLossDb = std::max(
                    maximumWindowLossDb,
                    10.0 * std::log10((windowReferenceEnergy + 1.0e-30)
                                      / (windowDuckedEnergy + 1.0e-30)));
            windowReferenceEnergy = 0.0;
            windowDuckedEnergy = 0.0;
        }
    }

    std::array<double, 4> bandLossDb {};
    for (std::size_t band = 0; band < bandLossDb.size(); ++band)
        bandLossDb[band] = 10.0 * std::log10(
            (referenceBandEnergy[band] + 1.0e-30)
            / (duckedBandEnergy[band] + 1.0e-30));
    const auto totalLossDb = 10.0 * std::log10(
        (referenceEnergy + 1.0e-30) / (duckedEnergy + 1.0e-30));
    const auto vocalPresenceLossDb = 10.0 * std::log10(
        (referenceBandEnergy[2] + referenceBandEnergy[3] + 1.0e-30)
        / (duckedBandEnergy[2] + duckedBandEnergy[3] + 1.0e-30));

    std::cout << "[METRIC] Perceptual vocal Ducking: bands="
              << bandLossDb[0] << ',' << bandLossDb[1] << ','
              << bandLossDb[2] << ',' << bandLossDb[3]
              << " dB, presence=" << vocalPresenceLossDb
              << " dB, total=" << totalLossDb
              << " dB, max 50-ms loss=" << maximumWindowLossDb << " dB\n";

    require(referenceEnergy > 1.0e-10 && peak < 4.0f,
            "Perceptual vocal Ducking lost or destabilised the wet signal");
    require(vocalPresenceLossDb >= 1.0,
            "Perceptual Ducking does not create enough vocal presence contrast");
    require(totalLossDb <= 1.6,
            "Perceptual vocal Ducking collapses the full wet tail");
    require(vocalPresenceLossDb - bandLossDb[0] >= 0.20,
            "Perceptual vocal Ducking does not favour clarity over low-band loss");
    require(bandLossDb[0] <= 1.5,
            "Perceptual vocal Ducking removes excessive low wet energy");
    require(maximumWindowLossDb <= 2.5,
            "Perceptual vocal Ducking creates an audible short-term wet hole");
    require(*std::min_element(bandLossDb.begin(), bandLossDb.end()) >= -0.5,
            "Perceptual vocal Ducking unintentionally boosts a wet band");
}

void testPerceptualDuckingKickBass190NoPumping()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto warmupBeats = 16;
    constexpr auto measuredBeats = 32;
    constexpr auto beatSeconds = 60.0 / 190.0;
    constexpr auto totalBeats = warmupBeats + measuredBeats;
    constexpr auto windowSamples = 480;
    const auto patternSamples = static_cast<int>(
        std::ceil(static_cast<double>(totalBeats) * beatSeconds * sampleRate));

    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 8.0f;
    parameters.size = 1.1f;
    parameters.preDelayMs = 0.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 12000.0f;
    parameters.evolution = 0.75f;
    parameters.width = 1.0f;
    parameters.ducking = 0.0f;

    FDNReverb wetGenerator;
    wetGenerator.setParameters(parameters);
    wetGenerator.prepare(sampleRate, 512);
    SpatialDucker ducker;
    ducker.prepare(sampleRate, 1.0f);
    auto referenceEnergy = 0.0;
    auto duckedEnergy = 0.0;
    auto attackReferenceEnergy = 0.0;
    auto attackDuckedEnergy = 0.0;
    auto cloudReferenceEnergy = 0.0;
    auto cloudDuckedEnergy = 0.0;
    auto windowReferenceEnergy = 0.0;
    auto windowDuckedEnergy = 0.0;
    auto maximumWindowLossDb = 0.0;
    std::array<double, 2> halfReferenceEnergy {};
    std::array<double, 2> halfDuckedEnergy {};
    auto peak = 0.0f;

    for (auto sample = 0; sample < patternSamples; ++sample)
    {
        const auto dry = kickBass190Sample(sample, sampleRate);
        auto wetLeft = dry;
        auto wetRight = dry;
        wetGenerator.processSample(wetLeft, wetRight);
        const auto ducked = ducker.process(dry, dry, wetLeft, wetRight);
        require(std::isfinite(ducked.left) && std::isfinite(ducked.right),
                "Perceptual 190-BPM Ducking produced NaN/Inf");
        peak = std::max({ peak, std::abs(ducked.left), std::abs(ducked.right) });

        const auto time = static_cast<double>(sample) / sampleRate;
        const auto beat = static_cast<int>(std::floor(time / beatSeconds));
        if (beat < warmupBeats || beat >= totalBeats)
            continue;

        const auto referenceSampleEnergy = static_cast<double>(wetLeft) * wetLeft
                                         + static_cast<double>(wetRight) * wetRight;
        const auto duckedSampleEnergy = static_cast<double>(ducked.left) * ducked.left
                                      + static_cast<double>(ducked.right) * ducked.right;
        referenceEnergy += referenceSampleEnergy;
        duckedEnergy += duckedSampleEnergy;
        const auto measuredBeat = beat - warmupBeats;
        const auto half = measuredBeat < measuredBeats / 2 ? 0u : 1u;
        halfReferenceEnergy[half] += referenceSampleEnergy;
        halfDuckedEnergy[half] += duckedSampleEnergy;

        const auto beatPosition = std::fmod(time, beatSeconds);
        const auto quarterBeat = beatSeconds * 0.25;
        const auto notePosition = std::fmod(beatPosition, quarterBeat);
        if (notePosition < 0.012)
        {
            attackReferenceEnergy += referenceSampleEnergy;
            attackDuckedEnergy += duckedSampleEnergy;
        }
        else if (notePosition >= 0.035 && notePosition < 0.065)
        {
            cloudReferenceEnergy += referenceSampleEnergy;
            cloudDuckedEnergy += duckedSampleEnergy;
        }

        windowReferenceEnergy += referenceSampleEnergy;
        windowDuckedEnergy += duckedSampleEnergy;
        const auto measuredSample = sample
            - static_cast<int>(std::ceil(warmupBeats * beatSeconds * sampleRate)) + 1;
        if (measuredSample > 0 && measuredSample % windowSamples == 0)
        {
            if (windowReferenceEnergy > 1.0e-12)
                maximumWindowLossDb = std::max(
                    maximumWindowLossDb,
                    10.0 * std::log10((windowReferenceEnergy + 1.0e-30)
                                      / (windowDuckedEnergy + 1.0e-30)));
            windowReferenceEnergy = 0.0;
            windowDuckedEnergy = 0.0;
        }
    }

    const auto totalLossDb = 10.0 * std::log10(
        (referenceEnergy + 1.0e-30) / (duckedEnergy + 1.0e-30));
    const auto attackContrastDb = 10.0 * std::log10(
        (attackReferenceEnergy + 1.0e-30) / (attackDuckedEnergy + 1.0e-30));
    const auto cloudLossDb = 10.0 * std::log10(
        (cloudReferenceEnergy + 1.0e-30) / (cloudDuckedEnergy + 1.0e-30));
    const auto firstHalfRatio = halfDuckedEnergy[0] / (halfReferenceEnergy[0] + 1.0e-30);
    const auto secondHalfRatio = halfDuckedEnergy[1] / (halfReferenceEnergy[1] + 1.0e-30);

    auto tailReferenceEnergy = 0.0;
    auto tailErrorEnergy = 0.0;
    const auto tailSamples = static_cast<int>(sampleRate);
    const auto tailMeasurementStart = static_cast<int>(sampleRate * 0.45);
    for (auto sample = 0; sample < tailSamples; ++sample)
    {
        auto wetLeft = 0.0f;
        auto wetRight = 0.0f;
        wetGenerator.processSample(wetLeft, wetRight);
        const auto ducked = ducker.process(0.0f, 0.0f, wetLeft, wetRight);
        require(std::isfinite(ducked.left) && std::isfinite(ducked.right),
                "Perceptual Ducking tail recovery produced NaN/Inf");
        if (sample >= tailMeasurementStart)
        {
            tailReferenceEnergy += static_cast<double>(wetLeft) * wetLeft
                                 + static_cast<double>(wetRight) * wetRight;
            const auto leftError = static_cast<double>(ducked.left - wetLeft);
            const auto rightError = static_cast<double>(ducked.right - wetRight);
            tailErrorEnergy += leftError * leftError + rightError * rightError;
        }
    }
    const auto tailRecoveryError = std::sqrt(
        tailErrorEnergy / (tailReferenceEnergy + 1.0e-30));

    std::cout << "[METRIC] Perceptual Ducking kick+bass 190: total="
              << totalLossDb << " dB, attacks=" << attackContrastDb
              << " dB, cloud=" << cloudLossDb
              << " dB, max 10-ms loss=" << maximumWindowLossDb
              << " dB, half ratio=" << secondHalfRatio / firstHalfRatio
              << ", tail NRMS=" << tailRecoveryError << '\n';

    require(referenceEnergy > 1.0e-10 && peak < 4.0f,
            "Perceptual 190-BPM Ducking lost or destabilised the wet signal");
    require(totalLossDb <= 1.5,
            "Perceptual 190-BPM Ducking collapses the wet pattern");
    require(attackContrastDb >= 0.35,
            "Perceptual Ducking does not expose kick/bass attacks");
    require(cloudLossDb <= 1.5,
            "Perceptual Ducking removes too much inter-attack cloud");
    require(maximumWindowLossDb <= 2.5,
            "Perceptual Ducking creates a pumping hole at 190 BPM");
    require(secondHalfRatio / firstHalfRatio >= 0.90
                && secondHalfRatio / firstHalfRatio <= 1.10,
            "Perceptual Ducking gain drifts over the repeated 190-BPM pattern");
    require(tailRecoveryError <= 0.02,
            "Perceptual Ducking remains imprinted on the released tail");
}

void testPerceptualDuckingFreezeIsolation()
{
    constexpr auto sampleRate = 48000.0;
    constexpr double twoPi = 6.28318530717958647692;
    ReverbParameters referenceParameters;
    referenceParameters.mode = ReverbMode::drift;
    referenceParameters.mix = 1.0f;
    referenceParameters.decaySeconds = 30.0f;
    referenceParameters.size = 1.2f;
    referenceParameters.preDelayMs = 0.0f;
    referenceParameters.lowCutHz = 30.0f;
    referenceParameters.highDampingHz = 16000.0f;
    referenceParameters.evolution = 1.0f;
    referenceParameters.width = 1.4f;
    referenceParameters.ducking = 0.0f;
    auto duckedParameters = referenceParameters;
    duckedParameters.ducking = 1.0f;

    FDNReverb reference;
    FDNReverb ducked;
    reference.setParameters(referenceParameters);
    ducked.setParameters(duckedParameters);
    reference.prepare(sampleRate, 127);
    ducked.prepare(sampleRate, 127);

    const auto seedSamples = static_cast<int>(sampleRate * 0.7);
    for (auto sample = 0; sample < seedSamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto input = static_cast<float>(0.13 * std::sin(twoPi * 311.0 * time)
                                              + 0.09 * std::sin(twoPi * 727.0 * time)
                                              + 0.11 * std::sin(twoPi * 2200.0 * time));
        auto referenceLeft = input;
        auto referenceRight = input;
        auto duckedLeft = input;
        auto duckedRight = input;
        reference.processSample(referenceLeft, referenceRight);
        ducked.processSample(duckedLeft, duckedRight);
    }

    referenceParameters.freeze = true;
    duckedParameters.freeze = true;
    reference.setParameters(referenceParameters);
    ducked.setParameters(duckedParameters);

    const auto settleSamples = static_cast<int>(sampleRate * 4.0);
    for (auto sample = 0; sample < settleSamples; ++sample)
    {
        auto referenceLeft = 0.0f;
        auto referenceRight = 0.0f;
        auto duckedLeft = 0.0f;
        auto duckedRight = 0.0f;
        reference.processSample(referenceLeft, referenceRight);
        ducked.processSample(duckedLeft, duckedRight);
        require(std::isfinite(duckedLeft) && std::isfinite(duckedRight),
                "Spatial Ducking Freeze settle produced NaN/Inf");
    }

    const auto pulseSamples = static_cast<int>(sampleRate * 0.4);
    const auto measureStart = static_cast<int>(sampleRate * 0.1);
    double referenceLeftEnergy = 0.0;
    double duckedLeftEnergy = 0.0;
    double referenceRightEnergy = 0.0;
    double rightErrorEnergy = 0.0;
    for (auto sample = 0; sample < pulseSamples; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto detectorInput = static_cast<float>(
            0.28 * std::sin(twoPi * 2200.0 * time + 0.23));
        auto referenceLeft = detectorInput;
        auto referenceRight = 0.0f;
        auto duckedLeft = detectorInput;
        auto duckedRight = 0.0f;
        reference.processSample(referenceLeft, referenceRight);
        ducked.processSample(duckedLeft, duckedRight);
        require(std::isfinite(duckedLeft) && std::isfinite(duckedRight),
                "Spatial Ducking frozen pulse produced NaN/Inf");
        if (sample >= measureStart)
        {
            referenceLeftEnergy += static_cast<double>(referenceLeft) * referenceLeft;
            duckedLeftEnergy += static_cast<double>(duckedLeft) * duckedLeft;
            referenceRightEnergy += static_cast<double>(referenceRight) * referenceRight;
            const auto rightError = static_cast<double>(duckedRight - referenceRight);
            rightErrorEnergy += rightError * rightError;
        }
    }

    require(referenceLeftEnergy > 1.0e-10 && referenceRightEnergy > 1.0e-10,
            "Frozen tail became silent before the spatial Ducking test");
    const auto leftEnergyRatio = duckedLeftEnergy / referenceLeftEnergy;
    const auto rightNormalisedError = std::sqrt(rightErrorEnergy / referenceRightEnergy);
    require(leftEnergyRatio >= 0.45 && leftEnergyRatio <= 0.90,
            "Perceptual Ducking does not balance local clarity and frozen-tail energy");
    require(rightNormalisedError <= 1.0e-6,
            "Hard-left detector changed the right frozen tail");

    duckedParameters.ducking = 0.0f;
    ducked.setParameters(duckedParameters);
    const auto recoverySamples = static_cast<int>(sampleRate * 4.0);
    const auto comparisonStart = recoverySamples - static_cast<int>(sampleRate * 0.5);
    double referenceEnergy = 0.0;
    double errorEnergy = 0.0;
    for (auto sample = 0; sample < recoverySamples; ++sample)
    {
        auto referenceLeft = 0.0f;
        auto referenceRight = 0.0f;
        auto duckedLeft = 0.0f;
        auto duckedRight = 0.0f;
        reference.processSample(referenceLeft, referenceRight);
        ducked.processSample(duckedLeft, duckedRight);
        require(std::isfinite(duckedLeft) && std::isfinite(duckedRight),
                "Spatial Ducking Freeze recovery produced NaN/Inf");
        if (sample >= comparisonStart)
        {
            referenceEnergy += static_cast<double>(referenceLeft) * referenceLeft
                             + static_cast<double>(referenceRight) * referenceRight;
            const auto leftError = static_cast<double>(duckedLeft - referenceLeft);
            const auto rightError = static_cast<double>(duckedRight - referenceRight);
            errorEnergy += leftError * leftError + rightError * rightError;
        }
    }

    require(referenceEnergy > 1.0e-10, "Frozen tail vanished during Ducking recovery");
    const auto recoveryError = std::sqrt(errorEnergy / referenceEnergy);
    require(recoveryError <= 1.0e-5,
            "Ducking altered the internal frozen FDN state");

    std::cout << "[METRIC] Perceptual Ducking Freeze: left energy ratio=" << leftEnergyRatio
              << ", right NRMS=" << rightNormalisedError
              << ", recovered NRMS=" << recoveryError << '\n';
}

enum class AnalyzerStereoPlacement
{
    centred,
    leftOnly,
    rightOnly,
    antiPhase
};

[[nodiscard]] HarmonicAnalysisFrame analyseSyntheticChord(
    double sampleRate,
    const std::array<int, 4>& midiNotes,
    int noteCount,
    AnalyzerStereoPlacement placement = AnalyzerStereoPlacement::centred,
    double seconds = 2.0,
    float level = 0.16f)
{
    HarmonicAnalyzer analyzer;
    analyzer.prepare(sampleRate);
    const auto sampleCount = static_cast<int>(sampleRate * seconds);
    const auto voiceGain = level / static_cast<float>(std::max(1, noteCount));
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        auto signal = 0.0f;
        for (auto voice = 0; voice < noteCount; ++voice)
        {
            const auto phase = 2.0 * 3.14159265358979323846
                             * midiFrequency(midiNotes[static_cast<std::size_t>(voice)])
                             * time
                             + 0.37 * static_cast<double>(voice);
            signal += voiceGain * static_cast<float>(
                std::sin(phase)
                + 0.30 * std::sin(2.0 * phase + 0.19)
                + 0.10 * std::sin(4.0 * phase + 0.43));
        }

        auto left = signal;
        auto right = signal;
        switch (placement)
        {
            case AnalyzerStereoPlacement::leftOnly:
                right = 0.0f;
                break;
            case AnalyzerStereoPlacement::rightOnly:
                left = 0.0f;
                break;
            case AnalyzerStereoPlacement::antiPhase:
                right = -signal;
                break;
            case AnalyzerStereoPlacement::centred:
            default:
                break;
        }
        static_cast<void>(analyzer.processSample(left, right));
    }
    return analyzer.getFrame();
}

[[nodiscard]] float chromaCosine(
    const HarmonicAnalyzer::PitchClassWeights& first,
    const HarmonicAnalyzer::PitchClassWeights& second)
{
    auto dot = 0.0;
    auto firstEnergy = 0.0;
    auto secondEnergy = 0.0;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        dot += static_cast<double>(first[index]) * second[index];
        firstEnergy += static_cast<double>(first[index]) * first[index];
        secondEnergy += static_cast<double>(second[index]) * second[index];
    }
    return firstEnergy > 1.0e-20 && secondEnergy > 1.0e-20
        ? static_cast<float>(dot / std::sqrt(firstEnergy * secondEnergy))
        : 0.0f;
}

struct AnalyzerSourceMetrics
{
    HarmonicAnalyzer::PitchClassWeights confidenceWeightedPitchClasses {};
    float meanConfidence = 0.0f;
    float percentile95Confidence = 0.0f;
    float maximumConfidence = 0.0f;
    std::size_t frameCount = 0;
};

template <typename Source>
[[nodiscard]] AnalyzerSourceMetrics analyseSourceStatistics(
    Source source,
    double seconds = 6.0,
    double warmupSeconds = 1.0)
{
    constexpr auto sampleRate = 48000.0;
    HarmonicAnalyzer analyzer;
    analyzer.prepare(sampleRate);

    std::vector<float> confidences;
    confidences.reserve(static_cast<std::size_t>(seconds * 200.0));
    HarmonicAnalyzer::PitchClassWeights confidenceWeightedSums {};
    auto confidenceSum = 0.0;
    const auto sampleCount = static_cast<int>(std::ceil(seconds * sampleRate));
    const auto warmupSamples = static_cast<int>(
        std::ceil(warmupSeconds * sampleRate));
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto stereo = source(sample, sampleRate);
        if (!analyzer.processSample(stereo[0], stereo[1])
            || sample < warmupSamples)
            continue;

        const auto& frame = analyzer.getFrame();
        confidences.push_back(frame.confidence);
        confidenceSum += frame.confidence;
        for (std::size_t pitchClass = 0;
             pitchClass < confidenceWeightedSums.size();
             ++pitchClass)
        {
            confidenceWeightedSums[pitchClass]
                += frame.confidence * frame.pitchClassWeights[pitchClass];
        }
    }

    require(!confidences.empty(),
            "Harmonic Analyzer source statistics produced no analysis frames");
    AnalyzerSourceMetrics metrics;
    metrics.frameCount = confidences.size();
    metrics.meanConfidence = static_cast<float>(
        std::accumulate(confidences.begin(), confidences.end(), 0.0)
        / static_cast<double>(confidences.size()));
    metrics.maximumConfidence = *std::max_element(
        confidences.begin(), confidences.end());
    std::sort(confidences.begin(), confidences.end());
    const auto percentileIndex = static_cast<std::size_t>(
        std::floor(0.95 * static_cast<double>(confidences.size() - 1)));
    metrics.percentile95Confidence = confidences[percentileIndex];

    const auto inverseConfidence = confidenceSum > 1.0e-12
        ? static_cast<float>(1.0 / confidenceSum)
        : 0.0f;
    for (std::size_t pitchClass = 0;
         pitchClass < metrics.confidenceWeightedPitchClasses.size();
         ++pitchClass)
    {
        metrics.confidenceWeightedPitchClasses[pitchClass]
            = confidenceWeightedSums[pitchClass] * inverseConfidence;
    }
    return metrics;
}

[[nodiscard]] float strongestWrongPitchClass(
    const AnalyzerSourceMetrics& metrics,
    std::size_t expectedPitchClass)
{
    auto strongestWrong = 0.0f;
    for (std::size_t pitchClass = 0;
         pitchClass < metrics.confidenceWeightedPitchClasses.size();
         ++pitchClass)
    {
        if (pitchClass != expectedPitchClass)
        {
            strongestWrong = std::max(
                strongestWrong,
                metrics.confidenceWeightedPitchClasses[pitchClass]);
        }
    }
    return strongestWrong;
}

void testHarmonicAnalyzerKickBassConfidenceRegression()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto seconds = 6.0;
    const auto kick = analyseSourceStatistics(
        [] (int sample, double rate)
        {
            const auto value = kickOnly190Sample(sample, rate);
            return std::array { value, value };
        },
        seconds);
    const auto bass = analyseSourceStatistics(
        [] (int sample, double rate)
        {
            const auto value = bassOnly190Sample(sample, rate);
            return std::array { value, value };
        },
        seconds);
    const auto combined = analyseSourceStatistics(
        [] (int sample, double rate)
        {
            const auto value = kickBass190Sample(sample, rate);
            return std::array { value, value };
        },
        seconds);

    auto maximumReconstructionError = 0.0f;
    const auto sampleCount = static_cast<int>(seconds * sampleRate);
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto reconstructed = std::clamp(
            kickOnly190Sample(sample, sampleRate)
                + bassOnly190Sample(sample, sampleRate),
            -1.0f, 1.0f);
        maximumReconstructionError = std::max(
            maximumReconstructionError,
            std::abs(reconstructed
                     - kickBass190Sample(sample, sampleRate)));
    }

    constexpr auto aPitchClass = std::size_t { 9 };
    const auto combinedA
        = combined.confidenceWeightedPitchClasses[aPitchClass];
    const auto combinedWrong = strongestWrongPitchClass(
        combined, aPitchClass);
    const auto bassA = bass.confidenceWeightedPitchClasses[aPitchClass];
    const auto bassWrong = strongestWrongPitchClass(bass, aPitchClass);

    std::cout << "[METRIC] Harmonic Analyzer 190 BPM confidence"
              << ": kick mean/p95/max=" << kick.meanConfidence << '/'
              << kick.percentile95Confidence << '/' << kick.maximumConfidence
              << ", bass=" << bass.meanConfidence << '/'
              << bass.percentile95Confidence << '/' << bass.maximumConfidence
              << ", combined=" << combined.meanConfidence << '/'
              << combined.percentile95Confidence << '/'
              << combined.maximumConfidence
              << ", bass A/wrong=" << bassA << '/' << bassWrong
              << ", combined A/wrong=" << combinedA << '/'
              << combinedWrong
              << ", reconstruction error=" << maximumReconstructionError
              << '\n';

    require(maximumReconstructionError <= 2.0e-6f,
            "Kick-only and bass-only probes no longer reconstruct "
            "kickBass190Sample");
    require(kick.meanConfidence <= 0.10f
                && kick.percentile95Confidence <= 0.18f,
            "Harmonic Analyzer treats the 190 BPM kick as stable harmony");
    require(bass.meanConfidence <= 0.30f
                && bass.percentile95Confidence <= 0.45f,
            "Harmonic Analyzer assigns chord-level confidence to a monophonic "
            "A bassline");
    require(combined.meanConfidence <= 0.35f
                && combined.percentile95Confidence <= 0.50f,
            "Harmonic Analyzer assigns chord-level confidence to the exact "
            "190 BPM kick+bass pattern");
    require(bass.meanConfidence <= 0.20f
                || bassA >= 0.85f * bassWrong,
            "A confident monophonic bassline points away from its A pitch class");
    require(combined.meanConfidence <= 0.20f
                || combinedA >= 0.85f * combinedWrong,
            "A confident 190 BPM kick+bass analysis points away from A");
}

void testHarmonicAnalyzerOpenVoicingRegression()
{
    constexpr std::array midiNotes { 48, 67, 76 }; // C3, G4, E5
    constexpr std::array gains { 0.10f, 0.05f, 0.05f };
    constexpr auto twoPi = 6.28318530717958647692;
    const auto settled = analyseSourceStatistics(
        [midiNotes, gains] (int sample, double sampleRate)
        {
            const auto time = static_cast<double>(sample) / sampleRate;
            auto signal = 0.0f;
            for (std::size_t voice = 0; voice < midiNotes.size(); ++voice)
            {
                const auto phase = twoPi * midiFrequency(midiNotes[voice]) * time
                                 + 0.31 * static_cast<double>(voice);
                signal += gains[voice] * static_cast<float>(
                    std::sin(phase)
                    + 0.18 * std::sin(2.0 * phase + 0.2));
            }
            return std::array { signal, signal };
        },
        4.0,
        2.0);

    constexpr auto cPitchClass = std::size_t { 0 };
    constexpr auto ePitchClass = std::size_t { 4 };
    constexpr auto gPitchClass = std::size_t { 7 };
    const auto cRecall
        = settled.confidenceWeightedPitchClasses[cPitchClass];
    const auto eRecall
        = settled.confidenceWeightedPitchClasses[ePitchClass];
    const auto gRecall
        = settled.confidenceWeightedPitchClasses[gPitchClass];

    std::cout << "[METRIC] Harmonic Analyzer open C3-G4-E5"
              << ": settled confidence=" << settled.meanConfidence
              << ", C/E/G recall=" << cRecall << '/'
              << eRecall << '/' << gRecall << '\n';

    require(settled.meanConfidence >= 0.20f,
            "Root-dominant open C major loses chord-level confidence");
    require(cRecall >= 0.75f && eRecall >= 0.30f && gRecall >= 0.30f,
            "Root-dominant open C major loses a genuine C/E/G chord tone");
}

void testHarmonicAnalyzerConfidentWrongRegression()
{
    constexpr auto c3Frequency = 130.81278265;
    constexpr auto twoPi = 6.28318530717958647692;
    const auto saw = analyseSourceStatistics(
        [] (int sample, double sampleRate)
        {
            const auto time = static_cast<double>(sample) / sampleRate;
            auto signal = 0.0;
            for (auto harmonic = 1; harmonic <= 20; ++harmonic)
            {
                signal += std::sin(twoPi * c3Frequency * harmonic * time)
                        / static_cast<double>(harmonic);
            }
            const auto value = static_cast<float>(0.12 * signal);
            return std::array { value, value };
        });
    const auto square = analyseSourceStatistics(
        [] (int sample, double sampleRate)
        {
            const auto time = static_cast<double>(sample) / sampleRate;
            auto signal = 0.0;
            for (auto harmonic = 1; harmonic <= 21; harmonic += 2)
            {
                signal += std::sin(twoPi * c3Frequency * harmonic * time)
                        / static_cast<double>(harmonic);
            }
            const auto value = static_cast<float>(0.12 * signal);
            return std::array { value, value };
        });

    const DriftVocalSource vocalSource;
    constexpr auto vocalSamples = 6 * 48000;
    const auto vocal = analyseSourceStatistics(
        [&vocalSource] (int sample, double sampleRate)
        {
            const auto value = vocalSource.sample(
                sample, vocalSamples, sampleRate);
            return std::array { value, 0.91f * value };
        });
    const auto bell = analyseSourceStatistics(
        [] (int sample, double sampleRate)
        {
            constexpr std::array ratios { 1.0, 2.71, 4.08, 5.43 };
            constexpr std::array gains { 1.0, 0.70, 0.50, 0.35 };
            const auto time = static_cast<double>(sample) / sampleRate;
            auto signal = 0.0;
            for (std::size_t partial = 0; partial < ratios.size(); ++partial)
            {
                signal += gains[partial]
                        * std::sin(twoPi * 261.625565
                                   * ratios[partial] * time);
            }
            const auto value = static_cast<float>(0.08 * signal);
            return std::array { value, value };
        });
    const auto conflict = analyseSourceStatistics(
        [] (int sample, double sampleRate)
        {
            const auto time = static_cast<double>(sample) / sampleRate;
            auto left = 0.0;
            auto right = 0.0;
            for (const auto note : { 60, 64, 67 })
                left += 0.04 * std::sin(
                    twoPi * midiFrequency(note) * time);
            for (const auto note : { 66, 70, 73 })
                right += 0.04 * std::sin(
                    twoPi * midiFrequency(note) * time);
            return std::array { static_cast<float>(left),
                                static_cast<float>(right) };
        });

    constexpr auto cPitchClass = std::size_t { 0 };
    const auto sawC = saw.confidenceWeightedPitchClasses[cPitchClass];
    const auto sawWrong = strongestWrongPitchClass(saw, cPitchClass);
    const auto squareC = square.confidenceWeightedPitchClasses[cPitchClass];
    const auto squareWrong = strongestWrongPitchClass(square, cPitchClass);
    std::cout << "[METRIC] Harmonic Analyzer confident-wrong sources"
              << ": saw mean/p95=" << saw.meanConfidence << '/'
              << saw.percentile95Confidence
              << " C/wrong=" << sawC << '/' << sawWrong
              << ", square=" << square.meanConfidence << '/'
              << square.percentile95Confidence
              << " C/wrong=" << squareC << '/' << squareWrong
              << ", vocal=" << vocal.meanConfidence << '/'
              << vocal.percentile95Confidence
              << ", bell=" << bell.meanConfidence << '/'
              << bell.percentile95Confidence
              << ", conflicting stereo chords=" << conflict.meanConfidence
              << '/' << conflict.percentile95Confidence << '\n';

    require(saw.meanConfidence <= 0.35f
                && saw.percentile95Confidence <= 0.50f
                && sawC >= 0.85f * sawWrong,
            "A saw bass creates a confidently wrong harmonic field");
    require(square.meanConfidence <= 0.35f
                && square.percentile95Confidence <= 0.50f
                && squareC >= 0.85f * squareWrong,
            "A square bass is mistaken for a polyphonic chord");
    require(vocal.meanConfidence <= 0.35f
                && vocal.percentile95Confidence <= 0.50f,
            "A monophonic vocal is mistaken for a stable chord");
    require(bell.meanConfidence <= 0.35f
                && bell.percentile95Confidence <= 0.50f,
            "Inharmonic bell partials are mistaken for a stable chord");
    require(conflict.meanConfidence <= 0.65f
                && conflict.percentile95Confidence <= 0.75f,
            "Unrelated C-major/F#-major stereo fields are treated as one "
            "certain harmony");
}

void testHarmonicAnalyzerPitchStereoAndSampleRates()
{
    auto minimumWinnerRatio = std::numeric_limits<float>::max();
    auto minimumSingleConfidence = 1.0f;
    auto maximumSingleConfidence = 0.0f;
    for (auto pitchClass = 0; pitchClass < 12; ++pitchClass)
    {
        const auto midiNote = 60 + pitchClass;
        const auto frame = analyseSyntheticChord(
            48000.0, { midiNote, 0, 0, 0 }, 1);
        const auto expected = static_cast<std::size_t>(pitchClass);
        auto strongestWrong = 0.0f;
        for (std::size_t index = 0; index < frame.pitchClassWeights.size(); ++index)
        {
            require(std::isfinite(frame.pitchClassWeights[index])
                        && frame.pitchClassWeights[index] >= 0.0f
                        && frame.pitchClassWeights[index] <= 1.0f,
                    "Harmonic Analyzer produced an invalid pitch weight");
            if (index != expected)
                strongestWrong = std::max(strongestWrong,
                                          frame.pitchClassWeights[index]);
        }
        const auto winnerRatio = frame.pitchClassWeights[expected]
            / std::max(strongestWrong, 1.0e-6f);
        minimumWinnerRatio = std::min(minimumWinnerRatio, winnerRatio);
        minimumSingleConfidence = std::min(minimumSingleConfidence,
                                           frame.confidence);
        maximumSingleConfidence = std::max(maximumSingleConfidence,
                                           frame.confidence);
        require(frame.pitchClassWeights[expected] >= 0.75f
                    && winnerRatio >= 1.6f,
                "Harmonic Analyzer selected the wrong pitch class "
                    + std::to_string(pitchClass)
                    + ": expected=" + std::to_string(
                        frame.pitchClassWeights[expected])
                    + ", strongest wrong=" + std::to_string(strongestWrong)
                    + ", confidence=" + std::to_string(frame.confidence)
                    + ", activity=" + std::to_string(frame.activity)
                    + ", transient=" + std::to_string(
                        frame.transientReliability));
        require(frame.confidence >= 0.05f && frame.confidence <= 0.75f,
                "Harmonic Analyzer single-note confidence is unreasonable: "
                    + std::to_string(frame.confidence));
    }

    for (const auto midiNote : { 60, 66, 69 })
    {
        for (const auto cents : { -30.0, 30.0 })
        {
            HarmonicAnalyzer detuned;
            detuned.prepare(48000.0);
            const auto frequency = midiFrequency(midiNote)
                                 * std::exp2(cents / 1200.0);
            for (auto sample = 0; sample < 96000; ++sample)
            {
                const auto phase = 2.0 * 3.14159265358979323846
                                 * frequency * sample / 48000.0;
                const auto signal = 0.14f * static_cast<float>(
                    std::sin(phase) + 0.28 * std::sin(2.0 * phase + 0.17));
                static_cast<void>(detuned.processSample(signal, -signal));
            }
            const auto& frame = detuned.getFrame();
            const auto expected = static_cast<std::size_t>(midiNote % 12);
            auto strongestWrong = 0.0f;
            for (std::size_t index = 0; index < frame.pitchClassWeights.size(); ++index)
                if (index != expected)
                    strongestWrong = std::max(strongestWrong,
                                              frame.pitchClassWeights[index]);
            require(frame.pitchClassWeights[expected]
                        >= 1.25f * std::max(strongestWrong, 1.0e-6f),
                    "Harmonic Analyzer loses MIDI "
                        + std::to_string(midiNote)
                        + " at " + std::to_string(cents)
                        + " cents: expected="
                        + std::to_string(frame.pitchClassWeights[expected])
                        + ", wrong=" + std::to_string(strongestWrong));
        }
    }

    const auto centred = analyseSyntheticChord(
        48000.0, { 60, 64, 67, 0 }, 3);
    for (const auto placement : {
             AnalyzerStereoPlacement::leftOnly,
             AnalyzerStereoPlacement::rightOnly,
             AnalyzerStereoPlacement::antiPhase
         })
    {
        const auto placed = analyseSyntheticChord(
            48000.0, { 60, 64, 67, 0 }, 3, placement);
        require(chromaCosine(centred.pitchClassWeights,
                             placed.pitchClassWeights) >= 0.98f,
                "Harmonic Analyzer depends on stereo placement or polarity");
        require(std::abs(centred.confidence - placed.confidence) <= 0.08f,
                "Harmonic Analyzer confidence depends on stereo placement");
    }

    std::array<HarmonicAnalysisFrame, 4> rateFrames {};
    constexpr std::array<double, 4> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0
    };
    for (std::size_t index = 0; index < sampleRates.size(); ++index)
        rateFrames[index] = analyseSyntheticChord(
            sampleRates[index], { 57, 60, 64, 0 }, 3);
    auto minimumRateCosine = 1.0f;
    auto minimumRateConfidence = 1.0f;
    auto maximumRateConfidence = 0.0f;
    for (const auto& frame : rateFrames)
    {
        minimumRateCosine = std::min(
            minimumRateCosine,
            chromaCosine(rateFrames[1].pitchClassWeights,
                         frame.pitchClassWeights));
        minimumRateConfidence = std::min(minimumRateConfidence,
                                         frame.confidence);
        maximumRateConfidence = std::max(maximumRateConfidence,
                                         frame.confidence);
    }
    require(minimumRateCosine >= 0.95f,
            "Harmonic Analyzer chroma changes with sample rate");
    require(maximumRateConfidence - minimumRateConfidence <= 0.10f,
            "Harmonic Analyzer confidence changes with sample rate");

    std::cout << "[METRIC] Harmonic Analyzer min winner ratio="
              << minimumWinnerRatio
              << ", single confidence=" << minimumSingleConfidence
              << ".." << maximumSingleConfidence
              << ", sample-rate cosine=" << minimumRateCosine << '\n';
}

void testHarmonicAnalyzerChordsRejectionAndDropout()
{
    auto minimumChordRecall = 3;
    auto minimumChordConfidence = 1.0f;
    auto minimumChordContrast = std::numeric_limits<float>::max();
    for (const auto minor : { false, true })
    {
        for (auto root = 0; root < 12; ++root)
        {
            const auto third = root + (minor ? 3 : 4);
            const auto fifth = root + 7;
            const auto frame = analyseSyntheticChord(
                48000.0, { 48 + root, 48 + third, 48 + fifth, 0 }, 3);
            std::array<bool, 12> targets {};
            targets[static_cast<std::size_t>(root % 12)] = true;
            targets[static_cast<std::size_t>(third % 12)] = true;
            targets[static_cast<std::size_t>(fifth % 12)] = true;

            auto recall = 0;
            auto targetSum = 0.0f;
            auto offTargetSum = 0.0f;
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                if (targets[index])
                {
                    targetSum += frame.pitchClassWeights[index];
                    if (frame.pitchClassWeights[index] >= 0.35f)
                        ++recall;
                }
                else
                {
                    offTargetSum += frame.pitchClassWeights[index];
                }
            }
            const auto contrast = (targetSum / 3.0f)
                                / std::max(offTargetSum / 9.0f, 1.0e-4f);
            minimumChordRecall = std::min(minimumChordRecall, recall);
            minimumChordConfidence = std::min(minimumChordConfidence,
                                              frame.confidence);
            minimumChordContrast = std::min(minimumChordContrast, contrast);
            require(recall >= 2 && contrast >= 1.8f,
                    "Harmonic Analyzer lost a major/minor chord");
            require(frame.confidence >= 0.20f,
                    "Harmonic Analyzer chord confidence is too low at root "
                        + std::to_string(root)
                        + (minor ? " minor: " : " major: ")
                        + std::to_string(frame.confidence)
                        + ", activity=" + std::to_string(frame.activity)
                        + ", transient=" + std::to_string(
                            frame.transientReliability));
        }
    }

    constexpr std::array extendedChords {
        std::array { 48, 50, 55, 0 },  // C sus2
        std::array { 48, 53, 55, 0 },  // C sus4
        std::array { 48, 52, 55, 58 }, // C7
        std::array { 48, 52, 55, 59 }, // Cmaj7
        std::array { 48, 51, 55, 58 }  // Cm7
    };
    for (const auto& notes : extendedChords)
    {
        const auto noteCount = notes[3] == 0 ? 3 : 4;
        const auto frame = analyseSyntheticChord(
            48000.0, notes, noteCount);
        auto recalled = 0;
        for (auto note = 0; note < noteCount; ++note)
        {
            const auto pitchClass = static_cast<std::size_t>(
                notes[static_cast<std::size_t>(note)] % 12);
            if (frame.pitchClassWeights[pitchClass] >= 0.25f)
                ++recalled;
        }
        require(recalled >= noteCount - 1 && frame.confidence >= 0.15f,
                "Harmonic Analyzer rejects sus/seventh MIDI "
                    + std::to_string(notes[0]) + "/"
                    + std::to_string(notes[1]) + "/"
                    + std::to_string(notes[2]) + "/"
                    + std::to_string(notes[3])
                    + ": recall=" + std::to_string(recalled)
                    + ", confidence=" + std::to_string(frame.confidence));
    }

    HarmonicAnalyzer noiseAnalyzer;
    noiseAnalyzer.prepare(48000.0);
    std::uint32_t noiseState = 0x592ac17du;
    auto noiseMaximumConfidence = 0.0f;
    for (auto sample = 0; sample < 144000; ++sample)
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        const auto left = 0.18f
            * static_cast<float>(static_cast<std::int32_t>(noiseState))
            / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        noiseState = noiseState * 1664525u + 1013904223u;
        const auto right = 0.18f
            * static_cast<float>(static_cast<std::int32_t>(noiseState))
            / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        if (noiseAnalyzer.processSample(left, right) && sample > 48000)
            noiseMaximumConfidence = std::max(
                noiseMaximumConfidence,
                noiseAnalyzer.getFrame().confidence);
    }
    require(noiseMaximumConfidence <= 0.18f,
            "Harmonic Analyzer is confidently detecting white noise");

    HarmonicAnalyzer kickAnalyzer;
    kickAnalyzer.prepare(48000.0);
    constexpr auto samplesPerBeat = 48000.0 * 60.0 / 190.0;
    auto kickMaximumConfidence = 0.0f;
    for (auto sample = 0; sample < 192000; ++sample)
    {
        const auto beatTime = std::fmod(
            static_cast<double>(sample), samplesPerBeat) / 48000.0;
        const auto envelope = beatTime < 0.19
            ? std::exp(-beatTime * 25.0)
            : 0.0;
        const auto phase = 2.0 * 3.14159265358979323846
            * (44.0 * beatTime + 55.0 * (1.0 - std::exp(-beatTime * 32.0)) / 32.0);
        const auto kick = static_cast<float>(0.82 * envelope * std::sin(phase));
        if (kickAnalyzer.processSample(kick, kick) && sample > 48000)
            kickMaximumConfidence = std::max(
                kickMaximumConfidence,
                kickAnalyzer.getFrame().confidence);
    }
    require(kickMaximumConfidence <= 0.25f,
            "Harmonic Analyzer mistakes a 190 BPM kick for harmony: "
                + std::to_string(kickMaximumConfidence));

    HarmonicAnalyzer dropout;
    dropout.prepare(48000.0);
    for (auto sample = 0; sample < 96000; ++sample)
    {
        const auto time = static_cast<double>(sample) / 48000.0;
        const auto signal = 0.055f * static_cast<float>(
            std::sin(2.0 * 3.14159265358979323846 * midiFrequency(60) * time)
            + std::sin(2.0 * 3.14159265358979323846 * midiFrequency(64) * time)
            + std::sin(2.0 * 3.14159265358979323846 * midiFrequency(67) * time));
        static_cast<void>(dropout.processSample(signal, signal));
    }
    const auto activeConfidence = dropout.getFrame().confidence;
    for (auto sample = 0; sample < 1920; ++sample)
        static_cast<void>(dropout.processSample(0.0f, 0.0f));
    const auto shortDropoutConfidence = dropout.getFrame().confidence;
    for (auto sample = 1920; sample < 36000; ++sample)
        static_cast<void>(dropout.processSample(0.0f, 0.0f));
    const auto mediumDropoutConfidence = dropout.getFrame().confidence;
    for (auto sample = 36000; sample < 72000; ++sample)
        static_cast<void>(dropout.processSample(0.0f, 0.0f));
    const auto silentFrame = dropout.getFrame();
    const auto maximumSilentWeight = *std::max_element(
        silentFrame.pitchClassWeights.begin(),
        silentFrame.pitchClassWeights.end());
    require(activeConfidence >= 0.20f
                && shortDropoutConfidence >= activeConfidence * 0.65f
                && mediumDropoutConfidence <= 0.08f
                && silentFrame.confidence <= 0.01f
                && maximumSilentWeight <= 1.0e-6f,
            "Harmonic Analyzer confidence does not release in silence: active="
                + std::to_string(activeConfidence)
                + ", 40ms=" + std::to_string(shortDropoutConfidence)
                + ", 750ms=" + std::to_string(mediumDropoutConfidence)
                + ", silent=" + std::to_string(silentFrame.confidence)
                + ", stale weight=" + std::to_string(maximumSilentWeight));

    for (auto sample = 0; sample < 48000; ++sample)
    {
        const auto invalid = sample == 0
            ? std::numeric_limits<float>::quiet_NaN()
            : sample == 1 ? std::numeric_limits<float>::infinity()
                          : 0.0f;
        static_cast<void>(dropout.processSample(invalid, -invalid));
    }
    const auto recovered = dropout.getFrame();
    require(std::isfinite(recovered.confidence)
                && recovered.confidence >= 0.0f
                && recovered.confidence <= 1.0f,
            "Harmonic Analyzer did not recover from NaN/Inf");
    for (const auto weight : recovered.pitchClassWeights)
        require(std::isfinite(weight) && weight >= 0.0f && weight <= 1.0f,
                "Harmonic Analyzer recovered with an invalid pitch weight");

    std::cout << "[METRIC] Harmonic Analyzer chord recall="
              << minimumChordRecall
              << ", contrast=" << minimumChordContrast
              << ", confidence>=" << minimumChordConfidence
              << ", noise confidence=" << noiseMaximumConfidence
              << ", kick confidence=" << kickMaximumConfidence << '\n';
}

void testHarmonicAnalyzerProgressionAndFdnIntegration()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto chordSamples = 72000;
    HarmonicAnalyzer progression;
    progression.prepare(sampleRate);
    HarmonicAnalyzer::PitchClassWeights previousWeights {};
    auto firstNewDominanceSample = -1;
    auto maximumHopDelta = 0.0f;
    for (auto sample = 0; sample < chordSamples * 2; ++sample)
    {
        const auto secondChord = sample >= chordSamples;
        const std::array<int, 3> notes = secondChord
            ? std::array<int, 3> { 66, 70, 73 } // F# major
            : std::array<int, 3> { 60, 64, 67 }; // C major
        const auto time = static_cast<double>(sample) / sampleRate;
        auto left = 0.0f;
        auto right = 0.0f;
        for (std::size_t voice = 0; voice < notes.size(); ++voice)
        {
            const auto phase = 2.0 * 3.14159265358979323846
                             * midiFrequency(notes[voice]) * time
                             + 0.29 * static_cast<double>(voice);
            const auto tone = static_cast<float>(
                std::sin(phase) + 0.24 * std::sin(2.0 * phase + 0.21));
            left += 0.035f * std::array { 1.0f, 0.72f, 0.43f }[voice] * tone;
            right += 0.035f * std::array { 0.42f, 0.74f, 1.0f }[voice] * tone;
        }

        if (!progression.processSample(left, right))
            continue;

        const auto& frame = progression.getFrame();
        for (std::size_t pitchClass = 0;
             sample > static_cast<int>(sampleRate * 0.5)
                 && pitchClass < frame.pitchClassWeights.size();
             ++pitchClass)
        {
            maximumHopDelta = std::max(
                maximumHopDelta,
                std::abs(frame.pitchClassWeights[pitchClass]
                         - previousWeights[pitchClass]));
        }
        previousWeights = frame.pitchClassWeights;
        if (secondChord && firstNewDominanceSample < 0)
        {
            const auto oldChord = frame.pitchClassWeights[0]
                                + frame.pitchClassWeights[4]
                                + frame.pitchClassWeights[7];
            const auto newChord = frame.pitchClassWeights[6]
                                + frame.pitchClassWeights[10]
                                + frame.pitchClassWeights[1];
            if (newChord > oldChord)
                firstNewDominanceSample = sample;
        }
    }

    const auto acquisitionSeconds = firstNewDominanceSample >= chordSamples
        ? static_cast<double>(firstNewDominanceSample - chordSamples) / sampleRate
        : std::numeric_limits<double>::infinity();
    const auto& settledFrame = progression.getFrame();
    require(acquisitionSeconds <= 0.40,
            "Harmonic Analyzer follows a chord change too slowly");
    require(settledFrame.pitchClassWeights[6] >= 0.70f
                && settledFrame.pitchClassWeights[10] >= 0.35f
                && settledFrame.pitchClassWeights[1] >= 0.35f
                && settledFrame.confidence >= 0.50f,
            "Harmonic Analyzer did not settle on the new chord");
    require(maximumHopDelta <= 0.45f,
            "Harmonic Analyzer pitch map changes too abruptly");

    constexpr auto sampleCount = 144000;
    std::vector<float> sourceLeft(static_cast<std::size_t>(sampleCount), 0.0f);
    std::vector<float> sourceRight(static_cast<std::size_t>(sampleCount), 0.0f);
    for (auto sample = 0; sample < 96000; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        const auto release = sample < 93600
            ? 1.0f
            : static_cast<float>(96000 - sample) / 2400.0f;
        const auto c = std::sin(2.0 * 3.14159265358979323846
                              * midiFrequency(60) * time);
        const auto e = std::sin(2.0 * 3.14159265358979323846
                              * midiFrequency(64) * time + 0.31);
        const auto g = std::sin(2.0 * 3.14159265358979323846
                              * midiFrequency(67) * time + 0.57);
        sourceLeft[static_cast<std::size_t>(sample)]
            = release * static_cast<float>(0.045 * (c + 0.72 * e + 0.41 * g));
        sourceRight[static_cast<std::size_t>(sample)]
            = release * static_cast<float>(0.045 * (0.38 * c + 0.76 * e + g));
    }

    ReverbParameters activeParameters;
    activeParameters.mix = 1.0f;
    activeParameters.preDelayMs = 0.0f;
    activeParameters.harmony = 1.0f;
    activeParameters.autoHarmony = true;
    auto bypassAutoParameters = activeParameters;
    bypassAutoParameters.harmony = 0.0f;
    auto bypassManualParameters = bypassAutoParameters;
    bypassManualParameters.autoHarmony = false;

    auto singleLeft = sourceLeft;
    auto singleRight = sourceRight;
    auto blockedLeft = sourceLeft;
    auto blockedRight = sourceRight;
    auto bypassAutoLeft = sourceLeft;
    auto bypassAutoRight = sourceRight;
    auto bypassManualLeft = sourceLeft;
    auto bypassManualRight = sourceRight;
    FDNReverb single;
    FDNReverb blocked;
    FDNReverb bypassAuto;
    FDNReverb bypassManual;
    single.setParameters(activeParameters);
    blocked.setParameters(activeParameters);
    bypassAuto.setParameters(bypassAutoParameters);
    bypassManual.setParameters(bypassManualParameters);
    single.prepare(sampleRate, 1);
    blocked.prepare(sampleRate, 257);
    bypassAuto.prepare(sampleRate, 127);
    bypassManual.prepare(sampleRate, 127);

    for (auto sample = 0; sample < sampleCount; ++sample)
        single.processSample(singleLeft[static_cast<std::size_t>(sample)],
                             singleRight[static_cast<std::size_t>(sample)]);
    for (auto offset = 0; offset < sampleCount; offset += 257)
    {
        const auto blockSize = std::min(257, sampleCount - offset);
        blocked.process(blockedLeft.data() + offset,
                        blockedRight.data() + offset, blockSize);
    }
    bypassAuto.process(bypassAutoLeft.data(), bypassAutoRight.data(), sampleCount);
    bypassManual.process(bypassManualLeft.data(), bypassManualRight.data(), sampleCount);

    auto referenceEnergy = 0.0;
    auto differenceEnergy = 0.0;
    auto peak = 0.0f;
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        require(std::bit_cast<std::uint32_t>(singleLeft[index])
                    == std::bit_cast<std::uint32_t>(blockedLeft[index])
                    && std::bit_cast<std::uint32_t>(singleRight[index])
                        == std::bit_cast<std::uint32_t>(blockedRight[index]),
                "Auto Harmony depends on host block segmentation");
        require(std::bit_cast<std::uint32_t>(bypassAutoLeft[index])
                    == std::bit_cast<std::uint32_t>(bypassManualLeft[index])
                    && std::bit_cast<std::uint32_t>(bypassAutoRight[index])
                        == std::bit_cast<std::uint32_t>(bypassManualRight[index]),
                "Auto Harmony changed the audio at zero Amount");
        require(std::isfinite(singleLeft[index]) && std::isfinite(singleRight[index]),
                "Auto Harmony FDN integration produced NaN/Inf");
        const auto leftDifference = static_cast<double>(
            singleLeft[index] - bypassAutoLeft[index]);
        const auto rightDifference = static_cast<double>(
            singleRight[index] - bypassAutoRight[index]);
        referenceEnergy += static_cast<double>(bypassAutoLeft[index])
                         * bypassAutoLeft[index]
                         + static_cast<double>(bypassAutoRight[index])
                         * bypassAutoRight[index];
        differenceEnergy += leftDifference * leftDifference
                          + rightDifference * rightDifference;
        peak = std::max({ peak, std::abs(singleLeft[index]),
                         std::abs(singleRight[index]) });
    }
    const auto normalisedDifference = std::sqrt(
        differenceEnergy / std::max(referenceEnergy, 1.0e-20));
    require(normalisedDifference >= 0.005 && normalisedDifference <= 0.50,
            "Auto Harmony FDN contribution is inaudible or excessive");
    require(peak < 4.0f, "Auto Harmony FDN integration exceeded its safety range");

    std::cout << "[METRIC] Harmonic Analyzer chord acquisition="
              << acquisitionSeconds * 1000.0 << " ms, max hop delta="
              << maximumHopDelta << ", Auto FDN NRMS="
              << normalisedDifference << '\n';
}

void testHarmonicTailIdentityStereoAndReset()
{
    constexpr auto sampleRate = 48000.0;
    const auto cMajor = harmonyWeights({ 0, 4, 7 });
    HarmonicTail bypass;
    bypass.prepare(sampleRate, cMajor, 1.0f);

    std::uint32_t noiseState = 0x71c3a5d9u;
    for (auto sample = 0; sample < 24000; ++sample)
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        const auto left = 0.25f * static_cast<float>(static_cast<std::int32_t>(noiseState))
                        / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        noiseState = noiseState * 1664525u + 1013904223u;
        const auto right = 0.25f * static_cast<float>(static_cast<std::int32_t>(noiseState))
                         / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        const auto output = bypass.process(left, right, 0.0f, 0.0f);
        require(std::bit_cast<std::uint32_t>(output.left)
                    == std::bit_cast<std::uint32_t>(left)
                    && std::bit_cast<std::uint32_t>(output.right)
                        == std::bit_cast<std::uint32_t>(right),
                "Harmonic Tail zero amount is not bit-exact");
    }

    HarmonicTail first;
    HarmonicTail repeat;
    first.prepare(sampleRate, cMajor, 1.0f);
    repeat.prepare(sampleRate, cMajor, 1.0f);
    auto oppositeChannelPeak = 0.0f;
    auto activePeak = 0.0f;
    for (auto sample = 0; sample < 96000; ++sample)
    {
        const auto input = 0.12f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(60) * sample / sampleRate));
        const auto output = first.process(input, 0.0f, 1.0f, 0.0f);
        const auto repeated = repeat.process(input, 0.0f, 1.0f, 0.0f);
        require(std::bit_cast<std::uint32_t>(output.left)
                    == std::bit_cast<std::uint32_t>(repeated.left)
                    && std::bit_cast<std::uint32_t>(output.right)
                        == std::bit_cast<std::uint32_t>(repeated.right),
                "Harmonic Tail render is not deterministic");
        require(std::isfinite(output.left) && std::isfinite(output.right),
                "Harmonic Tail produced NaN/Inf");
        oppositeChannelPeak = std::max(oppositeChannelPeak, std::abs(output.right));
        activePeak = std::max(activePeak, std::abs(output.left));
    }
    require(oppositeChannelPeak == 0.0f,
            "Harmonic Tail cross-fed a hard-left signal into the right channel");
    require(activePeak < 4.0f, "Harmonic Tail exceeded its safety range");

    // The modal bank uses a tighter safety clamp than the FDN projection.
    // Enabling an infinitesimal amount must not substitute that clamp for the
    // unprocessed wet base and create an abrupt level step.
    const auto loudBypass = bypass.process(5.0f, -5.0f, 0.0f, 0.0f);
    const auto barelyActive = bypass.process(5.0f, -5.0f, 1.0e-4f, 0.0f);
    require(std::bit_cast<std::uint32_t>(loudBypass.left)
                == std::bit_cast<std::uint32_t>(5.0f)
                && std::bit_cast<std::uint32_t>(loudBypass.right)
                    == std::bit_cast<std::uint32_t>(-5.0f),
            "Harmonic Tail loud bypass changed the wet base");
    require(std::abs(barelyActive.left - loudBypass.left) < 1.0e-5f
                && std::abs(barelyActive.right - loudBypass.right) < 1.0e-5f,
            "Harmonic Tail jumps when Amount leaves zero");

    for (auto sample = 0; sample < 4096; ++sample)
    {
        const auto dirty = 0.31f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(61) * sample / sampleRate));
        static_cast<void>(first.process(dirty, 0.23f * dirty, 1.0f, 0.0f));
    }
    first.reset();
    repeat.reset();
    for (auto sample = 0; sample < 12000; ++sample)
    {
        const auto input = sample == 0 ? 0.5f : 0.0f;
        const auto output = first.process(input, -0.37f * input, 1.0f, 0.0f);
        const auto repeated = repeat.process(input, -0.37f * input, 1.0f, 0.0f);
        require(std::bit_cast<std::uint32_t>(output.left)
                    == std::bit_cast<std::uint32_t>(repeated.left)
                    && std::bit_cast<std::uint32_t>(output.right)
                        == std::bit_cast<std::uint32_t>(repeated.right),
                "Harmonic Tail reset is not deterministic");
    }

    const auto invalid = first.process(std::numeric_limits<float>::quiet_NaN(),
                                       std::numeric_limits<float>::infinity(),
                                       1.0f, 0.0f);
    require(std::isfinite(invalid.left) && std::isfinite(invalid.right),
            "Harmonic Tail did not recover from NaN/Inf");
}

void testHarmonicTailPitchFocusAndSampleRates()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<int, 3> selectedNotes { 48, 60, 72 }; // C3/C4/C5
    constexpr std::array<int, 3> rejectedNotes { 54, 66, 78 }; // F#3/F#4/F#5
    const auto cOnly = harmonyWeights({ 0 });
    std::array<double, sampleRates.size()> contrastsDb {};
    std::array<double, sampleRates.size()> selectedLevelsDb {};

    const auto effectEnergy = [&] (double sampleRate, int midiNote)
    {
        HarmonicTail tail;
        tail.prepare(sampleRate, cOnly, 1.0f);
        const auto totalSamples = static_cast<int>(sampleRate * 2.0);
        const auto analysisStart = static_cast<int>(sampleRate);
        const auto frequency = midiFrequency(midiNote);
        auto inputEnergy = 0.0;
        auto deltaEnergy = 0.0;
        auto peak = 0.0f;
        for (auto sample = 0; sample < totalSamples; ++sample)
        {
            const auto input = 0.10f * static_cast<float>(
                std::sin(2.0 * 3.14159265358979323846
                         * frequency * static_cast<double>(sample) / sampleRate));
            const auto output = tail.process(input, input, 1.0f, 0.0f);
            require(std::isfinite(output.left) && std::isfinite(output.right),
                    "Harmonic Tail pitch test produced NaN/Inf");
            peak = std::max({ peak, std::abs(output.left), std::abs(output.right) });
            if (sample >= analysisStart)
            {
                const auto delta = static_cast<double>(output.left - input);
                inputEnergy += static_cast<double>(input) * input;
                deltaEnergy += delta * delta;
            }
        }
        require(peak < 4.0f, "Harmonic Tail pitch test exceeded its safety range");
        return std::pair { deltaEnergy, inputEnergy };
    };

    for (std::size_t rateIndex = 0; rateIndex < sampleRates.size(); ++rateIndex)
    {
        auto selectedEffect = 0.0;
        auto selectedInput = 0.0;
        auto rejectedEffect = 0.0;
        auto rejectedInput = 0.0;
        for (const auto note : selectedNotes)
        {
            const auto [effect, input] = effectEnergy(sampleRates[rateIndex], note);
            selectedEffect += effect;
            selectedInput += input;
        }
        for (const auto note : rejectedNotes)
        {
            const auto [effect, input] = effectEnergy(sampleRates[rateIndex], note);
            rejectedEffect += effect;
            rejectedInput += input;
        }

        selectedLevelsDb[rateIndex] = 10.0 * std::log10(
            std::max(selectedEffect / selectedInput, 1.0e-20));
        const auto rejectedLevelDb = 10.0 * std::log10(
            std::max(rejectedEffect / rejectedInput, 1.0e-20));
        contrastsDb[rateIndex] = selectedLevelsDb[rateIndex] - rejectedLevelDb;
        require(selectedLevelsDb[rateIndex] >= -18.0,
                "Harmonic Tail selected pitch is too weak");
        require(contrastsDb[rateIndex] >= 18.0,
                "Harmonic Tail pitch-class focus is too broad");
    }

    const auto [minimumContrast, maximumContrast] = std::minmax_element(
        contrastsDb.begin(), contrastsDb.end());
    const auto [minimumSelected, maximumSelected] = std::minmax_element(
        selectedLevelsDb.begin(), selectedLevelsDb.end());
    require(*maximumContrast - *minimumContrast <= 4.0,
            "Harmonic Tail pitch contrast changes excessively with sample rate");
    require(*maximumSelected - *minimumSelected <= 1.0,
            "Harmonic Tail selected-pitch gain changes with sample rate");

    const auto [subEffect, subInput] = effectEnergy(48000.0, 33); // A1, 55 Hz
    const auto subEffectDb = 10.0 * std::log10(std::max(subEffect / subInput, 1.0e-20));
    require(subEffectDb <= -25.0,
            "Harmonic Tail adds excessive sub-bass energy");

    HarmonicTail dense;
    HarmonicTail::PitchClassWeights chromaticField {};
    chromaticField.fill(1.0f);
    dense.prepare(48000.0, chromaticField, 1.0f);
    std::uint32_t denseNoiseState = 0x93e7b51du;
    auto denseInputEnergy = 0.0;
    auto denseEffectEnergy = 0.0;
    auto densePeak = 0.0f;
    for (auto sample = 0; sample < 96000; ++sample)
    {
        denseNoiseState = denseNoiseState * 1664525u + 1013904223u;
        const auto input = 0.12f
            * static_cast<float>(static_cast<std::int32_t>(denseNoiseState))
            / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        const auto output = dense.process(input, -0.71f * input, 1.0f, 0.0f);
        require(std::isfinite(output.left) && std::isfinite(output.right),
                "Harmonic Tail dense map produced NaN/Inf");
        densePeak = std::max({ densePeak, std::abs(output.left),
                               std::abs(output.right) });
        if (sample >= 48000)
        {
            const auto delta = static_cast<double>(output.left - input);
            denseInputEnergy += static_cast<double>(input) * input;
            denseEffectEnergy += delta * delta;
        }
    }
    const auto denseEffectRatio = std::sqrt(
        denseEffectEnergy / std::max(denseInputEnergy, 1.0e-20));
    require(densePeak < 4.0f && denseEffectRatio < 0.75,
            "Harmonic Tail dense-map normalisation is excessive");

    std::cout << "[METRIC] Harmonic Tail selected level dB="
              << selectedLevelsDb[1]
              << ", pitch contrast dB=" << contrastsDb[1]
              << ", sub effect dB=" << subEffectDb
              << ", dense-map NRMS=" << denseEffectRatio << '\n';
}

void testHarmonicTailAutomationFreezeAndStability()
{
    constexpr auto sampleRate = 48000.0;
    const auto cMajor = harmonyWeights({ 0, 4, 7 });
    const auto fSharpMajor = harmonyWeights({ 1, 6, 10 });
    HarmonicTail held;
    HarmonicTail control;
    held.prepare(sampleRate, cMajor, 1.0f);
    control.prepare(sampleRate, cMajor, 1.0f);

    for (auto sample = 0; sample < 48000; ++sample)
    {
        const auto input = 0.08f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(60) * sample / sampleRate));
        static_cast<void>(held.process(input, -0.31f * input, 1.0f, 0.0f));
        static_cast<void>(control.process(input, -0.31f * input, 1.0f, 0.0f));
    }

    held.setPitchClassWeights(fSharpMajor, 1.0f);
    for (auto sample = 0; sample < 48000; ++sample)
    {
        const auto input = 0.08f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(60) * sample / sampleRate));
        const auto frozen = held.process(input, -0.31f * input, 1.0f, 1.0f);
        const auto reference = control.process(input, -0.31f * input, 1.0f, 1.0f);
        require(std::bit_cast<std::uint32_t>(frozen.left)
                    == std::bit_cast<std::uint32_t>(reference.left)
                    && std::bit_cast<std::uint32_t>(frozen.right)
                        == std::bit_cast<std::uint32_t>(reference.right),
                "Harmonic Tail changed its harmonic field during Freeze");
    }

    auto firstUnfrozenDelta = 0.0f;
    auto peak = 0.0f;
    auto lateReferenceEnergy = 0.0;
    auto lateDifferenceEnergy = 0.0;
    for (auto sample = 0; sample < 192000; ++sample)
    {
        const auto envelope = sample < 144000 ? 1.0f : 0.0f;
        const auto input = envelope * 0.08f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(60) * sample / sampleRate));
        const auto changed = held.process(input, -0.31f * input, 1.0f, 0.0f);
        const auto reference = control.process(input, -0.31f * input, 1.0f, 0.0f);
        if (sample == 0)
            firstUnfrozenDelta = std::max(std::abs(changed.left - reference.left),
                                          std::abs(changed.right - reference.right));
        require(std::isfinite(changed.left) && std::isfinite(changed.right),
                "Harmonic Tail automation produced NaN/Inf");
        peak = std::max({ peak, std::abs(changed.left), std::abs(changed.right) });
        if (sample >= 24000 && sample < 120000)
        {
            const auto leftDifference = static_cast<double>(changed.left
                                                            - reference.left);
            const auto rightDifference = static_cast<double>(changed.right
                                                             - reference.right);
            lateReferenceEnergy += static_cast<double>(reference.left)
                                 * reference.left
                                 + static_cast<double>(reference.right)
                                 * reference.right;
            lateDifferenceEnergy += leftDifference * leftDifference
                                  + rightDifference * rightDifference;
        }
    }
    require(firstUnfrozenDelta <= 0.005f,
            "Harmonic Tail harmonic-field morph starts with a discontinuity");
    require(std::sqrt(lateDifferenceEnergy
                      / std::max(lateReferenceEnergy, 1.0e-20)) >= 0.02,
            "Harmonic Tail did not apply the new map after Freeze");
    require(peak < 4.0f, "Harmonic Tail automation exceeded its safety range");

    // Excite the resonators while the pitch map fades completely away, leave
    // them inactive, then restore the map over silence. No energy from the old
    // chord may reappear when confidence returns.
    HarmonicTail dropout;
    dropout.prepare(sampleRate, cMajor, 1.0f);
    for (auto sample = 0; sample < 48000; ++sample)
    {
        const auto input = 0.75f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(60) * sample / sampleRate));
        static_cast<void>(dropout.process(input, input, 1.0f, 0.0f));
    }
    dropout.setPitchClassWeights({}, 1.0f);
    for (auto sample = 0; sample < 216000; ++sample)
    {
        const auto input = 0.75f * std::sin(
            static_cast<float>(2.0 * 3.14159265358979323846
                               * midiFrequency(60) * sample / sampleRate));
        static_cast<void>(dropout.process(input, input, 1.0f, 0.0f));
    }
    for (auto sample = 0; sample < 12000; ++sample)
        static_cast<void>(dropout.process(0.0f, 0.0f, 1.0f, 0.0f));

    dropout.setPitchClassWeights(cMajor, 1.0f);
    auto reactivationPeak = 0.0f;
    for (auto sample = 0; sample < 24000; ++sample)
    {
        const auto output = dropout.process(0.0f, 0.0f, 1.0f, 0.0f);
        reactivationPeak = std::max({ reactivationPeak,
                                      std::abs(output.left),
                                      std::abs(output.right) });
    }
    require(reactivationPeak <= 1.0e-8f,
            "Harmonic Tail revived a stale chord after confidence dropout");

    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        HarmonicTail tail;
        tail.prepare(rate, cMajor, 1.0f);
        const auto samples = static_cast<int>(rate * 2.0);
        auto ratePeak = 0.0f;
        auto earlySilenceEnergy = 0.0;
        auto lateSilenceEnergy = 0.0;
        for (auto sample = 0; sample < samples; ++sample)
        {
            const auto input = sample < static_cast<int>(rate * 0.5)
                ? 0.06f * std::sin(static_cast<float>(
                    2.0 * 3.14159265358979323846
                    * midiFrequency(60) * sample / rate))
                : 0.0f;
            const auto output = tail.process(input, -0.27f * input, 1.0f, 0.0f);
            require(std::isfinite(output.left) && std::isfinite(output.right),
                    "Harmonic Tail sample-rate stress produced NaN/Inf");
            ratePeak = std::max({ ratePeak, std::abs(output.left),
                                 std::abs(output.right) });
            const auto outputEnergy = static_cast<double>(output.left)
                                    * output.left
                                    + static_cast<double>(output.right)
                                    * output.right;
            if (sample >= static_cast<int>(rate * 0.5)
                && sample < static_cast<int>(rate * 0.75))
                earlySilenceEnergy += outputEnergy;
            if (sample >= static_cast<int>(rate * 1.75))
                lateSilenceEnergy += outputEnergy;
        }
        require(ratePeak < 4.0f,
                "Harmonic Tail sample-rate stress exceeded its safety range");
        require(lateSilenceEnergy <= earlySilenceEnergy * 0.01 + 1.0e-20,
                "Harmonic Tail modal energy did not decay");
    }
}

void testHarmonicTailFdnIntegrationAndBlockInvariance()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 96000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::defaultMode;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 5.0f;
    parameters.size = 1.12f;
    parameters.preDelayMs = 17.0f;
    parameters.lowCutHz = 55.0f;
    parameters.highDampingHz = 8500.0f;
    parameters.evolution = 0.45f;
    parameters.width = 1.15f;
    parameters.harmony = 1.0f;
    parameters.harmonyPitchClasses = harmonyWeights({ 0, 4, 7 });
    parameters.harmonyConfidence = 1.0f;

    std::vector<float> sourceLeft(sampleCount, 0.0f);
    std::vector<float> sourceRight(sampleCount, 0.0f);
    constexpr auto excitationSamples = 30000;
    for (auto sample = 0; sample < excitationSamples; ++sample)
    {
        const auto release = sample < excitationSamples - 2400
            ? 1.0f
            : static_cast<float>(excitationSamples - sample) / 2400.0f;
        const auto c = std::sin(2.0 * 3.14159265358979323846
                              * midiFrequency(60) * sample / sampleRate);
        const auto e = std::sin(2.0 * 3.14159265358979323846
                              * midiFrequency(64) * sample / sampleRate);
        const auto g = std::sin(2.0 * 3.14159265358979323846
                              * midiFrequency(67) * sample / sampleRate);
        sourceLeft[static_cast<std::size_t>(sample)]
            = release * static_cast<float>(0.035 * (c + 0.72 * e + 0.38 * g));
        sourceRight[static_cast<std::size_t>(sample)]
            = release * static_cast<float>(0.035 * (0.36 * c + 0.74 * e + g));
    }

    auto singleLeft = sourceLeft;
    auto singleRight = sourceRight;
    auto blockLeft = sourceLeft;
    auto blockRight = sourceRight;
    auto bypassLeft = sourceLeft;
    auto bypassRight = sourceRight;
    auto neutralLeft = sourceLeft;
    auto neutralRight = sourceRight;

    FDNReverb single;
    FDNReverb blocked;
    FDNReverb bypass;
    FDNReverb neutral;
    single.setParameters(parameters);
    blocked.setParameters(parameters);
    single.prepare(sampleRate, 1);
    blocked.prepare(sampleRate, 127);

    auto bypassParameters = parameters;
    bypassParameters.harmony = 0.0f;
    bypass.setParameters(bypassParameters);
    bypass.prepare(sampleRate, 512);

    auto neutralParameters = bypassParameters;
    neutralParameters.harmonyPitchClasses = {};
    neutralParameters.harmonyConfidence = 0.0f;
    neutral.setParameters(neutralParameters);
    neutral.prepare(sampleRate, 512);

    for (auto sample = 0; sample < sampleCount; ++sample)
        single.process(singleLeft.data() + sample, singleRight.data() + sample, 1);
    for (auto offset = 0; offset < sampleCount; offset += 127)
    {
        const auto blockSize = std::min(127, sampleCount - offset);
        blocked.process(blockLeft.data() + offset, blockRight.data() + offset, blockSize);
    }
    bypass.process(bypassLeft.data(), bypassRight.data(), sampleCount);
    neutral.process(neutralLeft.data(), neutralRight.data(), sampleCount);

    auto referenceEnergy = 0.0;
    auto differenceEnergy = 0.0;
    auto peak = 0.0f;
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto index = static_cast<std::size_t>(sample);
        require(std::abs(singleLeft[index] - blockLeft[index]) <= 1.0e-7f
                    && std::abs(singleRight[index] - blockRight[index]) <= 1.0e-7f,
                "Harmonic Tail depends on process block segmentation");
        require(std::bit_cast<std::uint32_t>(bypassLeft[index])
                    == std::bit_cast<std::uint32_t>(neutralLeft[index])
                    && std::bit_cast<std::uint32_t>(bypassRight[index])
                        == std::bit_cast<std::uint32_t>(neutralRight[index]),
                "Harmonic Tail changed the FDN at zero amount");
        require(std::isfinite(singleLeft[index]) && std::isfinite(singleRight[index]),
                "Harmonic Tail FDN integration produced NaN/Inf");

        const auto leftDifference = static_cast<double>(singleLeft[index]
                                                        - bypassLeft[index]);
        const auto rightDifference = static_cast<double>(singleRight[index]
                                                         - bypassRight[index]);
        referenceEnergy += static_cast<double>(bypassLeft[index]) * bypassLeft[index]
                         + static_cast<double>(bypassRight[index]) * bypassRight[index];
        differenceEnergy += leftDifference * leftDifference
                          + rightDifference * rightDifference;
        peak = std::max({ peak, std::abs(singleLeft[index]),
                         std::abs(singleRight[index]) });
    }

    require(referenceEnergy > 1.0e-10,
            "Harmonic Tail FDN reference render is silent");
    const auto normalisedDifference = std::sqrt(differenceEnergy / referenceEnergy);
    require(normalisedDifference >= 0.005 && normalisedDifference <= 0.50,
            "Harmonic Tail FDN contribution is inaudible or excessive");
    require(peak < 4.0f, "Harmonic Tail FDN integration exceeded its safety range");

    // Freeze must latch the current harmonic map on the exact control edge,
    // independently of the main FDN's click-free 50 ms Freeze gain ramp.
    FDNReverb frozenReference;
    FDNReverb frozenMapChange;
    frozenReference.setParameters(parameters);
    frozenMapChange.setParameters(parameters);
    frozenReference.prepare(sampleRate, 127);
    frozenMapChange.prepare(sampleRate, 127);
    for (auto sample = 0; sample < 48000; ++sample)
    {
        auto referenceLeft = sourceLeft[static_cast<std::size_t>(sample)];
        auto referenceRight = sourceRight[static_cast<std::size_t>(sample)];
        auto changedLeft = referenceLeft;
        auto changedRight = referenceRight;
        frozenReference.processSample(referenceLeft, referenceRight);
        frozenMapChange.processSample(changedLeft, changedRight);
    }

    auto frozenParameters = parameters;
    frozenParameters.freeze = true;
    frozenReference.setParameters(frozenParameters);
    frozenParameters.harmonyPitchClasses = harmonyWeights({ 1, 6, 10 });
    frozenMapChange.setParameters(frozenParameters);
    for (auto sample = 0; sample < 48000; ++sample)
    {
        auto referenceLeft = 0.0f;
        auto referenceRight = 0.0f;
        auto changedLeft = 0.0f;
        auto changedRight = 0.0f;
        frozenReference.processSample(referenceLeft, referenceRight);
        frozenMapChange.processSample(changedLeft, changedRight);
        require(std::bit_cast<std::uint32_t>(referenceLeft)
                    == std::bit_cast<std::uint32_t>(changedLeft)
                    && std::bit_cast<std::uint32_t>(referenceRight)
                        == std::bit_cast<std::uint32_t>(changedRight),
                "Harmonic map leaked through the FDN Freeze ramp");
    }

    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        auto stressParameters = parameters;
        stressParameters.mode = ReverbMode::drift;
        stressParameters.decaySeconds = 30.0f;
        stressParameters.size = 2.0f;
        stressParameters.evolution = 1.0f;
        stressParameters.highDampingHz = 20000.0f;
        FDNReverb stress;
        stress.setParameters(stressParameters);
        stress.prepare(rate, 257);
        std::uint32_t noiseState = 0x43a91f2du;
        auto stressPeak = 0.0f;
        const auto samples = static_cast<int>(rate * 3.0);
        for (auto sample = 0; sample < samples; ++sample)
        {
            if (sample == static_cast<int>(rate))
            {
                stressParameters.freeze = true;
                stress.setParameters(stressParameters);
            }
            if (sample == static_cast<int>(rate * 2.0))
            {
                stressParameters.harmonyPitchClasses = harmonyWeights({ 1, 6, 10 });
                stress.setParameters(stressParameters);
            }
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                             / static_cast<float>(
                                 std::numeric_limits<std::int32_t>::max());
            auto left = sample < static_cast<int>(rate * 0.35)
                ? (sample == 0 ? 0.8f : 0.012f * noise)
                : 0.0f;
            auto right = sample < static_cast<int>(rate * 0.35)
                ? (sample == 0 ? -0.3f : -0.009f * noise)
                : 0.0f;
            stress.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right),
                    "Harmonic Tail FDN stress produced NaN/Inf");
            stressPeak = std::max({ stressPeak, std::abs(left), std::abs(right) });
        }
        require(stressPeak < 4.0f,
                "Harmonic Tail FDN stress exceeded its safety range");
    }

    std::cout << "[METRIC] Harmonic Tail FDN NRMS="
              << normalisedDifference << ", peak=" << peak << '\n';
}

void testNoAllocationsInProcess()
{
    FDNReverb reverb;
    reverb.prepare(48000.0, 512);
    HarmonicTail harmonicTail;
    auto harmonicWeights = harmonyWeights({ 0, 4, 7 });
    harmonicTail.prepare(48000.0, harmonicWeights, 1.0f);
    ReverbParameters parameters;
    parameters.harmony = 1.0f;
    parameters.harmonyPitchClasses = harmonicWeights;
    parameters.harmonyConfidence = 1.0f;
    parameters.autoHarmony = true;
    constexpr std::array modes {
        ReverbMode::defaultMode,
        ReverbMode::bloom,
        ReverbMode::drift,
        ReverbMode::veil,
        ReverbMode::current,
        ReverbMode::fathom
    };
    auto modeIndex = std::size_t { 0 };

    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (auto sample = 0; sample < 100000; ++sample)
    {
        if (sample % 257 == 0)
        {
            modeIndex = (modeIndex + 1) % modes.size();
            parameters.mode = modes[modeIndex];
            // An even number of modes would pair each of them with one state
            // of the toggles; holding the toggles once per cycle lets every
            // mode meet both.
            if (modeIndex != 0)
            {
                parameters.evolution = 1.0f - parameters.evolution;
                parameters.ducking = 1.0f - parameters.ducking;
                parameters.size = parameters.size > 0.15f ? 0.15f : 2.0f;
                parameters.width = parameters.width > 0.0f ? 0.0f : 2.0f;
                parameters.monoSafeStereo = !parameters.monoSafeStereo;
                parameters.freeze = !parameters.freeze;
            }
            harmonicWeights = modeIndex % 2 == 0
                ? harmonyWeights({ 0, 4, 7 })
                : harmonyWeights({ 1, 6, 10 });
            parameters.harmonyPitchClasses = harmonicWeights;
            reverb.setParameters(parameters);
            harmonicTail.setPitchClassWeights(harmonicWeights, 1.0f);
        }
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        static_cast<void>(harmonicTail.process(left, right, 1.0f,
                                               parameters.freeze ? 1.0f : 0.0f));
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(allocationCount.load(std::memory_order_relaxed) == 0,
            "DSP allocated memory while processing audio");
}

template <int stressSeconds>
void runLongCharacterStress(ReverbMode mode)
{
    constexpr auto sampleRate = 44100.0;
    constexpr auto windowSeconds = 10;
    constexpr auto numWindows = stressSeconds / windowSeconds;
    static_assert(stressSeconds % windowSeconds == 0);

    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 30.0f;
    parameters.size = 2.0f;
    parameters.preDelayMs = 250.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.evolution = 1.0f;
    parameters.width = 2.0f;

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 512);

    std::uint32_t noiseState = 0x5eeda11u;
    for (auto sample = 0; sample < static_cast<int>(sampleRate * 0.5); ++sample)
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        const auto noise = static_cast<float>(static_cast<std::int32_t>(noiseState))
                         / static_cast<float>(std::numeric_limits<std::int32_t>::max());
        auto left = (sample == 0 ? 1.0f : 0.0f) + 0.015f * noise;
        auto right = (sample == 0 ? -0.5f : 0.0f) - 0.011f * noise;
        reverb.processSample(left, right);
    }

    parameters.freeze = true;
    reverb.setParameters(parameters);
    std::array<double, numWindows> windowEnergy {};
    auto peak = 0.0f;
    const auto totalSamples = static_cast<int>(sampleRate * stressSeconds);
    const auto samplesPerWindow = static_cast<int>(sampleRate * windowSeconds);
    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        auto left = 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        require(std::isfinite(left) && std::isfinite(right),
                "Long Character stress produced NaN/Inf");
        peak = std::max({ peak, std::abs(left), std::abs(right) });
        const auto window = static_cast<std::size_t>(sample / samplesPerWindow);
        windowEnergy[window] += static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
    }

    require(windowEnergy.front() > 1.0e-10, "Long Character stress tail became silent");
    for (const auto energy : windowEnergy)
        require(energy <= windowEnergy.front() * 1.5 + 1.0e-12,
                "Long Character stress found an energy-pumping LFO phase");
    require(windowEnergy.back() <= windowEnergy.front() * 1.25 + 1.0e-12,
            "Long Character stress tail grows over time");
    if (mode == ReverbMode::drift || mode == ReverbMode::veil)
    {
        const auto finalEnergyRatio = windowEnergy.back() / windowEnergy.front();
        const auto minimumRatio = mode == ReverbMode::veil ? 0.05 : 1.0e-8;
        require(windowEnergy.back() > 1.0e-16 && finalEnergyRatio >= minimumRatio,
                "Long Character Freeze tail collapsed to silence: last/first="
                    + std::to_string(finalEnergyRatio));
    }
    require(peak < 4.0f, "Long Character stress exceeded safety range");
}

[[nodiscard]] std::uint64_t appendFnv1aFloat(std::uint64_t hash, float value) noexcept
{
    constexpr std::uint64_t fnvPrime = 1099511628211ull;
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (auto byte = 0; byte < 4; ++byte)
    {
        hash ^= (bits >> (byte * 8)) & 0xffu;
        hash *= fnvPrime;
    }
    return hash;
}

[[nodiscard]] std::uint64_t renderFingerprint(ReverbMode mode, float evolution)
{
    constexpr auto sampleRate = 48000;
    constexpr auto sampleCount = sampleRate * 2;
    constexpr std::uint64_t fnvOffsetBasis = 14695981039346656037ull;

    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 5.0f;
    parameters.size = 1.15f;
    parameters.preDelayMs = 25.0f;
    parameters.lowCutHz = 60.0f;
    parameters.highDampingHz = 7000.0f;
    parameters.evolution = evolution;
    parameters.width = 1.25f;

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 512);
    auto hash = fnvOffsetBasis;
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        hash = appendFnv1aFloat(hash, left);
        hash = appendFnv1aFloat(hash, right);
    }
    return hash;
}

void testDeterministicRenderFingerprints()
{
    struct RenderCase
    {
        const char* name;
        ReverbMode mode;
        float evolution;
    };

    constexpr std::array renderCases {
        RenderCase { "Default low Evolution", ReverbMode::defaultMode, 0.0f },
        RenderCase { "Default high Evolution", ReverbMode::defaultMode, 1.0f },
        RenderCase { "Bloom low Evolution", ReverbMode::bloom, 0.0f },
        RenderCase { "Bloom high Evolution", ReverbMode::bloom, 1.0f },
        RenderCase { "Drift low Evolution", ReverbMode::drift, 0.0f },
        RenderCase { "Drift high Evolution", ReverbMode::drift, 1.0f },
        RenderCase { "Veil low Evolution", ReverbMode::veil, 0.0f },
        RenderCase { "Veil high Evolution", ReverbMode::veil, 1.0f },
        RenderCase { "Current low Evolution", ReverbMode::current, 0.0f },
        RenderCase { "Current high Evolution", ReverbMode::current, 1.0f },
        RenderCase { "Fathom low Evolution", ReverbMode::fathom, 0.0f },
        RenderCase { "Fathom high Evolution", ReverbMode::fathom, 1.0f }
    };

    std::array<std::uint64_t, renderCases.size()> fingerprints {};
    for (std::size_t index = 0; index < renderCases.size(); ++index)
    {
        const auto& renderCase = renderCases[index];
        const auto first = renderFingerprint(renderCase.mode, renderCase.evolution);
        const auto repeat = renderFingerprint(renderCase.mode, renderCase.evolution);
        require(first == repeat,
                std::string(renderCase.name) + " render fingerprint is not deterministic");
        fingerprints[index] = first;
    }

    for (std::size_t first = 0; first < fingerprints.size(); ++first)
        for (std::size_t second = first + 1; second < fingerprints.size(); ++second)
            require(fingerprints[first] != fingerprints[second],
                    "Distinct Character/Evolution endpoints produced identical renders");
}

void writeLittleEndian16(std::ofstream& stream, std::uint16_t value)
{
    const std::array<char, 2> bytes {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeLittleEndian32(std::ofstream& stream, std::uint32_t value)
{
    const std::array<char, 4> bytes {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeStereoFloatWav(const std::string& path,
                         const StereoRender& render,
                         int sampleRate)
{
    require(render.left.size() == render.right.size(),
            "Stereo WAV channels have different lengths");
    require(sampleRate > 0, "Stereo WAV sample rate is invalid");

    constexpr std::uint16_t channels = 2;
    std::vector<float> interleaved(render.left.size() * channels);
    for (std::size_t sample = 0; sample < render.left.size(); ++sample)
    {
        interleaved[sample * channels] = render.left[sample];
        interleaved[sample * channels + 1] = render.right[sample];
    }

    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "Could not open WAV output: " + path);

    constexpr std::uint32_t bytesPerSample = sizeof(float);
    const auto sampleRateValue = static_cast<std::uint32_t>(sampleRate);
    const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * bytesPerSample);
    stream.write("RIFF", 4);
    writeLittleEndian32(stream, 36u + dataBytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    writeLittleEndian32(stream, 16u);
    writeLittleEndian16(stream, 3u);
    writeLittleEndian16(stream, channels);
    writeLittleEndian32(stream, sampleRateValue);
    writeLittleEndian32(stream, sampleRateValue * channels * bytesPerSample);
    writeLittleEndian16(stream, static_cast<std::uint16_t>(channels * bytesPerSample));
    writeLittleEndian16(stream, 32u);
    stream.write("data", 4);
    writeLittleEndian32(stream, dataBytes);
    stream.write(reinterpret_cast<const char*>(interleaved.data()),
                 static_cast<std::streamsize>(dataBytes));
    require(stream.good(), "Failed while writing WAV output: " + path);
}

void renderImpulseResponse(const std::string& path, ReverbMode mode)
{
    constexpr auto sampleRate = 48000;
    constexpr auto seconds = 10;
    constexpr auto sampleCount = sampleRate * seconds;

    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 5.0f;
    parameters.size = 1.15f;
    parameters.preDelayMs = 25.0f;
    parameters.lowCutHz = 60.0f;
    parameters.highDampingHz = 7000.0f;
    parameters.evolution = 0.35f;
    parameters.width = 1.25f;

    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, 512);

    StereoRender render {
        std::vector<float>(sampleCount, 0.0f),
        std::vector<float>(sampleCount, 0.0f)
    };
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        render.left[static_cast<std::size_t>(sample)] = left;
        render.right[static_cast<std::size_t>(sample)] = right;
    }

    writeStereoFloatWav(path, render, sampleRate);
}

void renderHarmonyAb(const std::string& scene, const std::string& prefix)
{
    constexpr auto sampleRate = 48000;
    constexpr auto twoPi = 6.28318530717958647692;
    constexpr auto padChordSeconds = 2.0;
    constexpr std::array<std::array<int, 3>, 4> padNotes {{
        { 57, 60, 64 }, // A minor
        { 53, 57, 60 }, // F major
        { 60, 64, 67 }, // C major
        { 55, 59, 62 }  // G major
    }};

    const std::array padFields {
        harmonyWeights({ 9, 0, 4 }),
        harmonyWeights({ 5, 9, 0 }),
        harmonyWeights({ 0, 4, 7 }),
        harmonyWeights({ 7, 11, 2 })
    };
    const auto vocalField = harmonyWeights({ 7, 10, 2 }); // G minor
    const auto kickBassField = harmonyWeights({ 9, 0, 4 }); // A minor

    const auto isPad = scene == "pad";
    const auto isVocal = scene == "vocal";
    const auto isKickBass = scene == "kickbass190";
    require(isPad || isVocal || isKickBass,
            "Unknown Harmony A/B scene: " + scene);

    const auto contentSeconds = isPad ? padChordSeconds * padNotes.size()
                              : isVocal ? 4.0
                                        : 16.0 * 60.0 / 190.0;
    const auto totalSeconds = contentSeconds + 4.0;
    const auto sampleCount = static_cast<int>(std::ceil(totalSeconds * sampleRate));

    ReverbParameters baseParameters;
    baseParameters.mode = isPad ? ReverbMode::defaultMode : ReverbMode::drift;
    baseParameters.mix = 1.0f;
    baseParameters.decaySeconds = isPad ? 6.5f : 5.5f;
    baseParameters.size = 1.18f;
    baseParameters.preDelayMs = 18.0f;
    baseParameters.lowCutHz = isKickBass ? 35.0f : 70.0f;
    baseParameters.highDampingHz = 9500.0f;
    baseParameters.evolution = 0.48f;
    baseParameters.width = 1.20f;
    baseParameters.ducking = 0.0f;
    baseParameters.harmonyPitchClasses = isPad ? padFields.front()
                                               : isVocal ? vocalField
                                                         : kickBassField;
    baseParameters.harmonyConfidence = 1.0f;

    auto offParameters = baseParameters;
    offParameters.harmony = 0.0f;
    auto onParameters = baseParameters;
    onParameters.harmony = 1.0f;

    FDNReverb off;
    FDNReverb on;
    off.setParameters(offParameters);
    on.setParameters(onParameters);
    off.prepare(sampleRate, 512);
    on.prepare(sampleRate, 512);

    StereoRender offRender {
        std::vector<float>(static_cast<std::size_t>(sampleCount), 0.0f),
        std::vector<float>(static_cast<std::size_t>(sampleCount), 0.0f)
    };
    StereoRender onRender {
        std::vector<float>(static_cast<std::size_t>(sampleCount), 0.0f),
        std::vector<float>(static_cast<std::size_t>(sampleCount), 0.0f)
    };
    const DriftVocalSource vocal;
    auto activePadChord = std::size_t { 0 };
    auto differenceEnergy = 0.0;
    auto referenceEnergy = 0.0;

    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto time = static_cast<double>(sample) / sampleRate;
        auto dryLeft = 0.0f;
        auto dryRight = 0.0f;

        if (isPad && time < contentSeconds)
        {
            const auto chord = std::min<std::size_t>(
                static_cast<std::size_t>(time / padChordSeconds),
                padNotes.size() - 1);
            if (chord != activePadChord)
            {
                activePadChord = chord;
                offParameters.harmonyPitchClasses = padFields[chord];
                onParameters.harmonyPitchClasses = padFields[chord];
                off.setParameters(offParameters);
                on.setParameters(onParameters);
            }

            const auto chordTime = time - static_cast<double>(chord) * padChordSeconds;
            constexpr auto gateSeconds = 1.25;
            if (chordTime < gateSeconds)
            {
                const auto attack = std::min(1.0, chordTime / 0.08);
                const auto release = chordTime < gateSeconds - 0.30
                    ? 1.0
                    : (gateSeconds - chordTime) / 0.30;
                const auto envelope = attack * std::clamp(release, 0.0, 1.0);
                for (std::size_t note = 0; note < padNotes[chord].size(); ++note)
                {
                    const auto frequency = midiFrequency(padNotes[chord][note]);
                    const auto phase = twoPi * frequency * chordTime
                                     + 0.21 * static_cast<double>(note);
                    const auto tone = std::sin(phase)
                                    + 0.16 * std::sin(2.0 * phase + 0.37);
                    const auto leftGain = std::array { 1.0, 0.70, 0.42 }[note];
                    const auto rightGain = std::array { 0.44, 0.72, 1.0 }[note];
                    dryLeft += static_cast<float>(0.045 * envelope * leftGain * tone);
                    dryRight += static_cast<float>(0.045 * envelope * rightGain * tone);
                }
            }
        }
        else if (isVocal)
        {
            const auto voice = vocal.sample(sample, static_cast<int>(contentSeconds * sampleRate),
                                            sampleRate);
            dryLeft = voice;
            dryRight = 0.91f * voice;
        }
        else if (isKickBass && time < contentSeconds)
        {
            const auto source = kickBass190Sample(sample, sampleRate);
            dryLeft = source;
            dryRight = source;
        }

        auto offLeft = dryLeft;
        auto offRight = dryRight;
        auto onLeft = dryLeft;
        auto onRight = dryRight;
        off.processSample(offLeft, offRight);
        on.processSample(onLeft, onRight);

        constexpr auto dryMonitorGain = 0.30f;
        constexpr auto wetMonitorGain = 0.92f;
        const auto monitoredOffLeft = dryMonitorGain * dryLeft + wetMonitorGain * offLeft;
        const auto monitoredOffRight = dryMonitorGain * dryRight + wetMonitorGain * offRight;
        const auto monitoredOnLeft = dryMonitorGain * dryLeft + wetMonitorGain * onLeft;
        const auto monitoredOnRight = dryMonitorGain * dryRight + wetMonitorGain * onRight;
        require(std::isfinite(monitoredOffLeft) && std::isfinite(monitoredOffRight)
                    && std::isfinite(monitoredOnLeft) && std::isfinite(monitoredOnRight),
                "Harmony A/B render produced NaN/Inf");

        const auto index = static_cast<std::size_t>(sample);
        offRender.left[index] = monitoredOffLeft;
        offRender.right[index] = monitoredOffRight;
        onRender.left[index] = monitoredOnLeft;
        onRender.right[index] = monitoredOnRight;
        const auto differenceLeft = static_cast<double>(monitoredOnLeft
                                                        - monitoredOffLeft);
        const auto differenceRight = static_cast<double>(monitoredOnRight
                                                         - monitoredOffRight);
        differenceEnergy += differenceLeft * differenceLeft
                          + differenceRight * differenceRight;
        referenceEnergy += static_cast<double>(monitoredOffLeft) * monitoredOffLeft
                         + static_cast<double>(monitoredOffRight) * monitoredOffRight;
    }

    auto commonPeak = 0.0f;
    for (const auto* render : { &offRender, &onRender })
        for (std::size_t sample = 0; sample < render->left.size(); ++sample)
            commonPeak = std::max({ commonPeak,
                                    std::abs(render->left[sample]),
                                    std::abs(render->right[sample]) });
    require(commonPeak > 1.0e-6f, "Harmony A/B render is silent");
    constexpr auto targetPeak = 0.8912509381f; // -1 dBFS
    const auto commonGain = targetPeak / commonPeak;
    for (auto* render : { &offRender, &onRender })
    {
        for (auto& sample : render->left)
            sample *= commonGain;
        for (auto& sample : render->right)
            sample *= commonGain;
    }

    const auto offPath = prefix + "-" + scene + "-off.wav";
    const auto onPath = prefix + "-" + scene + "-on.wav";
    writeStereoFloatWav(offPath, offRender, sampleRate);
    writeStereoFloatWav(onPath, onRender, sampleRate);

    const auto normalisedDifference = std::sqrt(
        differenceEnergy / std::max(referenceEnergy, 1.0e-20));
    require(normalisedDifference >= 0.02 && normalisedDifference <= 0.50,
            "Harmony A/B render is identical or excessively different");
    std::cout << "[METRIC] Harmony A/B " << scene
              << ": NRMS=" << normalisedDifference
              << ", common source peak=" << commonPeak
              << ", common gain=" << commonGain << '\n'
              << "[PASS] wrote " << offPath << '\n'
              << "[PASS] wrote " << onPath << '\n';
}

// ---------------------------------------------------------------- Undertow

using amanita::dsp::HostTransport;
using amanita::dsp::UndertowLayer;

// A host for the tests of Undertow: blocks of a fixed length, a tempo, and a
// position that runs on with the frames while the transport does.
struct UndertowHost
{
    [[nodiscard]] FathomEngine::Transport at(long long frame) const noexcept
    {
        FathomEngine::Transport transport;
        transport.hasTempo = hasTempo;
        transport.bpm = tempo;
        transport.playing = playing;
        transport.quarterNotes = playing
            ? startQuarters + static_cast<double>(frame) * tempo / (60.0 * sampleRate)
            : startQuarters;
        return transport;
    }

    // What the host says in front of a frame, if a block begins there.
    void announce(FathomEngine& engine, long long frame) const noexcept
    {
        if (frame % blockFrames == 0)
            engine.setTransport(at(frame));
    }

    double sampleRate = 48000.0;
    int blockFrames = 512;
    double tempo = 120.0;
    bool hasTempo = true;
    bool playing = true;
    double startQuarters = 0.0;
};

void prepareUndertowEngine(FathomEngine& engine, const FathomEngine::Parameters& parameters,
                           double sampleRate, bool referenceArithmetic = false)
{
    engine.setLayer(FathomEngine::Layer::undertow);
    engine.setReferenceArithmetic(referenceArithmetic);
    engine.setParameters(parameters);
    engine.prepare(sampleRate);
}

// The engine's wet output for the test programme under a host, left and right
// interleaved; `firstFrame` is the host's frame of the engine's next one.
[[nodiscard]] std::vector<float> renderUndertowProgramme(FathomEngine& engine, const UndertowHost& host,
                                                         long long firstFrame, int frameCount)
{
    std::vector<float> rendered;
    rendered.reserve(2 * static_cast<std::size_t>(frameCount));
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        host.announce(engine, firstFrame + frame);
        const auto input = fathomProgramme(frame);
        const auto wet = engine.processSample(input.left, input.right);
        rendered.push_back(wet.left);
        rendered.push_back(wet.right);
    }
    return rendered;
}

void idleUndertow(FathomEngine& engine, const UndertowHost& host, long long firstFrame, int frameCount)
{
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        host.announce(engine, firstFrame + frame);
        engine.advanceIdle();
    }
}

[[nodiscard]] FathomEngine::Parameters undertowTestParameters(float macro) noexcept
{
    FathomEngine::Parameters parameters;
    parameters.decaySeconds = 1.5f;
    parameters.sizeScale = 1.2f;
    parameters.preDelaySeconds = 0.004f;
    parameters.macro = macro;
    return parameters;
}

void testUndertowEngineGoldenVectors()
{
    namespace golden = amanita::dsp::undertowgolden;
    // The model is the campaign's of the reference's Abyss mode with the clock
    // read from the host, in the reference's arithmetic. The engine stores the
    // signals of its lines in single precision and sits about 147 dB under the
    // model on whole renders, as it does without the layer; the model itself
    // meets the reference at -56 to -78 dB above Macro 0.
    constexpr auto nullLimitDb = -130.0;
    auto worstNullDb = -400.0;

    for (const auto& vector : golden::vectors)
    {
        auto frameCount = 0;
        for (const auto& excerpt : vector.excerpts)
            frameCount = std::max(frameCount,
                                  excerpt.firstFrame + static_cast<int>(excerpt.model.size() / 2));

        FathomEngine::Parameters parameters;
        parameters.decaySeconds = golden::decaySeconds;
        parameters.sizeScale = golden::sizeScale;
        parameters.macro = vector.macro;
        FathomEngine::ClockOrigins origins;
        origins.firstFrame = vector.firstFrame;
        origins.oscillators = vector.origin;
        origins.phasors = vector.origin;
        origins.freeRun = vector.origin;
        FathomEngine engine;
        engine.setClockOriginsForTesting(origins);
        prepareUndertowEngine(engine, parameters, static_cast<double>(vector.hostRate), true);

        const auto rate = static_cast<double>(vector.hostRate);
        const auto programmeFrames = static_cast<int>(vector.programme.size() / 2);
        std::vector<float> rendered;
        rendered.reserve(2 * static_cast<std::size_t>(frameCount));
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            if (frame % golden::hostBlockFrames == 0)
            {
                // The host's position in seconds times its tempo, as the
                // session host of the campaign forms it.
                const auto reported = vector.playing ? vector.firstFrame + frame + vector.aheadFrames
                                                     : vector.aheadFrames;
                FathomEngine::Transport transport;
                transport.hasTempo = true;
                transport.bpm = vector.tempo;
                transport.playing = vector.playing;
                transport.quarterNotes = static_cast<double>(reported) / rate * vector.tempo / 60.0;
                engine.setTransport(transport);
            }

            FathomEngine::Frame input;
            for (const auto& impulse : vector.impulses)
                if (impulse.frame == frame)
                    (impulse.channel == 0 ? input.left : input.right) += impulse.amplitude;
            const auto programmeFrame = frame - vector.programmeFirstFrame;
            if (programmeFrame >= 0 && programmeFrame < programmeFrames)
            {
                input.left += vector.programme[2 * static_cast<std::size_t>(programmeFrame)];
                input.right += vector.programme[2 * static_cast<std::size_t>(programmeFrame) + 1];
            }
            const auto wet = engine.processSample(input.left, input.right);
            rendered.push_back(wet.left);
            rendered.push_back(wet.right);
        }

        for (const auto& excerpt : vector.excerpts)
        {
            const auto nullDb = fathomNullDb(
                std::span<const float>(rendered).subspan(
                    2 * static_cast<std::size_t>(excerpt.firstFrame), excerpt.model.size()),
                excerpt.model);
            worstNullDb = std::max(worstNullDb, nullDb);
            require(nullDb <= nullLimitDb,
                    std::string("Undertow engine misses the golden vector ") + vector.name
                        + " from frame " + std::to_string(excerpt.firstFrame)
                        + ": null=" + std::to_string(nullDb) + " dB");
        }
    }

    std::cout << "[METRIC] Undertow golden vectors: worst null against the model=" << worstNullDb
              << " dB, limit=" << nullLimitDb << " dB\n";
}

// At Macro 0 every gain of the layer rests at zero and nothing is added: the
// engine is Fathom's at Macro 0 to the bit, whatever the host says.
void testUndertowIsFathomAtEvolutionZero()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Undertow at Evolution 0, " + std::to_string(static_cast<int>(sampleRate))
                         + " Hz: ";
        const auto frameCount = static_cast<int>(sampleRate * 1.6);
        const auto parameters = undertowTestParameters(0.0f);

        FathomEngine tide;
        tide.setParameters(parameters);
        tide.prepare(sampleRate);
        const auto expected = renderFathomProgramme(tide, frameCount);

        for (const auto playing : { true, false })
        {
            UndertowHost host;
            host.sampleRate = sampleRate;
            host.tempo = 133.0;
            host.playing = playing;
            host.startQuarters = 7.25;
            FathomEngine undertow;
            prepareUndertowEngine(undertow, parameters, sampleRate);
            requireSameFathomRender(renderUndertowProgramme(undertow, host, 0, frameCount), expected,
                                    label + (playing ? "running transport" : "stopped transport"));
        }

        // Macro came down from 100 %: once the layer and the network have
        // fallen silent the engine is Fathom's again.
        UndertowHost host;
        host.sampleRate = sampleRate;
        host.tempo = 999.0;
        auto loud = parameters;
        loud.decaySeconds = 0.2f;
        loud.macro = 1.0f;
        auto quiet = loud;
        quiet.macro = 0.0f;
        FathomEngine visited;
        prepareUndertowEngine(visited, loud, sampleRate);
        FathomEngine plain;
        plain.setParameters(quiet);
        plain.prepare(sampleRate);
        const auto visitFrames = static_cast<int>(sampleRate * 0.5);
        const auto restFrames = static_cast<int>(sampleRate * 9.0);
        auto lastSound = -1;
        for (auto frame = 0; frame < visitFrames + restFrames; ++frame)
        {
            host.announce(visited, frame);
            if (frame == visitFrames)
                visited.setParameters(quiet);
            const auto input = frame < visitFrames ? fathomProgramme(frame % 3000) : FathomEngine::Frame {};
            const auto wet = visited.processSample(input.left, input.right);
            static_cast<void>(plain.processSample(input.left, input.right));
            if (std::abs(wet.left) > 0.0f || std::abs(wet.right) > 0.0f)
                lastSound = frame;
        }
        require(lastSound >= visitFrames && lastSound < visitFrames + static_cast<int>(sampleRate * 8.0),
                label + "the engine did not fall silent behind Macro 100 %");
        const auto after = renderUndertowProgramme(visited, host, visitFrames + restFrames, frameCount);
        requireSameFathomRender(after, renderFathomProgramme(plain, frameCount),
                                label + "settled after a visit at Macro 100 %");
    }

    // Through the plug-in: a settled Undertow at Evolution 0 is a settled Fathom.
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;
    auto parameters = fathomNeutralParameters();
    parameters.evolution = 0.0f;
    parameters.preDelayMs = 7.0f;
    parameters.width = 1.3f;
    parameters.mix = 0.8f;
    auto undertowParameters = parameters;
    undertowParameters.mode = ReverbMode::undertow;
    FDNReverb fathom;
    FDNReverb undertow;
    fathom.setParameters(parameters);
    undertow.setParameters(undertowParameters);
    fathom.prepare(sampleRate, blockSize);
    undertow.prepare(sampleRate, blockSize);
    std::array<float, blockSize> fathomLeft {};
    std::array<float, blockSize> fathomRight {};
    std::array<float, blockSize> undertowLeft {};
    std::array<float, blockSize> undertowRight {};
    auto peak = 0.0f;
    for (auto block = 0; block < 150; ++block)
    {
        for (auto frame = 0; frame < blockSize; ++frame)
        {
            const auto input = fathomBursts(block * blockSize + frame, sampleRate);
            fathomLeft[static_cast<std::size_t>(frame)] = undertowLeft[static_cast<std::size_t>(frame)] = input.left;
            fathomRight[static_cast<std::size_t>(frame)] = undertowRight[static_cast<std::size_t>(frame)] = input.right;
        }
        HostTransport transport;
        transport.quarterNotes = 3.0 + block * blockSize / sampleRate * 2.0;
        transport.bpm = 120.0;
        transport.playing = true;
        transport.hasTempo = true;
        undertow.setHostTransport(transport);
        fathom.process(fathomLeft.data(), fathomRight.data(), blockSize);
        undertow.process(undertowLeft.data(), undertowRight.data(), blockSize);
        for (std::size_t frame = 0; frame < blockSize; ++frame)
        {
            require(sameBits(fathomLeft[frame], undertowLeft[frame])
                        && sameBits(fathomRight[frame], undertowRight[frame]),
                    "A settled Undertow at Evolution 0 is not a settled Fathom at Evolution 0");
            peak = std::max(peak, std::abs(undertowLeft[frame]));
        }
    }
    require(peak > 0.01f, "The Evolution 0 comparison of Undertow and Fathom saw no signal");
}

void testUndertowEngineDeterminismAndClocks()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Undertow engine at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        const auto warmupFrames = static_cast<int>(sampleRate * 0.35);
        const auto programmeFrames = static_cast<int>(sampleRate * 1.9);
        const auto parameters = undertowTestParameters(1.0f);
        UndertowHost host;
        host.sampleRate = sampleRate;
        host.tempo = 171.0;
        host.startQuarters = 11.5;

        const auto excite = [&](FathomEngine& engine, long long firstFrame, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
            {
                host.announce(engine, firstFrame + frame);
                static_cast<void>(engine.processSample(0.3f * fathomNoise(frame, 2),
                                                       0.3f * fathomNoise(frame, 3)));
            }
        };

        FathomEngine fresh;
        prepareUndertowEngine(fresh, parameters, sampleRate);
        idleUndertow(fresh, host, 0, warmupFrames);
        const auto expected = renderUndertowProgramme(fresh, host, warmupFrames, programmeFrames);

        // The layer is in the render: Macro 0 gives another one.
        {
            FathomEngine plain;
            prepareUndertowEngine(plain, undertowTestParameters(0.0f), sampleRate);
            idleUndertow(plain, host, 0, warmupFrames);
            const auto base = renderUndertowProgramme(plain, host, warmupFrames, programmeFrames);
            auto layerEnergy = 0.0;
            auto baseEnergy = 0.0;
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                require(std::isfinite(expected[index]), label + "the programme produced NaN/Inf");
                const auto difference = static_cast<double>(expected[index]) - base[index];
                layerEnergy += difference * difference;
                baseEnergy += static_cast<double>(base[index]) * base[index];
            }
            require(baseEnergy > 1.0e-6 && layerEnergy > 0.05 * baseEnergy,
                    label + "Macro 100 % added no layer to the programme");
        }
        {
            FathomEngine second;
            prepareUndertowEngine(second, parameters, sampleRate);
            idleUndertow(second, host, 0, warmupFrames);
            requireSameFathomRender(renderUndertowProgramme(second, host, warmupFrames, programmeFrames),
                                    expected, label + "second instance");
        }
        {
            FathomEngine used;
            prepareUndertowEngine(used, parameters, sampleRate);
            excite(used, 0, 9000);
            used.reset();
            idleUndertow(used, host, 0, warmupFrames);
            requireSameFathomRender(renderUndertowProgramme(used, host, warmupFrames, programmeFrames),
                                    expected, label + "after reset()");
        }
        {
            FathomEngine prepared;
            prepareUndertowEngine(prepared, parameters, sampleRate * 2.0);
            excite(prepared, 0, 9000);
            prepared.prepare(sampleRate);
            idleUndertow(prepared, host, 0, warmupFrames);
            requireSameFathomRender(renderUndertowProgramme(prepared, host, warmupFrames, programmeFrames),
                                    expected, label + "after prepare() at another rate");
        }
        {
            // Silence in gives exact silence out at every Macro: the layer
            // has no noise of its own.
            FathomEngine silent;
            prepareUndertowEngine(silent, parameters, sampleRate);
            for (auto frame = 0; frame < warmupFrames; ++frame)
            {
                host.announce(silent, frame);
                const auto wet = silent.processSample(0.0f, 0.0f);
                require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                        label + "silence in did not give silence out");
            }
            requireSameFathomRender(renderUndertowProgramme(silent, host, warmupFrames, programmeFrames),
                                    expected, label + "processed silence against advanceIdle()");
        }

        // Left idle after sound, the engine starts from silence with every
        // clock of the layer where a fresh instance has it.
        for (const auto idleFrames : { 1, 2, 3000 })
        {
            constexpr auto soundFrames = 31000;
            FathomEngine reference;
            prepareUndertowEngine(reference, parameters, sampleRate);
            idleUndertow(reference, host, 0, soundFrames + idleFrames);
            const auto afterIdle = renderUndertowProgramme(reference, host, soundFrames + idleFrames,
                                                           programmeFrames);

            FathomEngine sounded;
            prepareUndertowEngine(sounded, parameters, sampleRate);
            excite(sounded, 0, soundFrames);
            idleUndertow(sounded, host, soundFrames, idleFrames);
            requireSameFathomRender(
                renderUndertowProgramme(sounded, host, soundFrames + idleFrames, programmeFrames),
                afterIdle,
                label + "after sound and " + std::to_string(idleFrames) + " idle frames");
        }

        // Held, the network takes no input, and the layer is part of the
        // input: once Freeze has arrived nothing the engine is given changes
        // what it holds.
        {
            const auto holdFrame = static_cast<int>(sampleRate * 0.6);
            const auto partFrame = static_cast<int>(sampleRate * 0.75);
            const auto endFrame = static_cast<int>(sampleRate * 1.75);
            FathomEngine fed;
            FathomEngine starved;
            prepareUndertowEngine(fed, parameters, sampleRate);
            prepareUndertowEngine(starved, parameters, sampleRate);
            auto held = parameters;
            held.freeze = true;
            auto heldPeak = 0.0f;
            for (auto frame = 0; frame < endFrame; ++frame)
            {
                host.announce(fed, frame);
                host.announce(starved, frame);
                if (frame == holdFrame)
                {
                    fed.setParameters(held);
                    starved.setParameters(held);
                }
                const auto shared = frame < partFrame;
                const auto left = 0.3f * fathomNoise(frame, 2);
                const auto right = 0.3f * fathomNoise(frame, 3);
                const auto first = fed.processSample(left, right);
                const auto second = starved.processSample(shared ? left : 0.0f, shared ? right : 0.0f);
                require(sameBits(first.left, second.left) && sameBits(first.right, second.right),
                        label + "input reaches a held network through the layer, frame " + std::to_string(frame));
                if (!shared)
                    heldPeak = std::max({ heldPeak, std::abs(first.left), std::abs(first.right) });
            }
            require(heldPeak > 1.0e-3f, label + "Freeze held nothing");
        }
    }
}

// The engine's own arithmetic keeps the position as a line through the first
// block the host announced. A host that runs on by its frames gives the same
// render in blocks of any length; a stopped one gives what the reference's
// arithmetic gives, which there is exact as well.
void testUndertowHostBlockInvariance()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Undertow engine at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        const auto frameCount = static_cast<int>(sampleRate * 2.6);
        const auto parameters = undertowTestParameters(1.0f);

        for (const auto playing : { true, false })
        {
            UndertowHost host;
            host.sampleRate = sampleRate;
            host.tempo = 97.31;
            host.playing = playing;
            host.startQuarters = 37.123;
            std::vector<float> expected;
            for (const auto blockFrames : { 512, 64, 1, 333 })
            {
                host.blockFrames = blockFrames;
                FathomEngine engine;
                prepareUndertowEngine(engine, parameters, sampleRate);
                const auto rendered = renderUndertowProgramme(engine, host, 0, frameCount);
                if (expected.empty())
                    expected = rendered;
                else
                    requireSameFathomRender(rendered, expected,
                                            label + (playing ? "running" : "stopped") + " transport in blocks of "
                                                + std::to_string(blockFrames));
            }

            if (!playing)
            {
                host.blockFrames = 512;
                FathomEngine engine;
                prepareUndertowEngine(engine, parameters, sampleRate, true);
                requireSameFathomRender(renderUndertowProgramme(engine, host, 0, frameCount), expected,
                                        label + "stopped transport in the reference's arithmetic");
            }
        }
    }

    // Through the plug-in: process() in blocks of any length, each announced.
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 96000;
    auto parameters = fathomNeutralParameters();
    parameters.mode = ReverbMode::undertow;
    parameters.evolution = 0.85f;
    parameters.preDelayMs = 9.0f;
    std::vector<float> expectedLeft;
    std::vector<float> expectedRight;
    for (const auto blockFrames : { 512, 1, 64, 100 })
    {
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, blockFrames);
        std::vector<float> left(frameCount);
        std::vector<float> right(frameCount);
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            const auto input = fathomBursts(frame, sampleRate);
            left[static_cast<std::size_t>(frame)] = input.left;
            right[static_cast<std::size_t>(frame)] = input.right;
        }
        for (auto frame = 0; frame < frameCount; frame += blockFrames)
        {
            HostTransport transport;
            transport.quarterNotes = 12.0 + frame * 141.0 / (60.0 * sampleRate);
            transport.bpm = 141.0;
            transport.playing = true;
            transport.hasTempo = true;
            reverb.setHostTransport(transport);
            reverb.process(left.data() + frame, right.data() + frame,
                           std::min(blockFrames, frameCount - frame));
        }
        if (expectedLeft.empty())
        {
            expectedLeft = left;
            expectedRight = right;
            continue;
        }
        for (std::size_t index = 0; index < left.size(); ++index)
            require(sameBits(left[index], expectedLeft[index]) && sameBits(right[index], expectedRight[index]),
                    "Undertow depends on the host's block length: blocks of "
                        + std::to_string(blockFrames) + ", frame " + std::to_string(index));
    }
}

// The layer on its own at a host rate, for the tests that look at what it adds
// and at the chunks it gives its readers. Host frames and internal samples
// alternate as the engine's converters make them: an internal sample when the
// frames so far have reached it.
struct UndertowLayerRig
{
    explicit UndertowLayerRig(int rate, bool referenceArithmetic = false, double macro = 1.0)
        : hostRate(rate), lattice(FathomRateLattice::at(rate))
    {
        layer.setReferenceArithmetic(referenceArithmetic);
        layer.prepare(lattice, rate);
        layer.setMacro(macro, true);
    }

    // One host frame; `added` receives what the layer adds to each internal
    // sample the frame brings. Returns how many there were.
    template <typename Input>
    int frame(Input&& input, std::array<std::array<double, 2>, 4>& added)
    {
        layer.hostFrame();
        ++frames;
        auto count = 0;
        while ((samples + 1) * lattice.internalStep <= frames * lattice.hostStep)
        {
            const auto in = input(samples);
            auto& out = added[static_cast<std::size_t>(count++)];
            layer.process(in[0], in[1], out[0], out[1]);
            ++samples;
        }
        return count;
    }

    void idleFrame()
    {
        layer.hostFrame();
        ++frames;
        while ((samples + 1) * lattice.internalStep <= frames * lattice.hostStep)
        {
            layer.idle();
            ++samples;
        }
    }

    int hostRate;
    FathomRateLattice lattice;
    UndertowLayer layer;
    long long frames = 0;
    long long samples = 0;
};

// Two steady tones, one per input: what a reader plays back of them is as
// smooth as they are, so anything sharper in the layer's output is a click.
[[nodiscard]] std::array<double, 2> undertowTones(long long sample) noexcept
{
    constexpr auto twoPi = 6.28318530717958647692;
    constexpr auto pi = 3.14159265358979323846;
    constexpr auto onsetSamples = 8820.0;
    const auto seconds = static_cast<double>(sample) / 44100.0;
    // The tones begin as smoothly as they go on: a reader plays their
    // beginning too.
    const auto onset = static_cast<double>(sample) < onsetSamples
        ? 0.5 - 0.5 * std::cos(pi * static_cast<double>(sample) / onsetSamples) : 1.0;
    return { 0.5 * onset * std::sin(twoPi * 220.0 * seconds),
             0.5 * onset * std::sin(twoPi * 330.0 * seconds) };
}

// What the layer adds to the two tones under a transport that `say` describes
// block by block. The click measure is the largest second difference of the
// added signal: for a tone of amplitude a and angular frequency w per sample
// it is a w^2, and a step of height h gives h itself.
struct UndertowRun
{
    double click = 0.0;
    double peak = 0.0;
    // Peak over the last three seconds: what is left of the layer at the end.
    double latePeak = 0.0;
    bool finite = true;
};

template <typename Say, typename Macro>
[[nodiscard]] UndertowRun runUndertowLayer(int hostRate, int blockFrames, long long frameCount,
                                           Say&& say, Macro&& macro)
{
    UndertowLayerRig rig(hostRate);
    UndertowRun run;
    std::array<std::array<double, 2>, 2> before {};
    std::array<std::array<double, 2>, 4> added {};
    for (long long frame = 0; frame < frameCount; ++frame)
    {
        if (frame % blockFrames == 0)
        {
            rig.layer.setTransport(say(frame / blockFrames));
            macro(rig.layer, frame / blockFrames);
        }
        const auto count = rig.frame(undertowTones, added);
        for (auto index = 0; index < count; ++index)
        {
            const auto& now = added[static_cast<std::size_t>(index)];
            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                run.finite = run.finite && std::isfinite(now[channel]);
                run.peak = std::max(run.peak, std::abs(now[channel]));
                if (frame >= frameCount - 3LL * hostRate)
                    run.latePeak = std::max(run.latePeak, std::abs(now[channel]));
                run.click = std::max(run.click,
                                     std::abs(now[channel] - 2.0 * before[1][channel] + before[0][channel]));
            }
            before[0] = before[1];
            before[1] = now;
        }
    }
    return run;
}

[[nodiscard]] UndertowLayer::Transport undertowTransport(double quarterNotes, double bpm, bool playing,
                                                         bool hasTempo = true) noexcept
{
    UndertowLayer::Transport transport;
    transport.quarterNotes = quarterNotes;
    transport.bpm = bpm;
    transport.playing = playing;
    transport.hasTempo = hasTempo;
    return transport;
}

// Ocean's own rules for a transport that does not simply run on: nothing may
// click, whatever the host does, and everything stays finite and bounded.
void testUndertowTransportEvents()
{
    constexpr auto blockFrames = 256;
    constexpr auto eventBlock = 700LL;
    const auto notANumber = std::numeric_limits<double>::quiet_NaN();
    const auto infinite = std::numeric_limits<double>::infinity();
    const auto steadyMacro = [](UndertowLayer&, long long) {};

    for (const auto hostRate : { 44100, 48000 })
    {
        const auto rate = static_cast<double>(hostRate);
        const auto frameCount = static_cast<long long>(rate * 10.0);
        const auto running = [&](long long block, double tempo, double startQuarters = 0.0)
        {
            return startQuarters + static_cast<double>(block * blockFrames) * tempo / (60.0 * rate);
        };

        // The measure of a transport that simply runs.
        const auto calm = runUndertowLayer(hostRate, blockFrames, frameCount, [&](long long block)
        {
            return undertowTransport(running(block, 120.0), 120.0, true);
        }, steadyMacro);
        require(calm.finite && calm.peak > 0.3 && calm.peak < 2.0,
                "Undertow layer under a running transport is out of its range: peak="
                    + std::to_string(calm.peak));

        constexpr std::array<const char*, 11> scenarios {
            "start", "stop", "jump forward", "jump back", "loop", "tempo step", "tempo step while stopped",
            "tempo ramp", "tempo lost", "a jump at every block", "what is no transport"
        };
        const auto say = [&](std::size_t scenario, long long block)
        {
            switch (scenario)
            {
                case 0:
                    return block < eventBlock ? undertowTransport(0.0, 120.0, false)
                                              : undertowTransport(running(block - eventBlock, 120.0, 8.0), 120.0, true);
                case 1:
                    return undertowTransport(running(std::min(block, eventBlock), 120.0), 120.0, block < eventBlock);
                case 2:
                    return undertowTransport(running(block, 120.0, block < eventBlock ? 0.0 : 13.37), 120.0, true);
                case 3:
                    return undertowTransport(running(block, 120.0, block < eventBlock ? 9.0 : 1.5), 120.0, true);
                case 4:
                {
                    // Four quarter notes round and round: every two seconds a block jumps back.
                    const auto loopBlocks = static_cast<long long>(2.0 * rate / blockFrames);
                    return undertowTransport(running(block % loopBlocks, 120.0, 16.0), 120.0, true);
                }
                case 5:
                    return block < eventBlock
                        ? undertowTransport(running(block, 120.0), 120.0, true)
                        : undertowTransport(running(eventBlock, 120.0) + running(block - eventBlock, 87.0), 87.0, true);
                case 6:
                    return undertowTransport(0.0, block < eventBlock ? 120.0 : 163.0, false);
                case 7:
                {
                    // 120 to 150 BPM over 400 blocks; the position is the sum of what went before.
                    const auto along = std::clamp(block - 500LL, 0LL, 400LL);
                    const auto tempo = 120.0 + 30.0 * static_cast<double>(along) / 400.0;
                    const auto mean = 120.0 + 15.0 * static_cast<double>(along) / 400.0;
                    return undertowTransport(running(std::min(block, 500LL), 120.0) + running(along, mean)
                                                 + running(std::max(block - 900LL, 0LL), 150.0),
                                             tempo, true);
                }
                case 8:
                    return undertowTransport(running(block, block < eventBlock ? 97.0 : 120.0), 97.0, true,
                                             block < eventBlock);
                case 9:
                {
                    const auto storm = block >= eventBlock && block < eventBlock + 80;
                    const auto offset = storm ? 3.7 * static_cast<double>(fathomNoise(static_cast<int>(block), 9)) : 0.0;
                    return undertowTransport(running(block, 120.0, 5.0 + offset), 120.0, true);
                }
                default:
                {
                    // Positions and tempi that are no numbers, or none a host could mean.
                    const std::array<double, 8> positions { notANumber, infinite, -infinite, 1.0e30, -1.0e30,
                                                            1.0e11, -1.0e11, 0.0 };
                    const std::array<double, 8> tempi { notANumber, infinite, 0.0, -5.0, 1.0e9, 1.0e-9, 19.0, 1000.0 };
                    if (block < eventBlock || block % 37 != 0)
                        return undertowTransport(running(block, 120.0), 120.0, true);
                    const auto pick = static_cast<std::size_t>(block / 37);
                    return undertowTransport(positions[pick % positions.size()], tempi[(pick / 3) % tempi.size()],
                                             pick % 5 != 0);
                }
            }
        };

        for (std::size_t scenario = 0; scenario < scenarios.size(); ++scenario)
        {
            const auto run = runUndertowLayer(hostRate, blockFrames, frameCount, [&](long long block)
            {
                return say(scenario, block);
            }, steadyMacro);
            const auto label = std::string("Undertow layer at ") + std::to_string(hostRate) + " Hz, "
                             + scenarios[scenario] + ": ";
            std::cout << "[METRIC] " << label << "click measure=" << run.click << " (running transport "
                      << calm.click << "), peak=" << run.peak << ", in the last 3 s=" << run.latePeak << '\n';
            require(run.finite, label + "NaN/Inf");
            require(run.peak < 2.0, label + "left its range, peak=" + std::to_string(run.peak));
            require(run.latePeak > 0.3,
                    label + "the layer did not come back, late peak=" + std::to_string(run.latePeak));
            require(run.click <= 1.5 * calm.click,
                    label + "clicks: " + std::to_string(run.click) + " against "
                        + std::to_string(calm.click) + " of a running transport");
        }
    }

    // The chunks follow the tempo: successive chunks of a reader lie a note
    // value apart, at 120 BPM without a tempo and at the nearer end outside
    // 20 to 999 BPM.
    struct TempoCase
    {
        double bpm;
        bool hasTempo;
        double effective;
    };
    constexpr std::array<TempoCase, 6> tempoCases {{
        { 90.0, true, 90.0 }, { 120.0, true, 120.0 }, { 300.0, false, 120.0 },
        { 5.0, true, 20.0 }, { 5000.0, true, 999.0 }, { 61.7, true, 61.7 }
    }};
    constexpr std::array<double, 3> noteQuarters { 1.3333333730697632, 2.0, 2.6666667461395264 };
    for (const auto& tempoCase : tempoCases)
    {
        for (const auto playing : { true, false })
        {
            UndertowLayerRig rig(44100);
            std::array<UndertowLayer::ChunkStart, 3> last {};
            std::array<int, 3> spans {};
            const auto samplesPerQuarter = 60.0 / tempoCase.effective * 44100.0;
            const auto frameCount = static_cast<long long>(2.4 * noteQuarters[2] * samplesPerQuarter);
            for (long long frame = 0; frame < frameCount; ++frame)
            {
                if (frame % blockFrames == 0)
                    rig.layer.setTransport(undertowTransport(
                        static_cast<double>(frame) * tempoCase.effective / (60.0 * 44100.0),
                        tempoCase.bpm, playing, tempoCase.hasTempo));
                rig.idleFrame();
                for (std::size_t voice = 0; voice < 3; ++voice)
                {
                    const auto given = rig.layer.lastChunk(static_cast<UndertowLayer::Voice>(voice));
                    if (given.count == last[voice].count)
                        continue;
                    if (last[voice].count > 0)
                    {
                        const auto span = 0.5 * (given.mirrorSum - last[voice].mirrorSum);
                        require(given.index == last[voice].index + 1
                                    && std::abs(span - noteQuarters[voice] * samplesPerQuarter) < 1.01,
                                "Undertow chunks of voice " + std::to_string(voice) + " at "
                                    + std::to_string(tempoCase.bpm) + " BPM are " + std::to_string(span)
                                    + " samples apart");
                        ++spans[voice];
                    }
                    last[voice] = given;
                }
            }
            require(spans[0] >= 3 && spans[1] >= 2 && spans[2] >= 1,
                    "Undertow tempo test saw too few chunks at " + std::to_string(tempoCase.bpm) + " BPM");
        }
    }

    // The engine under the same kinds of transport on noise: finite and
    // inside the range of its base.
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        auto parameters = undertowTestParameters(1.0f);
        parameters.decaySeconds = 30.0f;
        FathomEngine engine;
        prepareUndertowEngine(engine, parameters, sampleRate);
        const auto frameCount = static_cast<int>(sampleRate * 6.0);
        auto peak = 0.0f;
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            if (frame % 96 == 0)
            {
                const auto block = frame / 96;
                FathomEngine::Transport transport;
                transport.hasTempo = block % 11 != 0;
                transport.bpm = block % 29 == 0 ? notANumber : block % 17 < 8 ? 120.0 : 71.5 + block % 13;
                transport.playing = block % 23 < 15;
                transport.quarterNotes = block % 31 == 0 ? infinite
                    : static_cast<double>(frame % 60000) * 120.0 / (60.0 * sampleRate) + (block % 7 == 0 ? 3.3 : 0.0);
                engine.setTransport(transport);
            }
            const auto wet = engine.processSample(0.5f * fathomNoise(frame, 30), 0.5f * fathomNoise(frame, 31));
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    "Undertow engine produced NaN/Inf under a restless transport");
            peak = std::max({ peak, std::abs(wet.left), std::abs(wet.right) });
        }
        require(peak > 0.05f && peak < 8.0f,
                "Undertow engine left its range under a restless transport: peak=" + std::to_string(peak));
    }
}

// Macro: three ramps, each gain through a one-pole of 10 ms behind its
// reversed reader; the octave gains in front of their grain readers; buffers
// and clocks run at every Macro.
void testUndertowMacroSteps()
{
    constexpr auto hostRate = 44100;
    constexpr auto blockFrames = 256;
    const auto say = [](long long block)
    {
        return undertowTransport(static_cast<double>(block * blockFrames) * 120.0 / (60.0 * 44100.0), 120.0, true);
    };

    // No step of Macro clicks.
    const auto calm = runUndertowLayer(hostRate, blockFrames, 441000, say, [](UndertowLayer&, long long) {});
    const auto stepped = runUndertowLayer(hostRate, blockFrames, 441000, say, [](UndertowLayer& layer, long long block)
    {
        constexpr std::array<double, 9> positions { 1.0, 0.0, 1.0, 0.3, 0.7, 0.05, 0.62, 0.0, 1.0 };
        layer.setMacro(positions[static_cast<std::size_t>(block / 173) % positions.size()], false);
    });
    std::cout << "[METRIC] Undertow Macro steps: click measure=" << stepped.click << " (Macro at rest "
              << calm.click << "), peak=" << stepped.peak << '\n';
    require(stepped.finite && stepped.peak < 2.0, "Undertow Macro steps left the layer's range");
    require(stepped.click <= 1.5 * calm.click, "Undertow Macro steps click");

    // The gain at pitch: what was stored at Macro 0 plays at the new gain at
    // once, through the one-pole. At Macro 14 % that voice sounds alone.
    constexpr auto stepSample = 60000LL;
    constexpr auto compared = 44;
    constexpr auto gainAtPitch = 0.6927952;
    const auto pole = std::exp(-1.0 / (0.010 * 44100.0));
    UndertowLayerRig alone(hostRate, false, 0.14);
    UndertowLayerRig step(hostRate, false, 0.0);
    std::array<std::array<double, 2>, 4> aloneAdded {};
    std::array<std::array<double, 2>, 4> stepAdded {};
    auto worst = 0.0;
    auto peak = 0.0;
    auto laterDifference = 0.0;
    for (long long frame = 0; frame < stepSample + 9000; ++frame)
    {
        if (frame % blockFrames == 0)
        {
            alone.layer.setTransport(say(frame / blockFrames));
            step.layer.setTransport(say(frame / blockFrames));
        }
        if (frame == stepSample)
            step.layer.setMacro(0.4, false);
        static_cast<void>(alone.frame(undertowTones, aloneAdded));
        static_cast<void>(step.frame(undertowTones, stepAdded));
        if (frame < stepSample)
        {
            require(!(std::abs(stepAdded[0][0]) > 0.0) && !(std::abs(stepAdded[0][1]) > 0.0)
                        && step.layer.atRest(),
                    "Undertow layer adds something at Macro 0");
            continue;
        }

        // The stream at pitch, from the layer that plays it alone.
        const auto arrived = 1.0 - std::pow(pole, static_cast<double>(frame - stepSample + 1));
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            const auto stream = aloneAdded[0][channel] / (gainAtPitch * 0.14 / 0.3);
            const auto atPitch = arrived * gainAtPitch * stream;
            if (frame < stepSample + compared)
            {
                // Nothing of the octave above has passed its grain reader yet.
                worst = std::max(worst, std::abs(stepAdded[0][channel] - atPitch));
                peak = std::max(peak, std::abs(atPitch));
            }
            else
            {
                laterDifference = std::max(laterDifference, std::abs(stepAdded[0][channel] - atPitch));
            }
        }
    }
    std::cout << "[METRIC] Undertow Macro step at pitch: distance from the one-pole=" << worst
              << ", peak=" << peak << ", octave above later=" << laterDifference << '\n';
    require(peak > 1.0e-3 && worst <= 1.0e-9 * peak,
            "Undertow gain at pitch does not follow a one-pole of 10 ms behind its reader");
    require(laterDifference > 1.0e-3,
            "Undertow octave above did not arrive behind its grain reader");

    // The engine through steps of Macro on noise.
    FathomEngine engine;
    auto parameters = undertowTestParameters(0.0f);
    prepareUndertowEngine(engine, parameters, 48000.0);
    UndertowHost host;
    auto enginePeak = 0.0f;
    for (auto frame = 0; frame < 240000; ++frame)
    {
        host.announce(engine, frame);
        if (frame % 7919 == 0)
        {
            parameters.macro = static_cast<float>((frame / 7919) % 6) / 5.0f;
            engine.setParameters(parameters);
        }
        const auto wet = engine.processSample(0.4f * fathomNoise(frame, 32), 0.4f * fathomNoise(frame, 33));
        require(std::isfinite(wet.left) && std::isfinite(wet.right), "Undertow Macro steps produced NaN/Inf");
        enginePeak = std::max({ enginePeak, std::abs(wet.left), std::abs(wet.right) });
    }
    require(enginePeak > 0.05f && enginePeak < 8.0f, "Undertow engine left its range under Macro steps");
}

// The boundary a chunk was given for, read back from the sum of its reversed
// read: twice the block that holds it, less two, plus the way into the block
// and that way rounded down (up for the octave-up reader).
[[nodiscard]] double undertowBoundary(const UndertowLayer::ChunkStart& chunk, bool roundsUp) noexcept
{
    const auto sum = chunk.mirrorSum + 2.0;
    if (!roundsUp)
    {
        const auto whole = std::floor(sum / 2.0);
        return whole + (sum - 2.0 * whole);
    }
    const auto half = sum / 2.0;
    if (!(half > std::floor(half)))
        return half;
    const auto whole = std::floor((sum - 1.0) / 2.0);
    return whole + (sum - 1.0 - 2.0 * whole);
}

// The engine's own arithmetic holds the position in double precision: hours
// into a session a chunk still begins where the host's position is a whole
// number of its note values. The reference's single-precision stamps do not
// reach that far.
void testUndertowLongRunStaysOnTheGrid()
{
    constexpr auto hostRate = 44100;
    constexpr auto blockFrames = 512;
    constexpr auto tempo = 97.31;
    constexpr std::array<double, 3> noteQuarters { 1.3333333730697632, 2.0, 2.6666667461395264 };
    const auto quartersPerFrame = tempo / (60.0 * 44100.0);

    // Worst distance of a chunk's boundary from its place on the grid, in
    // samples, over some seconds of a transport that began at `startQuarters`
    // `idleFrames` earlier.
    const auto offGrid = [&](bool referenceArithmetic, double startQuarters, long long idleFrames)
    {
        UndertowLayerRig rig(hostRate, referenceArithmetic);
        const auto announce = [&]
        {
            if (rig.frames % blockFrames == 0)
                rig.layer.setTransport(undertowTransport(
                    startQuarters + static_cast<double>(rig.frames) * quartersPerFrame, tempo, true));
        };
        for (long long frame = 0; frame < idleFrames; ++frame)
        {
            announce();
            rig.idleFrame();
        }

        std::array<std::int64_t, 3> counted {};
        for (std::size_t voice = 0; voice < 3; ++voice)
            counted[voice] = rig.layer.lastChunk(static_cast<UndertowLayer::Voice>(voice)).count;
        auto worst = 0.0;
        auto chunks = 0;
        for (long long frame = 0; frame < 44100 * 9; ++frame)
        {
            announce();
            rig.idleFrame();
            for (std::size_t voice = 0; voice < 3; ++voice)
            {
                const auto given = rig.layer.lastChunk(static_cast<UndertowLayer::Voice>(voice));
                if (given.count == counted[voice])
                    continue;
                counted[voice] = given.count;
                // At this rate an internal sample is a host frame.
                const auto boundary = undertowBoundary(given, voice == 1);
                const auto quarters = startQuarters + boundary * quartersPerFrame;
                worst = std::max(worst, std::abs(quarters - static_cast<double>(given.index) * noteQuarters[voice])
                                            / quartersPerFrame);
                ++chunks;
            }
        }
        require(chunks >= 9, "Undertow grid test saw too few chunks");
        return worst;
    };

    // Fourteen hours into the host's timeline, and an hour of the instance's own run.
    const auto farPosition = offGrid(false, 81234.5, 0);
    const auto longRun = offGrid(false, 3.25, 44100LL * 3600);
    const auto nearPosition = offGrid(false, 3.25, 0);
    const auto referenceFar = offGrid(true, 81234.5, 0);
    std::cout << "[METRIC] Undertow chunk boundaries off the grid, in samples: " << nearPosition
              << " at the start, " << longRun << " after an hour's run, " << farPosition
              << " fourteen hours into the timeline; the reference's arithmetic there " << referenceFar << '\n';
    require(nearPosition < 1.0e-6 && longRun < 1.0e-5 && farPosition < 1.0e-5,
            "Undertow chunks leave the grid in the engine's own arithmetic");
    require(referenceFar > 10.0,
            "The reference's arithmetic was expected off the grid fourteen hours in: the test does not see the grid");
}

void testUndertowSilenceDenormalsAndHostileInput()
{
    // The recirculation of the octave-up voice loses 14 dB a pass and no more:
    // without a floor it would run through the denormal range for minutes. At
    // the fastest tempo a pass is 120 ms.
    UndertowLayerRig rig(44100);
    std::array<std::array<double, 2>, 4> added {};
    auto lastSound = -1LL;
    auto denormals = 0;
    for (long long frame = 0; frame < 44100 * 12; ++frame)
    {
        if (frame % 256 == 0)
            rig.layer.setTransport(undertowTransport(static_cast<double>(frame) * 999.0 / (60.0 * 44100.0),
                                                     999.0, true));
        const auto count = rig.frame([](long long sample)
        {
            return sample < 22050 ? undertowTones(sample) : std::array<double, 2> { 0.0, 0.0 };
        }, added);
        require(count == 1, "Undertow rig is out of step at 44.1 kHz");
        for (const auto value : added[0])
        {
            if (std::fpclassify(value) == FP_SUBNORMAL)
                ++denormals;
            if (std::abs(value) > 0.0)
                lastSound = frame;
        }
    }
    require(denormals == 0, "Undertow layer lets its recirculation reach the denormal range");
    require(lastSound > 44100 && lastSound < 44100 * 10,
            "Undertow layer did not fall silent behind its input: last sound at sample "
                + std::to_string(lastSound));

    constexpr std::array<float, 8> hostileSamples {
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
        -1.0e30f, std::numeric_limits<float>::denorm_min(), 1.0e-39f, -0.0f
    };
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto label = "Undertow engine at " + std::to_string(static_cast<int>(sampleRate)) + " Hz ";
        UndertowHost host;
        host.sampleRate = sampleRate;
        host.tempo = 999.0;

        // Input that is all denormal is silence to the layer and to the engine.
        {
            FathomEngine engine;
            prepareUndertowEngine(engine, undertowTestParameters(1.0f), sampleRate);
            for (auto frame = 0; frame < 30000; ++frame)
            {
                host.announce(engine, frame);
                const auto wet = engine.processSample(frame % 2 == 0 ? 1.0e-39f : -1.0e-41f,
                                                      std::numeric_limits<float>::denorm_min());
                require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                        label + "turned denormal input into sound");
            }
        }

        auto parameters = undertowTestParameters(1.0f);
        parameters.decaySeconds = 30.0f;
        parameters.sizeScale = 2.0f;
        parameters.preDelaySeconds = 0.25f;
        FathomEngine engine;
        FathomEngine::LevelStage levelStage;
        prepareUndertowEngine(engine, parameters, sampleRate);
        levelStage.prepare(sampleRate);
        auto peak = 0.0f;
        const auto frames = static_cast<int>(sampleRate * 1.5);
        for (auto frame = 0; frame < frames; ++frame)
        {
            host.announce(engine, frame);
            if (frame % 997 == 0)
            {
                const auto step = frame / 997;
                parameters.decaySeconds = step % 3 == 0 ? 0.2f : 60.0f;
                parameters.sizeScale = step % 2 == 0 ? 0.15f : 2.0f;
                parameters.macro = step % 4 == 0 ? 0.0f : step % 4 == 1 ? 1.0f
                                 : std::numeric_limits<float>::quiet_NaN();
                parameters.freeze = step % 6 == 5;
                engine.setParameters(parameters);
            }
            auto left = fathomNoise(frame, 4);
            auto right = fathomNoise(frame, 5);
            if (frame % 61 == 0)
                left = hostileSamples[static_cast<std::size_t>(frame / 61) % hostileSamples.size()];
            if (frame % 89 == 0)
                right = hostileSamples[static_cast<std::size_t>(frame / 89) % hostileSamples.size()];
            auto wet = levelStage.process(left, right, engine.processSample(left, right));
            wet = FathomEngine::applyWidth(wet, 2.0f);
            wet.left = FathomEngine::clip(wet.left);
            wet.right = FathomEngine::clip(wet.right);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "let hostile input through as NaN/Inf");
            peak = std::max({ peak, std::abs(wet.left), std::abs(wet.right) });
        }
        require(peak <= 3.99f, label + "left the clipper's range under hostile input");

        // Once the input is gone a short Decay brings the engine back to
        // exact silence, the layer with it.
        parameters = undertowTestParameters(1.0f);
        parameters.decaySeconds = 0.2f;
        parameters.sizeScale = 1.0f;
        parameters.preDelaySeconds = 0.0f;
        engine.setParameters(parameters);
        auto lastFrame = -1;
        const auto tailFrames = static_cast<int>(sampleRate * 12.0);
        for (auto frame = 0; frame < tailFrames; ++frame)
        {
            host.announce(engine, frames + frame);
            const auto wet = engine.processSample(0.0f, 0.0f);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "kept NaN/Inf after hostile input");
            if (std::abs(wet.left) > 0.0f || std::abs(wet.right) > 0.0f)
                lastFrame = frame;
        }
        require(lastFrame < static_cast<int>(sampleRate * 11.0),
                label + "did not return to silence after hostile input");
    }
}

void testUndertowEngineAllocatesOnlyInPrepare()
{
    constexpr std::array<double, 2> sampleRates { 48000.0, 44101.0 };
    for (const auto sampleRate : sampleRates)
    {
        for (const auto referenceArithmetic : { false, true })
        {
            FathomEngine engine;
            FathomEngine::LevelStage levelStage;
            auto parameters = undertowTestParameters(1.0f);
            prepareUndertowEngine(engine, parameters, sampleRate, referenceArithmetic);
            levelStage.prepare(sampleRate);

            allocationCount.store(0, std::memory_order_relaxed);
            countAllocations.store(true, std::memory_order_relaxed);
            for (auto frame = 0; frame < 90000; ++frame)
            {
                if (frame % 128 == 0)
                {
                    const auto block = frame / 128;
                    FathomEngine::Transport transport;
                    transport.hasTempo = block % 19 != 0;
                    transport.bpm = block % 40 < 20 ? 120.0 : 83.0;
                    transport.playing = block % 50 < 35;
                    transport.quarterNotes = (frame % 30000) * 120.0 / (60.0 * sampleRate);
                    engine.setTransport(transport);
                }
                if (frame % 211 == 0)
                {
                    parameters.decaySeconds = parameters.decaySeconds > 1.0f ? 0.2f : 30.0f;
                    parameters.macro = static_cast<float>((frame / 211) % 5) / 4.0f;
                    parameters.freeze = frame % 422 == 0;
                    engine.setParameters(parameters);
                }
                if (frame % 37000 == 36000)
                    engine.reset();
                if ((frame / 5000) % 3 == 2)
                {
                    engine.advanceIdle();
                    continue;
                }
                const auto left = 0.5f * fathomNoise(frame, 6);
                const auto right = 0.5f * fathomNoise(frame, 7);
                static_cast<void>(levelStage.process(left, right, engine.processSample(left, right)));
            }
            countAllocations.store(false, std::memory_order_relaxed);

            require(allocationCount.load(std::memory_order_relaxed) == 0,
                    "Undertow engine allocated memory outside prepare()");
        }
    }

    FDNReverb reverb;
    auto parameters = fathomNeutralParameters();
    parameters.mode = ReverbMode::undertow;
    reverb.setParameters(parameters);
    reverb.prepare(48000.0, 256);
    std::array<float, 256> left {};
    std::array<float, 256> right {};
    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (auto block = 0; block < 300; ++block)
    {
        for (std::size_t frame = 0; frame < left.size(); ++frame)
        {
            left[frame] = 0.3f * fathomNoise(block * 256 + static_cast<int>(frame), 8);
            right[frame] = 0.3f * fathomNoise(block * 256 + static_cast<int>(frame), 9);
        }
        if (block % 40 == 20)
        {
            parameters.mode = parameters.mode == ReverbMode::undertow ? ReverbMode::fathom : ReverbMode::undertow;
            reverb.setParameters(parameters);
        }
        HostTransport transport;
        transport.quarterNotes = (block % 90) * 256.0 * 120.0 / (60.0 * 48000.0);
        transport.bpm = 120.0;
        transport.playing = block % 70 < 50;
        transport.hasTempo = true;
        reverb.setHostTransport(transport);
        reverb.process(left.data(), right.data(), 256);
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(allocationCount.load(std::memory_order_relaxed) == 0,
            "Undertow allocated memory in process");
}

// The chain Undertow must reduce to while Ocean's own controls are neutral:
// Fathom's, with the engine under the Undertow layer and told of the host's
// transport.
struct UndertowReferenceChain
{
    UndertowReferenceChain(const ReverbParameters& parameters, double sampleRate)
        : mix(parameters.mix), width(parameters.width)
    {
        engine.setLayer(FathomEngine::Layer::undertow);
        engine.setParameters(fathomEngineParameters(parameters));
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);
    }

    void announce(const HostTransport& transport) noexcept
    {
        FathomEngine::Transport forwarded;
        forwarded.quarterNotes = transport.quarterNotes;
        forwarded.bpm = transport.bpm;
        forwarded.playing = transport.playing;
        forwarded.hasTempo = transport.hasTempo;
        engine.setTransport(forwarded);
    }

    void idle() noexcept
    {
        engine.advanceIdle();
    }

    [[nodiscard]] FathomEngine::Frame wet(float dryLeft, float dryRight) noexcept
    {
        return levelStage.process(dryLeft, dryRight, engine.processSample(dryLeft, dryRight));
    }

    [[nodiscard]] FathomEngine::Frame process(float dryLeft, float dryRight) noexcept
    {
        const auto widened = FathomEngine::applyWidth(wet(dryLeft, dryRight), width);
        return { FathomReferenceChain::guard(FathomEngine::clip(oceanMix(dryLeft, widened.left, mix))),
                 FathomReferenceChain::guard(FathomEngine::clip(oceanMix(dryRight, widened.right, mix))) };
    }

    FathomEngine engine;
    FathomEngine::LevelStage levelStage;
    float mix;
    float width;
};

// A host of the plug-in for these tests: blocks of 64 frames at 108 BPM from
// the position of 5 quarter notes.
[[nodiscard]] HostTransport undertowPlugInTransport(int frame, double sampleRate) noexcept
{
    HostTransport transport;
    transport.quarterNotes = 5.0 + static_cast<double>(frame) * 108.0 / (60.0 * sampleRate);
    transport.bpm = 108.0;
    transport.playing = true;
    transport.hasTempo = true;
    return transport;
}

// One frame of the plug-in under that host. FDNReverb takes the transport per
// process() call, so a block of the host is a call.
void processUndertowPlugIn(FDNReverb& reverb, int frame, double sampleRate, float& left, float& right)
{
    constexpr auto blockFrames = 64;
    if (frame % blockFrames == 0)
        reverb.setHostTransport(undertowPlugInTransport(frame, sampleRate));
    // A call of one frame: the transport said for a block's first frame holds
    // for it, and the frames behind it say nothing new.
    if (frame % blockFrames == 0)
        reverb.process(&left, &right, 1);
    else
        reverb.processSample(left, right);
}

void testUndertowThroughThePlugIn()
{
    constexpr auto blockFrames = 64;

    // Routing at Ocean's neutral controls: the engine's wet through its level stage.
    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        for (const auto evolution : { 0.0f, 0.45f, 1.0f })
        {
            auto parameters = fathomNeutralParameters();
            parameters.mode = ReverbMode::undertow;
            parameters.evolution = evolution;
            parameters.preDelayMs = 12.3f;
            FDNReverb reverb;
            reverb.setParameters(parameters);
            reverb.prepare(sampleRate, blockFrames);
            UndertowReferenceChain chain(parameters, sampleRate);

            auto wetPeak = 0.0f;
            const auto frameCount = static_cast<int>(sampleRate * 1.75);
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                if (frame % blockFrames == 0)
                    chain.announce(undertowPlugInTransport(frame, sampleRate));
                const auto input = fathomBursts(frame, sampleRate);
                const auto expected = chain.wet(input.left, input.right);
                wetPeak = std::max({ wetPeak, std::abs(expected.left), std::abs(expected.right) });
                auto left = input.left;
                auto right = input.right;
                processUndertowPlugIn(reverb, frame, sampleRate, left, right);
                require(sameBits(left, expected.left) && sameBits(right, expected.right),
                        "Undertow is not the engine's wet through its level stage at "
                            + std::to_string(static_cast<int>(sampleRate)) + " Hz, Evolution "
                            + std::to_string(evolution) + ", frame " + std::to_string(frame));
            }
            require(wetPeak > 0.01f, "Undertow routing programme did not exercise the engine");
        }
    }

    constexpr auto sampleRate = 48000.0;
    constexpr auto switchSample = 60000;
    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    constexpr auto endSample = switchSample + morphSamples + 12000;

    // Fathom and Undertow crossfade directly: nothing of the FDN shows.
    for (const auto intoUndertow : { true, false })
    {
        const auto label = std::string(intoUndertow ? "Fathom to Undertow" : "Undertow to Fathom") + " switch";
        auto parameters = fathomNeutralParameters();
        parameters.decaySeconds = 6.0f;
        parameters.evolution = 0.8f;
        auto switched = parameters;
        switched.mode = intoUndertow ? ReverbMode::fathom : ReverbMode::undertow;
        FDNReverb reverb;
        reverb.setParameters(switched);
        reverb.prepare(sampleRate, blockFrames);
        FathomReferenceChain fathomSide(parameters, sampleRate);
        UndertowReferenceChain undertowSide(parameters, sampleRate);
        FathomMorphRamp undertowAmount(intoUndertow ? 0.0f : 1.0f, intoUndertow ? 1.0f : 0.0f, morphSamples);
        FathomMorphRamp fathomAmount(intoUndertow ? 1.0f : 0.0f, intoUndertow ? 0.0f : 1.0f, morphSamples);

        auto largestDistance = 0.0f;
        auto peak = 1.0e-3f;
        double sideDifferenceEnergy = 0.0;
        double sideEnergy = 0.0;
        for (auto sample = 0; sample < endSample; ++sample)
        {
            if (sample == switchSample)
            {
                switched.mode = intoUndertow ? ReverbMode::undertow : ReverbMode::fathom;
                reverb.setParameters(switched);
            }
            if (sample % blockFrames == 0)
                undertowSide.announce(undertowPlugInTransport(sample, sampleRate));

            const auto input = fathomBursts(sample, sampleRate, 0.5f);
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(reverb, sample, sampleRate, left, right);

            // Each side sounds while its amount is above zero and keeps time otherwise.
            const auto fathomSounds = intoUndertow ? sample < switchSample + morphSamples - 1
                                                   : sample >= switchSample;
            const auto undertowSounds = intoUndertow ? sample >= switchSample
                                                     : sample < switchSample + morphSamples - 1;
            auto fathom = FathomEngine::Frame {};
            auto undertow = FathomEngine::Frame {};
            if (fathomSounds)
                fathom = fathomSide.process(input.left, input.right);
            else
                fathomSide.idle();
            if (undertowSounds)
                undertow = undertowSide.process(input.left, input.right);
            else
                undertowSide.idle();

            if (sample < switchSample)
            {
                const auto& before = intoUndertow ? fathom : undertow;
                require(sameBits(left, before.left) && sameBits(right, before.right),
                        label + " differs before the switch");
                continue;
            }
            // Each amount moves on its own ramp; the two share the wet by them.
            const auto rising = undertowAmount.next();
            const auto falling = fathomAmount.next();
            const auto amount = rising / (falling + rising);
            if (sample >= switchSample + morphSamples - 1)
            {
                const auto& after = intoUndertow ? undertow : fathom;
                require(sameBits(left, after.left) && sameBits(right, after.right),
                        label + " is not the new Character alone after the crossfade, at sample "
                            + std::to_string(sample));
                continue;
            }
            largestDistance = std::max({
                largestDistance,
                std::abs(left - (fathom.left + amount * (undertow.left - fathom.left))),
                std::abs(right - (fathom.right + amount * (undertow.right - fathom.right)))
            });
            peak = std::max({ peak, std::abs(fathom.left), std::abs(fathom.right),
                              std::abs(undertow.left), std::abs(undertow.right) });
            sideDifferenceEnergy += static_cast<double>(undertow.left - fathom.left) * (undertow.left - fathom.left)
                                  + static_cast<double>(undertow.right - fathom.right)
                                        * (undertow.right - fathom.right);
            sideEnergy += 0.5 * (static_cast<double>(fathom.left) * fathom.left
                                 + static_cast<double>(fathom.right) * fathom.right
                                 + static_cast<double>(undertow.left) * undertow.left
                                 + static_cast<double>(undertow.right) * undertow.right);
        }
        std::cout << "[METRIC] " << label << ": largest distance from the crossfade="
                  << largestDistance << ", peak of its two sides=" << peak << '\n';
        require(largestDistance <= 4.0e-6f * peak,
                label + " is not a crossfade of the two Characters: distance="
                    + std::to_string(largestDistance / peak) + " of the peak");
        require(sideEnergy > 1.0e-8 && sideDifferenceEnergy > 0.25 * sideEnergy,
                label + " crossfades two sides that do not differ");
    }

    // Default and Undertow: a crossfade of the FDN and the engine's chain,
    // which starts from silence when Undertow is entered.
    for (const auto intoUndertow : { true, false })
    {
        const auto label = std::string(intoUndertow ? "Default to Undertow" : "Undertow to Default") + " switch";
        auto parameters = fathomNeutralParameters();
        parameters.mode = ReverbMode::undertow;
        parameters.decaySeconds = 6.0f;
        parameters.evolution = 0.8f;
        auto switched = parameters;
        switched.mode = intoUndertow ? ReverbMode::defaultMode : ReverbMode::undertow;
        auto fdnParameters = parameters;
        fdnParameters.mode = ReverbMode::defaultMode;
        FDNReverb reverb;
        FDNReverb fdnSide;
        reverb.setParameters(switched);
        fdnSide.setParameters(fdnParameters);
        reverb.prepare(sampleRate, blockFrames);
        fdnSide.prepare(sampleRate, blockFrames);
        UndertowReferenceChain undertowSide(parameters, sampleRate);
        FathomMorphRamp undertowAmount(intoUndertow ? 0.0f : 1.0f, intoUndertow ? 1.0f : 0.0f, morphSamples);

        auto largestDistance = 0.0f;
        auto peak = 1.0e-3f;
        for (auto sample = 0; sample < endSample; ++sample)
        {
            if (sample == switchSample)
            {
                switched.mode = intoUndertow ? ReverbMode::undertow : ReverbMode::defaultMode;
                reverb.setParameters(switched);
            }
            if (sample % blockFrames == 0)
                undertowSide.announce(undertowPlugInTransport(sample, sampleRate));
            const auto input = fathomBursts(sample, sampleRate, 0.5f);
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(reverb, sample, sampleRate, left, right);
            auto fdnLeft = input.left;
            auto fdnRight = input.right;
            fdnSide.processSample(fdnLeft, fdnRight);
            auto undertow = FathomEngine::Frame {};
            if (intoUndertow ? sample >= switchSample : sample < switchSample + morphSamples - 1)
                undertow = undertowSide.process(input.left, input.right);
            else
                undertowSide.idle();

            if (sample < switchSample)
            {
                require(intoUndertow ? sameBits(left, fdnLeft) && sameBits(right, fdnRight)
                                     : sameBits(left, undertow.left) && sameBits(right, undertow.right),
                        label + " differs before the switch");
                continue;
            }
            const auto amount = undertowAmount.next();
            if (sample >= switchSample + morphSamples - 1)
            {
                require(intoUndertow ? sameBits(left, undertow.left) && sameBits(right, undertow.right)
                                     : sameBits(left, fdnLeft) && sameBits(right, fdnRight),
                        label + " is not the new Character alone after the crossfade, at sample "
                            + std::to_string(sample));
                continue;
            }
            largestDistance = std::max({
                largestDistance,
                std::abs(left - (fdnLeft + amount * (undertow.left - fdnLeft))),
                std::abs(right - (fdnRight + amount * (undertow.right - fdnRight)))
            });
            peak = std::max({ peak, std::abs(fdnLeft), std::abs(fdnRight),
                              std::abs(undertow.left), std::abs(undertow.right) });
        }
        std::cout << "[METRIC] " << label << ": largest distance from the crossfade="
                  << largestDistance << ", peak of its two sides=" << peak << '\n';
        require(largestDistance <= 4.0e-6f * peak,
                label + " is not a crossfade of the two Characters: distance="
                    + std::to_string(largestDistance / peak) + " of the peak");
    }

    // Left for longer than its fade and selected again, Undertow returns as
    // an engine that has only kept time since it was prepared.
    {
        constexpr auto leaveSample = 50000;
        constexpr auto returnSample = leaveSample + 3 * morphSamples;
        constexpr auto settledSample = returnSample + morphSamples;
        auto parameters = fathomNeutralParameters();
        parameters.mode = ReverbMode::undertow;
        parameters.decaySeconds = 30.0f;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, blockFrames);
        UndertowReferenceChain chain(parameters, sampleRate);
        auto peak = 0.0f;
        for (auto sample = 0; sample < settledSample + 70000; ++sample)
        {
            if (sample == leaveSample || sample == returnSample)
            {
                parameters.mode = sample == leaveSample ? ReverbMode::bloom : ReverbMode::undertow;
                reverb.setParameters(parameters);
            }
            if (sample % blockFrames == 0)
                chain.announce(undertowPlugInTransport(sample, sampleRate));
            const auto input = sample < leaveSample || sample >= returnSample
                ? fathomBursts(sample, sampleRate, 0.5f) : FathomEngine::Frame {};
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(reverb, sample, sampleRate, left, right);
            auto expected = FathomEngine::Frame {};
            if (sample < returnSample)
                chain.idle();
            else
                expected = chain.process(input.left, input.right);
            if (sample < settledSample)
                continue;
            require(sameBits(left, expected.left) && sameBits(right, expected.right),
                    "Undertow selected again is not an engine that only kept time, at sample "
                        + std::to_string(sample));
            peak = std::max(peak, std::abs(left));
        }
        require(peak > 0.01f, "Undertow selected again stayed silent");
    }

    // Every rate, Freeze, input that is no signal, and the value behind the last mode.
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::undertow;
        parameters.mix = 0.6f;
        parameters.decaySeconds = 30.0f;
        parameters.size = FDNReverb::maximumSizeScale;
        parameters.evolution = 1.0f;
        parameters.width = 2.0f;
        parameters.harmony = 0.5f;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(rate, 128);
        auto peak = 0.0f;
        const auto frames = static_cast<int>(rate * 3.0);
        for (auto frame = 0; frame < frames; ++frame)
        {
            if (frame == static_cast<int>(rate * 1.5))
            {
                parameters.freeze = true;
                reverb.setParameters(parameters);
            }
            auto left = frame % 4001 == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.9f * fathomNoise(frame, 40);
            auto right = frame % 5003 == 0 ? std::numeric_limits<float>::infinity() : 0.9f * fathomNoise(frame, 41);
            if (frame % 128 == 0)
            {
                auto transport = undertowPlugInTransport(frame % 100000, rate);
                transport.playing = (frame / 128) % 9 != 0;
                transport.hasTempo = (frame / 128) % 13 != 0;
                reverb.setHostTransport(transport);
                reverb.process(&left, &right, 1);
            }
            else
            {
                reverb.processSample(left, right);
            }
            require(std::isfinite(left) && std::isfinite(right), "Undertow produced NaN/Inf in the plug-in");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }
        require(peak > 0.05f && peak < 4.0f,
                "Undertow stress test left the safety range: peak=" + std::to_string(peak));
    }

    FDNReverb reverb;
    ReverbParameters lastKnown;
    lastKnown.mode = ReverbMode::undertow;
    reverb.setParameters(lastKnown);
    require(reverb.getParameters().mode == ReverbMode::undertow, "Undertow mode was not accepted");
}

// What the layer adds to the two tones over a run under a host that says its
// transport in front of every block: the click measure, the mean square over
// a window of frames, the restarts of its readers, and whether its chunks
// follow the host at the end. `watch` sees the layer in front of every block.
struct UndertowWatched
{
    double click = 0.0;
    double peak = 0.0;
    double windowEnergy = 0.0;
    std::int64_t restarts = 0;
    bool followsHost = false;
    bool finite = true;
    // What was added to the left input over the window, for a comparison.
    std::vector<float> window;
};

template <typename Say, typename Watch>
[[nodiscard]] UndertowWatched watchUndertowLayer(int hostRate, int blockFrames, double seconds,
                                                 double windowFromSeconds, Say&& say, Watch&& watch)
{
    UndertowLayerRig rig(hostRate);
    UndertowWatched run;
    const auto frameCount = static_cast<long long>(seconds * hostRate);
    const auto windowFrom = static_cast<long long>(windowFromSeconds * hostRate);
    std::array<std::array<double, 2>, 2> before {};
    std::array<std::array<double, 2>, 4> added {};
    for (long long frame = 0; frame < frameCount; ++frame)
    {
        if (frame % blockFrames == 0)
        {
            watch(rig.layer, static_cast<double>(frame) / hostRate);
            rig.layer.setTransport(say(static_cast<double>(frame) / hostRate, frame / blockFrames));
        }
        const auto count = rig.frame(undertowTones, added);
        for (auto index = 0; index < count; ++index)
        {
            const auto& now = added[static_cast<std::size_t>(index)];
            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                run.finite = run.finite && std::isfinite(now[channel]);
                run.peak = std::max(run.peak, std::abs(now[channel]));
                run.click = std::max(run.click,
                                     std::abs(now[channel] - 2.0 * before[1][channel] + before[0][channel]));
                if (frame >= windowFrom)
                    run.windowEnergy += now[channel] * now[channel];
            }
            if (frame >= windowFrom)
                run.window.push_back(static_cast<float>(now[0]));
            before[0] = before[1];
            before[1] = now;
        }
    }
    run.restarts = rig.layer.restartCount();
    run.followsHost = rig.layer.followsHost();
    return run;
}

// Ocean's own rule for a tempo in motion. A tempo that keeps changing is a
// ramp and is followed without a new start, however large the change from one
// host block to the next; a change with none near it is a step, and the
// readers begin anew for it once, a quarter second behind it.
void testUndertowTempoRampsAndSteps()
{
    constexpr auto seconds = 11.0;
    static constexpr auto rampFrom = 3.0;
    static constexpr auto rampSeconds = 3.0;
    constexpr auto settleSeconds = 0.25;
    const auto nothing = [](const UndertowLayer&, double) {};

    // Tempo and position of a host at a time: 120 BPM, then a way to 180 BPM
    // over three seconds, then 180 BPM. The position is the integral of the
    // tempo, as a host that counts every sample has it.
    const auto smooth = [](double time)
    {
        const auto along = std::clamp(time - rampFrom, 0.0, rampSeconds);
        const auto behind = std::max(time - rampFrom - rampSeconds, 0.0);
        const auto tempo = 120.0 + 20.0 * along;
        const auto quarters = 2.0 * std::min(time, rampFrom) + (120.0 * along + 10.0 * along * along) / 60.0
                            + 3.0 * behind;
        return undertowTransport(quarters, tempo, true);
    };
    // The same way in stairs of a tenth of a second, as a sequencer may draw it.
    const auto stairs = [](double time)
    {
        constexpr auto stair = 0.1;
        const auto along = std::clamp(time - rampFrom, 0.0, rampSeconds);
        const auto behind = std::max(time - rampFrom - rampSeconds, 0.0);
        const auto whole = std::min(std::floor(along / stair + 1.0e-9), rampSeconds / stair);
        const auto tempo = 120.0 + 2.0 * whole;
        const auto quarters = 2.0 * std::min(time, rampFrom)
                            + stair / 60.0 * (120.0 * whole + whole * (whole - 1.0))
                            + tempo / 60.0 * (along - whole * stair) + 3.0 * behind;
        return undertowTransport(quarters, tempo, true);
    };
    // One step from 120 to 87 BPM.
    const auto step = [](double time)
    {
        const auto behind = std::max(time - rampFrom, 0.0);
        return undertowTransport(2.0 * std::min(time, rampFrom) + 1.45 * behind,
                                 time < rampFrom ? 120.0 : 87.0, true);
    };

    for (const auto hostRate : { 44100, 48000 })
    {
        for (const auto blockFrames : { 64, 512, 2400, 8192 })
        {
            const auto label = "Undertow layer at " + std::to_string(hostRate) + " Hz in blocks of "
                             + std::to_string(blockFrames) + ", ";
            const auto calm = watchUndertowLayer(hostRate, blockFrames, seconds, rampFrom, [](double time, long long)
            {
                return undertowTransport(2.0 * time, 120.0, true);
            }, nothing);
            require(calm.finite && calm.restarts == 0 && calm.windowEnergy > 0.0,
                    label + "a running transport began its readers anew");

            const auto requireFollowed = [&](const UndertowWatched& run, const std::string& name)
            {
                const auto level = 10.0 * std::log10(run.windowEnergy / calm.windowEnergy);
                std::cout << "[METRIC] " << label << name << ": reader restarts=" << run.restarts
                          << ", level against a steady tempo=" << level << " dB, click measure=" << run.click
                          << " (steady " << calm.click << ")\n";
                require(run.finite && run.peak < 2.0, label + name + " left the layer's range");
                // Three readers: one new start each is what a single event
                // costs. A ramp may cost a reader one where a boundary falls
                // into the change of a block; a storm is one at every block.
                require(run.restarts <= 3,
                        label + name + " began the readers anew " + std::to_string(run.restarts) + " times");
                require(level > -3.0 && level < 3.0,
                        label + name + " changed the layer's level by " + std::to_string(level) + " dB");
                require(run.click <= 1.5 * calm.click, label + name + " clicks");
                require(run.followsHost, label + name + " lost the host's position");
            };
            requireFollowed(watchUndertowLayer(hostRate, blockFrames, seconds, rampFrom,
                                               [&](double time, long long) { return smooth(time); }, nothing),
                            "tempo ramp");
            requireFollowed(watchUndertowLayer(hostRate, blockFrames, seconds, rampFrom,
                                               [&](double time, long long) { return stairs(time); }, nothing),
                            "tempo ramp in stairs");

            // A step: every reader begins anew once, when the settle time
            // behind the host's word of it has passed.
            auto firstRestart = -1.0;
            auto restartsThen = std::int64_t { 0 };
            const auto stepped = watchUndertowLayer(hostRate, blockFrames, seconds, rampFrom,
                                                    [&](double time, long long) { return step(time); },
                                                    [&](const UndertowLayer& layer, double time)
            {
                if (firstRestart < 0.0 && layer.restartCount() > 0)
                {
                    firstRestart = time;
                    restartsThen = layer.restartCount();
                }
            });
            const auto blockSeconds = static_cast<double>(blockFrames) / hostRate;
            std::cout << "[METRIC] " << label << "tempo step at " << rampFrom << " s: reader restarts="
                      << stepped.restarts << ", the first seen at " << firstRestart << " s, click measure="
                      << stepped.click << '\n';
            require(stepped.restarts == 3 && restartsThen == 3,
                    label + "a tempo step began the readers anew " + std::to_string(stepped.restarts)
                        + " times, not each of the three once");
            require(firstRestart >= rampFrom + settleSeconds
                        && firstRestart <= rampFrom + settleSeconds + 4.0 * blockSeconds + 0.01,
                    label + "a tempo step began the readers anew at " + std::to_string(firstRestart) + " s");
            require(stepped.finite && stepped.click <= 1.5 * calm.click && stepped.followsHost,
                    label + "a tempo step clicks or loses the host");
        }
    }
}

// Ocean's own rule for a position that tells nothing. A host that says its
// transport runs while its position stands still, is another one at every
// block, or runs at a tempo it does not report, would have every block count
// as a jump. After three jumps in a row the layer keeps time itself, as under
// a stopped transport, and it follows the position again once that has run
// for a second.
void testUndertowPositionThatTellsNothing()
{
    constexpr auto seconds = 12.0;
    constexpr auto windowFrom = 9.0;
    const auto nothing = [](const UndertowLayer&, double) {};

    for (const auto hostRate : { 44100, 48000 })
    {
        for (const auto blockFrames : { 64, 512, 2400, 8192 })
        {
            const auto label = "Undertow layer at " + std::to_string(hostRate) + " Hz in blocks of "
                             + std::to_string(blockFrames) + ", ";
            // What the layer does under a stopped transport, with a tempo and without.
            const auto stopped = watchUndertowLayer(hostRate, blockFrames, seconds, windowFrom, [](double, long long)
            {
                return undertowTransport(0.0, 120.0, false);
            }, nothing);
            const auto calm = watchUndertowLayer(hostRate, blockFrames, seconds, windowFrom, [](double time, long long)
            {
                return undertowTransport(2.0 * time, 120.0, true);
            }, nothing);
            require(stopped.finite && stopped.restarts == 0 && !stopped.followsHost && stopped.windowEnergy > 0.0,
                    label + "the stopped transport is not the measure it should be");

            constexpr std::array<const char*, 3> names {
                "a position that stands while the transport runs", "another position at every block",
                "a position at 90 BPM without a tempo"
            };
            for (std::size_t scenario = 0; scenario < names.size(); ++scenario)
            {
                const auto run = watchUndertowLayer(hostRate, blockFrames, seconds, windowFrom,
                                                    [&](double time, long long block)
                {
                    if (scenario == 0)
                        return undertowTransport(7.25, 120.0, true);
                    if (scenario == 1)
                        return undertowTransport(
                            32.0 + 31.0 * static_cast<double>(fathomNoise(static_cast<int>(block), 50)), 120.0, true);
                    return undertowTransport(1.5 * time, 90.0, true, false);
                }, nothing);

                // Against the stopped transport: the same level, and in the
                // end the same signal, because it is the same clock.
                const auto level = 10.0 * std::log10(run.windowEnergy / stopped.windowEnergy);
                auto distance = 0.0;
                auto reference = 0.0;
                require(run.window.size() == stopped.window.size(), label + "window lengths differ");
                for (std::size_t index = 0; index < run.window.size(); ++index)
                {
                    distance = std::max(distance, static_cast<double>(std::abs(run.window[index] - stopped.window[index])));
                    reference = std::max(reference, static_cast<double>(std::abs(stopped.window[index])));
                }
                std::cout << "[METRIC] " << label << names[scenario] << ": reader restarts=" << run.restarts
                          << ", level against the stopped transport=" << level << " dB, largest distance from it="
                          << distance / reference << " of its peak, click measure=" << run.click
                          << " (running transport " << calm.click << ")\n";
                require(run.finite && run.peak < 2.0, label + names[scenario] + ": left the layer's range");
                require(!run.followsHost, label + names[scenario] + ": the layer still follows the host");
                // Three jumps of three readers, and the start on the layer's own clock.
                require(run.restarts <= 12,
                        label + names[scenario] + ": the readers began anew " + std::to_string(run.restarts) + " times");
                require(level > -0.5 && level < 0.5,
                        label + names[scenario] + ": level " + std::to_string(level) + " dB off the stopped transport's");
                require(distance <= 1.0e-3 * reference,
                        label + names[scenario] + ": not the stopped transport's signal in the end");
                require(run.click <= 1.5 * calm.click, label + names[scenario] + ": clicks");
            }

            // How far a position may run off the tempo does not depend on the
            // block length either: 3 % fast or slow is a drift and is followed
            // without a new start, 5 % is left.
            for (const auto excess : { 0.03, -0.03, 0.05, -0.05 })
            {
                const auto run = watchUndertowLayer(hostRate, blockFrames, seconds, windowFrom,
                                                    [&](double time, long long)
                {
                    return undertowTransport(2.0 * (1.0 + excess) * time, 120.0, true);
                }, nothing);
                const auto level = 10.0 * std::log10(run.windowEnergy / calm.windowEnergy);
                std::cout << "[METRIC] " << label << "a position at " << 100.0 * (1.0 + excess) << " % of the tempo: "
                          << (run.followsHost ? "followed" : "left") << ", reader restarts=" << run.restarts
                          << ", level against a running transport=" << level << " dB, click measure=" << run.click << '\n';
                require(run.finite && run.click <= 1.5 * calm.click,
                        label + "a position that runs off the tempo clicks");
                require(std::abs(excess) < 0.04 ? run.followsHost && run.restarts == 0
                                                : !run.followsHost && run.restarts <= 12,
                        label + "a position at " + std::to_string(100.0 * (1.0 + excess))
                            + " % of the tempo is on the wrong side of the rule");
                require(level > -3.0 && level < 3.0,
                        label + "a position that runs off the tempo changed the layer's level by "
                            + std::to_string(level) + " dB");
            }

            // A position that tells nothing for two seconds and then runs: the
            // layer is back on it a second after it began to run, with one new
            // start of its readers.
            const auto blockSeconds = static_cast<double>(blockFrames) / hostRate;
            auto followedAgain = -1.0;
            auto lostAt = -1.0;
            auto restartsWhileLost = std::int64_t { 0 };
            const auto back = watchUndertowLayer(hostRate, blockFrames, seconds, windowFrom,
                                                 [&](double time, long long block)
            {
                if (time < 2.0)
                    return undertowTransport(
                        32.0 + 31.0 * static_cast<double>(fathomNoise(static_cast<int>(block), 51)), 120.0, true);
                return undertowTransport(5.0 + 2.0 * time, 120.0, true);
            }, [&](const UndertowLayer& layer, double time)
            {
                if (lostAt < 0.0 && time > 0.0 && !layer.followsHost())
                    lostAt = time;
                if (lostAt >= 0.0 && followedAgain < 0.0 && layer.followsHost())
                    followedAgain = time;
                if (followedAgain < 0.0)
                    restartsWhileLost = layer.restartCount();
            });
            std::cout << "[METRIC] " << label << "a position that runs again from 2 s: left at " << lostAt
                      << " s, followed again at " << followedAgain << " s, reader restarts=" << back.restarts << '\n';
            require(lostAt > 0.0 && lostAt <= 4.0 * blockSeconds + 0.02,
                    label + "the layer did not leave a position that tells nothing");
            require(followedAgain >= 3.0 && followedAgain <= 3.0 + 3.0 * blockSeconds + 0.01,
                    label + "the layer followed the host again at " + std::to_string(followedAgain) + " s");
            require(back.followsHost && back.restarts == restartsWhileLost + 3,
                    label + "the return to the host's position is not one new start of the readers");
            require(back.finite && back.click <= 1.5 * calm.click, label + "the return to the host's position clicks");

            // A locate, and a locate straight into the end of a loop: jumps as
            // before. Each begins the readers anew once and the layer stays
            // on the host's position.
            for (const auto jumps : { 1, 2 })
            {
                auto leftTheHost = false;
                const auto located = watchUndertowLayer(hostRate, blockFrames, seconds, windowFrom,
                                                        [&](double time, long long block)
                {
                    const auto jumpBlock = static_cast<long long>(3.0 / blockSeconds);
                    const auto offset = block < jumpBlock ? 0.0 : block < jumpBlock + jumps - 1 ? 40.0 : 17.0;
                    return undertowTransport(offset + 2.0 * time, 120.0, true);
                }, [&](const UndertowLayer& layer, double time)
                {
                    leftTheHost = leftTheHost || (time > 0.0 && !layer.followsHost());
                });
                require(!leftTheHost && located.followsHost,
                        label + std::to_string(jumps) + " jump(s) made the layer leave the host's position");
                // The second of two jumps in a row finds the readers of the
                // first still fading or just begun: each reader begins anew
                // once or twice.
                require(located.restarts >= 3 && located.restarts <= 3 * jumps,
                        label + std::to_string(jumps) + " jump(s) began the readers anew "
                            + std::to_string(located.restarts) + " times");
                require(located.finite && located.click <= 1.5 * calm.click,
                        label + std::to_string(jumps) + " jump(s) click");
            }
        }
    }

    // On the host's position again, the chunks are on the host's note values.
    constexpr std::array<double, 3> noteQuarters { 1.3333333730697632, 2.0, 2.6666667461395264 };
    UndertowLayerRig rig(44100);
    std::array<std::int64_t, 3> counted {};
    auto onGrid = 0;
    for (long long frame = 0; frame < 44100 * 10; ++frame)
    {
        const auto time = static_cast<double>(frame) / 44100.0;
        if (frame % 512 == 0)
            rig.layer.setTransport(time < 2.0
                ? undertowTransport(32.0 + 31.0 * static_cast<double>(fathomNoise(static_cast<int>(frame / 512), 52)),
                                    120.0, true)
                : undertowTransport(5.0 + 2.0 * time, 120.0, true));
        rig.idleFrame();
        for (std::size_t voice = 0; voice < 3; ++voice)
        {
            const auto given = rig.layer.lastChunk(static_cast<UndertowLayer::Voice>(voice));
            if (given.count == counted[voice])
                continue;
            const auto first = counted[voice] == 0;
            counted[voice] = given.count;
            // The chunk a reader begins anew with mirrors about the block it
            // begins at; the chunks behind it are on the grid.
            if (time < 5.0 || first)
                continue;
            const auto quarters = 5.0 + undertowBoundary(given, voice == 1) * 2.0 / 44100.0;
            require(rig.layer.followsHost()
                        && std::abs(quarters - static_cast<double>(given.index) * noteQuarters[voice]) * 22050.0 < 1.0e-6,
                    "Undertow chunk of voice " + std::to_string(voice)
                        + " is off the host's grid after the layer followed the host again");
            ++onGrid;
        }
    }
    require(onGrid >= 9, "Undertow return test saw too few chunks on the host's grid");
}

// ------------------------------------------------------------------- Spume

using amanita::dsp::SpumeDiffuser;
using amanita::dsp::SpumeLayer;

void prepareSpumeEngine(FathomEngine& engine, const FathomEngine::Parameters& parameters,
                        double sampleRate)
{
    engine.setLayer(FathomEngine::Layer::spume);
    engine.setParameters(parameters);
    engine.prepare(sampleRate);
}

[[nodiscard]] FathomEngine::Parameters spumeTestParameters(float macro) noexcept
{
    FathomEngine::Parameters parameters;
    parameters.decaySeconds = 1.5f;
    parameters.sizeScale = 1.2f;
    parameters.preDelaySeconds = 0.004f;
    parameters.macro = macro;
    return parameters;
}

void idleSpume(FathomEngine& engine, int frameCount) noexcept
{
    for (auto frame = 0; frame < frameCount; ++frame)
        engine.advanceIdle();
}

// The diffuser alone against the campaign's model of it: its answer to an
// impulse sample by sample over the first 12000, and by its sums as far as
// 60000, behind the crest of the wash.
void testSpumeDiffuserAgainstTheModel()
{
    namespace golden = amanita::dsp::spumegolden;
    namespace spume = amanita::dsp::spume;

    auto longest = 0;
    auto total = 0;
    for (const auto& stage : spume::delaySamples)
    {
        for (const auto delay : stage)
        {
            longest = std::max(longest, delay);
            total += delay;
        }
    }
    require(longest == spume::longestDelaySamples && total == spume::totalDelaySamples,
            "Spume constants disagree about the delays of the diffuser");
    require(std::abs(spume::gainBlockRetention - std::exp(-44.0 / 441.0)) <= 1.0e-16,
            "Spume gains do not keep what a one-pole of 10 ms keeps over a block");

    const auto reach = golden::diffuserKernelSums[std::size(golden::diffuserKernelSums) - 1].samples;
    SpumeDiffuser diffuser;
    diffuser.prepare();
    std::vector<double> answer(static_cast<std::size_t>(reach));
    for (auto sample = 0; sample < reach; ++sample)
    {
        auto left = 0.0;
        auto right = 0.0;
        diffuser.process(sample == 0 ? 1.0 : 0.0, sample == 0 ? -0.5 : 0.0, left, right);
        // The two inputs go through two of the same.
        require(!(right < -0.5 * left) && !(right > -0.5 * left),
                "Spume diffuser treats its two inputs differently, sample " + std::to_string(sample));
        answer[static_cast<std::size_t>(sample)] = left;
    }

    std::size_t listed = 0;
    auto worstSample = 0.0;
    for (auto sample = 0; sample < golden::diffuserKernelSamples; ++sample)
    {
        auto expected = 0.0;
        if (listed < std::size(golden::diffuserKernel) && golden::diffuserKernel[listed].sample == sample)
            expected = golden::diffuserKernel[listed++].value;
        worstSample = std::max(worstSample,
                               std::abs(answer[static_cast<std::size_t>(sample)] - expected));
    }
    require(listed == std::size(golden::diffuserKernel),
            "Spume golden kernel is not in the order of its samples");

    auto weightedSum = 0.0;
    auto energy = 0.0;
    auto worstSum = 0.0;
    std::size_t row = 0;
    for (auto sample = 0; sample < reach; ++sample)
    {
        const auto value = answer[static_cast<std::size_t>(sample)];
        weightedSum += static_cast<double>(fathomNoise(sample, 0)) * value;
        energy += value * value;
        if (row < std::size(golden::diffuserKernelSums)
            && golden::diffuserKernelSums[row].samples == sample + 1)
        {
            worstSum = std::max({ worstSum,
                                  std::abs(weightedSum - golden::diffuserKernelSums[row].weightedSum),
                                  std::abs(energy - golden::diffuserKernelSums[row].energy) });
            ++row;
        }
    }
    require(row == std::size(golden::diffuserKernelSums), "Spume golden kernel sums were not all reached");

    std::cout << "[METRIC] Spume diffuser against the model: worst sample=" << worstSample
              << ", worst sum=" << worstSum << ", energy of " << reach << " samples=" << energy << '\n';
    require(worstSample <= 1.0e-15,
            "Spume diffuser misses the model's answer to an impulse: " + std::to_string(worstSample));
    require(worstSum <= 1.0e-12,
            "Spume diffuser misses the sums of the model's answer to an impulse: "
                + std::to_string(worstSum));

    // clear() leaves the lines' memory as it is and forgets what they hold all the same.
    for (auto sample = 0; sample < 40000; ++sample)
    {
        auto left = 0.0;
        auto right = 0.0;
        diffuser.process(static_cast<double>(fathomNoise(sample, 70)),
                         static_cast<double>(fathomNoise(sample, 71)), left, right);
    }
    diffuser.clear();
    for (auto sample = 0; sample < reach; ++sample)
    {
        auto left = 0.0;
        auto right = 0.0;
        diffuser.process(sample == 0 ? 1.0 : 0.0, sample == 0 ? -0.5 : 0.0, left, right);
        require(sameBits(left, answer[static_cast<std::size_t>(sample)]),
                "Spume diffuser kept something through clear(), sample " + std::to_string(sample));
    }
}

void testSpumeEngineGoldenVectors()
{
    namespace golden = amanita::dsp::spumegolden;
    namespace spume = amanita::dsp::spume;
    // The model is the campaign's of the reference's Foam mode. The engine
    // stores the signals of its network's lines in single precision and sits
    // about 147 dB under the model on whole renders, as it does without the
    // layer; the model itself meets the reference at -97 to -130 dB.
    constexpr auto nullLimitDb = -130.0;
    // The model names the block of 44 internal samples that first uses each
    // new Macro. Handed over one block late, a change must show.
    constexpr auto lateNullFloorDb = -60.0;
    auto worstNullDb = -400.0;
    auto bestLateNullDb = 0.0;

    // The engine's render of a vector, with every change of Macro handed over
    // `lateSamples` internal samples behind its block.
    const auto render = [](const golden::Vector& vector, int frameCount, long long lateSamples)
    {
        FathomEngine::Parameters parameters;
        parameters.decaySeconds = golden::decaySeconds;
        parameters.sizeScale = golden::sizeScale;
        parameters.macro = vector.macro;
        FathomEngine::ClockOrigins origins;
        origins.firstFrame = vector.firstFrame;
        origins.oscillators = vector.origin;
        FathomEngine engine;
        engine.setClockOriginsForTesting(origins);
        prepareSpumeEngine(engine, parameters, static_cast<double>(vector.hostRate));

        const auto programmeFrames = static_cast<int>(vector.programme.size() / 2);
        std::size_t change = 0;
        std::vector<float> rendered;
        rendered.reserve(2 * static_cast<std::size_t>(frameCount));
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            // The layer takes a new Macro at the block that begins next, so a
            // value meant for the block at a given sample is handed over once
            // the block in front of it has begun.
            for (; change < vector.changes.size()
                   && engine.nextCoreSampleForTesting()
                          > vector.changes[change].firstBlockSample + lateSamples - spume::gainBlockSamples;
                 ++change)
            {
                parameters.macro = vector.changes[change].macro;
                engine.setParameters(parameters);
            }

            FathomEngine::Frame input;
            for (const auto& impulse : vector.impulses)
                if (impulse.frame == frame)
                    (impulse.channel == 0 ? input.left : input.right) += impulse.amplitude;
            const auto programmeFrame = frame - vector.programmeFirstFrame;
            if (programmeFrame >= 0 && programmeFrame < programmeFrames)
            {
                input.left += vector.programme[2 * static_cast<std::size_t>(programmeFrame)];
                input.right += vector.programme[2 * static_cast<std::size_t>(programmeFrame) + 1];
            }
            if (frame < vector.noiseFrames)
            {
                input.left += golden::noiseAmplitude * fathomNoise(frame, golden::noiseChannel);
                input.right += golden::noiseAmplitude * fathomNoise(frame, golden::noiseChannel + 1);
            }
            const auto wet = engine.processSample(input.left, input.right);
            rendered.push_back(wet.left);
            rendered.push_back(wet.right);
        }
        return rendered;
    };

    for (const auto& vector : golden::vectors)
    {
        auto frameCount = 0;
        for (const auto& excerpt : vector.excerpts)
            frameCount = std::max(frameCount,
                                  excerpt.firstFrame + static_cast<int>(excerpt.model.size() / 2));

        const auto rendered = render(vector, frameCount, 0);
        for (const auto& excerpt : vector.excerpts)
        {
            const auto nullDb = fathomNullDb(
                std::span<const float>(rendered).subspan(
                    2 * static_cast<std::size_t>(excerpt.firstFrame), excerpt.model.size()),
                excerpt.model);
            worstNullDb = std::max(worstNullDb, nullDb);
            require(nullDb <= nullLimitDb,
                    std::string("Spume engine misses the golden vector ") + vector.name
                        + " from frame " + std::to_string(excerpt.firstFrame)
                        + ": null=" + std::to_string(nullDb) + " dB");
        }

        if (vector.changes.empty())
            continue;
        const auto late = render(vector, frameCount, spume::gainBlockSamples);
        for (const auto& excerpt : vector.excerpts)
        {
            const auto nullDb = fathomNullDb(
                std::span<const float>(late).subspan(
                    2 * static_cast<std::size_t>(excerpt.firstFrame), excerpt.model.size()),
                excerpt.model);
            bestLateNullDb = std::min(bestLateNullDb, nullDb);
            require(nullDb >= lateNullFloorDb,
                    std::string("Spume golden vector ") + vector.name + " from frame "
                        + std::to_string(excerpt.firstFrame)
                        + " does not tell the block of a change from the next: null="
                        + std::to_string(nullDb) + " dB");
        }
    }

    std::cout << "[METRIC] Spume golden vectors: worst null against the model=" << worstNullDb
              << " dB, limit=" << nullLimitDb << " dB; Macro handed over a block late="
              << bestLateNullDb << " dB at best\n";
}

// At Macro 0 the plain gain rests at one and the diffused one at zero, and
// the layer hands the network its input as it is: the engine is Fathom's at
// Macro 0 to the bit.
void testSpumeIsFathomAtEvolutionZero()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Spume at Evolution 0, " + std::to_string(static_cast<int>(sampleRate))
                         + " Hz: ";
        const auto frameCount = static_cast<int>(sampleRate * 1.6);
        const auto parameters = spumeTestParameters(0.0f);

        FathomEngine tide;
        tide.setParameters(parameters);
        tide.prepare(sampleRate);
        const auto expected = renderFathomProgramme(tide, frameCount);

        FathomEngine spume;
        prepareSpumeEngine(spume, parameters, sampleRate);
        requireSameFathomRender(renderFathomProgramme(spume, frameCount), expected,
                                label + "fresh instance");

        // Macro came down from 100 %: once the gains have arrived and the
        // network has fallen silent the engine is Fathom's again, whatever the
        // diffuser still holds.
        auto loud = parameters;
        loud.decaySeconds = 0.2f;
        loud.macro = 1.0f;
        auto quiet = loud;
        quiet.macro = 0.0f;
        FathomEngine visited;
        prepareSpumeEngine(visited, loud, sampleRate);
        FathomEngine plain;
        plain.setParameters(quiet);
        plain.prepare(sampleRate);
        const auto visitFrames = static_cast<int>(sampleRate * 0.5);
        const auto restFrames = static_cast<int>(sampleRate * 9.0);
        auto lastSound = -1;
        for (auto frame = 0; frame < visitFrames + restFrames; ++frame)
        {
            if (frame == visitFrames)
                visited.setParameters(quiet);
            const auto input = frame < visitFrames ? fathomProgramme(frame % 3000) : FathomEngine::Frame {};
            const auto wet = visited.processSample(input.left, input.right);
            static_cast<void>(plain.processSample(input.left, input.right));
            if (std::abs(wet.left) > 0.0f || std::abs(wet.right) > 0.0f)
                lastSound = frame;
        }
        require(lastSound >= visitFrames && lastSound < visitFrames + static_cast<int>(sampleRate * 8.0),
                label + "the engine did not fall silent behind Macro 100 %");
        requireSameFathomRender(renderFathomProgramme(visited, frameCount),
                                renderFathomProgramme(plain, frameCount),
                                label + "settled after a visit at Macro 100 %");
    }

    // Through the plug-in: a settled Spume at Evolution 0 is a settled Fathom
    // and a settled Undertow.
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;
    auto parameters = fathomNeutralParameters();
    parameters.evolution = 0.0f;
    parameters.preDelayMs = 7.0f;
    parameters.width = 1.3f;
    parameters.mix = 0.8f;
    constexpr std::array<ReverbMode, 3> modes { ReverbMode::fathom, ReverbMode::undertow, ReverbMode::spume };
    std::array<FDNReverb, 3> reverbs;
    for (std::size_t index = 0; index < modes.size(); ++index)
    {
        auto own = parameters;
        own.mode = modes[index];
        reverbs[index].setParameters(own);
        reverbs[index].prepare(sampleRate, blockSize);
    }
    std::array<std::array<float, blockSize>, 3> left {};
    std::array<std::array<float, blockSize>, 3> right {};
    auto peak = 0.0f;
    for (auto block = 0; block < 150; ++block)
    {
        for (std::size_t frame = 0; frame < blockSize; ++frame)
        {
            const auto input = fathomBursts(block * blockSize + static_cast<int>(frame), sampleRate);
            for (std::size_t index = 0; index < modes.size(); ++index)
            {
                left[index][frame] = input.left;
                right[index][frame] = input.right;
            }
        }
        HostTransport transport;
        transport.quarterNotes = 3.0 + block * blockSize / sampleRate * 2.0;
        transport.bpm = 120.0;
        transport.playing = true;
        transport.hasTempo = true;
        for (std::size_t index = 0; index < modes.size(); ++index)
        {
            reverbs[index].setHostTransport(transport);
            reverbs[index].process(left[index].data(), right[index].data(), blockSize);
        }
        for (std::size_t frame = 0; frame < blockSize; ++frame)
        {
            require(sameBits(left[2][frame], left[0][frame]) && sameBits(right[2][frame], right[0][frame]),
                    "A settled Spume at Evolution 0 is not a settled Fathom at Evolution 0");
            require(sameBits(left[2][frame], left[1][frame]) && sameBits(right[2][frame], right[1][frame]),
                    "A settled Spume at Evolution 0 is not a settled Undertow at Evolution 0");
            peak = std::max(peak, std::abs(left[2][frame]));
        }
    }
    require(peak > 0.01f, "The Evolution 0 comparison of Spume, Fathom and Undertow saw no signal");
}

void testSpumeEngineDeterminismAndReset()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    for (const auto sampleRate : sampleRates)
    {
        const auto label = "Spume engine at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        const auto warmupFrames = static_cast<int>(sampleRate * 0.35);
        const auto programmeFrames = static_cast<int>(sampleRate * 1.9);
        const auto parameters = spumeTestParameters(1.0f);

        const auto excite = [](FathomEngine& engine, int frames)
        {
            for (auto frame = 0; frame < frames; ++frame)
                static_cast<void>(engine.processSample(0.3f * fathomNoise(frame, 2),
                                                       0.3f * fathomNoise(frame, 3)));
        };

        FathomEngine fresh;
        prepareSpumeEngine(fresh, parameters, sampleRate);
        idleSpume(fresh, warmupFrames);
        const auto expected = renderFathomProgramme(fresh, programmeFrames);

        // The layer is in the render: Macro 0 gives another one.
        {
            FathomEngine plain;
            prepareSpumeEngine(plain, spumeTestParameters(0.0f), sampleRate);
            idleSpume(plain, warmupFrames);
            const auto base = renderFathomProgramme(plain, programmeFrames);
            auto layerEnergy = 0.0;
            auto baseEnergy = 0.0;
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                require(std::isfinite(expected[index]), label + "the programme produced NaN/Inf");
                const auto difference = static_cast<double>(expected[index]) - base[index];
                layerEnergy += difference * difference;
                baseEnergy += static_cast<double>(base[index]) * base[index];
            }
            require(baseEnergy > 1.0e-6 && layerEnergy > 0.05 * baseEnergy,
                    label + "Macro 100 % left the programme as Macro 0 has it");
        }
        {
            FathomEngine second;
            prepareSpumeEngine(second, parameters, sampleRate);
            idleSpume(second, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(second, programmeFrames), expected,
                                    label + "second instance");
        }
        {
            FathomEngine used;
            prepareSpumeEngine(used, parameters, sampleRate);
            excite(used, 9000);
            used.reset();
            idleSpume(used, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(used, programmeFrames), expected,
                                    label + "after reset()");
        }
        {
            FathomEngine prepared;
            prepareSpumeEngine(prepared, parameters, sampleRate * 2.0);
            excite(prepared, 9000);
            prepared.prepare(sampleRate);
            idleSpume(prepared, warmupFrames);
            requireSameFathomRender(renderFathomProgramme(prepared, programmeFrames), expected,
                                    label + "after prepare() at another rate");
        }
        {
            // Silence in gives exact silence out at every Macro: the layer
            // has no noise of its own.
            FathomEngine silent;
            prepareSpumeEngine(silent, parameters, sampleRate);
            for (auto frame = 0; frame < warmupFrames; ++frame)
            {
                const auto wet = silent.processSample(0.0f, 0.0f);
                require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                        label + "silence in did not give silence out");
            }
            requireSameFathomRender(renderFathomProgramme(silent, programmeFrames), expected,
                                    label + "processed silence against advanceIdle()");
        }
        {
            // The layer does not hear the host's transport.
            FathomEngine told;
            prepareSpumeEngine(told, parameters, sampleRate);
            idleSpume(told, warmupFrames);
            std::vector<float> rendered;
            rendered.reserve(2 * static_cast<std::size_t>(programmeFrames));
            for (auto frame = 0; frame < programmeFrames; ++frame)
            {
                if (frame % 96 == 0)
                {
                    FathomEngine::Transport transport;
                    transport.hasTempo = (frame / 96) % 5 != 0;
                    transport.bpm = 60.0 + static_cast<double>(frame % 700);
                    transport.playing = (frame / 96) % 3 != 0;
                    transport.quarterNotes = static_cast<double>(frame % 9000) * 0.01;
                    told.setTransport(transport);
                }
                const auto input = fathomProgramme(frame);
                const auto wet = told.processSample(input.left, input.right);
                rendered.push_back(wet.left);
                rendered.push_back(wet.right);
            }
            requireSameFathomRender(rendered, expected, label + "under a host that says its transport");
        }

        // Left idle after sound, the engine starts from silence, the diffuser
        // empty, with its blocks where a fresh instance has them.
        for (const auto idleFrames : { 1, 2, 3000 })
        {
            constexpr auto soundFrames = 31000;
            FathomEngine reference;
            prepareSpumeEngine(reference, parameters, sampleRate);
            idleSpume(reference, soundFrames + idleFrames);
            const auto afterIdle = renderFathomProgramme(reference, programmeFrames);

            FathomEngine sounded;
            prepareSpumeEngine(sounded, parameters, sampleRate);
            excite(sounded, soundFrames);
            idleSpume(sounded, idleFrames);
            requireSameFathomRender(renderFathomProgramme(sounded, programmeFrames), afterIdle,
                                    label + "after sound and " + std::to_string(idleFrames) + " idle frames");
        }

        // The diffuser takes the input behind the pre-delay, as the network
        // does: Pre Delay moves the whole of the wet and nothing within it.
        {
            auto delayedParameters = parameters;
            delayedParameters.preDelaySeconds = 0.05f;
            auto plainParameters = parameters;
            plainParameters.preDelaySeconds = 0.0f;
            // The reference's pre-delay is one frame short of its time.
            const auto delayFrames = static_cast<int>(
                std::floor(50.0f * static_cast<float>(sampleRate) / 1000.0f)) - 1;
            FathomEngine delayed;
            FathomEngine early;
            prepareSpumeEngine(delayed, delayedParameters, sampleRate);
            prepareSpumeEngine(early, plainParameters, sampleRate);
            auto peak = 0.0f;
            for (auto frame = 0; frame < programmeFrames; ++frame)
            {
                const auto now = fathomProgramme(frame);
                const auto then = frame >= delayFrames ? fathomProgramme(frame - delayFrames)
                                                       : FathomEngine::Frame {};
                const auto first = delayed.processSample(now.left, now.right);
                const auto second = early.processSample(then.left, then.right);
                require(sameBits(first.left, second.left) && sameBits(first.right, second.right),
                        label + "Pre Delay is not a delay of the layer's input, frame "
                            + std::to_string(frame));
                peak = std::max({ peak, std::abs(first.left), std::abs(first.right) });
            }
            require(peak > 1.0e-3f, label + "the Pre Delay comparison saw no signal");
        }

        // Held, the network takes no input, and the layer is part of the
        // input: once Freeze has arrived nothing the engine is given changes
        // what it holds, and the diffuser's wash of what came before stays out.
        {
            const auto holdFrame = static_cast<int>(sampleRate * 0.6);
            const auto partFrame = static_cast<int>(sampleRate * 0.75);
            const auto endFrame = static_cast<int>(sampleRate * 1.75);
            FathomEngine fed;
            FathomEngine starved;
            prepareSpumeEngine(fed, parameters, sampleRate);
            prepareSpumeEngine(starved, parameters, sampleRate);
            auto held = parameters;
            held.freeze = true;
            auto heldPeak = 0.0f;
            for (auto frame = 0; frame < endFrame; ++frame)
            {
                if (frame == holdFrame)
                {
                    fed.setParameters(held);
                    starved.setParameters(held);
                }
                const auto shared = frame < partFrame;
                const auto left = 0.3f * fathomNoise(frame, 2);
                const auto right = 0.3f * fathomNoise(frame, 3);
                const auto first = fed.processSample(left, right);
                const auto second = starved.processSample(shared ? left : 0.0f, shared ? right : 0.0f);
                require(sameBits(first.left, second.left) && sameBits(first.right, second.right),
                        label + "input reaches a held network through the layer, frame " + std::to_string(frame));
                if (!shared)
                    heldPeak = std::max({ heldPeak, std::abs(first.left), std::abs(first.right) });
            }
            require(heldPeak > 1.0e-3f, label + "Freeze held nothing");
        }
    }
}

// Nothing in Spume counts the host's blocks: the layer's own blocks of 44
// internal samples count frames. However the host cuts its calls and whatever
// it says of its transport, the output is the same, also while Evolution moves.
void testSpumeHostBlockInvariance()
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto label = "Spume at " + std::to_string(static_cast<int>(sampleRate)) + " Hz, ";
        const auto frameCount = static_cast<int>(sampleRate * 1.5);
        // Evolution moves in front of these frames; the third move comes
        // before the gains have settled on the second.
        const std::array<std::pair<int, float>, 5> moves {{
            { static_cast<int>(sampleRate * 0.30), 0.2f },
            { static_cast<int>(sampleRate * 0.55), 1.0f },
            { static_cast<int>(sampleRate * 0.55) + 173, 0.6f },
            { static_cast<int>(sampleRate * 0.90), 0.0f },
            { static_cast<int>(sampleRate * 1.20), 0.85f }
        }};

        const auto render = [&](int blockFrames, bool saysTransport)
        {
            ReverbParameters parameters;
            parameters.mode = ReverbMode::spume;
            parameters.mix = 0.7f;
            parameters.decaySeconds = 3.0f;
            parameters.preDelayMs = 11.0f;
            parameters.evolution = 0.8f;
            parameters.width = 1.2f;
            FDNReverb reverb;
            reverb.setParameters(parameters);
            reverb.prepare(sampleRate, blockFrames);

            std::vector<float> left(static_cast<std::size_t>(frameCount));
            std::vector<float> right(static_cast<std::size_t>(frameCount));
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                const auto input = fathomBursts(frame, sampleRate, 0.5f);
                left[static_cast<std::size_t>(frame)] = input.left;
                right[static_cast<std::size_t>(frame)] = input.right;
            }

            std::size_t move = 0;
            for (auto offset = 0; offset < frameCount;)
            {
                for (; move < moves.size() && moves[move].first == offset; ++move)
                {
                    parameters.evolution = moves[move].second;
                    reverb.setParameters(parameters);
                }
                // A call ends where Evolution moves next.
                auto count = std::min(blockFrames, frameCount - offset);
                if (move < moves.size())
                    count = std::min(count, moves[move].first - offset);
                if (saysTransport)
                {
                    HostTransport transport;
                    transport.quarterNotes = 3.0 + static_cast<double>(offset % 7001) * 0.013;
                    transport.bpm = 60.0 + static_cast<double>(offset % 190);
                    transport.playing = (offset / blockFrames) % 3 != 0;
                    transport.hasTempo = (offset / blockFrames) % 11 != 0;
                    reverb.setHostTransport(transport);
                }
                reverb.process(left.data() + offset, right.data() + offset, count);
                offset += count;
            }

            std::vector<float> rendered;
            rendered.reserve(2 * static_cast<std::size_t>(frameCount));
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                rendered.push_back(left[static_cast<std::size_t>(frame)]);
                rendered.push_back(right[static_cast<std::size_t>(frame)]);
            }
            return rendered;
        };

        const auto expected = render(1, false);
        auto peak = 0.0f;
        for (const auto value : expected)
            peak = std::max(peak, std::abs(value));
        require(peak > 0.05f, label + "the block length programme stayed silent");
        for (const auto blockFrames : { 32, 127, 512, 1000 })
            requireSameFathomRender(render(blockFrames, false), expected,
                                    label + "host blocks of " + std::to_string(blockFrames) + " frames");
        requireSameFathomRender(render(64, true), expected, label + "a host that says its transport");
    }
}

// The layer on its own at the internal rate, beside a diffuser that takes the
// same input: what the layer hands the network is its input and that
// diffuser's output, each times its gain.
struct SpumeLayerRig
{
    explicit SpumeLayerRig(double macro)
    {
        layer.prepare();
        layer.setMacro(macro, true);
        diffuser.prepare();
    }

    // One internal sample.
    void process(const std::array<double, 2>& input) noexcept
    {
        plain = input;
        diffuser.process(input[0], input[1], diffused[0], diffused[1]);
        output = input;
        layer.process(output[0], output[1]);
        ++samples;
    }

    SpumeLayer layer;
    SpumeDiffuser diffuser;
    std::array<double, 2> plain {};
    std::array<double, 2> diffused {};
    std::array<double, 2> output {};
    // Samples taken so far: the index of the next one in the layer's count.
    long long samples = 0;
};

// What may come in front of a sample of a run of the layer.
struct SpumeMove
{
    double macro = -1.0;   // below zero: Macro stays
    bool atOnce = false;
};

struct SpumeRun
{
    // Largest second difference of what the layer hands on: for a tone of
    // amplitude a and angular frequency w per sample it is a w^2, and a step
    // of height h gives h itself.
    double click = 0.0;
    // Largest distance of what the layer hands on from the law of its gains.
    double lawDistance = 0.0;
    double plainPeak = 0.0;
    double diffusedPeak = 0.0;
    bool finite = true;
};

// The layer over two steady tones while `move` moves Macro, against the law
// in the test's own words: the plain gain goes to cos(pi/2 Macro) and the
// diffused one to sin(pi/2 Macro); at the first sample of a block of 44,
// samples 43 modulo 44 of the layer's count, each keeps exp(-44/441) of its
// distance from where it is going, and it is held over the block.
template <typename Move>
[[nodiscard]] SpumeRun runSpumeLayer(double firstMacro, long long sampleCount, Move&& move)
{
    constexpr auto halfPi = 1.57079632679489661923;
    const auto retention = std::exp(-44.0 / 441.0);
    auto plainTarget = std::cos(halfPi * firstMacro);
    auto diffusedTarget = std::sin(halfPi * firstMacro);
    auto plainGain = plainTarget;
    auto diffusedGain = diffusedTarget;

    SpumeLayerRig rig(firstMacro);
    SpumeRun run;
    std::array<std::array<double, 2>, 2> before {};
    for (long long sample = 0; sample < sampleCount; ++sample)
    {
        const SpumeMove moved = move(sample);
        if (!(moved.macro < 0.0))
        {
            rig.layer.setMacro(moved.macro, moved.atOnce);
            plainTarget = std::cos(halfPi * moved.macro);
            diffusedTarget = std::sin(halfPi * moved.macro);
            if (moved.atOnce)
            {
                plainGain = plainTarget;
                diffusedGain = diffusedTarget;
            }
        }
        if (sample % 44 == 43)
        {
            plainGain = plainTarget + (plainGain - plainTarget) * retention;
            diffusedGain = diffusedTarget + (diffusedGain - diffusedTarget) * retention;
        }

        rig.process(undertowTones(sample));
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            const auto now = rig.output[channel];
            run.finite = run.finite && std::isfinite(now);
            run.lawDistance = std::max(
                run.lawDistance,
                std::abs(now - (plainGain * rig.plain[channel] + diffusedGain * rig.diffused[channel])));
            run.click = std::max(run.click,
                                 std::abs(now - 2.0 * before[1][channel] + before[0][channel]));
            run.plainPeak = std::max(run.plainPeak, std::abs(rig.plain[channel]));
            run.diffusedPeak = std::max(run.diffusedPeak, std::abs(rig.diffused[channel]));
        }
        before[0] = before[1];
        before[1] = rig.output;
    }
    return run;
}

// Macro in motion. The two gains, not Macro, are what moves, by the law the
// campaign measured: a one-pole of 10 ms run once per block of 44 internal
// samples and held over it. Ocean's own is only where a new Macro enters: at
// the first block that begins once it has arrived.
void testSpumeEvolutionStepsAndRamps()
{
    namespace spume = amanita::dsp::spume;
    constexpr auto halfPi = 1.57079632679489661923;
    const auto retention = std::exp(-44.0 / 441.0);

    // From the output alone. Both inputs are constant and the diffuser has
    // settled on them, where its four paths add up to twice the input: what
    // the layer hands on is then a constant times each gain, and every step
    // of it is a step of the gains.
    {
        constexpr auto level = 0.25;
        constexpr std::array<double, 2> constant { level, -0.5 * level };
        SpumeLayerRig rig(1.0);
        for (auto sample = 0; sample < 14 * 44100; ++sample)
            rig.process(constant);
        require(std::abs(rig.diffused[0] - 2.0 * level) <= 1.0e-13
                    && std::abs(rig.diffused[1] + level) <= 1.0e-13,
                "Spume diffuser did not settle on twice a constant input");

        // How far into a block of 44 a change arrives, and the Macro it brings.
        struct Step
        {
            int intoBlock;
            double macro;
        };
        constexpr std::array<Step, 6> steps {{
            { 10, 0.3 }, { 0, 0.0 }, { 43, 1.0 }, { 21, 0.62 }, { 1, 0.07 }, { 30, 0.0 }
        }};
        constexpr auto settledBlocks = 400;
        auto worstStair = 0.0;
        auto largestStep = 0.0;
        for (const auto& step : steps)
        {
            while ((rig.samples - spume::gainBlockPhase) % spume::gainBlockSamples != step.intoBlock)
                rig.process(constant);
            const auto from = rig.output;
            const auto gain = std::cos(halfPi * step.macro) + 2.0 * std::sin(halfPi * step.macro);
            const std::array<double, 2> target { constant[0] * gain, constant[1] * gain };

            rig.layer.setMacro(step.macro, false);
            auto blocks = 0;
            auto last = from;
            for (auto sample = 0; sample < settledBlocks * spume::gainBlockSamples; ++sample)
            {
                // The sample that comes next begins a block.
                const auto begins = (rig.samples - spume::gainBlockPhase) % spume::gainBlockSamples == 0;
                if (begins)
                    ++blocks;
                rig.process(constant);
                const auto kept = std::pow(retention, blocks);
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    worstStair = std::max(
                        worstStair,
                        std::abs(rig.output[channel] - (target[channel] + (from[channel] - target[channel]) * kept)));
                    // Within a block the gains are held.
                    require(begins || std::abs(rig.output[channel] - last[channel]) <= 1.0e-15,
                            "Spume gains move inside a block of 44 samples");
                    largestStep = std::max(largestStep,
                                           std::abs(rig.output[channel] - last[channel])
                                               / std::max(1.0e-300, std::abs(from[channel] - target[channel])));
                }
                last = rig.output;
            }
            require(std::abs(rig.output[0] - target[0]) <= 1.0e-13
                        && std::abs(rig.output[1] - target[1]) <= 1.0e-13,
                    "Spume gains did not arrive at Macro " + std::to_string(step.macro));
            if (!(step.macro > 0.0))
                require(rig.layer.atRest() && sameBits(rig.output[0], constant[0])
                            && sameBits(rig.output[1], constant[1]),
                        "Spume layer does not come to rest at Macro 0");
        }
        std::cout << "[METRIC] Spume gain steps on a constant: worst distance from the one-pole of 10 ms at "
                     "the block rate=" << worstStair << ", largest step=" << largestStep
                  << " of the way (1 - exp(-44/441)=" << 1.0 - retention << ")\n";
        require(worstStair <= 2.0e-12,
                "Spume gains do not follow a one-pole of 10 ms run once per block of 44 samples: "
                    + std::to_string(worstStair));
        require(largestStep <= (1.0 - retention) * (1.0 + 1.0e-9),
                "Spume gains take a larger step than the law gives them");
    }

    // Two tones through steps of Macro and through a ramp of it.
    constexpr long long sampleCount = 441000;
    constexpr std::array<double, 8> positions { 0.0, 1.0, 0.3, 0.7, 0.05, 0.62, 0.0, 1.0 };
    const auto stepsOf = [&](bool atOnce)
    {
        return [&positions, atOnce](long long sample)
        {
            constexpr long long first = 9000;
            constexpr long long apart = 17641;
            if (sample < first || (sample - first) % apart != 0)
                return SpumeMove {};
            return SpumeMove { positions[static_cast<std::size_t>((sample - first) / apart) % positions.size()],
                               atOnce };
        };
    };
    const auto calm = runSpumeLayer(0.62, sampleCount, [](long long) { return SpumeMove {}; });
    const auto stepped = runSpumeLayer(1.0, sampleCount, stepsOf(false));
    const auto switched = runSpumeLayer(1.0, sampleCount, stepsOf(true));
    // A host that automates Evolution in blocks of 256 frames: up over three
    // seconds, down to 20 % over two.
    const auto ramped = runSpumeLayer(0.0, sampleCount, [](long long sample)
    {
        constexpr long long first = 44100;
        if (sample < first || (sample - first) % 256 != 0)
            return SpumeMove {};
        const auto seconds = static_cast<double>(sample - first) / 44100.0;
        return SpumeMove { seconds < 3.0 ? seconds / 3.0 : std::max(0.2, 1.0 - 0.4 * (seconds - 3.0)), false };
    });

    std::cout << "[METRIC] Spume Macro in motion: click measure at rest=" << calm.click
              << ", steps=" << stepped.click << ", steps taken at once=" << switched.click
              << ", ramp=" << ramped.click << "; distance from the law, steps=" << stepped.lawDistance
              << ", ramp=" << ramped.lawDistance << '\n';
    require(calm.finite && stepped.finite && switched.finite && ramped.finite,
            "Spume layer produced NaN/Inf under Macro in motion");
    require(calm.diffusedPeak > 0.1 && calm.lawDistance <= 1.0e-11,
            "Spume layer at rest is not its input and the diffused one by their gains");
    require(stepped.lawDistance <= 1.0e-11 && ramped.lawDistance <= 1.0e-11,
            "Spume gains do not follow the measured law under Macro in motion");
    // No step of Macro puts more into a sample than the law's share of the
    // way; taken at once the same steps are several times that.
    require(stepped.click <= calm.click + (1.0 - retention) * (stepped.plainPeak + stepped.diffusedPeak),
            "Spume Macro steps click");
    require(switched.click >= 3.0 * stepped.click, "Spume click measure does not see a step taken at once");
    require(ramped.click <= 2.0 * calm.click, "Spume Macro ramp clicks");

    // The engine through steps of Macro on noise.
    FathomEngine engine;
    auto parameters = spumeTestParameters(0.0f);
    prepareSpumeEngine(engine, parameters, 48000.0);
    auto enginePeak = 0.0f;
    for (auto frame = 0; frame < 240000; ++frame)
    {
        if (frame % 7919 == 0)
        {
            parameters.macro = static_cast<float>((frame / 7919) % 6) / 5.0f;
            engine.setParameters(parameters);
        }
        const auto wet = engine.processSample(0.4f * fathomNoise(frame, 32), 0.4f * fathomNoise(frame, 33));
        require(std::isfinite(wet.left) && std::isfinite(wet.right), "Spume Macro steps produced NaN/Inf");
        enginePeak = std::max({ enginePeak, std::abs(wet.left), std::abs(wet.right) });
    }
    require(enginePeak > 0.05f && enginePeak < 8.0f, "Spume engine left its range under Macro steps");
}

void testSpumeSilenceDenormalsAndHostileInput()
{
    // The longest all-pass of the diffuser loses 11 dB a pass of 0.4 s and no
    // more. Its lines hold nothing under a floor, so the wash ends in exact
    // silence and never reaches the denormal range.
    {
        SpumeDiffuser diffuser;
        diffuser.prepare();
        auto lastSound = -1;
        auto denormals = 0;
        for (auto sample = 0; sample < 44100 * 40; ++sample)
        {
            const auto input = sample < 22050 ? undertowTones(sample) : std::array<double, 2> { 0.0, 0.0 };
            auto left = 0.0;
            auto right = 0.0;
            diffuser.process(input[0], input[1], left, right);
            for (const auto value : { left, right })
            {
                if (std::fpclassify(value) == FP_SUBNORMAL)
                    ++denormals;
                if (std::abs(value) > 0.0)
                    lastSound = sample;
            }
        }
        std::cout << "[METRIC] Spume diffuser: last sound " << static_cast<double>(lastSound) / 44100.0
                  << " s behind the start of half a second of input\n";
        require(denormals == 0, "Spume diffuser lets its lines reach the denormal range");
        require(lastSound > 44100 * 10 && lastSound < 44100 * 35,
                "Spume diffuser did not fall silent behind its input: last sound at sample "
                    + std::to_string(lastSound));
    }

    constexpr std::array<float, 8> hostileSamples {
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
        -1.0e30f, std::numeric_limits<float>::denorm_min(), 1.0e-39f, -0.0f
    };
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto label = "Spume engine at " + std::to_string(static_cast<int>(sampleRate)) + " Hz ";

        // Input that is all denormal is silence to the engine.
        {
            FathomEngine engine;
            prepareSpumeEngine(engine, spumeTestParameters(1.0f), sampleRate);
            for (auto frame = 0; frame < 30000; ++frame)
            {
                const auto wet = engine.processSample(frame % 2 == 0 ? 1.0e-39f : -1.0e-41f,
                                                      std::numeric_limits<float>::denorm_min());
                require(!(std::abs(wet.left) > 0.0f) && !(std::abs(wet.right) > 0.0f),
                        label + "turned denormal input into sound");
            }
        }

        auto parameters = spumeTestParameters(1.0f);
        parameters.decaySeconds = 30.0f;
        parameters.sizeScale = 2.0f;
        parameters.preDelaySeconds = 0.25f;
        FathomEngine engine;
        FathomEngine::LevelStage levelStage;
        prepareSpumeEngine(engine, parameters, sampleRate);
        levelStage.prepare(sampleRate);
        auto peak = 0.0f;
        const auto frames = static_cast<int>(sampleRate * 1.5);
        for (auto frame = 0; frame < frames; ++frame)
        {
            if (frame % 997 == 0)
            {
                const auto step = frame / 997;
                parameters.decaySeconds = step % 3 == 0 ? 0.2f : 60.0f;
                parameters.sizeScale = step % 2 == 0 ? 0.15f : 2.0f;
                parameters.macro = step % 4 == 0 ? 0.0f : step % 4 == 1 ? 1.0f
                                 : step % 4 == 2 ? 0.5f : std::numeric_limits<float>::quiet_NaN();
                parameters.freeze = step % 6 == 5;
                engine.setParameters(parameters);
            }
            auto left = fathomNoise(frame, 4);
            auto right = fathomNoise(frame, 5);
            if (frame % 61 == 0)
                left = hostileSamples[static_cast<std::size_t>(frame / 61) % hostileSamples.size()];
            if (frame % 89 == 0)
                right = hostileSamples[static_cast<std::size_t>(frame / 89) % hostileSamples.size()];
            auto wet = levelStage.process(left, right, engine.processSample(left, right));
            wet = FathomEngine::applyWidth(wet, 2.0f);
            wet.left = FathomEngine::clip(wet.left);
            wet.right = FathomEngine::clip(wet.right);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "let hostile input through as NaN/Inf");
            peak = std::max({ peak, std::abs(wet.left), std::abs(wet.right) });
        }
        require(peak <= 3.99f, label + "left the clipper's range under hostile input");

        // Once the input is gone the wash of the diffuser falls by 28 dB a
        // second whatever Decay says, and the engine comes to exact silence
        // behind it.
        parameters = spumeTestParameters(1.0f);
        parameters.decaySeconds = 0.2f;
        parameters.sizeScale = 1.0f;
        parameters.preDelaySeconds = 0.0f;
        engine.setParameters(parameters);
        auto lastFrame = -1;
        const auto tailFrames = static_cast<int>(sampleRate * 36.0);
        for (auto frame = 0; frame < tailFrames; ++frame)
        {
            const auto wet = engine.processSample(0.0f, 0.0f);
            require(std::isfinite(wet.left) && std::isfinite(wet.right),
                    label + "kept NaN/Inf after hostile input");
            if (std::abs(wet.left) > 0.0f || std::abs(wet.right) > 0.0f)
                lastFrame = frame;
        }
        require(lastFrame > static_cast<int>(sampleRate * 3.0) && lastFrame < static_cast<int>(sampleRate * 33.0),
                label + "did not return to silence behind the diffuser's wash after hostile input: last "
                        "sound at frame " + std::to_string(lastFrame));
    }
}

void testSpumeEngineAllocatesOnlyInPrepare()
{
    constexpr std::array<double, 2> sampleRates { 48000.0, 44101.0 };
    for (const auto sampleRate : sampleRates)
    {
        FathomEngine engine;
        FathomEngine::LevelStage levelStage;
        auto parameters = spumeTestParameters(1.0f);
        prepareSpumeEngine(engine, parameters, sampleRate);
        levelStage.prepare(sampleRate);

        allocationCount.store(0, std::memory_order_relaxed);
        countAllocations.store(true, std::memory_order_relaxed);
        for (auto frame = 0; frame < 90000; ++frame)
        {
            if (frame % 211 == 0)
            {
                parameters.decaySeconds = parameters.decaySeconds > 1.0f ? 0.2f : 30.0f;
                parameters.macro = static_cast<float>((frame / 211) % 5) / 4.0f;
                parameters.freeze = frame % 422 == 0;
                engine.setParameters(parameters);
            }
            if (frame % 37000 == 36000)
                engine.reset();
            if ((frame / 5000) % 3 == 2)
            {
                engine.advanceIdle();
                continue;
            }
            const auto left = 0.5f * fathomNoise(frame, 6);
            const auto right = 0.5f * fathomNoise(frame, 7);
            static_cast<void>(levelStage.process(left, right, engine.processSample(left, right)));
        }
        countAllocations.store(false, std::memory_order_relaxed);

        require(allocationCount.load(std::memory_order_relaxed) == 0,
                "Spume engine allocated memory outside prepare()");
    }

    FDNReverb reverb;
    auto parameters = fathomNeutralParameters();
    parameters.mode = ReverbMode::spume;
    reverb.setParameters(parameters);
    reverb.prepare(48000.0, 256);
    std::array<float, 256> left {};
    std::array<float, 256> right {};
    constexpr std::array<ReverbMode, 4> visits {
        ReverbMode::fathom, ReverbMode::spume, ReverbMode::undertow, ReverbMode::spume
    };
    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (auto block = 0; block < 300; ++block)
    {
        for (std::size_t frame = 0; frame < left.size(); ++frame)
        {
            left[frame] = 0.3f * fathomNoise(block * 256 + static_cast<int>(frame), 8);
            right[frame] = 0.3f * fathomNoise(block * 256 + static_cast<int>(frame), 9);
        }
        if (block % 40 == 20)
        {
            parameters.mode = visits[static_cast<std::size_t>(block / 40) % visits.size()];
            reverb.setParameters(parameters);
        }
        if (block % 7 == 3)
        {
            parameters.evolution = static_cast<float>(block % 5) / 4.0f;
            reverb.setParameters(parameters);
        }
        reverb.process(left.data(), right.data(), 256);
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(allocationCount.load(std::memory_order_relaxed) == 0,
            "Spume allocated memory in process");
}

// The chain each of the three Characters that are engines must reduce to
// while Ocean's own controls are neutral: Fathom's, with the engine under the
// Character's layer and told of the host's transport, which only Undertow
// hears.
struct EngineCharacterChain
{
    EngineCharacterChain(ReverbMode mode, const ReverbParameters& parameters, double sampleRate)
        : mix(parameters.mix), width(parameters.width)
    {
        engine.setLayer(mode == ReverbMode::spume ? FathomEngine::Layer::spume
                      : mode == ReverbMode::undertow ? FathomEngine::Layer::undertow
                                                     : FathomEngine::Layer::tide);
        engine.setParameters(fathomEngineParameters(parameters));
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);
    }

    void announce(const HostTransport& transport) noexcept
    {
        FathomEngine::Transport forwarded;
        forwarded.quarterNotes = transport.quarterNotes;
        forwarded.bpm = transport.bpm;
        forwarded.playing = transport.playing;
        forwarded.hasTempo = transport.hasTempo;
        engine.setTransport(forwarded);
    }

    void idle() noexcept
    {
        engine.advanceIdle();
    }

    [[nodiscard]] FathomEngine::Frame wet(float dryLeft, float dryRight) noexcept
    {
        return levelStage.process(dryLeft, dryRight, engine.processSample(dryLeft, dryRight));
    }

    [[nodiscard]] FathomEngine::Frame process(float dryLeft, float dryRight) noexcept
    {
        const auto widened = FathomEngine::applyWidth(wet(dryLeft, dryRight), width);
        return { FathomReferenceChain::guard(FathomEngine::clip(oceanMix(dryLeft, widened.left, mix))),
                 FathomReferenceChain::guard(FathomEngine::clip(oceanMix(dryRight, widened.right, mix))) };
    }

    // A frame of the chain while its amount is above zero, a frame of time otherwise.
    [[nodiscard]] FathomEngine::Frame frame(bool sounds, const FathomEngine::Frame& input) noexcept
    {
        if (sounds)
            return process(input.left, input.right);
        idle();
        return {};
    }

    FathomEngine engine;
    FathomEngine::LevelStage levelStage;
    float mix;
    float width;
};

// Two of the three Characters that are engines crossfade directly: the output
// is the two chains by their amounts and nothing of the FDN shows.
void requireEngineCharacterCrossfade(ReverbMode from, ReverbMode to, const std::string& label)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockFrames = 64;
    constexpr auto switchSample = 60000;
    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    constexpr auto endSample = switchSample + morphSamples + 12000;

    auto parameters = fathomNeutralParameters();
    parameters.decaySeconds = 6.0f;
    parameters.evolution = 0.8f;
    auto switched = parameters;
    switched.mode = from;
    FDNReverb reverb;
    reverb.setParameters(switched);
    reverb.prepare(sampleRate, blockFrames);
    EngineCharacterChain leaving(from, parameters, sampleRate);
    EngineCharacterChain entering(to, parameters, sampleRate);
    FathomMorphRamp enteringAmount(0.0f, 1.0f, morphSamples);
    FathomMorphRamp leavingAmount(1.0f, 0.0f, morphSamples);

    auto largestDistance = 0.0f;
    auto peak = 1.0e-3f;
    double sideDifferenceEnergy = 0.0;
    double sideEnergy = 0.0;
    for (auto sample = 0; sample < endSample; ++sample)
    {
        if (sample == switchSample)
        {
            switched.mode = to;
            reverb.setParameters(switched);
        }
        if (sample % blockFrames == 0)
        {
            leaving.announce(undertowPlugInTransport(sample, sampleRate));
            entering.announce(undertowPlugInTransport(sample, sampleRate));
        }

        const auto input = fathomBursts(sample, sampleRate, 0.5f);
        auto left = input.left;
        auto right = input.right;
        processUndertowPlugIn(reverb, sample, sampleRate, left, right);

        // Each side sounds while its amount is above zero and keeps time otherwise.
        const auto old = leaving.frame(sample < switchSample + morphSamples - 1, input);
        const auto fresh = entering.frame(sample >= switchSample, input);
        if (sample < switchSample)
        {
            require(sameBits(left, old.left) && sameBits(right, old.right),
                    label + " differs before the switch");
            continue;
        }
        // Each amount moves on its own ramp; the two share the wet by them.
        const auto rising = enteringAmount.next();
        const auto falling = leavingAmount.next();
        const auto amount = rising / (falling + rising);
        if (sample >= switchSample + morphSamples - 1)
        {
            require(sameBits(left, fresh.left) && sameBits(right, fresh.right),
                    label + " is not the new Character alone after the crossfade, at sample "
                        + std::to_string(sample));
            continue;
        }
        largestDistance = std::max({
            largestDistance,
            std::abs(left - (old.left + amount * (fresh.left - old.left))),
            std::abs(right - (old.right + amount * (fresh.right - old.right)))
        });
        peak = std::max({ peak, std::abs(old.left), std::abs(old.right),
                          std::abs(fresh.left), std::abs(fresh.right) });
        sideDifferenceEnergy += static_cast<double>(fresh.left - old.left) * (fresh.left - old.left)
                              + static_cast<double>(fresh.right - old.right) * (fresh.right - old.right);
        sideEnergy += 0.5 * (static_cast<double>(old.left) * old.left
                             + static_cast<double>(old.right) * old.right
                             + static_cast<double>(fresh.left) * fresh.left
                             + static_cast<double>(fresh.right) * fresh.right);
    }
    std::cout << "[METRIC] " << label << ": largest distance from the crossfade="
              << largestDistance << ", peak of its two sides=" << peak << '\n';
    require(largestDistance <= 4.0e-6f * peak,
            label + " is not a crossfade of the two Characters: distance="
                + std::to_string(largestDistance / peak) + " of the peak");
    require(sideEnergy > 1.0e-8 && sideDifferenceEnergy > 0.25 * sideEnergy,
            label + " crossfades two sides that do not differ");
}

void testSpumeThroughThePlugIn()
{
    constexpr auto blockFrames = 64;

    // Routing at Ocean's neutral controls: the engine's wet through its level
    // stage, whatever the host says of its transport.
    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        for (const auto evolution : { 0.0f, 0.45f, 1.0f })
        {
            auto parameters = fathomNeutralParameters();
            parameters.mode = ReverbMode::spume;
            parameters.evolution = evolution;
            parameters.preDelayMs = 12.3f;
            FDNReverb reverb;
            reverb.setParameters(parameters);
            reverb.prepare(sampleRate, blockFrames);
            EngineCharacterChain chain(ReverbMode::spume, parameters, sampleRate);

            auto wetPeak = 0.0f;
            const auto frameCount = static_cast<int>(sampleRate * 1.75);
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                const auto input = fathomBursts(frame, sampleRate);
                const auto expected = chain.wet(input.left, input.right);
                wetPeak = std::max({ wetPeak, std::abs(expected.left), std::abs(expected.right) });
                auto left = input.left;
                auto right = input.right;
                processUndertowPlugIn(reverb, frame, sampleRate, left, right);
                require(sameBits(left, expected.left) && sameBits(right, expected.right),
                        "Spume is not the engine's wet through its level stage at "
                            + std::to_string(static_cast<int>(sampleRate)) + " Hz, Evolution "
                            + std::to_string(evolution) + ", frame " + std::to_string(frame));
            }
            require(wetPeak > 0.01f, "Spume routing programme did not exercise the engine");
        }
    }

    // Spume crossfades directly with Fathom and with Undertow.
    requireEngineCharacterCrossfade(ReverbMode::fathom, ReverbMode::spume, "Fathom to Spume switch");
    requireEngineCharacterCrossfade(ReverbMode::spume, ReverbMode::fathom, "Spume to Fathom switch");
    requireEngineCharacterCrossfade(ReverbMode::undertow, ReverbMode::spume, "Undertow to Spume switch");
    requireEngineCharacterCrossfade(ReverbMode::spume, ReverbMode::undertow, "Spume to Undertow switch");

    constexpr auto sampleRate = 48000.0;
    constexpr auto switchSample = 60000;
    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    constexpr auto endSample = switchSample + morphSamples + 12000;

    // Fathom to Undertow and, half a fade on, to Spume: the three sound at
    // once, each by its amount, and still nothing of the FDN shows.
    {
        const std::string label = "Fathom to Undertow to Spume switch";
        constexpr auto secondSwitch = switchSample + morphSamples / 2;
        constexpr auto lastSample = secondSwitch + morphSamples + 12000;
        auto parameters = fathomNeutralParameters();
        parameters.decaySeconds = 6.0f;
        parameters.evolution = 0.8f;
        auto switched = parameters;
        FDNReverb reverb;
        reverb.setParameters(switched);
        reverb.prepare(sampleRate, blockFrames);
        EngineCharacterChain fathomSide(ReverbMode::fathom, parameters, sampleRate);
        EngineCharacterChain undertowSide(ReverbMode::undertow, parameters, sampleRate);
        EngineCharacterChain spumeSide(ReverbMode::spume, parameters, sampleRate);
        FathomMorphRamp fathomRamp(1.0f, 0.0f, morphSamples);
        FathomMorphRamp undertowRamp(0.0f, 1.0f, morphSamples);
        FathomMorphRamp spumeRamp(0.0f, 1.0f, morphSamples);
        auto fathomAmount = 1.0f;
        auto undertowAmount = 0.0f;
        auto spumeAmount = 0.0f;

        auto largestDistance = 0.0f;
        auto peak = 1.0e-3f;
        auto threeAtOnce = 0;
        for (auto sample = 0; sample < lastSample; ++sample)
        {
            if (sample == switchSample || sample == secondSwitch)
            {
                switched.mode = sample == switchSample ? ReverbMode::undertow : ReverbMode::spume;
                reverb.setParameters(switched);
            }
            // Undertow turns round where it stands.
            if (sample == secondSwitch)
                undertowRamp = FathomMorphRamp(undertowAmount, 0.0f, morphSamples);
            if (sample >= switchSample)
            {
                fathomAmount = fathomRamp.next();
                undertowAmount = undertowRamp.next();
            }
            if (sample >= secondSwitch)
                spumeAmount = spumeRamp.next();
            if (sample % blockFrames == 0)
                undertowSide.announce(undertowPlugInTransport(sample, sampleRate));

            const auto input = fathomBursts(sample, sampleRate, 0.5f);
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(reverb, sample, sampleRate, left, right);
            const auto fathom = fathomSide.frame(fathomAmount > 0.0f, input);
            const auto undertow = undertowSide.frame(undertowAmount > 0.0f, input);
            const auto spume = spumeSide.frame(spumeAmount > 0.0f, input);

            if (sample < switchSample)
            {
                require(sameBits(left, fathom.left) && sameBits(right, fathom.right),
                        label + " differs before the first switch");
                continue;
            }
            if (!(fathomAmount > 0.0f) && !(undertowAmount > 0.0f))
            {
                require(sameBits(left, spume.left) && sameBits(right, spume.right),
                        label + " is not Spume alone after the crossfades, at sample "
                            + std::to_string(sample));
                continue;
            }
            // Fathom and Undertow by their amounts, and the two with Spume by theirs.
            auto expected = fathom;
            if (undertowAmount > 0.0f)
            {
                const auto share = fathomAmount > 0.0f ? undertowAmount / (fathomAmount + undertowAmount) : 1.0f;
                expected.left += share * (undertow.left - fathom.left);
                expected.right += share * (undertow.right - fathom.right);
            }
            if (spumeAmount > 0.0f)
            {
                const auto share = spumeAmount / (fathomAmount + undertowAmount + spumeAmount);
                expected.left += share * (spume.left - expected.left);
                expected.right += share * (spume.right - expected.right);
            }
            if (fathomAmount > 0.0f && undertowAmount > 0.0f && spumeAmount > 0.0f)
                ++threeAtOnce;
            largestDistance = std::max({ largestDistance, std::abs(left - expected.left),
                                         std::abs(right - expected.right) });
            peak = std::max({ peak, std::abs(fathom.left), std::abs(fathom.right),
                              std::abs(undertow.left), std::abs(undertow.right),
                              std::abs(spume.left), std::abs(spume.right) });
        }
        std::cout << "[METRIC] " << label << ": largest distance from the three by their amounts="
                  << largestDistance << ", peak of its sides=" << peak << ", samples with all three="
                  << threeAtOnce << '\n';
        require(threeAtOnce > morphSamples / 4, label + " never had the three Characters at once");
        require(largestDistance <= 4.0e-6f * peak,
                label + " is not the three Characters by their amounts: distance="
                    + std::to_string(largestDistance / peak) + " of the peak");
    }

    // Default and Spume: a crossfade of the FDN and the engine's chain, which
    // starts from silence when Spume is entered.
    for (const auto intoSpume : { true, false })
    {
        const auto label = std::string(intoSpume ? "Default to Spume" : "Spume to Default") + " switch";
        auto parameters = fathomNeutralParameters();
        parameters.mode = ReverbMode::spume;
        parameters.decaySeconds = 6.0f;
        parameters.evolution = 0.8f;
        auto switched = parameters;
        switched.mode = intoSpume ? ReverbMode::defaultMode : ReverbMode::spume;
        auto fdnParameters = parameters;
        fdnParameters.mode = ReverbMode::defaultMode;
        FDNReverb reverb;
        FDNReverb fdnSide;
        reverb.setParameters(switched);
        fdnSide.setParameters(fdnParameters);
        reverb.prepare(sampleRate, blockFrames);
        fdnSide.prepare(sampleRate, blockFrames);
        EngineCharacterChain spumeSide(ReverbMode::spume, parameters, sampleRate);
        FathomMorphRamp spumeAmount(intoSpume ? 0.0f : 1.0f, intoSpume ? 1.0f : 0.0f, morphSamples);

        auto largestDistance = 0.0f;
        auto peak = 1.0e-3f;
        for (auto sample = 0; sample < endSample; ++sample)
        {
            if (sample == switchSample)
            {
                switched.mode = intoSpume ? ReverbMode::spume : ReverbMode::defaultMode;
                reverb.setParameters(switched);
            }
            const auto input = fathomBursts(sample, sampleRate, 0.5f);
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(reverb, sample, sampleRate, left, right);
            auto fdnLeft = input.left;
            auto fdnRight = input.right;
            fdnSide.processSample(fdnLeft, fdnRight);
            const auto spume = spumeSide.frame(
                intoSpume ? sample >= switchSample : sample < switchSample + morphSamples - 1, input);

            if (sample < switchSample)
            {
                require(intoSpume ? sameBits(left, fdnLeft) && sameBits(right, fdnRight)
                                  : sameBits(left, spume.left) && sameBits(right, spume.right),
                        label + " differs before the switch");
                continue;
            }
            const auto amount = spumeAmount.next();
            if (sample >= switchSample + morphSamples - 1)
            {
                require(intoSpume ? sameBits(left, spume.left) && sameBits(right, spume.right)
                                  : sameBits(left, fdnLeft) && sameBits(right, fdnRight),
                        label + " is not the new Character alone after the crossfade, at sample "
                            + std::to_string(sample));
                continue;
            }
            largestDistance = std::max({
                largestDistance,
                std::abs(left - (fdnLeft + amount * (spume.left - fdnLeft))),
                std::abs(right - (fdnRight + amount * (spume.right - fdnRight)))
            });
            peak = std::max({ peak, std::abs(fdnLeft), std::abs(fdnRight),
                              std::abs(spume.left), std::abs(spume.right) });
        }
        std::cout << "[METRIC] " << label << ": largest distance from the crossfade="
                  << largestDistance << ", peak of its two sides=" << peak << '\n';
        require(largestDistance <= 4.0e-6f * peak,
                label + " is not a crossfade of the two Characters: distance="
                    + std::to_string(largestDistance / peak) + " of the peak");
    }

    // Left for longer than its fade and selected again, Spume returns as an
    // engine that has only kept time since it was prepared: the diffuser and
    // the network empty, whatever was played before and while it was away.
    for (const auto away : { ReverbMode::bloom, ReverbMode::fathom })
    {
        constexpr auto leaveSample = 50000;
        constexpr auto returnSample = leaveSample + 3 * morphSamples;
        constexpr auto settledSample = returnSample + morphSamples;
        auto parameters = fathomNeutralParameters();
        parameters.mode = ReverbMode::spume;
        parameters.decaySeconds = 30.0f;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, blockFrames);
        EngineCharacterChain chain(ReverbMode::spume, parameters, sampleRate);
        auto peak = 0.0f;
        for (auto sample = 0; sample < settledSample + 70000; ++sample)
        {
            if (sample == leaveSample || sample == returnSample)
            {
                parameters.mode = sample == leaveSample ? away : ReverbMode::spume;
                reverb.setParameters(parameters);
            }
            const auto input = fathomBursts(sample, sampleRate, 0.5f);
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(reverb, sample, sampleRate, left, right);
            const auto expected = chain.frame(sample >= returnSample, input);
            if (sample < settledSample)
                continue;
            require(sameBits(left, expected.left) && sameBits(right, expected.right),
                    "Spume selected again is not an engine that only kept time, at sample "
                        + std::to_string(sample));
            peak = std::max(peak, std::abs(left));
        }
        require(peak > 0.01f, "Spume selected again stayed silent");
    }

    // Every rate, Evolution in motion, Freeze, input that is no signal, and
    // the mode itself.
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::spume;
        parameters.mix = 0.6f;
        parameters.decaySeconds = 30.0f;
        parameters.size = FDNReverb::maximumSizeScale;
        parameters.evolution = 1.0f;
        parameters.width = 2.0f;
        parameters.harmony = 0.5f;
        FDNReverb reverb;
        reverb.setParameters(parameters);
        reverb.prepare(rate, 128);
        auto peak = 0.0f;
        const auto frames = static_cast<int>(rate * 3.0);
        for (auto frame = 0; frame < frames; ++frame)
        {
            if (frame % 128 == 0)
            {
                parameters.evolution = 0.5f + 0.5f * fathomNoise(frame / 128, 42);
                parameters.freeze = frame >= static_cast<int>(rate * 1.5);
                reverb.setParameters(parameters);
            }
            auto left = frame % 4001 == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.9f * fathomNoise(frame, 40);
            auto right = frame % 5003 == 0 ? std::numeric_limits<float>::infinity() : 0.9f * fathomNoise(frame, 41);
            reverb.processSample(left, right);
            require(std::isfinite(left) && std::isfinite(right), "Spume produced NaN/Inf in the plug-in");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
        }
        require(peak > 0.05f && peak < 4.0f,
                "Spume stress test left the safety range: peak=" + std::to_string(peak));
    }

    FDNReverb reverb;
    ReverbParameters lastKnown;
    lastKnown.mode = ReverbMode::spume;
    reverb.setParameters(lastKnown);
    require(reverb.getParameters().mode == ReverbMode::spume, "Spume mode was not accepted");
}

// ------------------------------------- the outer stages of the engine Characters

// A programme for the level stages and the Sub Anchors of the three Characters
// that are engines. Bursts of 125 ms with pauses of 125 ms, by turns loud and
// quiet: a loud one peaks at 0.9, above the knee of the reference's level
// stage (0.5629), a quiet one at 0.54, under it. Each is a tone of 88 Hz that
// is not the same on the two sides, with a little noise, so that there is
// Side under the anchor's 145 Hz.
[[nodiscard]] FathomEngine::Frame engineStagesProgramme(int frame, double sampleRate) noexcept
{
    constexpr auto twoPi = 6.28318530717958647692;
    const auto burstAndPause = static_cast<int>(sampleRate * 0.25);
    const auto position = frame % (2 * burstAndPause);
    if (position % burstAndPause >= burstAndPause / 2)
        return {};

    const auto level = position < burstAndPause ? 1.0f : 0.6f;
    const auto phase = twoPi * 88.0 * static_cast<double>(frame) / sampleRate + 1.0;
    return { level * (0.84f * static_cast<float>(std::sin(phase)) + 0.06f * fathomNoise(frame, 80)),
             level * (-0.60f * static_cast<float>(std::sin(phase + 1.0)) - 0.05f * fathomNoise(frame, 81)) };
}

// What the plug-in has behind the engines of its three engine Characters, in
// the order Fathom, Undertow, Spume: a level stage for each, the reference's
// Width law on what the three give together, a Sub Anchor for each with Mono
// Safe all the way in, the reference's clipper and the output guard; Mix at
// 100 %. A stage that restarts begins from rest whenever its Character is
// entered again, as the plug-in's must; one that does not carries on from
// where it stood when its Character was left.
struct EngineOuterStages
{
    static constexpr std::size_t count = 3;

    EngineOuterStages(double sampleRate, float widthScale, bool levelStagesRestart, bool anchorsRestart)
        : width(widthScale), restartsLevelStages(levelStagesRestart), restartsAnchors(anchorsRestart)
    {
        for (auto& stage : levelStages)
            stage.prepare(sampleRate);
        for (auto& anchor : subAnchors)
            anchor.prepare(sampleRate);
    }

    // Three values by their amounts as the plug-in takes them: Fathom and
    // Undertow first, then the two with Spume.
    template <typename Value>
    [[nodiscard]] static Value together(const std::array<Value, count>& values,
                                        const std::array<float, count>& amounts) noexcept
    {
        const auto two = [](Value first, const Value& second, float firstAmount, float secondAmount)
        {
            if (!(secondAmount > 0.0f))
                return first;
            if (!(firstAmount > 0.0f))
                return second;
            const auto secondShare = secondAmount / (firstAmount + secondAmount);
            first.left += secondShare * (second.left - first.left);
            first.right += secondShare * (second.right - first.right);
            return first;
        };
        return two(two(values[0], values[1], amounts[0], amounts[1]), values[2],
                   amounts[0] + amounts[1], amounts[2]);
    }

    // One frame: `raw` is the wet of each engine and `amounts` how far each
    // Character is in. An engine whose Character is out gives nothing.
    [[nodiscard]] FathomEngine::Frame process(const FathomEngine::Frame& input,
                                              const std::array<FathomEngine::Frame, count>& raw,
                                              const std::array<float, count>& amounts) noexcept
    {
        std::array<FathomEngine::Frame, count> wet {};
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto isIn = amounts[index] > 0.0f;
            if (isIn && !in[index])
            {
                if (restartsLevelStages)
                    levelStages[index].reset();
                if (restartsAnchors)
                    subAnchors[index].reset();
            }
            in[index] = isIn;
            if (isIn)
                wet[index] = levelStages[index].process(input.left, input.right, raw[index]);
        }

        const auto widened = FathomEngine::applyWidth(together(wet, amounts), width);
        std::array<StereoField::Frame, count> anchored {};
        for (std::size_t index = 0; index < count; ++index)
            if (in[index])
                anchored[index] = subAnchors[index].applyWidth(widened.left, widened.right, 1.0f);
        const auto anchoredWet = together(anchored, amounts);
        return { FathomReferenceChain::guard(
                     FathomEngine::clip(widened.left + (anchoredWet.left - widened.left))),
                 FathomReferenceChain::guard(
                     FathomEngine::clip(widened.right + (anchoredWet.right - widened.right))) };
    }

    std::array<FathomEngine::LevelStage, count> levelStages;
    std::array<StereoField, count> subAnchors;
    std::array<bool, count> in {};
    float width;
    bool restartsLevelStages;
    bool restartsAnchors;
};

// Each of the three Characters that are engines has a level stage and a Sub
// Anchor of its own, and both start from rest whenever the Character is
// entered again. A stage that two Characters shared would be stepped twice a
// frame while they crossfade and then reset under the one that stays; a stage
// that was not restarted would carry what it held when its Character was left
// into the next visit.
//
// The programme makes each of these show. Mono Safe is on, so the anchors are
// in the circuit, and Width is away from 100 %. Every switch comes 90 ms into
// a quiet burst. The Character that is left has gone 200 ms later, 40 ms into
// a loud burst: its level stage is turned down then, and its anchor holds a
// low Side. The Character that is entered hears the rest of the quiet burst
// and a pause first, under which a level stage from rest does nothing and one
// that kept its reduction still recovers. Every pair of the three crossfades,
// and each of the three is entered again after a stay away.
//
// Two comparisons. The plug-in against the three engines with stages of their
// own that restart, frame by frame through every crossfade: that is where a
// shared stage shows, and a level stage that kept its reduction. And every
// second visit against a plug-in that had never been in that Character, to
// the bit: what an anchor that kept its low Side adds lasts a few
// milliseconds at the foot of a fade, where its Character is hardly in yet,
// and is not much above what the first comparison allows. Beside them the
// test measures what the programme tells: the same stages without the
// restart of the level stages, and without that of the anchors, against
// those with it.
void testEngineCharactersOwnOuterStages()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockFrames = 64;
    constexpr auto morphSamples = static_cast<int>(sampleRate * 0.20);
    // A loud burst, a quiet one and their pauses.
    constexpr auto pairFrames = 24000;
    constexpr auto firstSwitch = 2 * pairFrames + 16320;
    constexpr auto stayFrames = 2 * pairFrames;
    constexpr std::array<ReverbMode, 6> visits {
        ReverbMode::fathom, ReverbMode::spume, ReverbMode::fathom,
        ReverbMode::undertow, ReverbMode::spume, ReverbMode::undertow
    };
    constexpr auto frameCount = firstSwitch + static_cast<int>(visits.size() - 1) * stayFrames;
    constexpr std::array<ReverbMode, EngineOuterStages::count> modes {
        ReverbMode::fathom, ReverbMode::undertow, ReverbMode::spume
    };
    const auto indexOf = [](ReverbMode mode) -> std::size_t
    {
        return mode == ReverbMode::spume ? 2 : mode == ReverbMode::undertow ? 1 : 0;
    };
    // The frame a visit begins at.
    const auto startOf = [&](int visit)
    {
        return visit == 0 ? 0 : firstSwitch + (visit - 1) * stayFrames;
    };

    auto neutral = fathomNeutralParameters();
    neutral.evolution = 0.5f;
    neutral.width = 1.5f;
    neutral.monoSafeStereo = true;
    auto parameters = neutral;
    FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, blockFrames);

    // The three engines, each under its Character's layer, and how far each
    // Character is in.
    EngineCharacterChain fathom(ReverbMode::fathom, neutral, sampleRate);
    EngineCharacterChain undertow(ReverbMode::undertow, neutral, sampleRate);
    EngineCharacterChain spume(ReverbMode::spume, neutral, sampleRate);
    const std::array<EngineCharacterChain*, EngineOuterStages::count> engines { &fathom, &undertow, &spume };
    std::array<FathomMorphRamp, EngineOuterStages::count> ramps {
        FathomMorphRamp(1.0f, 1.0f, 1), FathomMorphRamp(0.0f, 0.0f, 1), FathomMorphRamp(0.0f, 0.0f, 1)
    };

    EngineOuterStages own(sampleRate, neutral.width, true, true);
    EngineOuterStages keptLevelStages(sampleRate, neutral.width, false, true);
    EngineOuterStages keptAnchors(sampleRate, neutral.width, true, false);

    std::vector<float> played;
    played.reserve(2 * static_cast<std::size_t>(frameCount));
    auto largestDistance = 0.0f;
    auto peak = 1.0e-3f;
    // Over the fade of a second visit: how far the stages that were not
    // restarted stand from those that were. The smallest of the second visits.
    auto levelStagesTold = std::numeric_limits<float>::max();
    auto anchorsTold = std::numeric_limits<float>::max();
    auto levelStagesToldNow = 0.0f;
    auto anchorsToldNow = 0.0f;
    std::array<bool, EngineOuterStages::count> seen { true, false, false };
    std::vector<int> secondVisits;
    auto secondVisit = false;
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        const auto visit = frame < firstSwitch ? 0 : 1 + (frame - firstSwitch) / stayFrames;
        const auto sinceSwitch = frame - startOf(visit);
        if (visit > 0 && sinceSwitch == 0)
        {
            const auto entered = indexOf(visits[static_cast<std::size_t>(visit)]);
            parameters.mode = modes[entered];
            reverb.setParameters(parameters);
            ramps[indexOf(visits[static_cast<std::size_t>(visit) - 1])] = FathomMorphRamp(1.0f, 0.0f, morphSamples);
            ramps[entered] = FathomMorphRamp(0.0f, 1.0f, morphSamples);
            secondVisit = seen[entered];
            seen[entered] = true;
            if (secondVisit)
                secondVisits.push_back(visit);
            levelStagesToldNow = 0.0f;
            anchorsToldNow = 0.0f;
        }
        if (frame % blockFrames == 0)
            undertow.announce(undertowPlugInTransport(frame, sampleRate));

        const auto input = engineStagesProgramme(frame, sampleRate);
        auto left = input.left;
        auto right = input.right;
        processUndertowPlugIn(reverb, frame, sampleRate, left, right);
        played.push_back(left);
        played.push_back(right);

        // Each engine sounds while its Character's amount is above zero and
        // keeps time otherwise.
        std::array<float, EngineOuterStages::count> amounts {};
        std::array<FathomEngine::Frame, EngineOuterStages::count> raw {};
        for (std::size_t index = 0; index < engines.size(); ++index)
        {
            amounts[index] = ramps[index].next();
            if (amounts[index] > 0.0f)
                raw[index] = engines[index]->engine.processSample(input.left, input.right);
            else
                engines[index]->idle();
        }

        const auto expected = own.process(input, raw, amounts);
        largestDistance = std::max({ largestDistance, std::abs(left - expected.left),
                                     std::abs(right - expected.right) });
        peak = std::max({ peak, std::abs(expected.left), std::abs(expected.right) });

        const auto withKeptLevelStages = keptLevelStages.process(input, raw, amounts);
        const auto withKeptAnchors = keptAnchors.process(input, raw, amounts);
        if (!secondVisit || sinceSwitch >= morphSamples)
            continue;
        levelStagesToldNow = std::max({ levelStagesToldNow, std::abs(withKeptLevelStages.left - expected.left),
                                        std::abs(withKeptLevelStages.right - expected.right) });
        anchorsToldNow = std::max({ anchorsToldNow, std::abs(withKeptAnchors.left - expected.left),
                                    std::abs(withKeptAnchors.right - expected.right) });
        if (sinceSwitch == morphSamples - 1)
        {
            levelStagesTold = std::min(levelStagesTold, levelStagesToldNow);
            anchorsTold = std::min(anchorsTold, anchorsToldNow);
        }
    }

    // The blends of FDNReverb round in the last place of the larger side.
    constexpr auto tolerance = 4.0e-6f;
    // The grain of single precision at the level of the wet.
    const auto grain = std::numeric_limits<float>::epsilon() * peak;
    std::cout << "[METRIC] Engine Characters' outer stages through " << visits.size() - 1
              << " switches: largest distance from stages of their own, from rest at every visit="
              << largestDistance << ", peak=" << peak << ", allowed=" << tolerance * peak
              << "; level stages that were not restarted stand " << levelStagesTold
              << " away at least, anchors that were not " << anchorsTold << " (grain " << grain << ")\n";
    require(secondVisits.size() == EngineOuterStages::count,
            "The programme does not enter each of the three engine Characters a second time");
    require(levelStagesTold >= 20.0f * tolerance * peak,
            "The programme does not tell a level stage from rest from one that kept its reduction");
    require(anchorsTold >= 16.0f * grain,
            "The programme does not tell a Sub Anchor from rest from one that kept its low Side");
    require(largestDistance <= tolerance * peak,
            "The engine Characters do not each keep a level stage and a Sub Anchor of their own, from rest "
            "at every visit: distance=" + std::to_string(largestDistance / peak) + " of the peak");

    // A Character entered again is the Character entered for the first time.
    // The other plug-in is prepared in the Character of the visit before and
    // hears nothing until that visit begins; from there it goes the same way.
    // A quarter of a second in front of the switch the two are one already:
    // a stay is long enough for what a crossfade left in an anchor to go.
    constexpr auto settledFrames = static_cast<int>(sampleRate * 0.25);
    for (const auto visit : secondVisits)
    {
        const auto index = static_cast<std::size_t>(visit);
        const auto label = std::string("The engine Character of visit ") + std::to_string(visit);
        const auto soundFrom = startOf(visit - 1);
        const auto switchFrame = startOf(visit);
        auto otherParameters = neutral;
        otherParameters.mode = visits[index - 1];
        FDNReverb other;
        other.setParameters(otherParameters);
        other.prepare(sampleRate, blockFrames);
        for (auto frame = 0; frame < switchFrame + stayFrames; ++frame)
        {
            if (frame == switchFrame)
            {
                otherParameters.mode = visits[index];
                other.setParameters(otherParameters);
            }
            const auto input = frame >= soundFrom ? engineStagesProgramme(frame, sampleRate)
                                                  : FathomEngine::Frame {};
            auto left = input.left;
            auto right = input.right;
            processUndertowPlugIn(other, frame, sampleRate, left, right);
            if (frame < switchFrame - settledFrames)
                continue;
            const auto same = sameBits(left, played[2 * static_cast<std::size_t>(frame)])
                           && sameBits(right, played[2 * static_cast<std::size_t>(frame) + 1]);
            if (frame < switchFrame)
                require(same, label + " follows a Character that has not become, a stay after its crossfade, "
                                      "what it is when it was there from the start: frame "
                                    + std::to_string(frame));
            else
                require(same, label + ", entered again, is not that Character entered for the first time: "
                                      + std::to_string(frame - switchFrame) + " frames behind the switch");
        }
    }
}

// ---------------------------------------------------- the layers under Freeze

// Noise for the tests of the layers under the engine's hold, while it sounds.
[[nodiscard]] FathomEngine::Frame freezeNoise(int frame, bool sounds) noexcept
{
    return sounds ? FathomEngine::Frame { 0.3f * fathomNoise(frame, 90), 0.3f * fathomNoise(frame, 91) }
                  : FathomEngine::Frame {};
}

// The engine's wet under a layer and a host that runs at 120 BPM, with Freeze
// on from frame `freezeFrom` to frame `freezeTo` (never, if they are
// negative); left and right interleaved. `programme` gives a frame's input.
template <typename Programme>
[[nodiscard]] std::vector<float> renderThroughFreeze(FathomEngine::Layer layer, double sampleRate, float macro,
                                                     int freezeFrom, int freezeTo, int frameCount,
                                                     Programme&& programme)
{
    FathomEngine::Parameters parameters;
    parameters.decaySeconds = 2.0f;
    parameters.macro = macro;
    FathomEngine engine;
    engine.setLayer(layer);
    engine.setParameters(parameters);
    engine.prepare(sampleRate);
    UndertowHost host;
    host.sampleRate = sampleRate;

    std::vector<float> rendered;
    rendered.reserve(2 * static_cast<std::size_t>(frameCount));
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        host.announce(engine, frame);
        if (frame == freezeFrom || frame == freezeTo)
        {
            parameters.freeze = frame == freezeFrom;
            engine.setParameters(parameters);
        }
        const FathomEngine::Frame input = programme(frame);
        const auto wet = engine.processSample(input.left, input.right);
        rendered.push_back(wet.left);
        rendered.push_back(wet.right);
    }
    return rendered;
}

// Energy of the frames [from, to) of a render, or of what two renders differ by there.
[[nodiscard]] double freezeEnergy(const std::vector<float>& render, int from, int to,
                                  const std::vector<float>* other = nullptr)
{
    auto energy = 0.0;
    for (auto index = 2 * static_cast<std::size_t>(from); index < 2 * static_cast<std::size_t>(to); ++index)
    {
        const auto value = static_cast<double>(render[index])
                         - (other != nullptr ? static_cast<double>((*other)[index]) : 0.0);
        energy += value * value;
    }
    return energy;
}

// A level against a reference in decibels for a metric line; no energy at all is "nothing".
[[nodiscard]] std::string freezeLevel(double energy, double reference)
{
    if (!(energy > 0.0))
        return "nothing";
    auto text = std::to_string(10.0 * std::log10(energy / reference));
    return text.substr(0, text.find('.') + 3) + " dB";
}

// What the engine's hold means to a layer in front of the network (Ocean's
// own): held, the network takes no input, and neither does the layer, by the
// same glide. So what is played under Freeze is nowhere when Freeze ends,
// while what the layer took before runs out as it would.
//
// Freeze lasts five seconds here, under a host at 120 BPM. `name` is the
// Character of the layer, `mode` its mode in the plug-in.
void requireLayerTakesNothingUnderFreeze(FathomEngine::Layer layer, ReverbMode mode, const std::string& name)
{
    constexpr auto sampleRate = 48000.0;
    const auto at = [](double seconds) { return static_cast<int>(seconds * sampleRate); };
    const auto within = [](int frame, int from, int to) { return frame >= from && frame < to; };
    const auto freezeFrom = at(1.5);
    const auto freezeTo = at(6.5);
    const auto frameCount = at(10.5);
    // Something for Freeze to hold.
    const auto earlier = [&](int frame) { return within(frame, at(0.2), at(0.45)); };

    // A burst of a quarter second played wholly inside the Freeze, from
    // `burstFrom` seconds. What it adds to the four seconds behind the
    // release, and the same burst played with no Freeze at all over the four
    // seconds from its start: two energies.
    const auto returned = [&](FathomEngine::Layer ofLayer, double burstFrom, bool mustBeNothing)
    {
        const auto from = at(burstFrom);
        const auto burst = [&](int frame) { return within(frame, from, from + at(0.25)); };
        const auto with = renderThroughFreeze(ofLayer, sampleRate, 1.0f, freezeFrom, freezeTo, frameCount,
                                              [&](int frame) { return freezeNoise(frame, earlier(frame) || burst(frame)); });
        const auto without = renderThroughFreeze(ofLayer, sampleRate, 1.0f, freezeFrom, freezeTo, frameCount,
                                                 [&](int frame) { return freezeNoise(frame, earlier(frame)); });
        const auto unfrozen = renderThroughFreeze(ofLayer, sampleRate, 1.0f, -1, -1, frameCount,
                                                  [&](int frame) { return freezeNoise(frame, burst(frame)); });
        const auto held = freezeEnergy(without, at(5.0), freezeTo);
        const auto reference = freezeEnergy(unfrozen, from, from + at(4.0));
        require(held > 1.0e-6 && reference > 1.0e-6,
                name + " under Freeze: the programme holds nothing, or its burst does not sound unfrozen");
        if (mustBeNothing)
            requireSameFathomRender(with, without,
                                    name + ": a burst played from " + std::to_string(burstFrom)
                                        + " s, wholly inside a Freeze, did not leave the output as it is without it");
        return std::pair { freezeEnergy(with, freezeTo, freezeTo + at(4.0), &without), reference };
    };
    // The burst ends 1.05 s in front of the release, and 50 ms in front of it.
    const auto layered = layer != FathomEngine::Layer::tide;
    const auto early = returned(layer, 5.2, true);
    const auto late = returned(layer, 6.2, true);
    std::cout << "[METRIC] " << name << " under Freeze at Evolution 100 %: of a burst played wholly inside it, "
              << "behind the release there is " << freezeLevel(early.first, early.second)
              << " (the burst ended 1.05 s in front of the release) and " << freezeLevel(late.first, late.second)
              << " (50 ms in front)";
    if (layered)
    {
        const auto fathomEarly = returned(FathomEngine::Layer::tide, 5.2, false);
        const auto fathomLate = returned(FathomEngine::Layer::tide, 6.2, false);
        std::cout << "; Fathom at Evolution 100 % by the same measure: "
                  << freezeLevel(fathomEarly.first, fathomEarly.second) << " and "
                  << freezeLevel(fathomLate.first, fathomLate.second);
    }
    std::cout << '\n';

    // A sound that began before Freeze and goes on two seconds into it is that
    // sound ended where the hold's glide of 50 ms ends (with room for what
    // the rate converters reach): what it played under Freeze is nowhere, and
    // what it played before is held by the network and runs out in the layer.
    {
        const auto soundFrom = at(1.0);
        const auto cutFrame = freezeFrom + at(0.05) + 256;
        const auto goesOn = renderThroughFreeze(layer, sampleRate, 1.0f, freezeFrom, freezeTo, frameCount,
                                                [&](int frame) { return freezeNoise(frame, within(frame, soundFrom, at(3.5))); });
        const auto ends = renderThroughFreeze(layer, sampleRate, 1.0f, freezeFrom, freezeTo, frameCount,
                                              [&](int frame) { return freezeNoise(frame, within(frame, soundFrom, cutFrame)); });
        require(freezeEnergy(ends, at(5.0), freezeTo) > 1.0e-6 && freezeEnergy(ends, freezeTo, at(8.0)) > 1.0e-6,
                name + " under Freeze: the sound that began before it is not held");
        requireSameFathomRender(goesOn, ends,
                                name + ": a sound that went on under Freeze is not that sound ended at the Freeze");
    }

    // At Evolution 0 the engine is Fathom's to the bit through a Freeze and
    // across both its edges, under a sound that runs through them.
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        if (!layered)
            break;
        const auto frames = static_cast<int>(rate * 3.5);
        const auto from = static_cast<int>(rate * 0.8);
        const auto to = static_cast<int>(rate * 1.8);
        const auto sound = [&](int frame)
        {
            return freezeNoise(frame, within(frame, static_cast<int>(rate * 0.2), static_cast<int>(rate * 2.6)));
        };
        requireSameFathomRender(renderThroughFreeze(layer, rate, 0.0f, from, to, frames, sound),
                                renderThroughFreeze(FathomEngine::Layer::tide, rate, 0.0f, from, to, frames, sound),
                                name + " at Evolution 0 through a Freeze at "
                                    + std::to_string(static_cast<int>(rate)) + " Hz");
    }

    // Through the plug-in at Evolution 100 %: the burst inside the Freeze
    // leaves the output as it is without it, from the first frame to the last.
    {
        const auto play = [&](bool withBurst)
        {
            auto parameters = fathomNeutralParameters();
            parameters.mode = mode;
            parameters.decaySeconds = 2.0f;
            FDNReverb reverb;
            reverb.setParameters(parameters);
            reverb.prepare(sampleRate, 64);
            std::vector<float> played;
            played.reserve(2 * static_cast<std::size_t>(frameCount));
            for (auto frame = 0; frame < frameCount; ++frame)
            {
                if (frame == freezeFrom || frame == freezeTo)
                {
                    parameters.freeze = frame == freezeFrom;
                    reverb.setParameters(parameters);
                }
                const auto input = freezeNoise(frame, earlier(frame)
                                                          || (withBurst && within(frame, at(5.2), at(5.45))));
                auto left = input.left;
                auto right = input.right;
                processUndertowPlugIn(reverb, frame, sampleRate, left, right);
                played.push_back(left);
                played.push_back(right);
            }
            return played;
        };
        const auto without = play(false);
        require(freezeEnergy(without, at(5.0), freezeTo) > 1.0e-6,
                name + " under Freeze in the plug-in holds nothing");
        requireSameFathomRender(play(true), without,
                                name + " in the plug-in: a burst played wholly inside a Freeze did not leave "
                                       "the output as it is without it");
    }
}

// A hold of four seconds for the tests of the layers on their own, in
// internal samples: it begins and ends away from the zeros of the two tones
// the layers are given, and its glide is the network's.
constexpr long long freezeGlideSamples = 2205;
constexpr long long freezeHoldFrom = 2 * 44100 + 57;
constexpr long long freezeHoldTo = 6 * 44100 + 57;

// The share of its input a layer takes at an internal sample of that hold:
// one, none under the hold, and between the two the straight lines the
// network's hold moves along; or the same at once.
[[nodiscard]] double freezeShareAt(long long sample, bool atOnce) noexcept
{
    if (sample < freezeHoldFrom || sample >= freezeHoldTo + (atOnce ? 0 : freezeGlideSamples))
        return 1.0;
    if (atOnce || (sample >= freezeHoldFrom + freezeGlideSamples && sample < freezeHoldTo))
        return 0.0;
    return sample < freezeHoldTo
        ? 1.0 - static_cast<double>(sample - freezeHoldFrom + 1) / static_cast<double>(freezeGlideSamples)
        : static_cast<double>(sample - freezeHoldTo + 1) / static_cast<double>(freezeGlideSamples);
}

void testSpumeUnderFreeze()
{
    requireLayerTakesNothingUnderFreeze(FathomEngine::Layer::spume, ReverbMode::spume, "Spume");

    // The layer itself. What it hands the network is the plain input by its
    // gain, which the hold does not touch here, and by the other gain the
    // diffuser's answer to the input by its share. The click measure is that
    // of the Evolution test: the largest second difference of what the layer
    // hands on for two steady tones.
    constexpr auto halfPi = 1.57079632679489661923;
    constexpr long long sampleCount = 10 * 44100;
    for (const auto macro : { 1.0, 0.62 })
    {
        const auto run = [&](bool held, bool atOnce, double& lawDistance)
        {
            SpumeLayer layer;
            layer.prepare();
            layer.setMacro(macro, true);
            SpumeDiffuser diffuser;
            diffuser.prepare();
            const auto plainGain = std::cos(halfPi * macro);
            const auto diffusedGain = std::sin(halfPi * macro);
            std::array<std::array<double, 2>, 2> before {};
            auto click = 0.0;
            lawDistance = 0.0;
            for (long long sample = 0; sample < sampleCount; ++sample)
            {
                const auto share = held ? freezeShareAt(sample, atOnce) : 1.0;
                const auto input = undertowTones(sample);
                std::array<double, 2> diffused {};
                diffuser.process(share * input[0], share * input[1], diffused[0], diffused[1]);
                auto output = input;
                layer.process(output[0], output[1], share);
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    lawDistance = std::max(lawDistance,
                                           std::abs(output[channel] - (plainGain * input[channel]
                                                                       + diffusedGain * diffused[channel])));
                    click = std::max(click, std::abs(output[channel] - 2.0 * before[1][channel]
                                                     + before[0][channel]));
                }
                before[0] = before[1];
                before[1] = output;
            }
            return click;
        };
        auto calmDistance = 0.0;
        auto heldDistance = 0.0;
        auto cutDistance = 0.0;
        const auto calmClick = run(false, false, calmDistance);
        const auto heldClick = run(true, false, heldDistance);
        const auto cutClick = run(true, true, cutDistance);
        std::cout << "[METRIC] Spume layer at Macro " << macro << " through a hold of four seconds: click measure="
                  << heldClick << " (without the hold " << calmClick << ", with the share taken at once "
                  << cutClick << "), distance from the input by its gain and the diffused input by its share="
                  << heldDistance << '\n';
        require(calmDistance <= 1.0e-12 && heldDistance <= 1.0e-12 && cutDistance <= 1.0e-12,
                "Spume diffuser does not take its input by the share it is given");
        require(heldClick <= 1.5 * calmClick, "Spume layer clicks where the hold begins or ends");
        require(cutClick >= 3.0 * heldClick, "Spume click measure does not see a share taken at once");
    }

    // At Macro 0 the layer is at rest whatever share it is given.
    {
        SpumeLayer layer;
        layer.prepare();
        layer.setMacro(0.0, true);
        for (long long sample = 0; sample < sampleCount; sample += 1)
        {
            const auto input = undertowTones(sample);
            auto output = input;
            layer.process(output[0], output[1], freezeShareAt(sample, false));
            require(layer.atRest() && sameBits(output[0], input[0]) && sameBits(output[1], input[1]),
                    "Spume layer at Macro 0 does not hand its input on as it is under a hold");
        }
    }

    // The share is the network's own to the sample. At 44.1 kHz a frame is an
    // internal sample. An impulse j frames behind the release meets a hold
    // that has j + 1 of its 2205 steps behind it; from j = 2160 on all the
    // diffuser makes of it reaches the lines when the hold is over, so the
    // engine must be the engine that never held, given the impulse times
    // (j + 1) / 2205. The same with the share of the next sample is the
    // control.
    constexpr auto glideSamples = static_cast<int>(freezeGlideSamples);
    constexpr auto freezeFrom = 2000;
    constexpr auto freezeTo = 60000;
    constexpr auto frameCount = freezeTo + 3 * 44100;
    auto worstNullDb = -400.0;
    auto bestControlDb = 0.0;
    for (const auto behind : { 2160, 2175, 2190 })
    {
        const auto impulse = [&](float amplitude)
        {
            return [=](int frame)
            {
                return frame == freezeTo + behind ? FathomEngine::Frame { amplitude, -0.8f * amplitude }
                                                  : FathomEngine::Frame {};
            };
        };
        const auto shareOf = [&](int steps) { return static_cast<float>(steps) / static_cast<float>(glideSamples); };
        const auto held = renderThroughFreeze(FathomEngine::Layer::spume, 44100.0, 1.0f, freezeFrom, freezeTo,
                                              frameCount, impulse(0.5f));
        const auto never = renderThroughFreeze(FathomEngine::Layer::spume, 44100.0, 1.0f, -1, -1, frameCount,
                                               impulse(0.5f * shareOf(behind + 1)));
        const auto next = renderThroughFreeze(FathomEngine::Layer::spume, 44100.0, 1.0f, -1, -1, frameCount,
                                              impulse(0.5f * shareOf(behind + 2)));
        const auto window = std::span<const float>(held).subspan(2 * static_cast<std::size_t>(freezeTo));
        worstNullDb = std::max(worstNullDb, fathomNullDb(
            window, std::span<const float>(never).subspan(2 * static_cast<std::size_t>(freezeTo))));
        bestControlDb = std::min(bestControlDb, fathomNullDb(
            window, std::span<const float>(next).subspan(2 * static_cast<std::size_t>(freezeTo))));
    }
    std::cout << "[METRIC] Spume engine, an impulse on the last steps of the hold's glide against the engine "
                 "that never held, given the impulse by the share of its sample: null=" << worstNullDb
              << " dB at worst (by the share of the next sample " << bestControlDb << " dB at best)\n";
    require(worstNullDb <= -120.0, "Spume diffuser does not take its input by the network's share of the sample");
    require(bestControlDb >= -90.0, "The impulses on the hold's glide do not tell a sample from the next");
}

void testUndertowUnderFreeze()
{
    requireLayerTakesNothingUnderFreeze(FathomEngine::Layer::undertow, ReverbMode::undertow, "Undertow");

    // The layer itself at Macro 100 % under a host at 120 BPM. What it is
    // given to keep is its input by its share, and nothing else of it changes:
    // it adds what it adds for that input. The click measure is that of the
    // transport tests: the largest second difference of what the layer adds
    // to two steady tones.
    constexpr auto hostRate = 44100;
    constexpr auto blockFrames = 256;
    constexpr long long sampleCount = 10 * 44100;
    struct Run
    {
        double click = 0.0;
        // Peak of what the layer adds over the first second of the hold, and
        // over its last.
        double earlyPeak = 0.0;
        double latePeak = 0.0;
    };
    const auto run = [&](bool held, bool atOnce, bool sharesTheInput)
    {
        UndertowLayer layer;
        layer.prepare(FathomRateLattice::at(hostRate), hostRate);
        layer.setMacro(1.0, true);
        // The same layer given the input by its share in place of the share.
        UndertowLayer given;
        given.prepare(FathomRateLattice::at(hostRate), hostRate);
        given.setMacro(1.0, true);
        Run result;
        std::array<std::array<double, 2>, 2> before {};
        for (long long sample = 0; sample < sampleCount; ++sample)
        {
            if (sample % blockFrames == 0)
            {
                const auto transport = undertowTransport(
                    static_cast<double>(sample) * 120.0 / (60.0 * hostRate), 120.0, true);
                layer.setTransport(transport);
                given.setTransport(transport);
            }
            const auto share = held ? freezeShareAt(sample, atOnce) : 1.0;
            const auto input = undertowTones(sample);
            std::array<double, 2> added {};
            layer.hostFrame();
            layer.process(input[0], input[1], share, added[0], added[1]);
            if (sharesTheInput)
            {
                std::array<double, 2> addedToGiven {};
                given.hostFrame();
                given.process(share * input[0], share * input[1], addedToGiven[0], addedToGiven[1]);
                require(sameBits(added[0], addedToGiven[0]) && sameBits(added[1], addedToGiven[1]),
                        "Undertow layer does not keep its input by the share it is given");
            }
            for (std::size_t channel = 0; channel < 2; ++channel)
            {
                result.click = std::max(result.click, std::abs(added[channel] - 2.0 * before[1][channel]
                                                               + before[0][channel]));
                if (sample >= freezeHoldFrom + freezeGlideSamples
                    && sample < freezeHoldFrom + freezeGlideSamples + 44100)
                    result.earlyPeak = std::max(result.earlyPeak, std::abs(added[channel]));
                if (sample >= freezeHoldTo - 44100 && sample < freezeHoldTo)
                    result.latePeak = std::max(result.latePeak, std::abs(added[channel]));
            }
            before[0] = before[1];
            before[1] = added;
        }
        return result;
    };
    const auto calm = run(false, false, false);
    const auto held = run(true, false, true);
    const auto cut = run(true, true, false);
    std::cout << "[METRIC] Undertow layer at Macro 100 % through a hold of four seconds: click measure="
              << held.click << " (without the hold " << calm.click << ", with the share taken at once "
              << cut.click << "); what it kept from before the hold: peak " << held.earlyPeak
              << " over the hold's first second, " << held.latePeak << " over its last (without the hold "
              << calm.latePeak << ")\n";
    require(held.click <= 1.5 * calm.click, "Undertow layer clicks where the hold begins or ends");
    require(cut.click >= 3.0 * held.click, "Undertow click measure does not see a share taken at once");
    // What the readers held before the hold runs out under it and is not cut.
    require(held.earlyPeak > 0.1 * calm.latePeak && held.latePeak < 0.1 * held.earlyPeak,
            "Undertow layer does not let what it kept before the hold run out under it");

    // The share is the network's own to the sample. At 44.1 kHz a frame is an
    // internal sample. An impulse j frames behind the release meets a hold
    // that has j + 1 of its 2205 steps behind it, and the layer keeps it by
    // that share; from j = 2160 on the network takes the impulse whole, and
    // all the layer makes of it. Layer and network are linear in what they
    // are given, so the engine must be that share of the engine that never
    // held and the rest of the same engine at Macro 0, where the layer adds
    // nothing. The same with the share of the next sample is the control.
    constexpr auto glideSamples = static_cast<int>(freezeGlideSamples);
    constexpr auto freezeFrom = 2000;
    constexpr auto freezeTo = 60000;
    constexpr auto frameCount = freezeTo + 3 * 44100;
    auto worstNullDb = -400.0;
    auto bestControlDb = 0.0;
    for (const auto behind : { 2160, 2175, 2190 })
    {
        const auto impulse = [=](int frame)
        {
            return frame == freezeTo + behind ? FathomEngine::Frame { 0.5f, -0.4f } : FathomEngine::Frame {};
        };
        const auto heldEngine = renderThroughFreeze(FathomEngine::Layer::undertow, 44100.0, 1.0f, freezeFrom,
                                                    freezeTo, frameCount, impulse);
        const auto never = renderThroughFreeze(FathomEngine::Layer::undertow, 44100.0, 1.0f, -1, -1,
                                               frameCount, impulse);
        const auto plain = renderThroughFreeze(FathomEngine::Layer::undertow, 44100.0, 0.0f, -1, -1,
                                               frameCount, impulse);
        const auto nullDb = [&](int steps)
        {
            const auto share = static_cast<double>(steps) / static_cast<double>(glideSamples);
            auto differenceEnergy = 0.0;
            auto energy = 0.0;
            for (auto index = 2 * static_cast<std::size_t>(freezeTo); index < heldEngine.size(); ++index)
            {
                const auto expected = share * static_cast<double>(never[index])
                                    + (1.0 - share) * static_cast<double>(plain[index]);
                const auto difference = static_cast<double>(heldEngine[index]) - expected;
                differenceEnergy += difference * difference;
                energy += static_cast<double>(heldEngine[index]) * heldEngine[index];
            }
            return 10.0 * std::log10(std::max(differenceEnergy, 1.0e-300) / std::max(energy, 1.0e-300));
        };
        worstNullDb = std::max(worstNullDb, nullDb(behind + 1));
        bestControlDb = std::min(bestControlDb, nullDb(behind + 2));
    }
    std::cout << "[METRIC] Undertow engine, an impulse on the last steps of the hold's glide against the engine "
                 "that never held, the layer's part by the share of its sample: null=" << worstNullDb
              << " dB at worst (by the share of the next sample " << bestControlDb << " dB at best)\n";
    require(worstNullDb <= -120.0, "Undertow layer does not keep its input by the network's share of the sample");
    require(bestControlDb >= -95.0, "The impulses on the hold's glide do not tell a sample from the next");
}

// An input that the engine's input equaliser turns into a unit impulse at its
// first sample and nothing behind it: the impulse through the inverse of each
// section, whose zeros lie inside the unit circle.
[[nodiscard]] std::vector<double> inverseEqualisedImpulse(std::size_t length)
{
    std::vector<double> signal(length, 0.0);
    signal[0] = 1.0;
    for (const auto& section : amanita::dsp::fathom::equaliser)
    {
        auto state0 = 0.0;
        auto state1 = 0.0;
        for (auto& sample : signal)
        {
            const auto input = sample;
            const auto output = (input + state0) / section.b0;
            state0 = section.a1 * input - section.b1 * output + state1;
            state1 = section.a2 * input - section.b2 * output;
            sample = output;
        }
    }
    return signal;
}

// Fathom's own layer under the engine's hold (Ocean's own): the comb of the
// Tide layer takes its input by the share the lines are given, so what is
// played under Freeze is not in the comb when Freeze ends, at any Evolution.
void testFathomUnderFreeze()
{
    requireLayerTakesNothingUnderFreeze(FathomEngine::Layer::tide, ReverbMode::fathom, "Fathom");

    // At Evolution 0 the three Characters that are engines are one engine
    // through a Freeze and across both its edges, to the bit.
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto frames = static_cast<int>(rate * 3.5);
        const auto from = static_cast<int>(rate * 0.8);
        const auto to = static_cast<int>(rate * 1.8);
        const auto sound = [&](int frame)
        {
            return freezeNoise(frame, frame >= static_cast<int>(rate * 0.2) && frame < static_cast<int>(rate * 2.6));
        };
        const auto fathom = renderThroughFreeze(FathomEngine::Layer::tide, rate, 0.0f, from, to, frames, sound);
        const auto label = " at Evolution 0 through a Freeze at " + std::to_string(static_cast<int>(rate)) + " Hz";
        requireSameFathomRender(renderThroughFreeze(FathomEngine::Layer::undertow, rate, 0.0f, from, to, frames, sound),
                                fathom, "Undertow against Fathom" + label);
        requireSameFathomRender(renderThroughFreeze(FathomEngine::Layer::spume, rate, 0.0f, from, to, frames, sound),
                                fathom, "Spume against Fathom" + label);
    }

    // The comb takes nothing under Freeze at Evolution 0 either, where it is
    // out of the circuit and only keeps its history for the time Evolution
    // rises. A burst ends 50 ms in front of the release and Evolution goes to
    // 100 % with the release: the output is that without the burst.
    {
        constexpr auto sampleRate = 48000.0;
        const auto at = [](double seconds) { return static_cast<int>(seconds * sampleRate); };
        const auto play = [&](bool withBurst)
        {
            FathomEngine::Parameters parameters;
            parameters.decaySeconds = 2.0f;
            FathomEngine engine;
            engine.setParameters(parameters);
            engine.prepare(sampleRate);
            std::vector<float> played;
            played.reserve(2 * static_cast<std::size_t>(at(10.5)));
            for (auto frame = 0; frame < at(10.5); ++frame)
            {
                if (frame == at(1.5) || frame == at(6.5))
                {
                    parameters.freeze = frame == at(1.5);
                    parameters.macro = frame == at(1.5) ? 0.0f : 1.0f;
                    engine.setParameters(parameters);
                }
                const auto input = freezeNoise(frame, (frame >= at(0.2) && frame < at(0.45))
                                                          || (withBurst && frame >= at(6.2) && frame < at(6.45)));
                const auto wet = engine.processSample(input.left, input.right);
                played.push_back(wet.left);
                played.push_back(wet.right);
            }
            return played;
        };
        const auto without = play(false);
        require(freezeEnergy(without, at(6.5), at(8.0)) > 1.0e-6,
                "Fathom from Evolution 0 under Freeze to 100 % behind it: nothing sounds behind the release");
        requireSameFathomRender(play(true), without,
                                "Fathom: a burst played under Freeze at Evolution 0 is in the comb when "
                                "Evolution rises behind the release");
    }

    // The share is the lines' own to the sample. At 44.1 kHz a frame is an
    // internal sample, and at Macro 100 % the lines take the comb's output
    // alone. An input the equaliser turns into an impulse j frames behind the
    // release reaches the comb by a hold that has j + 1 of its 2205 steps
    // behind it; from j = 1965 on all the comb makes of it reaches the lines
    // when the hold is over. So the engine must be the engine that never held,
    // given that input times (j + 1) / 2205. The same with the share of the
    // next sample is the control.
    constexpr auto glideSamples = static_cast<int>(freezeGlideSamples);
    constexpr auto freezeFrom = 2000;
    constexpr auto freezeTo = 60000;
    constexpr auto frameCount = freezeTo + 3 * 44100;
    const auto impulse = inverseEqualisedImpulse(1024);
    auto worstNullDb = -400.0;
    auto bestControlDb = 0.0;
    for (const auto behind : { 2000, 2100, 2190 })
    {
        const auto input = [&](double scale)
        {
            return [&impulse, behind, scale](int frame)
            {
                const auto index = frame - (freezeTo + behind);
                if (index < 0 || index >= static_cast<int>(impulse.size()))
                    return FathomEngine::Frame {};
                const auto value = scale * impulse[static_cast<std::size_t>(index)];
                return FathomEngine::Frame { static_cast<float>(0.5 * value), static_cast<float>(-0.4 * value) };
            };
        };
        const auto shareOf = [&](int steps) { return static_cast<double>(steps) / static_cast<double>(glideSamples); };
        const auto held = renderThroughFreeze(FathomEngine::Layer::tide, 44100.0, 1.0f, freezeFrom, freezeTo,
                                              frameCount, input(1.0));
        const auto never = renderThroughFreeze(FathomEngine::Layer::tide, 44100.0, 1.0f, -1, -1, frameCount,
                                               input(shareOf(behind + 1)));
        const auto next = renderThroughFreeze(FathomEngine::Layer::tide, 44100.0, 1.0f, -1, -1, frameCount,
                                              input(shareOf(behind + 2)));
        const auto window = std::span<const float>(held).subspan(2 * static_cast<std::size_t>(freezeTo));
        require(freezeEnergy(held, freezeTo, frameCount) > 1.0e-6,
                "Fathom's comb gives nothing for an impulse on the hold's glide");
        worstNullDb = std::max(worstNullDb, fathomNullDb(
            window, std::span<const float>(never).subspan(2 * static_cast<std::size_t>(freezeTo))));
        bestControlDb = std::min(bestControlDb, fathomNullDb(
            window, std::span<const float>(next).subspan(2 * static_cast<std::size_t>(freezeTo))));
    }
    std::cout << "[METRIC] Fathom engine, an impulse at the comb on the hold's glide against the engine that never "
                 "held, given it by the share of its sample: null=" << worstNullDb
              << " dB at worst (by the share of the next sample " << bestControlDb << " dB at best)\n";
    require(worstNullDb <= -120.0, "Fathom's comb does not take its input by the lines' share of the sample");
    require(bestControlDb >= -90.0, "The impulses on the hold's glide do not tell a sample from the next");

    // The click measure of the layers has no place to be taken here: the comb
    // lies inside the network. At the engine's output, two steady tones
    // through a hold of four seconds at Macro 100 %: the largest second
    // difference of the wet from where the hold begins to a second behind its
    // end, against the same tones over the same time with no hold.
    const auto wetClick = [](bool held)
    {
        const auto wet = renderThroughFreeze(
            FathomEngine::Layer::tide, 44100.0, 1.0f, held ? static_cast<int>(freezeHoldFrom) : -1,
            held ? static_cast<int>(freezeHoldTo) : -1, 8 * 44100, [](int frame)
            {
                const auto tones = undertowTones(frame);
                return FathomEngine::Frame { static_cast<float>(tones[0]), static_cast<float>(tones[1]) };
            });
        auto click = 0.0;
        for (auto index = 2 * static_cast<std::size_t>(freezeHoldFrom);
             index < 2 * static_cast<std::size_t>(freezeHoldTo + 44100); ++index)
            click = std::max(click, std::abs(static_cast<double>(wet[index]) - 2.0 * wet[index - 2]
                                             + wet[index - 4]));
        return click;
    };
    const auto calmClick = wetClick(false);
    const auto heldClick = wetClick(true);
    std::cout << "[METRIC] Fathom engine at Macro 100 % through a hold of four seconds: largest second difference "
                 "of the wet=" << heldClick << " (without the hold " << calmClick << ")\n";
    require(heldClick <= 1.5 * calmClick, "Fathom clicks where the hold begins or ends");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--render-harmony-ab") == 0)
    {
        try
        {
            renderHarmonyAb(argv[2], argv[3]);
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "[FAIL] Harmony A/B render: " << error.what() << '\n';
            return 1;
        }
    }

    struct NamedTest
    {
        const char* name;
        void (*function)();
    };

    const std::array tests {
        NamedTest { "orthonormal feedback matrix", testFeedbackMatrix },
        NamedTest { "Stereo Field Lateral Decay profile",
                    testLateralDecayProfile },
        NamedTest { "mono-safe Stereo Field and Sub Anchor",
                    testMonoSafeStereoField },
        NamedTest { "Stereo Field Mono Safe voicing switch",
                    testStereoFieldVoicingSwitch },
        NamedTest { "Stereo Field FDN integration",
                    testStereoFieldFdnIntegration },
        NamedTest { "Stereo Field Lateral Decay FDN tail",
                    testLateralDecayFdnTail },
        NamedTest { "Drift superposition linearity",
                    testDriftSuperpositionLinearity },
        NamedTest { "unified Drift identity and sub bypass",
                    testDriftCharacterIdentityAndSubBypass },
        NamedTest { "Drift band-limited vocal tail",
                    testDriftBandLimitedVocalTail },
        NamedTest { "fully engaged Drift Freeze linearity and vocal tail",
                    testDriftFreezeLinearityAndBandLimitedTail },
        NamedTest { "Veil disperser kernel", testVeilDisperserKernel },
        NamedTest { "Veil impulse softening and energy",
                    testVeilImpulseSofteningAndEnergy },
        NamedTest { "Character excitation covariance normalisation",
                    testCharacterExcitationNormalisation },
        NamedTest { "Character Evolution loudness normalisation",
                    testCharacterEvolutionLoudnessNormalisation },
        NamedTest { "Current Field geometry and rate safety",
                    testCurrentFieldGeometryAndRateSafety },
        NamedTest { "delay geometry and sample rates", testDelayGeometryAndSampleRates },
        NamedTest { "minimum Size sample rates and stability",
                    testMinimumSizeSampleRatesAndStability },
        NamedTest { "natural Decay excitation",
                    testNaturalDecayExcitation },
        NamedTest { "impulse decay and finite output", testImpulseDecayAndFiniteOutput },
        NamedTest { "feedback freeze and bad inputs", testFeedbackFreezeAndBadInputs },
        NamedTest { "independent DC Guard during Freeze",
                    testIndependentDcGuardDuringFreeze },
        NamedTest { "parameter jumps and block segmentation", testParameterJumpsAndBlockSegmentation },
        NamedTest { "Bloom sample rates and stability", testBloomSampleRatesAndStability },
        NamedTest { "Bloom block invariance and mode switching",
                    testBloomBlockInvarianceAndModeSwitching },
        NamedTest { "Bloom stereo evolution and dry path", testBloomStereoEvolutionAndDryPath },
        NamedTest { "Veil sample rates and stability", testVeilSampleRatesAndStability },
        NamedTest { "Veil block invariance and mode switching",
                    testVeilBlockInvarianceAndModeSwitching },
        NamedTest { "unified Drift Evolution sample rates and stability",
                    testDriftSampleRatesAndStability },
        NamedTest { "Drift block invariance and mode switching",
                    testDriftBlockInvarianceAndModeSwitching },
        NamedTest { "Drift spectral motion", testDriftSpectralMotion },
        NamedTest { "Current FDN minimum field, movement and switching",
                    testCurrentFdnMinimumMovementAndSwitching },
        NamedTest { "Current Mono Safe stereo switching",
                    testCurrentMonoSafeVoicingSwitch },
        NamedTest { "Current sample rates, Freeze and stability",
                    testCurrentSampleRatesFreezeAndStability },
        NamedTest { "Fathom routing at Ocean's neutral controls",
                    testFathomRoutingAtNeutralControls },
        NamedTest { "Fathom Low Cut, High Damping and reset through the plug-in",
                    testFathomLoopControlsAndResetReachTheEngine },
        NamedTest { "Fathom Width, Mix and clipper routes",
                    testFathomWidthMixAndClipperRoutes },
        NamedTest { "Fathom dry level against the other Characters at every Mix",
                    testFathomDryLevelIsThatOfEveryCharacter },
        NamedTest { "Fathom voice seed through the plug-in",
                    testFathomVoiceSeedThroughThePlugIn },
        NamedTest { "Fathom under Focus, Harmony, Mono Safe and Freeze",
                    testFathomOceanOwnControls },
        NamedTest { "Fathom sample rates and stability",
                    testFathomSampleRatesAndStability },
        NamedTest { "Fathom block invariance and mode switching",
                    testFathomBlockInvarianceAndModeSwitching },
        NamedTest { "Fathom engine golden vectors", testFathomEngineGoldenVectors },
        NamedTest { "Fathom rate lattice and converters",
                    testFathomRateLatticeAndConverters },
        NamedTest { "Fathom engine determinism, clocks and parameters at rest",
                    testFathomEngineDeterminismAndClocks },
        NamedTest { "Fathom engine silence, hostile input and sample rates",
                    testFathomEngineSilenceHostileInputAndRates },
        NamedTest { "Fathom engine allocates only in prepare",
                    testFathomEngineAllocatesOnlyInPrepare },
        NamedTest { "Fathom outer laws", testFathomOuterLaws },
        NamedTest { "Fathom Low Cut and High Damping in and out of the loop",
                    testFathomOceanLoopControls },
        NamedTest { "Fathom Freeze hold", testFathomFreezeHold },
        NamedTest { "Fathom parameter glides", testFathomParameterGlides },
        NamedTest { "Fathom Size in motion under a steady tone", testFathomSizeInMotion },
        NamedTest { "Fathom engine at rest after its tail", testFathomEngineComesToRest },
        NamedTest { "Fathom voice phase generator against the campaign's numbers",
                    testFathomVoicePhaseGenerator },
        NamedTest { "Fathom Tide layer determinism and voice seeds",
                    testFathomTideDeterminismAndSeeds },
        NamedTest { "Fathom Tide layer out of the circuit at Macro 0",
                    testFathomTideOutOfCircuitAtMacroZero },
        NamedTest { "Fathom Tide layer depth and plain share below full depth",
                    testFathomTideDepthBelowFullDepth },
        NamedTest { "Fathom Tide layer under hostile input and Macro automation",
                    testFathomTideHostileInputAndAutomation },
        NamedTest { "Fathom Tide layer smooth parameter motion",
                    testFathomTideSmoothMotion },
        NamedTest { "Fathom under Freeze takes no new input", testFathomUnderFreeze },
        NamedTest { "Undertow engine golden vectors", testUndertowEngineGoldenVectors },
        NamedTest { "Undertow at Evolution 0", testUndertowIsFathomAtEvolutionZero },
        NamedTest { "Undertow engine determinism, clocks, reset and Freeze",
                    testUndertowEngineDeterminismAndClocks },
        NamedTest { "Undertow independence of the host's block length",
                    testUndertowHostBlockInvariance },
        NamedTest { "Undertow transport start, stop, jump, loop and tempo",
                    testUndertowTransportEvents },
        NamedTest { "Undertow tempo ramps and steps in host blocks of any length",
                    testUndertowTempoRampsAndSteps },
        NamedTest { "Undertow position that tells nothing", testUndertowPositionThatTellsNothing },
        NamedTest { "Undertow Macro steps", testUndertowMacroSteps },
        NamedTest { "Undertow long run stays on the grid", testUndertowLongRunStaysOnTheGrid },
        NamedTest { "Undertow silence, denormals and hostile input",
                    testUndertowSilenceDenormalsAndHostileInput },
        NamedTest { "Undertow engine allocation-free processing",
                    testUndertowEngineAllocatesOnlyInPrepare },
        NamedTest { "Undertow routing, crossfades and return through the plug-in",
                    testUndertowThroughThePlugIn },
        NamedTest { "Undertow under Freeze takes no new input", testUndertowUnderFreeze },
        NamedTest { "Spume diffuser against the model", testSpumeDiffuserAgainstTheModel },
        NamedTest { "Spume engine golden vectors", testSpumeEngineGoldenVectors },
        NamedTest { "Spume at Evolution 0", testSpumeIsFathomAtEvolutionZero },
        NamedTest { "Spume engine determinism, reset, Pre Delay and Freeze",
                    testSpumeEngineDeterminismAndReset },
        NamedTest { "Spume independence of the host's block length",
                    testSpumeHostBlockInvariance },
        NamedTest { "Spume Evolution steps and ramps", testSpumeEvolutionStepsAndRamps },
        NamedTest { "Spume silence, denormals and hostile input",
                    testSpumeSilenceDenormalsAndHostileInput },
        NamedTest { "Spume engine allocation-free processing",
                    testSpumeEngineAllocatesOnlyInPrepare },
        NamedTest { "Spume routing, crossfades and return through the plug-in",
                    testSpumeThroughThePlugIn },
        NamedTest { "Spume under Freeze takes no new input", testSpumeUnderFreeze },
        NamedTest { "Engine Characters' own level stages and Sub Anchors through switches",
                    testEngineCharactersOwnOuterStages },
        NamedTest { "Drift low/high Evolution kick+bass 190 BPM",
                    testDriftEvolutionKickBass190 },
        NamedTest { "Veil kick+bass 190 BPM", testVeilKickBass190 },
        NamedTest { "Perceptual Ducking spectral selectivity and sample rates",
                    testPerceptualDuckerSpectralSelectivityAndSampleRates },
        NamedTest { "Perceptual Ducking automation and adaptive stereo",
                    testPerceptualDuckerAutomationAndAdaptiveStereo },
        NamedTest { "Perceptual Ducking vocal clarity without collapse",
                    testPerceptualDuckingVocalClarityWithoutCollapse },
        NamedTest { "Perceptual Ducking kick+bass 190 BPM without pumping",
                    testPerceptualDuckingKickBass190NoPumping },
        NamedTest { "Perceptual Ducking Freeze isolation",
                    testPerceptualDuckingFreezeIsolation },
        NamedTest { "Harmonic Analyzer pitch, stereo and sample rates",
                    testHarmonicAnalyzerPitchStereoAndSampleRates },
        NamedTest { "Harmonic Analyzer 190 BPM source confidence regression",
                    testHarmonicAnalyzerKickBassConfidenceRegression },
        NamedTest { "Harmonic Analyzer open voicing retention regression",
                    testHarmonicAnalyzerOpenVoicingRegression },
        NamedTest { "Harmonic Analyzer confident-wrong source regression",
                    testHarmonicAnalyzerConfidentWrongRegression },
        NamedTest { "Harmonic Analyzer chords, rejection and dropout",
                    testHarmonicAnalyzerChordsRejectionAndDropout },
        NamedTest { "Harmonic Analyzer progression and FDN integration",
                    testHarmonicAnalyzerProgressionAndFdnIntegration },
        NamedTest { "Harmonic Tail identity, stereo and reset",
                    testHarmonicTailIdentityStereoAndReset },
        NamedTest { "Harmonic Tail pitch focus and sample rates",
                    testHarmonicTailPitchFocusAndSampleRates },
        NamedTest { "Harmonic Tail automation, Freeze and stability",
                    testHarmonicTailAutomationFreezeAndStability },
        NamedTest { "Harmonic Tail FDN integration and block invariance",
                    testHarmonicTailFdnIntegrationAndBlockInvariance },
        NamedTest { "deterministic Character/Evolution fingerprints",
                    testDeterministicRenderFingerprints },
        NamedTest { "no allocations in process", testNoAllocationsInProcess }
    };

    const auto wantsDuckingTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-ducking") == 0;
    const auto wantsHarmonyTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-harmony") == 0;
    const auto wantsDecayTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-decay-energy") == 0;
    const auto wantsDcGuardTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-dc-guard") == 0;
    const auto wantsStereoTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-stereo-field") == 0;
    const auto wantsModeSwitchTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-mode-switching") == 0;
    const auto wantsCharacterNormalisationTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-character-normalisation") == 0;
    const auto wantsCurrentTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-current") == 0;
    const auto wantsFathomTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-fathom") == 0;
    const auto wantsUndertowTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-undertow") == 0;
    const auto wantsSpumeTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-spume") == 0;
    const auto wantsEngineStageTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-engine-stages") == 0;
    const auto wantsFreezeTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-freeze") == 0;
    // A host that emulates its processor runs the suite hundreds of times
    // slower. "--shard K/N" runs every N-th test from the K-th, so that N
    // processes, K = 1..N, run each test once between them.
    auto shard = 0L;
    auto shardCount = 1L;
    if (argc == 3 && std::strcmp(argv[1], "--shard") == 0)
    {
        char* end = nullptr;
        shard = std::strtol(argv[2], &end, 10);
        shardCount = *end == '/' ? std::strtol(end + 1, &end, 10) : 0L;
        if (*end != '\0' || shard < 1 || shard > shardCount)
        {
            std::cerr << "[FAIL] --shard takes K/N with 1 <= K <= N\n";
            return 1;
        }
        --shard;
    }
    auto failures = 0;
    for (const auto& test : tests)
    {
        if ((&test - tests.data()) % shardCount != shard)
            continue;
        if (wantsDuckingTestsOnly
            && std::strstr(test.name, "Ducking") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsHarmonyTestsOnly
            && std::strstr(test.name, "Harmonic") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsDecayTestsOnly
            && std::strstr(test.name, "natural Decay") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsDcGuardTestsOnly
            && std::strstr(test.name, "DC Guard") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsStereoTestsOnly
            && std::strstr(test.name, "Stereo Field") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsModeSwitchTestsOnly
            && std::strstr(test.name, "mode switching") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsCharacterNormalisationTestsOnly
            && std::strstr(test.name, "normalisation") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsCurrentTestsOnly
            && std::strstr(test.name, "Current") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsFathomTestsOnly
            && std::strstr(test.name, "Fathom") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsUndertowTestsOnly
            && std::strstr(test.name, "Undertow") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsSpumeTestsOnly
            && std::strstr(test.name, "Spume") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsEngineStageTestsOnly
            && std::strstr(test.name, "Engine Characters") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        if (wantsFreezeTestsOnly
            && std::strstr(test.name, "under Freeze") == nullptr
            && std::strcmp(test.name, "no allocations in process") != 0)
            continue;
        try
        {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    const auto wantsDefaultRender = argc == 3 && std::strcmp(argv[1], "--render") == 0;
    const auto wantsBloomRender = argc == 3 && std::strcmp(argv[1], "--render-bloom") == 0;
    const auto wantsDriftRender = argc == 3 && std::strcmp(argv[1], "--render-drift") == 0;
    const auto wantsVeilRender = argc == 3 && std::strcmp(argv[1], "--render-veil") == 0;
    const auto wantsCurrentRender = argc == 3 && std::strcmp(argv[1], "--render-current") == 0;
    const auto wantsFathomRender = argc == 3 && std::strcmp(argv[1], "--render-fathom") == 0;
    if (failures == 0
        && (wantsDefaultRender || wantsBloomRender || wantsDriftRender
            || wantsVeilRender || wantsCurrentRender || wantsFathomRender))
    {
        try
        {
            const auto mode = wantsFathomRender ? ReverbMode::fathom
                            : wantsCurrentRender ? ReverbMode::current
                            : wantsVeilRender ? ReverbMode::veil
                            : wantsBloomRender ? ReverbMode::bloom
                            : wantsDriftRender ? ReverbMode::drift
                                               : ReverbMode::defaultMode;
            renderImpulseResponse(argv[2], mode);
            std::cout << "[PASS] wrote impulse response to " << argv[2] << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] offline render: " << error.what() << '\n';
        }
    }

    if (failures == 0 && argc == 2 && std::strcmp(argv[1], "--stress-bloom") == 0)
    {
        try
        {
            runLongCharacterStress<90>(ReverbMode::bloom);
            std::cout << "[PASS] 90-second Bloom Evolution/Freeze stress\n";
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] long Bloom stress: " << error.what() << '\n';
        }
    }

    if (failures == 0 && argc == 2 && std::strcmp(argv[1], "--stress-drift") == 0)
    {
        try
        {
            runLongCharacterStress<120>(ReverbMode::drift);
            std::cout << "[PASS] 120-second Drift spectral/Freeze stress\n";
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] long Drift stress: " << error.what() << '\n';
        }
    }

    if (failures == 0 && argc == 2 && std::strcmp(argv[1], "--stress-veil") == 0)
    {
        try
        {
            runLongCharacterStress<90>(ReverbMode::veil);
            std::cout << "[PASS] 90-second Veil diffusion/Freeze stress\n";
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] long Veil stress: " << error.what() << '\n';
        }
    }

    return failures == 0 ? 0 : 1;
}
