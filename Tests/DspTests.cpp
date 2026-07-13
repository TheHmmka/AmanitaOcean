#include "dsp/FDNReverb.h"
#include "dsp/Drift2Character.h"
#include "dsp/DriftCharacter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
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
using amanita::dsp::Drift2Character;
using amanita::dsp::DriftCharacter;
using amanita::dsp::DriftModel;
using amanita::dsp::ReverbMode;
using amanita::dsp::ReverbParameters;

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

void testDriftInstantaneousNormContraction()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<float, 3> characterAmounts { 0.25f, 0.67f, 1.0f };
    constexpr std::array<float, 3> modulationAmounts { 0.0f, 0.5f, 1.0f };

    for (const auto sampleRate : sampleRates)
    {
        for (const auto characterAmount : characterAmounts)
        {
            for (const auto modulationAmount : modulationAmounts)
            {
                DriftCharacter drift;
                drift.prepare(sampleRate);
                std::uint32_t state = 0x6d2b79f5u;

                for (auto iteration = 0; iteration < 8192; ++iteration)
                {
                    std::array<float, DriftCharacter::numFeedbackLines> feedback {};
                    auto inputNorm = 0.0;
                    for (auto& value : feedback)
                    {
                        state = state * 1664525u + 1013904223u;
                        value = 0.75f
                              * static_cast<float>(static_cast<std::int32_t>(state))
                              / static_cast<float>(
                                  std::numeric_limits<std::int32_t>::max());
                        inputNorm += static_cast<double>(value) * value;
                    }

                    // Exercise stored filter energy as well as ordinary broadband input.
                    if (iteration % 257 == 0)
                    {
                        feedback.fill(0.0f);
                        inputNorm = 0.0;
                    }

                    drift.processFeedback(feedback, characterAmount, modulationAmount);

                    auto outputNorm = 0.0;
                    for (const auto value : feedback)
                    {
                        require(std::isfinite(value),
                                "Drift contraction produced NaN/Inf");
                        outputNorm += static_cast<double>(value) * value;
                    }

                    const auto tolerance = std::max(1.0e-10, inputNorm * 2.0e-5);
                    require(outputNorm <= inputNorm + tolerance,
                            "Drift feedback processing increased instantaneous norm at "
                                + std::to_string(sampleRate) + " Hz: input="
                                + std::to_string(inputNorm) + " output="
                                + std::to_string(outputNorm));
                }
            }
        }
    }
}

void testDrift2KernelSafetyAndSubBypass()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<float, 3> characterAmounts { 0.25f, 0.67f, 1.0f };
    constexpr std::array<float, 3> modulationAmounts { 0.0f, 0.5f, 1.0f };
    constexpr std::array<float, 2> subFrequencies { 55.0f, 80.0f };
    constexpr float inverseSqrtEight = 0.35355339059327376220f;
    constexpr float twoPi = 6.28318530717958647692f;
    constexpr std::array<std::array<float, Drift2Character::numFeedbackLines>, 2> axes {{
        { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f },
        { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f }
    }};

    for (const auto sampleRate : sampleRates)
    {
        Drift2Character identity;
        identity.prepare(sampleRate);
        std::uint32_t identityState = 0x3c6ef372u;
        for (auto iteration = 0; iteration < 4096; ++iteration)
        {
            std::array<float, Drift2Character::numFeedbackLines> feedback {};
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
                        "Drift 2 amount=0 is not bit-transparent");
        }

        for (const auto characterAmount : characterAmounts)
        {
            for (const auto modulationAmount : modulationAmounts)
            {
                Drift2Character drift2;
                drift2.prepare(sampleRate);
                std::uint32_t state = 0xbb67ae85u;
                for (auto iteration = 0; iteration < 8192; ++iteration)
                {
                    std::array<float, Drift2Character::numFeedbackLines> feedback {};
                    auto inputNorm = 0.0;
                    for (auto& value : feedback)
                    {
                        state = state * 1664525u + 1013904223u;
                        value = 0.75f
                              * static_cast<float>(static_cast<std::int32_t>(state))
                              / static_cast<float>(
                                  std::numeric_limits<std::int32_t>::max());
                        inputNorm += static_cast<double>(value) * value;
                    }
                    if (iteration % 257 == 0)
                    {
                        feedback.fill(0.0f);
                        inputNorm = 0.0;
                    }

                    drift2.processFeedback(feedback, characterAmount, modulationAmount);
                    auto outputNorm = 0.0;
                    for (const auto value : feedback)
                    {
                        require(std::isfinite(value),
                                "Drift 2 contraction produced NaN/Inf");
                        outputNorm += static_cast<double>(value) * value;
                    }
                    const auto tolerance = std::max(1.0e-10, inputNorm * 2.0e-5);
                    require(outputNorm <= inputNorm + tolerance,
                            "Drift 2 increased instantaneous feedback norm at "
                                + std::to_string(sampleRate) + " Hz: input="
                                + std::to_string(inputNorm) + " output="
                                + std::to_string(outputNorm));
                }
            }
        }

        for (const auto frequency : subFrequencies)
        {
            for (const auto& axis : axes)
            {
                Drift2Character drift2;
                drift2.prepare(sampleRate);
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
                    std::array<float, Drift2Character::numFeedbackLines> feedback {};
                    for (std::size_t index = 0; index < feedback.size(); ++index)
                        feedback[index] = inverseSqrtEight * axis[index] * tone;
                    if (sample >= measurementStart)
                        inputEnergy += static_cast<double>(tone) * tone;
                    drift2.processFeedback(feedback, 1.0f, 1.0f);
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
        parameters.modulation = 0.0f;

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
    parameters.modulation = 0.25f;

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
    parameters.modulation = 1.0f;
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
    ReverbParameters parameters;
    parameters.mix = 0.25f;
    parameters.decaySeconds = 2.0f;
    parameters.preDelayMs = 0.0f;
    parameters.modulation = 0.0f;

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
    parameters.modulation = 1.0f;
    parameters.width = 2.0f;
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
    singleLeft[0] = blockLeft[0] = 1.0f;

    parameters = {};
    parameters.mix = 1.0f;
    parameters.preDelayMs = 7.0f;

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
        parameters.modulation = 1.0f;
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
    parameters.modulation = 0.8f;

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
    bloomParameters.modulation = 0.75f;
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
    parameters.modulation = 0.8f;

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
    require(maximumResidualDerivative <= std::max(2.0e-4f, 0.10f * preSwitchPeak),
            label + " morph changes too abruptly");
    require(returnedReferenceEnergy > 1.0e-12,
            label + " post-return reference became silent");
    const auto normalisedReturnedRms = std::sqrt(
        returnedDifferenceEnergy / returnedReferenceEnergy);
    require(normalisedReturnedRms > 0.01,
            label + " has no measurable DSP effect after the first delay return: RMS="
                + std::to_string(normalisedReturnedRms));
}

void requireSmoothDriftModelSwitch(DriftModel fromModel,
                                   DriftModel toModel,
                                   const std::string& label)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto switchSample = 30000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.driftModel = fromModel;
    parameters.mix = 0.7f;
    parameters.decaySeconds = 8.0f;
    parameters.preDelayMs = 0.0f;
    parameters.highDampingHz = 16000.0f;
    parameters.modulation = 1.0f;

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
            parameters.driftModel = toModel;
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
    require(maximumResidualDerivative <= std::max(2.0e-4f, 0.10f * preSwitchPeak),
            label + " morph changes too abruptly");
    require(returnedReferenceEnergy > 1.0e-12,
            label + " post-return reference became silent");
    const auto normalisedReturnedRms = std::sqrt(
        returnedDifferenceEnergy / returnedReferenceEnergy);
    require(normalisedReturnedRms > 0.01,
            label + " has no measurable effect after first delay return: RMS="
                + std::to_string(normalisedReturnedRms));
}

void testDriftSampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    FDNReverb reverb;

    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::drift;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.modulation = 1.0f;
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
                    + std::to_string(sampleRate) + " Hz: last/first="
                    + std::to_string(sustainedEnergyRatio));
        require(lastWindowEnergy <= firstWindowEnergy * 1.25 + 1.0e-12,
                "Drift Freeze feedback energy grows over time");
        require(peak < 4.0f, "Drift stress test exceeded safety range");
    }
}

void testDrift2SampleRatesAndStability()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    FDNReverb reverb;

    for (const auto sampleRate : sampleRates)
    {
        ReverbParameters parameters;
        parameters.mode = ReverbMode::drift;
        parameters.driftModel = DriftModel::drift2;
        parameters.mix = 1.0f;
        parameters.decaySeconds = 30.0f;
        parameters.size = 2.0f;
        parameters.preDelayMs = 0.0f;
        parameters.lowCutHz = 20.0f;
        parameters.highDampingHz = 20000.0f;
        parameters.modulation = 1.0f;
        parameters.width = 2.0f;
        reverb.setParameters(parameters);
        reverb.prepare(sampleRate, 512);

        std::uint32_t noiseState = 0x510e527fu;
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
                    "Drift 2 excitation produced NaN/Inf");
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
                    "Drift 2 Freeze produced NaN/Inf");
            peak = std::max({ peak, std::abs(left), std::abs(right) });
            const auto energy = static_cast<double>(left) * left
                              + static_cast<double>(right) * right;
            if (sample >= static_cast<int>(sampleRate)
                && sample < static_cast<int>(sampleRate * 2.0))
                firstWindowEnergy += energy;
            if (sample >= static_cast<int>(sampleRate * 5.0))
                lastWindowEnergy += energy;
        }

        require(firstWindowEnergy > 1.0e-10, "Drift 2 Freeze tail became silent");
        const auto sustainedEnergyRatio = lastWindowEnergy / firstWindowEnergy;
        require(lastWindowEnergy > 1.0e-12 && sustainedEnergyRatio >= 1.0e-4,
                "Drift 2 Freeze tail collapsed at " + std::to_string(sampleRate)
                    + " Hz: last/first=" + std::to_string(sustainedEnergyRatio));
        require(lastWindowEnergy <= firstWindowEnergy * 1.25 + 1.0e-12,
                "Drift 2 Freeze feedback energy grows over time");
        require(peak < 4.0f, "Drift 2 stress test exceeded safety range");
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
    parameters.modulation = 1.0f;

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

    parameters = {};
    parameters.mix = 1.0f;
    parameters.decaySeconds = 10.0f;
    parameters.highDampingHz = 18000.0f;
    parameters.modulation = 1.0f;
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

void testDrift2BlockInvarianceAndModelSwitching()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto comparisonSamples = 48000;
    ReverbParameters parameters;
    parameters.mode = ReverbMode::drift;
    parameters.driftModel = DriftModel::drift2;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 6.0f;
    parameters.preDelayMs = 9.0f;
    parameters.highDampingHz = 18000.0f;
    parameters.modulation = 1.0f;

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
                "Drift 2 result depends on process block segmentation");
    }

    requireSmoothDriftModelSwitch(DriftModel::original, DriftModel::drift2,
                                  "Original to Drift 2");
    requireSmoothDriftModelSwitch(DriftModel::drift2, DriftModel::original,
                                  "Drift 2 to Original");
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
    parameters.modulation = 1.0f;
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

void testDrift2KickBass190()
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
    parameters.modulation = 1.0f;
    parameters.width = 1.0f;

    FDNReverb original;
    parameters.driftModel = DriftModel::original;
    original.setParameters(parameters);
    original.prepare(sampleRate, 512);
    FDNReverb drift2;
    parameters.driftModel = DriftModel::drift2;
    drift2.setParameters(parameters);
    drift2.prepare(sampleRate, 512);

    FourBandMeter originalLeftMeter(sampleRate);
    FourBandMeter originalRightMeter(sampleRate);
    FourBandMeter drift2LeftMeter(sampleRate);
    FourBandMeter drift2RightMeter(sampleRate);
    KickBassAnalysis originalAnalysis;
    KickBassAnalysis drift2Analysis;
    auto differenceEnergy = 0.0;
    auto referenceEnergy = 0.0;

    for (auto sample = 0; sample < totalSamples; ++sample)
    {
        const auto input = kickBass190Sample(sample, sampleRate);
        auto originalLeft = input;
        auto originalRight = input;
        auto drift2Left = input;
        auto drift2Right = input;
        original.processSample(originalLeft, originalRight);
        drift2.processSample(drift2Left, drift2Right);
        require(std::isfinite(originalLeft) && std::isfinite(originalRight)
                    && std::isfinite(drift2Left) && std::isfinite(drift2Right),
                "Kick+bass comparison produced NaN/Inf");

        const auto originalLeftBands = originalLeftMeter.process(originalLeft);
        const auto originalRightBands = originalRightMeter.process(originalRight);
        const auto drift2LeftBands = drift2LeftMeter.process(drift2Left);
        const auto drift2RightBands = drift2RightMeter.process(drift2Right);
        const auto beat = static_cast<int>(std::floor(
            (static_cast<double>(sample) / sampleRate) / beatSeconds));
        if (beat < warmupBeats || beat >= totalBeats)
            continue;
        const auto measuredBeat = static_cast<std::size_t>(beat - warmupBeats);
        for (std::size_t band = 0; band < 4; ++band)
        {
            originalAnalysis.leftEnergy[measuredBeat][band]
                += originalLeftBands[band] * originalLeftBands[band];
            originalAnalysis.rightEnergy[measuredBeat][band]
                += originalRightBands[band] * originalRightBands[band];
            drift2Analysis.leftEnergy[measuredBeat][band]
                += drift2LeftBands[band] * drift2LeftBands[band];
            drift2Analysis.rightEnergy[measuredBeat][band]
                += drift2RightBands[band] * drift2RightBands[band];
        }
        const auto originalSubSide = originalLeftBands[0] - originalRightBands[0];
        const auto drift2SubSide = drift2LeftBands[0] - drift2RightBands[0];
        originalAnalysis.subSideEnergy[measuredBeat]
            += 0.5 * originalSubSide * originalSubSide;
        drift2Analysis.subSideEnergy[measuredBeat]
            += 0.5 * drift2SubSide * drift2SubSide;

        const auto differenceLeft = drift2Left - originalLeft;
        const auto differenceRight = drift2Right - originalRight;
        differenceEnergy += static_cast<double>(differenceLeft) * differenceLeft
                          + static_cast<double>(differenceRight) * differenceRight;
        referenceEnergy += 0.5
                         * (static_cast<double>(originalLeft) * originalLeft
                            + static_cast<double>(originalRight) * originalRight
                            + static_cast<double>(drift2Left) * drift2Left
                            + static_cast<double>(drift2Right) * drift2Right);
    }

    const auto originalLeftTrajectory = normalisedNonSubTrajectory(originalAnalysis.leftEnergy);
    const auto originalRightTrajectory = normalisedNonSubTrajectory(originalAnalysis.rightEnergy);
    const auto drift2LeftTrajectory = normalisedNonSubTrajectory(drift2Analysis.leftEnergy);
    const auto drift2RightTrajectory = normalisedNonSubTrajectory(drift2Analysis.rightEnergy);
    const auto originalMotion = 0.5
                              * (detrendedTrajectoryMotion(originalLeftTrajectory)
                                 + detrendedTrajectoryMotion(originalRightTrajectory));
    const auto drift2Motion = 0.5
                            * (detrendedTrajectoryMotion(drift2LeftTrajectory)
                               + detrendedTrajectoryMotion(drift2RightTrajectory));
    const auto originalStereoMotion = musicalStereoMotion(originalLeftTrajectory,
                                                           originalRightTrajectory);
    const auto drift2StereoMotion = musicalStereoMotion(drift2LeftTrajectory,
                                                         drift2RightTrajectory);
    const auto normalisedDifference = std::sqrt(differenceEnergy / referenceEnergy);

    std::array<double, kickBassMeasureBeats> originalSubEnergy {};
    std::array<double, kickBassMeasureBeats> drift2SubEnergy {};
    std::array<double, kickBassMeasureBeats> originalSubDb {};
    std::array<double, kickBassMeasureBeats> drift2SubDb {};
    auto originalSubSideTotal = 0.0;
    auto drift2SubSideTotal = 0.0;
    for (std::size_t beat = 0; beat < kickBassMeasureBeats; ++beat)
    {
        originalSubEnergy[beat] = originalAnalysis.leftEnergy[beat][0]
                                + originalAnalysis.rightEnergy[beat][0];
        drift2SubEnergy[beat] = drift2Analysis.leftEnergy[beat][0]
                              + drift2Analysis.rightEnergy[beat][0];
        originalSubDb[beat] = 10.0 * std::log10(originalSubEnergy[beat] + 1.0e-30);
        drift2SubDb[beat] = 10.0 * std::log10(drift2SubEnergy[beat] + 1.0e-30);
        originalSubSideTotal += originalAnalysis.subSideEnergy[beat];
        drift2SubSideTotal += drift2Analysis.subSideEnergy[beat];
    }
    const auto originalMeanSub = meanValue(originalSubEnergy);
    const auto drift2MeanSub = meanValue(drift2SubEnergy);
    const auto meanSubRatio = drift2MeanSub / originalMeanSub;
    const auto p95SubRatio = percentile95(drift2SubEnergy) / percentile95(originalSubEnergy);
    const auto originalSubMotion = detrendedScalarMotion(originalSubDb);
    const auto drift2SubMotion = detrendedScalarMotion(drift2SubDb);
    const auto originalSubSideFraction = originalSubSideTotal / originalMeanSub
                                       / static_cast<double>(kickBassMeasureBeats);
    const auto drift2SubSideFraction = drift2SubSideTotal / drift2MeanSub
                                     / static_cast<double>(kickBassMeasureBeats);
    auto earlySub = 0.0;
    auto lateSub = 0.0;
    for (std::size_t beat = 0; beat < 16; ++beat)
    {
        earlySub += drift2SubEnergy[beat];
        lateSub += drift2SubEnergy[kickBassMeasureBeats - 16 + beat];
    }
    const auto lateEarlySubRatio = lateSub / earlySub;

    std::cout << "[METRIC] Drift2 kick+bass 190 BPM: motion Original="
              << originalMotion << " Drift2=" << drift2Motion
              << ", stereo Original=" << originalStereoMotion
              << " Drift2=" << drift2StereoMotion
              << ", NRMS=" << normalisedDifference
              << ", sub mean ratio=" << meanSubRatio
              << ", sub p95 ratio=" << p95SubRatio
              << ", sub motion Original=" << originalSubMotion
              << " dB Drift2=" << drift2SubMotion
              << " dB, sub side Original=" << originalSubSideFraction
              << " Drift2=" << drift2SubSideFraction
              << ", sub late/early=" << lateEarlySubRatio << '\n';

    require(normalisedDifference > 0.15,
            "Drift 2 is too similar to Original on kick+bass");
    require(drift2Motion > originalMotion * 1.05,
            "Drift 2 non-sub motion is not stronger than Original");
    require(drift2StereoMotion > originalStereoMotion * 1.04,
            "Drift 2 stereo spectral motion is not stronger than Original");
    require(meanSubRatio <= 1.05 && p95SubRatio <= 1.10,
            "Drift 2 pumps excessive sub energy");
    require(drift2SubMotion <= originalSubMotion + 1.0e-3,
            "Drift 2 adds excessive slow sub modulation");
    require(lateEarlySubRatio <= 1.25,
            "Drift 2 sub energy grows over the repeated pattern");
    require(drift2SubSideFraction <= originalSubSideFraction + 0.05,
            "Drift 2 adds excessive stereo motion in the sub band");
}

void testNoAllocationsInProcess()
{
    FDNReverb reverb;
    reverb.prepare(48000.0, 512);
    ReverbParameters parameters;

    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (auto sample = 0; sample < 100000; ++sample)
    {
        if (sample % 257 == 0)
        {
            const auto nextMode = (static_cast<int>(parameters.mode) + 1) % 3;
            parameters.mode = static_cast<ReverbMode>(nextMode);
            parameters.driftModel = parameters.driftModel == DriftModel::original
                ? DriftModel::drift2
                : DriftModel::original;
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
void runLongCharacterStress(ReverbMode mode,
                            DriftModel driftModel = DriftModel::original)
{
    constexpr auto sampleRate = 44100.0;
    constexpr auto windowSeconds = 10;
    constexpr auto numWindows = stressSeconds / windowSeconds;
    static_assert(stressSeconds % windowSeconds == 0);

    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.driftModel = driftModel;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 30.0f;
    parameters.size = 2.0f;
    parameters.preDelayMs = 250.0f;
    parameters.lowCutHz = 20.0f;
    parameters.highDampingHz = 20000.0f;
    parameters.modulation = 1.0f;
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
    if (mode == ReverbMode::drift)
    {
        const auto finalEnergyRatio = windowEnergy.back() / windowEnergy.front();
        require(windowEnergy.back() > 1.0e-16 && finalEnergyRatio >= 1.0e-8,
                "Long Drift Freeze tail collapsed to silence: last/first="
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

[[nodiscard]] std::uint64_t renderLegacyFingerprint(
    ReverbMode mode,
    DriftModel driftModel = DriftModel::original)
{
    constexpr auto sampleRate = 48000;
    constexpr auto sampleCount = sampleRate * 10;
    constexpr std::uint64_t fnvOffsetBasis = 14695981039346656037ull;

    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.driftModel = driftModel;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 5.0f;
    parameters.size = 1.15f;
    parameters.preDelayMs = 25.0f;
    parameters.lowCutHz = 60.0f;
    parameters.highDampingHz = 7000.0f;
    parameters.modulation = 0.3f;
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

void testLegacyRenderFingerprints()
{
    struct GoldenRender
    {
        const char* name;
        ReverbMode mode;
        DriftModel driftModel;
        std::uint64_t fingerprint;
    };

    constexpr std::array goldenRenders {
        GoldenRender { "Default", ReverbMode::defaultMode, DriftModel::original,
                       0x4ce4294eb9bd24e1ull },
        GoldenRender { "Bloom", ReverbMode::bloom, DriftModel::original,
                       0xe237add5c29e3ec1ull },
        GoldenRender { "Original", ReverbMode::drift, DriftModel::original,
                       0x2a05c2760a082a37ull }
    };

    for (const auto& golden : goldenRenders)
    {
        const auto actual = renderLegacyFingerprint(golden.mode, golden.driftModel);
        require(actual == golden.fingerprint,
                std::string(golden.name) + " 10-second render fingerprint changed: expected="
                    + std::to_string(golden.fingerprint)
                    + " actual=" + std::to_string(actual));
    }
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

void renderImpulseResponse(const std::string& path,
                           ReverbMode mode,
                           DriftModel driftModel = DriftModel::original)
{
    constexpr auto sampleRate = 48000;
    constexpr auto channels = 2;
    constexpr auto seconds = 10;
    constexpr auto sampleCount = sampleRate * seconds;

    ReverbParameters parameters;
    parameters.mode = mode;
    parameters.driftModel = driftModel;
    parameters.mix = 1.0f;
    parameters.decaySeconds = 5.0f;
    parameters.size = 1.15f;
    parameters.preDelayMs = 25.0f;
    parameters.lowCutHz = 60.0f;
    parameters.highDampingHz = 7000.0f;
    parameters.modulation = 0.3f;
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
        NamedTest { "Drift instantaneous norm contraction",
                    testDriftInstantaneousNormContraction },
        NamedTest { "Drift 2 kernel safety and sub bypass",
                    testDrift2KernelSafetyAndSubBypass },
        NamedTest { "delay geometry and sample rates", testDelayGeometryAndSampleRates },
        NamedTest { "impulse decay and finite output", testImpulseDecayAndFiniteOutput },
        NamedTest { "feedback freeze and bad inputs", testFeedbackFreezeAndBadInputs },
        NamedTest { "parameter jumps and block segmentation", testParameterJumpsAndBlockSegmentation },
        NamedTest { "Bloom sample rates and stability", testBloomSampleRatesAndStability },
        NamedTest { "Bloom block invariance and mode switching",
                    testBloomBlockInvarianceAndModeSwitching },
        NamedTest { "Bloom stereo evolution and dry path", testBloomStereoEvolutionAndDryPath },
        NamedTest { "Drift sample rates and stability", testDriftSampleRatesAndStability },
        NamedTest { "Drift 2 sample rates and stability",
                    testDrift2SampleRatesAndStability },
        NamedTest { "Drift block invariance and mode switching",
                    testDriftBlockInvarianceAndModeSwitching },
        NamedTest { "Drift 2 block invariance and model switching",
                    testDrift2BlockInvarianceAndModelSwitching },
        NamedTest { "Drift spectral motion", testDriftSpectralMotion },
        NamedTest { "Drift 2 kick+bass 190 BPM", testDrift2KickBass190 },
        NamedTest { "legacy 10-second render fingerprints", testLegacyRenderFingerprints },
        NamedTest { "no allocations in process", testNoAllocationsInProcess }
    };

    auto failures = 0;
    for (const auto& test : tests)
    {
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
    const auto wantsDrift2Render = argc == 3 && std::strcmp(argv[1], "--render-drift2") == 0;
    if (failures == 0
        && (wantsDefaultRender || wantsBloomRender || wantsDriftRender || wantsDrift2Render))
    {
        try
        {
            const auto mode = wantsBloomRender ? ReverbMode::bloom
                            : (wantsDriftRender || wantsDrift2Render) ? ReverbMode::drift
                                                                    : ReverbMode::defaultMode;
            const auto driftModel = wantsDrift2Render ? DriftModel::drift2
                                                      : DriftModel::original;
            renderImpulseResponse(argv[2], mode, driftModel);
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
            std::cout << "[PASS] 90-second Bloom modulation/Freeze stress\n";
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

    if (failures == 0 && argc == 2 && std::strcmp(argv[1], "--stress-drift2") == 0)
    {
        try
        {
            runLongCharacterStress<120>(ReverbMode::drift, DriftModel::drift2);
            std::cout << "[PASS] 120-second Drift 2 spectral/Freeze stress\n";
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] long Drift 2 stress: " << error.what() << '\n';
        }
    }

    return failures == 0 ? 0 : 1;
}
