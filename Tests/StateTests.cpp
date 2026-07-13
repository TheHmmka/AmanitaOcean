#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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
    auto& character = choiceParameter(processor, "mode");
    auto& driftModel = choiceParameter(processor, "driftModel");

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

    auto modeState = findParameterState(legacyState, "mode");
    require(modeState.isValid(), "Saved state has no Character node");
    modeState.setProperty("value", 1.0f, nullptr);

    choiceParameter(processor, "driftModel").setValueNotifyingHost(1.0f);
    loadState(processor, legacyState);

    const auto& character = choiceParameter(processor, "mode");
    const auto& driftModel = choiceParameter(processor, "driftModel");
    require(character.getIndex() == 2 && character.getCurrentChoiceName() == "Bloom",
            "Legacy Bloom state was not migrated to index 2");
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "Legacy state without Drift Model did not reset to Original");

    juce::MemoryBlock migratedData;
    processor.getStateInformation(migratedData);
    const auto migratedState = decodeState(migratedData);
    require(static_cast<int>(migratedState.getProperty("schemaVersion", 0)) == 4,
            "Migrated state does not contain schemaVersion 4");
    require(std::abs(static_cast<float>(
                         findParameterState(migratedState, "mode").getProperty("value"))
                     - 2.0f) < 0.001f,
            "Migrated Bloom state did not save raw index 2");
    require(std::abs(static_cast<float>(
                         findParameterState(migratedState, "driftModel").getProperty("value")))
                < 0.001f,
            "Migrated state did not save Original Drift Model index 0");
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

    choiceParameter(processor, "driftModel").setValueNotifyingHost(1.0f);
    loadState(processor, version3State);

    const auto& character = choiceParameter(processor, "mode");
    const auto& driftModel = choiceParameter(processor, "driftModel");
    require(character.getIndex() == 1 && character.getCurrentChoiceName() == "Drift",
            "Version 3 Drift was incorrectly migrated as legacy Bloom");
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "Version 3 state without Drift Model did not reset to Original");
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
    require(static_cast<int>(savedState.getProperty("schemaVersion", 0)) == 4,
            "Current state does not contain schemaVersion 4");

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
    loadState(processor, oldState);

    require(character.getIndex() == 0 && character.getCurrentChoiceName() == "Default",
            "State created before Character did not fall back to Default");
    require(driftModel.getIndex() == 0 && driftModel.getCurrentChoiceName() == "Original",
            "State without Drift Model did not fall back to Original");
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
        testPreCharacterStateDefaultsSafely();
        std::cout << "[PASS] Character state compatibility\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Character state compatibility: " << error.what() << '\n';
        return 1;
    }
}
