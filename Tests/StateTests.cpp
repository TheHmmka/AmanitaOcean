#include "PluginProcessor.h"
#include "ui/PluginEditor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
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

struct GestureProbe final : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}

    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override
    {
        ++beginCount;
    }

    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override
    {
        ++endCount;
    }

    int beginCount = 0;
    int endCount = 0;
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
    constexpr std::array<const char*, 11> expectedIds {
        "algorithm", "mix", "decay", "size", "preDelay", "lowCut",
        "highDamping", "evolution", "width", "focus", "freeze"
    };

    const auto& parameters = processor.getParameters();
    require(parameters.size() == static_cast<int>(expectedIds.size()),
            "Host must expose exactly eleven parameters");

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
    constexpr std::array<const char*, 6> removedIds {
        "character", "mode", "driftModel", "veil", "modulation", "ducking"
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

    auto* focus = dynamic_cast<juce::AudioParameterFloat*>(
        findParameterById(processor, "focus"));
    require(focus != nullptr, "Focus is not a float parameter");
    const auto& focusRange = focus->getNormalisableRange();
    require(focus->getName(128) == "Focus", "Focus UI label changed");
    require(std::abs(focusRange.start) <= 1.0e-6f
                && std::abs(focusRange.end - 100.0f) <= 1.0e-6f,
            "Focus range must be 0..100 percent");
    require(std::abs(focus->get()) <= 1.0e-6f,
            "Focus does not default to zero percent");
    require(focus->getLabel() == "%", "Focus unit label changed");
}

void testCurrentStateRoundTrip()
{
    constexpr auto savedEvolutionHostValue = 0.625f;
    constexpr auto savedEvolutionRawValue = 62.5f;
    constexpr std::array<const char*, 6> removedIds {
        "character", "mode", "driftModel", "veil", "modulation", "ducking"
    };

    for (const auto& algorithmCase : algorithmCases)
    {
        AmanitaOceanAudioProcessor source;
        algorithmParameter(source).setValueNotifyingHost(algorithmCase.hostValue);
        parameterById(source, "evolution").setValueNotifyingHost(savedEvolutionHostValue);
        parameterById(source, "mix").setValueNotifyingHost(0.731f);
        parameterById(source, "focus").setValueNotifyingHost(0.58f);

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
        require(std::abs(parameterById(restored, "focus").getValue() - 0.58f) < 0.001f,
                "Focus did not survive save/load");
    }
}

juce::Component* findDescendantById(juce::Component& component,
                                    const juce::String& componentId)
{
    if (component.getComponentID() == componentId)
        return &component;

    for (auto* child : component.getChildren())
        if (auto* match = findDescendantById(*child, componentId))
            return match;

    return nullptr;
}

void testCustomEditorLayoutAndAttachments()
{
    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    auto* customEditor = dynamic_cast<AmanitaOceanAudioProcessorEditor*>(editor.get());
    require(customEditor != nullptr, "Processor did not create the custom Amanita editor");
    require(editor->getWidth() == AmanitaOceanAudioProcessorEditor::defaultWidth
                && editor->getHeight() == AmanitaOceanAudioProcessorEditor::defaultHeight,
            "Custom editor default size is wrong");
    require(editor->isResizable(), "Custom editor is not host-resizable");

    auto* constrainer = editor->getConstrainer();
    require(constrainer != nullptr, "Custom editor has no resize constrainer");
    require(constrainer->getMinimumWidth() == AmanitaOceanAudioProcessorEditor::minimumWidth
                && constrainer->getMinimumHeight()
                    == AmanitaOceanAudioProcessorEditor::minimumHeight
                && constrainer->getMaximumWidth()
                    == AmanitaOceanAudioProcessorEditor::maximumWidth
                && constrainer->getMaximumHeight()
                    == AmanitaOceanAudioProcessorEditor::maximumHeight,
            "Custom editor resize limits are wrong");
    require(std::abs(constrainer->getFixedAspectRatio() - 1.6) <= 1.0e-9,
            "Custom editor aspect ratio is wrong");

    constexpr std::array<const char*, 15> interactiveIds {
        "character-selector", "character-default", "character-bloom", "character-drift",
        "character-veil",
        "evolution", "preDelay", "size", "decay", "lowCut", "highDamping",
        "width", "focus", "mix", "freeze"
    };
    constexpr std::array<std::array<int, 2>, 3> editorSizes {{
        { AmanitaOceanAudioProcessorEditor::minimumWidth,
          AmanitaOceanAudioProcessorEditor::minimumHeight },
        { AmanitaOceanAudioProcessorEditor::defaultWidth,
          AmanitaOceanAudioProcessorEditor::defaultHeight },
        { AmanitaOceanAudioProcessorEditor::maximumWidth,
          AmanitaOceanAudioProcessorEditor::maximumHeight }
    }};

    for (const auto& size : editorSizes)
    {
        editor->setSize(size[0], size[1]);
        for (const auto* id : interactiveIds)
        {
            auto* component = findDescendantById(*editor, id);
            require(component != nullptr, std::string("Custom editor is missing control: ") + id);
            require(component->isVisible(), std::string("Custom editor control is hidden: ") + id);
            const auto localBounds = editor->getLocalArea(component, component->getLocalBounds());
            require(!localBounds.isEmpty(), std::string("Custom editor control is empty: ") + id);
            require(editor->getLocalBounds().contains(localBounds),
                    std::string("Custom editor control escapes its bounds: ") + id);
            require(component->isAccessible(),
                    std::string("Custom editor control is not accessible: ") + id);
        }

        auto* evolutionSliderForLayout = findDescendantById(*editor, "evolution");
        auto* evolutionName = findDescendantById(*editor, "evolution-name");
        auto* evolutionValue = findDescendantById(*editor, "evolution-value");
        require(evolutionSliderForLayout != nullptr
                    && evolutionName != nullptr
                    && evolutionValue != nullptr,
                "Evolution external labels were not found");

        const auto sliderBounds = editor->getLocalArea(
            evolutionSliderForLayout, evolutionSliderForLayout->getLocalBounds());
        const auto nameBounds = editor->getLocalArea(evolutionName,
                                                     evolutionName->getLocalBounds());
        const auto valueBounds = editor->getLocalArea(evolutionValue,
                                                      evolutionValue->getLocalBounds());
        require(nameBounds.getY() >= sliderBounds.getBottom(),
                "Evolution name must be below the hero dial");
        require(valueBounds.getY() >= nameBounds.getBottom() - 1,
                "Evolution value must be below its name");
    }

    auto* evolutionSlider = dynamic_cast<juce::Slider*>(
        findDescendantById(*editor, "evolution"));
    require(evolutionSlider != nullptr, "Evolution slider was not found");
    parameterById(processor, "evolution").setValueNotifyingHost(0.73f);
    require(std::abs(evolutionSlider->getValue() - 73.0) <= 0.11,
            "Host Evolution did not update the custom slider");
    evolutionSlider->setValue(62.5, juce::sendNotificationSync);
    require(std::abs(parameterById(processor, "evolution").getValue() - 0.625f) <= 0.001f,
            "Custom Evolution slider did not update the host parameter");

    auto* mixValueLabel = dynamic_cast<juce::Label*>(
        findDescendantById(*editor, "mix-value"));
    require(mixValueLabel != nullptr, "Editable Mix value label was not found");
    GestureProbe gestureProbe;
    processor.addListener(&gestureProbe);
    mixValueLabel->setText("42.5 %", juce::sendNotificationSync);
    processor.removeListener(&gestureProbe);
    require(std::abs(parameterById(processor, "mix").getValue() - 0.425f) <= 0.001f,
            "Typed Mix value did not update the host parameter");
    require(gestureProbe.beginCount == 1 && gestureProbe.endCount == 1,
            "Typed value edit did not produce one complete host gesture");

    auto* focusSlider = dynamic_cast<juce::Slider*>(
        findDescendantById(*editor, "focus"));
    require(focusSlider != nullptr, "Focus slider was not found");
    parameterById(processor, "focus").setValueNotifyingHost(0.64f);
    require(std::abs(focusSlider->getValue() - 64.0) <= 0.11,
            "Host Focus did not update the custom slider");
    focusSlider->setValue(37.5, juce::sendNotificationSync);
    require(std::abs(parameterById(processor, "focus").getValue() - 0.375f) <= 0.001f,
            "Custom Focus slider did not update the host parameter");

    auto* veilButton = dynamic_cast<juce::Button*>(
        findDescendantById(*editor, "character-veil"));
    require(veilButton != nullptr, "Veil selector button was not found");
    require(!veilButton->getWantsKeyboardFocus(),
            "Character segment must not create a keyboard-focus trap");
    veilButton->onClick();
    require(algorithmParameter(processor).getIndex() == 3,
            "Custom Character selector did not update the host parameter");

    auto* freezeButton = dynamic_cast<juce::ToggleButton*>(
        findDescendantById(*editor, "freeze"));
    require(freezeButton != nullptr, "Freeze toggle was not found");
    freezeButton->setToggleState(true, juce::sendNotificationSync);
    require(parameterById(processor, "freeze").getValue() > 0.5f,
            "Custom Freeze toggle did not update the host parameter");
}

void renderEditorPng(const juce::String& path, int characterIndex, int requestedWidth)
{
    AmanitaOceanAudioProcessor processor;
    const auto safeIndex = std::clamp(characterIndex, 0, 3);
    algorithmParameter(processor).setValueNotifyingHost(static_cast<float>(safeIndex) / 3.0f);
    parameterById(processor, "evolution").setValueNotifyingHost(0.68f);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Could not create editor for PNG render");
    const auto width = std::clamp(requestedWidth,
                                  AmanitaOceanAudioProcessorEditor::minimumWidth,
                                  AmanitaOceanAudioProcessorEditor::maximumWidth);
    const auto height = width * AmanitaOceanAudioProcessorEditor::defaultHeight
                      / AmanitaOceanAudioProcessorEditor::defaultWidth;
    editor->setSize(width, height);

    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f,
                                                        juce::SoftwareImageType {});
    require(image.isValid(), "Custom editor PNG snapshot is invalid");
    juce::File output(path);
    output.deleteFile();
    juce::FileOutputStream stream(output);
    require(stream.openedOk(), "Could not open custom editor PNG output");
    juce::PNGImageFormat png;
    require(png.writeImageToStream(image, stream), "Could not write custom editor PNG");
    stream.flush();
}

std::vector<float> renderProcessor(const AlgorithmCase& algorithmCase,
                                   float evolution,
                                   float focus = 0.0f)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    AmanitaOceanAudioProcessor processor;
    algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
    parameterById(processor, "mix").setValueNotifyingHost(1.0f);
    parameterById(processor, "preDelay").setValueNotifyingHost(0.0f);
    parameterById(processor, "evolution").setValueNotifyingHost(evolution);
    parameterById(processor, "focus").setValueNotifyingHost(focus);
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

std::vector<float> renderDsp(const AlgorithmCase& algorithmCase,
                             float evolution,
                             float focus = 0.0f)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    amanita::dsp::ReverbParameters parameters;
    parameters.mode = algorithmCase.mode;
    parameters.mix = 1.0f;
    parameters.preDelayMs = 0.0f;
    parameters.evolution = evolution;
    parameters.ducking = focus;

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

void testFocusParameterReachesDsp()
{
    constexpr auto focus = 0.78f;
    const auto& algorithmCase = algorithmCases.front();
    const auto processorRender = renderProcessor(algorithmCase, 0.35f, focus);
    const auto directRender = renderDsp(algorithmCase, 0.35f, focus);
    const auto bypassRender = renderProcessor(algorithmCase, 0.35f, 0.0f);
    require(processorRender.size() == directRender.size()
                && processorRender.size() == bypassRender.size(),
            "Focus routing render has the wrong size");

    auto maximumDifference = 0.0f;
    double bypassEnergy = 0.0;
    double duckingDifferenceEnergy = 0.0;
    for (std::size_t sample = 0; sample < processorRender.size(); ++sample)
    {
        require(std::isfinite(processorRender[sample]),
                "Focus routing produced NaN/Inf");
        maximumDifference = std::max(maximumDifference,
                                     std::abs(processorRender[sample] - directRender[sample]));
        const auto bypass = static_cast<double>(bypassRender[sample]);
        const auto difference = static_cast<double>(processorRender[sample]
                                                    - bypassRender[sample]);
        bypassEnergy += bypass * bypass;
        duckingDifferenceEnergy += difference * difference;
    }

    require(maximumDifference <= 2.0e-7f,
            "Host Focus parameter does not reach the expected DSP amount");
    require(bypassEnergy > 1.0e-10
                && std::sqrt(duckingDifferenceEnergy / bypassEnergy) >= 0.05,
            "Non-zero host Focus parameter has no meaningful DSP effect");
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

    try
    {
        testUnifiedHostContract();
        testCurrentStateRoundTrip();
        testCustomEditorLayoutAndAttachments();
        testUnifiedAlgorithmReachesDsp();
        testFocusParameterReachesDsp();
        if (argc >= 3 && std::strcmp(argv[1], "--render-ui") == 0)
        {
            const auto characterIndex = argc >= 4 ? std::atoi(argv[3]) : 0;
            const auto requestedWidth = argc >= 5
                ? std::atoi(argv[4])
                : AmanitaOceanAudioProcessorEditor::defaultWidth;
            renderEditorPng(argv[2], characterIndex, requestedWidth);
            std::cout << "[PASS] wrote custom editor PNG to " << argv[2] << '\n';
        }
        std::cout << "[PASS] Unified Algorithm/Evolution/Focus state/UI/DSP routing\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Unified Algorithm/Evolution/Focus state/UI/DSP routing: "
                  << error.what() << '\n';
        return 1;
    }
}
