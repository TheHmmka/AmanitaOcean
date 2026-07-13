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
struct CharacterCase
{
    const char* name;
    float hostValue;
    int rawIndex;
    amanita::dsp::ReverbMode mode;
    amanita::dsp::DriftModel driftModel;
};

constexpr std::array characterCases {
    CharacterCase { "Default", 0.00f, 0, amanita::dsp::ReverbMode::defaultMode,
                    amanita::dsp::DriftModel::original },
    CharacterCase { "Bloom",   0.25f, 1, amanita::dsp::ReverbMode::bloom,
                    amanita::dsp::DriftModel::original },
    CharacterCase { "Drift",   0.50f, 2, amanita::dsp::ReverbMode::drift,
                    amanita::dsp::DriftModel::original },
    CharacterCase { "Drift 2", 0.75f, 3, amanita::dsp::ReverbMode::drift,
                    amanita::dsp::DriftModel::drift2 },
    CharacterCase { "Veil",    1.00f, 4, amanita::dsp::ReverbMode::veil,
                    amanita::dsp::DriftModel::original }
};

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

juce::AudioParameterChoice& characterParameter(AmanitaOceanAudioProcessor& processor)
{
    auto* parameter = findParameterById(processor, "character");
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter);
    if (choice == nullptr)
        throw std::runtime_error("Character choice parameter was not found");
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
        "character", "mix", "decay", "size", "preDelay", "lowCut",
        "highDamping", "modulation", "width", "freeze"
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
    require(findParameterById(processor, "mode") == nullptr,
            "Removed mode parameter is still exposed to the host");
    require(findParameterById(processor, "driftModel") == nullptr,
            "Removed Drift Model parameter is still exposed to the host");
    require(findParameterById(processor, "veil") == nullptr,
            "Removed Veil override parameter is still exposed to the host");

    auto& character = characterParameter(processor);
    require(character.getName(128) == "Character", "Character UI label changed");
    require(character.choices.size() == static_cast<int>(characterCases.size()),
            "Character must contain exactly five choices");
    require(character.getIndex() == 0 && character.getCurrentChoiceName() == "Default",
            "Character does not default to Default");

    for (const auto& characterCase : characterCases)
    {
        require(character.choices[characterCase.rawIndex] == characterCase.name,
                std::string("Character choice order is wrong at ")
                    + characterCase.name);
        character.setValueNotifyingHost(characterCase.hostValue);
        require(character.getIndex() == characterCase.rawIndex
                    && character.getCurrentChoiceName() == characterCase.name,
                std::string("Host-normalized value does not select ")
                    + characterCase.name);
    }
}

void testCurrentStateRoundTrip()
{
    for (const auto& characterCase : characterCases)
    {
        AmanitaOceanAudioProcessor source;
        characterParameter(source).setValueNotifyingHost(characterCase.hostValue);
        parameterById(source, "mix").setValueNotifyingHost(0.731f);

        juce::MemoryBlock data;
        source.getStateInformation(data);
        require(!data.isEmpty(), "Current state was not saved");

        const auto savedState = decodeState(data);
        const auto savedCharacter = findParameterState(savedState, "character");
        require(savedCharacter.isValid(), "Saved state has no Character node");
        require(std::abs(static_cast<float>(savedCharacter.getProperty("value"))
                         - static_cast<float>(characterCase.rawIndex)) < 0.001f,
                std::string("Saved Character raw index is wrong for ")
                    + characterCase.name);
        require(!findParameterState(savedState, "mode").isValid(),
                "Saved state still contains the removed mode node");
        require(!findParameterState(savedState, "driftModel").isValid(),
                "Saved state still contains Drift Model");
        require(!findParameterState(savedState, "veil").isValid(),
                "Saved state still contains the Veil override");

        AmanitaOceanAudioProcessor restored;
        restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
        const auto& restoredCharacter = characterParameter(restored);
        require(restoredCharacter.getIndex() == characterCase.rawIndex
                    && restoredCharacter.getCurrentChoiceName() == characterCase.name,
                std::string("Character did not survive save/load for ")
                    + characterCase.name);
        require(std::abs(parameterById(restored, "mix").getValue() - 0.731f) < 0.001f,
                "Non-Character state did not survive save/load");
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

std::vector<float> renderProcessor(const CharacterCase& characterCase)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    AmanitaOceanAudioProcessor processor;
    characterParameter(processor).setValueNotifyingHost(characterCase.hostValue);
    parameterById(processor, "mix").setValueNotifyingHost(1.0f);
    parameterById(processor, "preDelay").setValueNotifyingHost(0.0f);
    parameterById(processor, "modulation").setValueNotifyingHost(1.0f);
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

std::vector<float> renderDsp(const CharacterCase& characterCase)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    amanita::dsp::ReverbParameters parameters;
    parameters.mode = characterCase.mode;
    parameters.driftModel = characterCase.driftModel;
    parameters.mix = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.modulation = 1.0f;

    amanita::dsp::FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.prepare(sampleRate, blockSize);

    std::vector<float> result(static_cast<std::size_t>(sampleCount * 2), 0.0f);
    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        auto left = sample == 0 ? 1.0f : 0.0f;
        auto right = 0.0f;
        reverb.processSample(left, right);
        result[static_cast<std::size_t>(sample * 2)] = left;
        result[static_cast<std::size_t>(sample * 2 + 1)] = right;
    }
    return result;
}

void testUnifiedCharacterReachesDsp()
{
    for (const auto& characterCase : characterCases)
    {
        const auto processorRender = renderProcessor(characterCase);
        const auto directRender = renderDsp(characterCase);
        require(processorRender.size() == directRender.size(),
                "Character routing render has the wrong size");

        for (std::size_t sample = 0; sample < processorRender.size(); ++sample)
        {
            require(std::isfinite(processorRender[sample]),
                    std::string("Character routing produced NaN/Inf for ")
                        + characterCase.name);
            require(std::abs(processorRender[sample] - directRender[sample]) <= 1.0e-7f,
                    std::string("Character does not route to the expected DSP for ")
                        + characterCase.name);
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
        testUnifiedCharacterReachesDsp();
        std::cout << "[PASS] Unified Character state/UI/DSP routing\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Unified Character state/UI/DSP routing: "
                  << error.what() << '\n';
        return 1;
    }
}
