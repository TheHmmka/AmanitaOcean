#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct AlgorithmCase
{
    const char* name;
    float hostValue;
    int rawIndex;
    amanita::dsp::ReverbMode mode;
};

constexpr std::array algorithmCases {
    AlgorithmCase { "Default", 0.0f,        0, amanita::dsp::ReverbMode::defaultMode },
    AlgorithmCase { "Bloom",   1.0f / 3.0f, 1, amanita::dsp::ReverbMode::bloom },
    AlgorithmCase { "Drift",   2.0f / 3.0f, 2, amanita::dsp::ReverbMode::drift },
    AlgorithmCase { "Veil",    1.0f,        3, amanita::dsp::ReverbMode::veil }
};

constexpr std::array evolutionAmounts { 0.0f, 0.5f, 1.0f };

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

juce::AudioProcessorParameterWithID* findParameterById(
    AmanitaOceanAudioProcessor& processor,
    const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
            withId != nullptr && withId->paramID == id)
            return withId;
    }

    return nullptr;
}

juce::AudioProcessorParameter& parameterById(AmanitaOceanAudioProcessor& processor,
                                             const juce::String& id)
{
    if (auto* parameter = findParameterById(processor, id))
        return *parameter;

    throw std::runtime_error("Parameter was not found: " + id.toStdString());
}

juce::AudioParameterChoice& algorithmParameter(AmanitaOceanAudioProcessor& processor)
{
    auto* parameter = findParameterById(processor, "algorithm");
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter);
    if (choice == nullptr)
        throw std::runtime_error("Algorithm choice parameter was not found");
    return *choice;
}

juce::ValueTree decodeState(const juce::MemoryBlock& data)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(),
                                                            static_cast<int>(data.getSize()));
    require(xml != nullptr, "Processor state is not valid XML");
    const auto state = juce::ValueTree::fromXml(*xml);
    require(state.isValid(), "Processor state is not a valid ValueTree");
    return state;
}

juce::ValueTree findParameterState(const juce::ValueTree& state, const juce::String& id)
{
    for (const auto& child : state)
    {
        if (child.getProperty("id").toString() == id)
            return child;
    }

    return {};
}

void testUnifiedHostContract()
{
    AmanitaOceanAudioProcessor processor;
    constexpr std::array<const char*, 10> expectedIds {
        "algorithm", "mix", "decay", "size", "preDelay", "lowCut",
        "highDamping", "evolution", "width", "freeze"
    };

    const auto& parameters = processor.getParameters();
    require(parameters.size() == static_cast<int>(expectedIds.size()),
            "Host must expose exactly ten parameters");

    auto choiceCount = 0;
    for (std::size_t index = 0; index < expectedIds.size(); ++index)
    {
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(
            parameters[static_cast<int>(index)]);
        require(withId != nullptr && withId->paramID == expectedIds[index],
                "Host parameter order/ID contract is wrong at index "
                    + std::to_string(index));
        require(withId->getVersionHint() == 1,
                "Host parameter version hint is wrong at index "
                    + std::to_string(index));
        if (dynamic_cast<juce::AudioParameterChoice*>(parameters[static_cast<int>(index)])
            != nullptr)
            ++choiceCount;
    }

    require(choiceCount == 1, "Host must expose exactly one choice parameter");
    constexpr std::array<const char*, 5> removedIds {
        "character", "mode", "driftModel", "veil", "modulation"
    };
    for (const auto* removedId : removedIds)
        require(findParameterById(processor, removedId) == nullptr,
                std::string("Removed parameter is still exposed to the host: ") + removedId);

    auto& algorithm = algorithmParameter(processor);
    require(algorithm.getName(128) == "Character", "Algorithm UI label changed");
    require(algorithm.choices.size() == static_cast<int>(algorithmCases.size()),
            "Algorithm must contain exactly four choices");
    require(algorithm.getIndex() == 0 && algorithm.getCurrentChoiceName() == "Default",
            "Algorithm does not default to Default");

    for (const auto& algorithmCase : algorithmCases)
    {
        require(algorithm.choices[algorithmCase.rawIndex] == algorithmCase.name,
                std::string("Algorithm choice order is wrong at ") + algorithmCase.name);
        algorithm.setValueNotifyingHost(algorithmCase.hostValue);
        require(algorithm.getIndex() == algorithmCase.rawIndex
                    && algorithm.getCurrentChoiceName() == algorithmCase.name,
                std::string("Host-normalized value does not select ")
                    + algorithmCase.name);
    }

    auto* evolution = dynamic_cast<juce::AudioParameterFloat*>(
        findParameterById(processor, "evolution"));
    require(evolution != nullptr, "Evolution is not a float parameter");
    const auto& evolutionRange = evolution->getNormalisableRange();
    require(evolution->getName(128) == "Evolution", "Evolution UI label changed");
    require(std::abs(evolutionRange.start) <= 1.0e-6f
                && std::abs(evolutionRange.end - 100.0f) <= 1.0e-6f,
            "Evolution range must be 0..100 percent");
    require(std::abs(evolution->get() - 35.0f) <= 1.0e-6f,
            "Evolution does not default to 35 percent");
    require(evolution->getLabel() == "%", "Evolution unit label changed");
}

void testCurrentStateRoundTrip()
{
    constexpr auto savedEvolutionHostValue = 0.625f;
    constexpr auto savedEvolutionRawValue = 62.5f;
    constexpr std::array<const char*, 5> removedIds {
        "character", "mode", "driftModel", "veil", "modulation"
    };

    for (const auto& algorithmCase : algorithmCases)
    {
        AmanitaOceanAudioProcessor source;
        algorithmParameter(source).setValueNotifyingHost(algorithmCase.hostValue);
        parameterById(source, "evolution").setValueNotifyingHost(savedEvolutionHostValue);
        parameterById(source, "mix").setValueNotifyingHost(0.731f);

        juce::MemoryBlock data;
        source.getStateInformation(data);
        require(!data.isEmpty(), "Current state was not saved");

        const auto savedState = decodeState(data);
        const auto savedAlgorithm = findParameterState(savedState, "algorithm");
        require(savedAlgorithm.isValid(), "Saved state has no Algorithm node");
        require(std::abs(static_cast<float>(savedAlgorithm.getProperty("value"))
                         - static_cast<float>(algorithmCase.rawIndex)) < 0.001f,
                std::string("Saved Algorithm raw index is wrong for ")
                    + algorithmCase.name);

        const auto savedEvolution = findParameterState(savedState, "evolution");
        require(savedEvolution.isValid(), "Saved state has no Evolution node");
        require(std::abs(static_cast<float>(savedEvolution.getProperty("value"))
                         - savedEvolutionRawValue) < 0.001f,
                "Saved Evolution value is wrong");
        for (const auto* removedId : removedIds)
            require(!findParameterState(savedState, removedId).isValid(),
                    std::string("Saved state still contains removed parameter: ")
                        + removedId);

        AmanitaOceanAudioProcessor restored;
        restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
        const auto& restoredAlgorithm = algorithmParameter(restored);
        require(restoredAlgorithm.getIndex() == algorithmCase.rawIndex
                    && restoredAlgorithm.getCurrentChoiceName() == algorithmCase.name,
                std::string("Algorithm did not survive save/load for ")
                    + algorithmCase.name);
        require(std::abs(parameterById(restored, "evolution").getValue()
                         - savedEvolutionHostValue) < 0.001f,
                "Evolution did not survive save/load");
        require(std::abs(parameterById(restored, "mix").getValue() - 0.731f) < 0.001f,
                "Non-Algorithm state did not survive save/load");
    }
}

void testEditorFitsUnifiedControls()
{
    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    require(editor->getHeight() == 420, "Generic editor height is not 420 px");

    auto* tree = dynamic_cast<juce::TreeView*>(editor->getChildComponent(0));
    require(tree != nullptr, "Generic editor parameter tree was not found");
    auto* viewport = tree->getViewport();
    require(viewport != nullptr && viewport->getViewedComponent() != nullptr,
            "Generic editor parameter viewport was not found");
    require(viewport->getViewHeight() >= viewport->getViewedComponent()->getHeight(),
            "Unified controls still require hidden vertical scrolling");
}

std::vector<float> renderProcessor(const AlgorithmCase& algorithmCase,
                                   float evolution)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    AmanitaOceanAudioProcessor processor;
    algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
    parameterById(processor, "mix").setValueNotifyingHost(1.0f);
    parameterById(processor, "preDelay").setValueNotifyingHost(0.0f);
    parameterById(processor, "evolution").setValueNotifyingHost(evolution);
    processor.prepareToPlay(sampleRate, blockSize);

    std::vector<float> result(static_cast<std::size_t>(sampleCount * 2), 0.0f);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (auto offset = 0; offset < sampleCount; offset += blockSize)
    {
        const auto samplesThisBlock = std::min(blockSize, sampleCount - offset);
        buffer.setSize(2, samplesThisBlock, false, false, true);
        buffer.clear();
        if (offset == 0)
            buffer.setSample(0, 0, 1.0f);
        processor.processBlock(buffer, midi);

        for (auto sample = 0; sample < samplesThisBlock; ++sample)
        {
            result[static_cast<std::size_t>((offset + sample) * 2)]
                = buffer.getSample(0, sample);
            result[static_cast<std::size_t>((offset + sample) * 2 + 1)]
                = buffer.getSample(1, sample);
        }
    }
    return result;
}

std::vector<float> renderDsp(const AlgorithmCase& algorithmCase, float evolution)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    amanita::dsp::ReverbParameters parameters;
    parameters.mode = algorithmCase.mode;
    parameters.mix = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.evolution = evolution;

    amanita::dsp::FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, blockSize);

    std::vector<float> left(static_cast<std::size_t>(sampleCount), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(sampleCount), 0.0f);
    left[0] = 1.0f;
    {
        juce::ScopedNoDenormals noDenormals;
        for (auto offset = 0; offset < sampleCount; offset += blockSize)
        {
            const auto samplesThisBlock = std::min(blockSize, sampleCount - offset);
            reverb.process(left.data() + offset, right.data() + offset, samplesThisBlock);
        }
    }

    std::vector<float> result(static_cast<std::size_t>(sampleCount * 2), 0.0f);
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        result[static_cast<std::size_t>(sample * 2)] = left[static_cast<std::size_t>(sample)];
        result[static_cast<std::size_t>(sample * 2 + 1)]
            = right[static_cast<std::size_t>(sample)];
    }
    return result;
}

void testUnifiedAlgorithmReachesDsp()
{
    for (const auto& algorithmCase : algorithmCases)
    {
        for (const auto evolution : evolutionAmounts)
        {
            const auto processorRender = renderProcessor(algorithmCase, evolution);
            const auto directRender = renderDsp(algorithmCase, evolution);
            require(processorRender.size() == directRender.size(),
                    "Algorithm routing render has the wrong size");

            auto maximumDifference = 0.0f;
            std::size_t maximumDifferenceSample = 0;
            for (std::size_t sample = 0; sample < processorRender.size(); ++sample)
            {
                require(std::isfinite(processorRender[sample]),
                        std::string("Algorithm routing produced NaN/Inf for ")
                            + algorithmCase.name + " at Evolution="
                            + std::to_string(evolution));
                const auto difference = std::abs(processorRender[sample] - directRender[sample]);
                if (difference > maximumDifference)
                {
                    maximumDifference = difference;
                    maximumDifferenceSample = sample;
                }
            }
            require(maximumDifference <= 2.0e-7f,
                    std::string("Algorithm does not route to the expected DSP for ")
                        + algorithmCase.name + " at Evolution="
                        + std::to_string(evolution) + ": maximum difference="
                        + std::to_string(maximumDifference * 1.0e9f) + "e-9 at interleaved sample="
                        + std::to_string(maximumDifferenceSample));
        }
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

    try
    {
        testUnifiedHostContract();
        testCurrentStateRoundTrip();
        testEditorFitsUnifiedControls();
        testUnifiedAlgorithmReachesDsp();
        std::cout << "[PASS] Unified Algorithm/Evolution state/UI/DSP routing\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Unified Algorithm/Evolution state/UI/DSP routing: "
                  << error.what() << '\n';
        return 1;
    }
}
