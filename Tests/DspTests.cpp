#include "dsp/FDNReverb.h"
#include "dsp/DriftCharacter.h"
#include "dsp/SpatialDucker.h"
#include "dsp/VeilCharacter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
std::atomic<bool> countAllocations { false };
std::atomic<std::size_t> allocationCount { 0 };

void noteAllocation() noexcept
{
    if (countAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
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
    void* memory = nullptr;
    if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size) == 0)
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
    void* memory = nullptr;
    return posix_memalign(&memory, static_cast<std::size_t>(alignment), size) == 0
        ? memory
        : nullptr;
}

void* operator new[](std::size_t size,
                     std::align_val_t alignment,
                     const std::nothrow_t&) noexcept
{
    return ::operator new(size, alignment, std::nothrow);
}

void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

namespace
{
using amanita::dsp::FDNReverb;
using amanita::dsp::DriftCharacter;

using amanita::dsp::ReverbMode;
using amanita::dsp::ReverbParameters;
using amanita::dsp::SpatialDucker;
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

void testParameterJumpsAndBlockSegmentation()
{
    constexpr auto sampleRate = 48000.0;
    constexpr float twoPi = 6.28318530717958647692f;
    ReverbParameters parameters;
    parameters.mix = 0.25f;
    parameters.decaySeconds = 2.0f;
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
    const auto derivativeLimit = std::max(2.0e-4f, 0.11f * preSwitchPeak);
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
    const auto derivativeLimit = std::max(2.0e-4f, 0.11f * preSwitchPeak);
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

void testNoAllocationsInProcess()
{
    FDNReverb reverb;
    reverb.prepare(48000.0, 512);
    ReverbParameters parameters;
    constexpr std::array modes {
        ReverbMode::defaultMode,
        ReverbMode::bloom,
        ReverbMode::drift,
        ReverbMode::veil
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
            parameters.evolution = 1.0f - parameters.evolution;
            parameters.ducking = 1.0f - parameters.ducking;
            parameters.freeze = !parameters.freeze;
            reverb.setParameters(parameters);
        }
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
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
        RenderCase { "Veil high Evolution", ReverbMode::veil, 1.0f }
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

void renderImpulseResponse(const std::string& path, ReverbMode mode)
{
    constexpr auto sampleRate = 48000;
    constexpr auto channels = 2;
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

    std::vector<float> interleaved(static_cast<std::size_t>(sampleCount * channels));
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        interleaved[static_cast<std::size_t>(sample * channels)] = left;
        interleaved[static_cast<std::size_t>(sample * channels + 1)] = right;
    }

    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "Could not open impulse render output: " + path);

    constexpr auto bytesPerSample = sizeof(float);
    const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * bytesPerSample);
    stream.write("RIFF", 4);
    writeLittleEndian32(stream, 36u + dataBytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    writeLittleEndian32(stream, 16u);
    writeLittleEndian16(stream, 3u);
    writeLittleEndian16(stream, channels);
    writeLittleEndian32(stream, sampleRate);
    writeLittleEndian32(stream, sampleRate * channels * bytesPerSample);
    writeLittleEndian16(stream, channels * bytesPerSample);
    writeLittleEndian16(stream, 32u);
    stream.write("data", 4);
    writeLittleEndian32(stream, dataBytes);
    stream.write(reinterpret_cast<const char*>(interleaved.data()),
                 static_cast<std::streamsize>(dataBytes));
    require(stream.good(), "Failed while writing impulse render: " + path);
}
} // namespace

int main(int argc, char** argv)
{
    struct NamedTest
    {
        const char* name;
        void (*function)();
    };

    const std::array tests {
        NamedTest { "orthonormal feedback matrix", testFeedbackMatrix },
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
        NamedTest { "delay geometry and sample rates", testDelayGeometryAndSampleRates },
        NamedTest { "impulse decay and finite output", testImpulseDecayAndFiniteOutput },
        NamedTest { "feedback freeze and bad inputs", testFeedbackFreezeAndBadInputs },
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
        NamedTest { "deterministic Character/Evolution fingerprints",
                    testDeterministicRenderFingerprints },
        NamedTest { "no allocations in process", testNoAllocationsInProcess }
    };

    const auto wantsDuckingTestsOnly = argc == 2
        && std::strcmp(argv[1], "--test-ducking") == 0;
    auto failures = 0;
    for (const auto& test : tests)
    {
        if (wantsDuckingTestsOnly
            && std::strstr(test.name, "Ducking") == nullptr
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
    if (failures == 0
        && (wantsDefaultRender || wantsBloomRender || wantsDriftRender || wantsVeilRender))
    {
        try
        {
            const auto mode = wantsVeilRender ? ReverbMode::veil
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
