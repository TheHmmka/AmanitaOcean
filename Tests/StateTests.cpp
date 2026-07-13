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
void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

juce::AudioParameterChoice& choiceParameter(AmanitaOceanAudioProcessor& processor,
                                            const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter);
            choice != nullptr && choice->paramID == id)
            return *choice;
    }

    throw std::runtime_error("Choice parameter was not found: " + id.toStdString());
}

juce::AudioParameterBool& boolParameter(AmanitaOceanAudioProcessor& processor,
                                        const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* boolean = dynamic_cast<juce::AudioParameterBool*>(parameter);
            boolean != nullptr && boolean->paramID == id)
            return *boolean;
    }

    throw std::runtime_error("Boolean parameter was not found: " + id.toStdString());
}

juce::AudioProcessorParameter& parameterById(AmanitaOceanAudioProcessor& processor,
                                             const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
            withId != nullptr && withId->paramID == id)
            return *parameter;

    throw std::runtime_error("Parameter was not found: " + id.toStdString());
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

juce::MemoryBlock encodeState(const juce::ValueTree& state)
{
    const auto xml = state.createXml();
    require(xml != nullptr, "Could not encode processor state");
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*xml, data);
    return data;
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

void loadState(AmanitaOceanAudioProcessor& processor, const juce::ValueTree& state)
{
    const auto data = encodeState(state);
    processor.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
}

void testHostMapping()
{
    AmanitaOceanAudioProcessor processor;
    constexpr std::array<const char*, 12> expectedIds {
        "mix", "decay", "size", "preDelay", "lowCut", "highDamping",
        "modulation", "width", "freeze", "mode", "driftModel", "veil"
    };
    constexpr std::array<int, 12> expectedVersionHints {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3
    };
    const auto& parameters = processor.getParameters();
    require(parameters.size() == static_cast<int>(expectedIds.size()),
            "Host parameter count changed unexpectedly");
    for (std::size_t index = 0; index < expectedIds.size(); ++index)
    {
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(
            parameters[static_cast<int>(index)]);
        require(withId != nullptr && withId->paramID == expectedIds[index],
                "Host parameter order/ID contract changed at index "
                    + std::to_string(index));
        require(withId->getVersionHint() == expectedVersionHints[index],
                "Host parameter version hint changed at index "
                    + std::to_string(index));
    }

    auto& character = choiceParameter(processor, "mode");
    auto& driftModel = choiceParameter(processor, "driftModel");
    auto& veil = boolParameter(processor, "veil");

    require(character.choices.size() == 3,
            "Legacy Character choice count changed and can break host automation");
    require(!veil.get(), "Veil does not default to Off");

    character.setValueNotifyingHost(0.0f);
    require(character.getIndex() == 0 && character.getCurrentChoiceName() == "Default",
            "Host-normalized value 0.0 no longer selects Default");

    character.setValueNotifyingHost(1.0f);
    require(character.getCurrentChoiceName() == "Bloom",
            "Legacy host-normalized value 1.0 no longer selects Bloom");

    character.setValueNotifyingHost(0.5f);
    require(character.getIndex() == 1 && character.getCurrentChoiceName() == "Drift",
            "Host-normalized value 0.5 does not select Drift");

    driftModel.setValueNotifyingHost(0.0f);
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "Drift Model does not default to Original");
    driftModel.setValueNotifyingHost(1.0f);
    require(driftModel.getIndex() == 1 && driftModel.getCurrentChoiceName() == "Drift 2",
            "Drift Model value 1.0 does not select Drift 2");
    require(driftModel.getVersionHint() > character.getVersionHint(),
            "New Drift Model parameter can reorder legacy AU parameters");

    veil.setValueNotifyingHost(0.0f);
    require(!veil.get(), "Veil host-normalized value 0.0 does not select Off");
    veil.setValueNotifyingHost(1.0f);
    require(veil.get(), "Veil host-normalized value 1.0 does not select On");
    require(veil.getVersionHint() > driftModel.getVersionHint(),
            "Veil parameter can reorder existing AU parameters");
}

void testLegacyBloomMigration()
{
    AmanitaOceanAudioProcessor processor;
    juce::MemoryBlock initialData;
    processor.getStateInformation(initialData);
    auto legacyState = decodeState(initialData);
    legacyState.removeProperty("schemaVersion", nullptr);
    const auto modelState = findParameterState(legacyState, "driftModel");
    require(modelState.isValid(), "Saved state has no Drift Model node");
    legacyState.removeChild(modelState, nullptr);
    const auto veilState = findParameterState(legacyState, "veil");
    require(veilState.isValid(), "Saved state has no Veil node");
    legacyState.removeChild(veilState, nullptr);

    auto modeState = findParameterState(legacyState, "mode");
    require(modeState.isValid(), "Saved state has no Character node");
    modeState.setProperty("value", 1.0f, nullptr);

    choiceParameter(processor, "driftModel").setValueNotifyingHost(1.0f);
    boolParameter(processor, "veil").setValueNotifyingHost(1.0f);
    loadState(processor, legacyState);

    const auto& character = choiceParameter(processor, "mode");
    const auto& driftModel = choiceParameter(processor, "driftModel");
    const auto& veil = boolParameter(processor, "veil");
    require(character.getIndex() == 2 && character.getCurrentChoiceName() == "Bloom",
            "Legacy Bloom state was not migrated to index 2");
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "Legacy state without Drift Model did not reset to Original");
    require(!veil.get(), "Legacy state without Veil did not reset to Off");

    juce::MemoryBlock migratedData;
    processor.getStateInformation(migratedData);
    const auto migratedState = decodeState(migratedData);
    require(static_cast<int>(migratedState.getProperty("schemaVersion", 0)) == 5,
            "Migrated state does not contain schemaVersion 5");
    require(std::abs(static_cast<float>(
                         findParameterState(migratedState, "mode").getProperty("value"))
                     - 2.0f) < 0.001f,
            "Migrated Bloom state did not save raw index 2");
    require(std::abs(static_cast<float>(
                         findParameterState(migratedState, "driftModel").getProperty("value")))
                < 0.001f,
            "Migrated state did not save Original Drift Model index 0");
    require(std::abs(static_cast<float>(
                         findParameterState(migratedState, "veil").getProperty("value")))
                < 0.001f,
            "Migrated state did not save Veil Off");
}

void testVersion3DriftBoundary()
{
    AmanitaOceanAudioProcessor processor;
    juce::MemoryBlock data;
    processor.getStateInformation(data);
    auto version3State = decodeState(data);
    version3State.setProperty("schemaVersion", 3, nullptr);
    findParameterState(version3State, "mode").setProperty("value", 1.0f, nullptr);
    const auto modelState = findParameterState(version3State, "driftModel");
    version3State.removeChild(modelState, nullptr);
    const auto veilState = findParameterState(version3State, "veil");
    version3State.removeChild(veilState, nullptr);

    choiceParameter(processor, "driftModel").setValueNotifyingHost(1.0f);
    boolParameter(processor, "veil").setValueNotifyingHost(1.0f);
    loadState(processor, version3State);

    const auto& character = choiceParameter(processor, "mode");
    const auto& driftModel = choiceParameter(processor, "driftModel");
    const auto& veil = boolParameter(processor, "veil");
    require(character.getIndex() == 1 && character.getCurrentChoiceName() == "Drift",
            "Version 3 Drift was incorrectly migrated as legacy Bloom");
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "Version 3 state without Drift Model did not reset to Original");
    require(!veil.get(), "Version 3 state without Veil did not reset to Off");
}

void testCurrentDrift2RoundTrip()
{
    AmanitaOceanAudioProcessor source;
    auto& sourceCharacter = choiceParameter(source, "mode");
    auto& sourceDriftModel = choiceParameter(source, "driftModel");
    sourceCharacter.setValueNotifyingHost(0.5f);
    sourceDriftModel.setValueNotifyingHost(1.0f);

    juce::MemoryBlock data;
    source.getStateInformation(data);
    const auto savedState = decodeState(data);
    require(static_cast<int>(savedState.getProperty("schemaVersion", 0)) == 5,
            "Current state does not contain schemaVersion 5");

    AmanitaOceanAudioProcessor restored;
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    const auto& restoredCharacter = choiceParameter(restored, "mode");
    const auto& restoredDriftModel = choiceParameter(restored, "driftModel");
    require(restoredCharacter.getIndex() == 1
                && restoredCharacter.getCurrentChoiceName() == "Drift",
            "Current Drift state changed during save/load");
    require(restoredDriftModel.getIndex() == 1
                && restoredDriftModel.getCurrentChoiceName() == "Drift 2",
            "Current Drift 2 model changed during save/load");
}

void testVersion4Drift2DefaultsVeilOff()
{
    AmanitaOceanAudioProcessor processor;
    juce::MemoryBlock data;
    processor.getStateInformation(data);
    auto version4State = decodeState(data);
    version4State.setProperty("schemaVersion", 4, nullptr);
    findParameterState(version4State, "mode").setProperty("value", 1.0f, nullptr);
    findParameterState(version4State, "driftModel").setProperty("value", 1.0f, nullptr);
    version4State.removeChild(findParameterState(version4State, "veil"), nullptr);

    boolParameter(processor, "veil").setValueNotifyingHost(1.0f);
    loadState(processor, version4State);

    const auto& character = choiceParameter(processor, "mode");
    const auto& driftModel = choiceParameter(processor, "driftModel");
    const auto& veil = boolParameter(processor, "veil");
    require(character.getIndex() == 1 && character.getCurrentChoiceName() == "Drift",
            "Version 4 Drift changed during Veil migration");
    require(driftModel.getIndex() == 1 && driftModel.getCurrentChoiceName() == "Drift 2",
            "Version 4 Drift 2 changed during Veil migration");
    require(!veil.get(), "Version 4 state without Veil did not default to Off");
}

void testCurrentVeilRoundTrip()
{
    AmanitaOceanAudioProcessor source;
    choiceParameter(source, "mode").setValueNotifyingHost(0.5f);
    boolParameter(source, "veil").setValueNotifyingHost(1.0f);

    juce::MemoryBlock data;
    source.getStateInformation(data);

    AmanitaOceanAudioProcessor restored;
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    auto& restoredCharacter = choiceParameter(restored, "mode");
    auto& restoredVeil = boolParameter(restored, "veil");
    require(restoredCharacter.getIndex() == 1
                && restoredCharacter.getCurrentChoiceName() == "Drift",
            "Veil state did not preserve the underlying Character");
    require(restoredVeil.get(), "Veil On did not survive save/load");

    restoredVeil.setValueNotifyingHost(0.0f);
    require(restoredCharacter.getIndex() == 1
                && restoredCharacter.getCurrentChoiceName() == "Drift",
            "Turning Veil Off did not restore the underlying Character");
}

void testEditorShowsAllParameters()
{
    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    require(editor->getHeight() >= 500,
            "Generic editor still hides Character parameters below the fold");
    auto* tree = dynamic_cast<juce::TreeView*>(editor->getChildComponent(0));
    require(tree != nullptr, "Generic editor parameter tree was not found");
    auto* viewport = tree->getViewport();
    require(viewport != nullptr && viewport->getViewedComponent() != nullptr,
            "Generic editor parameter viewport was not found");
    require(viewport->getViewHeight() >= viewport->getViewedComponent()->getHeight(),
            "Generic editor still requires hidden vertical scrolling");
}

void testVeilParameterReachesDsp()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    const auto renderProcessor = [&] (float characterValue, bool enableVeil)
    {
        AmanitaOceanAudioProcessor processor;
        choiceParameter(processor, "mode").setValueNotifyingHost(characterValue);
        parameterById(processor, "mix").setValueNotifyingHost(1.0f);
        parameterById(processor, "preDelay").setValueNotifyingHost(0.0f);
        parameterById(processor, "modulation").setValueNotifyingHost(0.0f);
        boolParameter(processor, "veil").setValueNotifyingHost(enableVeil ? 1.0f : 0.0f);
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
    };

    const auto renderDsp = [&] (amanita::dsp::ReverbMode mode)
    {
        amanita::dsp::ReverbParameters parameters;
        parameters.mode = mode;
        parameters.mix = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.modulation = 0.0f;
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
    };

    struct CharacterCase
    {
        float hostValue;
        amanita::dsp::ReverbMode expectedMode;
        const char* name;
    };
    constexpr std::array cases {
        CharacterCase { 0.0f, amanita::dsp::ReverbMode::defaultMode, "Default" },
        CharacterCase { 0.5f, amanita::dsp::ReverbMode::drift, "Drift" },
        CharacterCase { 1.0f, amanita::dsp::ReverbMode::bloom, "Bloom" }
    };
    const auto directVeil = renderDsp(amanita::dsp::ReverbMode::veil);

    for (const auto& characterCase : cases)
    {
        const auto directUnderlying = renderDsp(characterCase.expectedMode);
        const auto processorOff = renderProcessor(characterCase.hostValue, false);
        const auto processorOn = renderProcessor(characterCase.hostValue, true);
        require(processorOff.size() == directUnderlying.size()
                    && processorOn.size() == directVeil.size(),
                "Veil processor mapping render has the wrong size");

        for (std::size_t sample = 0; sample < processorOff.size(); ++sample)
        {
            require(std::isfinite(processorOff[sample]) && std::isfinite(processorOn[sample]),
                    "Veil processor mapping produced NaN/Inf");
            require(std::abs(processorOff[sample] - directUnderlying[sample]) <= 1.0e-7f,
                    std::string("Veil Off does not restore underlying ")
                        + characterCase.name + " DSP");
            require(std::abs(processorOn[sample] - directVeil[sample]) <= 1.0e-7f,
                    std::string("Veil On does not override underlying ")
                        + characterCase.name + " with Veil DSP");
        }
    }
}

void testPreCharacterStateDefaultsSafely()
{
    AmanitaOceanAudioProcessor processor;
    auto& character = choiceParameter(processor, "mode");
    auto& driftModel = choiceParameter(processor, "driftModel");
    character.setValueNotifyingHost(1.0f);
    driftModel.setValueNotifyingHost(1.0f);

    juce::MemoryBlock data;
    processor.getStateInformation(data);
    auto oldState = decodeState(data);
    oldState.removeProperty("schemaVersion", nullptr);
    const auto modeState = findParameterState(oldState, "mode");
    require(modeState.isValid(), "Saved state has no Character node");
    oldState.removeChild(modeState, nullptr);
    const auto modelState = findParameterState(oldState, "driftModel");
    require(modelState.isValid(), "Saved state has no Drift Model node");
    oldState.removeChild(modelState, nullptr);
    const auto veilState = findParameterState(oldState, "veil");
    require(veilState.isValid(), "Saved state has no Veil node");
    oldState.removeChild(veilState, nullptr);
    loadState(processor, oldState);

    require(character.getIndex() == 0 && character.getCurrentChoiceName() == "Default",
            "State created before Character did not fall back to Default");
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "State without Drift Model did not fall back to Original");
    require(!boolParameter(processor, "veil").get(),
            "State without Veil did not fall back to Off");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

    try
    {
        testHostMapping();
        testLegacyBloomMigration();
        testVersion3DriftBoundary();
        testCurrentDrift2RoundTrip();
        testVersion4Drift2DefaultsVeilOff();
        testCurrentVeilRoundTrip();
        testPreCharacterStateDefaultsSafely();
        testEditorShowsAllParameters();
        testVeilParameterReachesDsp();
        std::cout << "[PASS] Character state compatibility\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Character state compatibility: " << error.what() << '\n';
        return 1;
    }
}
