#include "dsp/FDNReverb.h"

#include <algorithm>
#include <array>
#include <atomic>
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

namespace
{
using amanita::dsp::FDNReverb;
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

void testNoAllocationsInProcess()
{
    FDNReverb reverb;
    reverb.prepare(48000.0, 512);

    allocationCount.store(0, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (auto sample = 0; sample < 100000; ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(allocationCount.load(std::memory_order_relaxed) == 0,
            "DSP allocated memory while processing audio");
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

void renderImpulseResponse(const std::string& path)
{
    constexpr auto sampleRate = 48000;
    constexpr auto channels = 2;
    constexpr auto seconds = 10;
    constexpr auto sampleCount = sampleRate * seconds;

    ReverbParameters parameters;
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
        NamedTest { "delay geometry and sample rates", testDelayGeometryAndSampleRates },
        NamedTest { "impulse decay and finite output", testImpulseDecayAndFiniteOutput },
        NamedTest { "feedback freeze and bad inputs", testFeedbackFreezeAndBadInputs },
        NamedTest { "parameter jumps and block segmentation", testParameterJumpsAndBlockSegmentation },
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

    if (failures == 0 && argc == 3 && std::strcmp(argv[1], "--render") == 0)
    {
        try
        {
            renderImpulseResponse(argv[2]);
            std::cout << "[PASS] wrote impulse response to " << argv[2] << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] offline render: " << error.what() << '\n';
        }
    }

    return failures == 0 ? 0 : 1;
}
