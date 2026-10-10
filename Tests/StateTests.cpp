#include "PluginProcessor.h"
#include "dsp/FathomNetwork.h"
#include "ui/CharacterPalette.h"
#include "ui/DeepCurrentRenderer.h"
#include "ui/PluginEditor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_opengl/juce_opengl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
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
    AlgorithmCase { "Default",  0.0f,        0, amanita::dsp::ReverbMode::defaultMode },
    AlgorithmCase { "Bloom",    1.0f / 7.0f, 1, amanita::dsp::ReverbMode::bloom },
    AlgorithmCase { "Drift",    2.0f / 7.0f, 2, amanita::dsp::ReverbMode::drift },
    AlgorithmCase { "Veil",     3.0f / 7.0f, 3, amanita::dsp::ReverbMode::veil },
    AlgorithmCase { "Current",  4.0f / 7.0f, 4, amanita::dsp::ReverbMode::current },
    AlgorithmCase { "Fathom",   5.0f / 7.0f, 5, amanita::dsp::ReverbMode::fathom },
    AlgorithmCase { "Undertow", 6.0f / 7.0f, 6, amanita::dsp::ReverbMode::undertow },
    AlgorithmCase { "Spume",    1.0f,        7, amanita::dsp::ReverbMode::spume }
};

static_assert(static_cast<std::size_t>(
                  amanita::ui::DeepCurrentRenderer::characterCount)
              == algorithmCases.size());
static_assert(static_cast<std::size_t>(
                  amanita::ui::CharacterSelector::characterCount)
              == algorithmCases.size());

constexpr auto lastCharacterIndex = static_cast<int>(algorithmCases.size()) - 1;

// The places of Fathom, Undertow and Spume among the Characters: the sixth,
// the seventh and the eighth.
constexpr auto fathomIndex = 5;
constexpr auto undertowIndex = 6;
constexpr auto spumeIndex = 7;
static_assert(algorithmCases[fathomIndex].mode == amanita::dsp::ReverbMode::fathom);
static_assert(algorithmCases[undertowIndex].mode == amanita::dsp::ReverbMode::undertow);
static_assert(algorithmCases[spumeIndex].mode == amanita::dsp::ReverbMode::spume);

// The texts of the description block as the owner approved them, in the order
// of the Characters.
struct ApprovedDescription
{
    const char* name;
    const char* subtitle;
    const char* paragraph;
};

constexpr std::array<ApprovedDescription, algorithmCases.size()> approvedDescriptions {{
    { "DEFAULT",
      "8-LINE FEEDBACK DELAY NETWORK",
      "Open water on a windless day. The sound settles into a clear, even space and fades "
      "without leaving a colour of its own. Evolution stirs the surface, slowly, from below." },
    { "BLOOM",
      "RISING TAPS, DOUBLE DIFFUSION",
      "The note goes under, and a moment later the water answers. A slow swell rises behind "
      "every sound and opens like something blooming in the dark. Evolution lets it rise "
      "longer and fuller." },
    { "DRIFT",
      "SPECTRAL FEEDBACK KERNELS",
      "A tail that never stays where it began. It leaves the surface as light and air and "
      "sinks, turn by turn, into warmth and weight. Evolution sends it further on its way." },
    { "VEIL",
      "ALL-PASS TRANSIENT DISPERSER",
      "Every attack dissolves before it lands, like a shape seen through moving water. Drums "
      "and plucked strings arrive as soft clouds, their edges gone. Evolution draws the veil "
      "closer." },
    { "CURRENT",
      "COHERENT FLOW FIELD",
      "One slow current carries the whole space with it. Colour, depth and direction turn "
      "together, the way a body of water turns: wide, liquid, never still. Evolution sets "
      "how hard it pulls." },
    { "FATHOM",
      "MODELLED 16-LINE TIDAL NETWORK",
      "Deep water with a tide of its own. The space opens and closes in long, slow breaths, "
      "dense and smooth as the dark below the light. Evolution brings the tide in; at zero "
      "the depth stands still." },
    { "UNDERTOW",
      "REVERSED VOICES IN TEMPO",
      "What you just played is pulled back under and returned in reverse: at pitch, an "
      "octave above, an octave below, in step with the tempo of the song. Evolution lets the "
      "three voices in, one by one." },
    { "SPUME",
      "28 ALL-PASS INPUT DIFFUSER",
      "Every sound breaks before it reaches the deep. The strike dissolves into a fine, slow "
      "spray that hangs for a moment and then sinks into the water below. Evolution turns "
      "the wave from a clean strike into pure spume." }
}};

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

struct HostDisplayProbe final : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}

    void audioProcessorChanged(juce::AudioProcessor*,
                               const ChangeDetails& details) override
    {
        ++changeCount;
        programChanged = programChanged || details.programChanged;
        latencyChanged = latencyChanged || details.latencyChanged;
        parameterInfoChanged = parameterInfoChanged || details.parameterInfoChanged;
        nonParameterStateChanged
            = nonParameterStateChanged || details.nonParameterStateChanged;
    }

    int changeCount = 0;
    bool programChanged = false;
    bool latencyChanged = false;
    bool parameterInfoChanged = false;
    bool nonParameterStateChanged = false;
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
    constexpr std::array<const char*, 13> expectedIds {
        "algorithm", "mix", "decay", "size", "preDelay", "lowCut",
        "highDamping", "evolution", "width", "focus", "freeze", "harmony",
        "monoSafe"
    };

    const auto& parameters = processor.getParameters();
    require(parameters.size() == static_cast<int>(expectedIds.size()),
            "Host must expose exactly thirteen parameters");

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
            "Algorithm must contain exactly eight choices");
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
    require(std::abs(focus->get() - 100.0f) <= 1.0e-6f,
            "Focus does not default to 100 percent");
    require(focus->getLabel() == "%", "Focus unit label changed");

    auto* harmony = dynamic_cast<juce::AudioParameterFloat*>(
        findParameterById(processor, "harmony"));
    require(harmony != nullptr, "Harmony is not a float parameter");
    const auto& harmonyRange = harmony->getNormalisableRange();
    require(harmony->getName(128) == "Harmony", "Harmony UI label changed");
    require(std::abs(harmonyRange.start) <= 1.0e-6f
                && std::abs(harmonyRange.end - 100.0f) <= 1.0e-6f,
            "Harmony range must be 0..100 percent");
    require(std::abs(harmony->get()) <= 1.0e-6f,
            "Harmony does not default to zero percent");
    require(harmony->getLabel() == "%", "Harmony unit label changed");

    auto* monoSafe = dynamic_cast<juce::AudioParameterBool*>(
        findParameterById(processor, "monoSafe"));
    require(monoSafe != nullptr, "Mono Safe is not a boolean parameter");
    require(monoSafe->getName(128) == "Mono Safe",
            "Mono Safe UI label changed");
    require(!monoSafe->get(), "Mono Safe must default to Off");

    auto* size = dynamic_cast<juce::AudioParameterFloat*>(
        findParameterById(processor, "size"));
    require(size != nullptr, "Size is not a float parameter");
    const auto& sizeRange = size->getNormalisableRange();
    require(size->getName(128) == "Size", "Size UI label changed");
    require(std::abs(sizeRange.start) <= 1.0e-6f
                && std::abs(sizeRange.end - 200.0f) <= 1.0e-6f,
            "Size range must be 0..200 percent");
    require(std::abs(sizeRange.interval - 0.1f) <= 1.0e-6f,
            "Size host interval must remain 0.1 percent");
    require(std::abs(size->get() - 100.0f) <= 1.0e-6f,
            "Size does not default to 100 percent");
    require(size->getLabel() == "%", "Size unit label changed");

    const auto requireContinuousTravel = [&](const char* parameterId,
                                             float expectedMinimum,
                                             float expectedMaximum,
                                             float expectedCentre,
                                             float expectedDefault,
                                             float firstDisplayedIncrement)
    {
        auto* parameter = dynamic_cast<juce::AudioParameterFloat*>(
            findParameterById(processor, parameterId));
        require(parameter != nullptr,
                std::string(parameterId) + " is not a float parameter");
        const auto& range = parameter->getNormalisableRange();
        require(std::abs(range.interval) <= 1.0e-9f,
                std::string(parameterId) + " travel is quantised");
        require(std::abs(range.start - expectedMinimum) <= 1.0e-6f
                    && std::abs(range.end - expectedMaximum) <= 1.0e-6f,
                std::string(parameterId) + " range changed unexpectedly");
        require(std::abs(range.convertFrom0to1(0.5f) - expectedCentre) <= 1.0e-4f,
                std::string(parameterId) + " skew midpoint changed unexpectedly");
        require(std::abs(parameter->get() - expectedDefault) <= 1.0e-6f,
                std::string(parameterId) + " default changed unexpectedly");

        auto previousPosition = -1.0f;
        constexpr auto dragDistance = 250;
        for (auto pixel = 0; pixel <= dragDistance; ++pixel)
        {
            const auto requestedPosition = static_cast<float>(pixel)
                                         / static_cast<float>(dragDistance);
            const auto snappedValue = range.snapToLegalValue(
                range.convertFrom0to1(requestedPosition));
            const auto actualPosition = range.convertTo0to1(snappedValue);
            require(std::abs(actualPosition - requestedPosition) <= 1.0e-4f,
                    std::string(parameterId)
                        + " jumps during normalized rotary travel");
            require(pixel == 0 || actualPosition > previousPosition,
                    std::string(parameterId)
                        + " has a dead zone at the start of rotary travel");
            previousPosition = actualPosition;
        }

        auto previousValue = expectedMinimum;
        constexpr auto denseSteps = 4096;
        for (auto step = 0; step <= denseSteps; ++step)
        {
            const auto position = static_cast<float>(step)
                                / static_cast<float>(denseSteps);
            const auto value = range.convertFrom0to1(position);
            require(std::isfinite(value)
                        && value >= expectedMinimum
                        && value <= expectedMaximum,
                    std::string(parameterId) + " mapping left its finite range");
            require(step == 0 || value > previousValue,
                    std::string(parameterId) + " mapping is not strictly monotonic");
            require(std::abs(range.convertTo0to1(value) - position) <= 2.0e-6f,
                    std::string(parameterId) + " mapping does not round-trip smoothly");
            previousValue = value;
        }

        const auto firstIncrementPixels
            = range.convertTo0to1(firstDisplayedIncrement)
            * static_cast<float>(dragDistance);
        require(firstIncrementPixels >= 1.0f && firstIncrementPixels <= 5.0f,
                std::string(parameterId)
                    + " spends too much rotary travel near its minimum");
        const auto firstPixelValue = range.convertFrom0to1(1.0f / dragDistance);
        const auto secondPixelValue = range.convertFrom0to1(2.0f / dragDistance);
        const auto firstLabelThreshold = (expectedMinimum + firstDisplayedIncrement) * 0.5f;
        require(firstPixelValue < firstLabelThreshold
                    && secondPixelValue >= firstLabelThreshold,
                std::string(parameterId)
                    + " first displayed increment is not reached near two pixels");
    };

    requireContinuousTravel("decay", 0.2f, 30.0f, 3.0f, 5.0f, 0.21f);
    requireContinuousTravel("lowCut", 20.0f, 1000.0f, 120.0f, 80.0f, 21.0f);
    require(parameterById(processor, "decay").getText(
                parameterById(processor, "decay").getValue(), 128) == "5.00",
            "Decay host text became excessively precise");
    require(parameterById(processor, "lowCut").getText(
                parameterById(processor, "lowCut").getValue(), 128) == "80",
            "Low Cut host text became excessively precise");
    auto* lowCut = dynamic_cast<juce::AudioParameterFloat*>(
        findParameterById(processor, "lowCut"));
    require(lowCut != nullptr
                && parameterById(processor, "lowCut").getText(
                       lowCut->convertTo0to1(26.3896f), 128) == "26",
            "Fractional Low Cut host value is not displayed as whole hertz");
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
        parameterById(source, "harmony").setValueNotifyingHost(0.42f);
        parameterById(source, "monoSafe").setValueNotifyingHost(1.0f);

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
        const auto savedHarmony = findParameterState(savedState, "harmony");
        require(savedHarmony.isValid(), "Saved state has no Harmony node");
        require(std::abs(static_cast<float>(savedHarmony.getProperty("value"))
                         - 42.0f) < 0.001f,
                "Saved Harmony value is wrong");
        const auto savedMonoSafe = findParameterState(savedState, "monoSafe");
        require(savedMonoSafe.isValid(), "Saved state has no Mono Safe node");
        require(static_cast<float>(savedMonoSafe.getProperty("value")) > 0.5f,
                "Saved Mono Safe value is wrong");
        for (const auto* removedId : removedIds)
            require(!findParameterState(savedState, removedId).isValid(),
                    std::string("Saved state still contains removed parameter: ")
                        + removedId);

        AmanitaOceanAudioProcessor restored;
        HostDisplayProbe hostDisplayProbe;
        restored.addListener(&hostDisplayProbe);
        restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
        restored.removeListener(&hostDisplayProbe);
        require(hostDisplayProbe.changeCount == 1 && hostDisplayProbe.programChanged,
                "State load did not notify the host to refresh parameter values");
        require(!hostDisplayProbe.latencyChanged
                    && !hostDisplayProbe.parameterInfoChanged
                    && !hostDisplayProbe.nonParameterStateChanged,
                "State load requested unrelated host refreshes");
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
        require(std::abs(parameterById(restored, "harmony").getValue() - 0.42f) < 0.001f,
                "Harmony did not survive save/load");
        require(parameterById(restored, "monoSafe").getValue() > 0.5f,
                "Mono Safe did not survive save/load");
    }

    // Projects saved before Mono Safe became a host parameter have no matching
    // ValueTree child. They must keep every existing setting and receive the
    // new feature's requested Off default.
    AmanitaOceanAudioProcessor legacySource;
    algorithmParameter(legacySource).setValueNotifyingHost(algorithmCases[2].hostValue);
    parameterById(legacySource, "mix").setValueNotifyingHost(0.812f);
    juce::MemoryBlock currentData;
    legacySource.getStateInformation(currentData);
    auto legacyState = decodeState(currentData);
    const auto monoSafeState = findParameterState(legacyState, "monoSafe");
    require(monoSafeState.isValid(), "Current state has no Mono Safe node to remove");
    legacyState.removeChild(monoSafeState, nullptr);
    juce::MemoryBlock legacyData;
    if (const auto xml = legacyState.createXml())
        juce::AudioProcessor::copyXmlToBinary(*xml, legacyData);
    require(!legacyData.isEmpty(), "Could not create pre-Mono-Safe state");

    AmanitaOceanAudioProcessor legacyRestored;
    legacyRestored.setStateInformation(
        legacyData.getData(), static_cast<int>(legacyData.getSize()));
    require(algorithmParameter(legacyRestored).getIndex() == 2
                && std::abs(parameterById(legacyRestored, "mix").getValue()
                            - 0.812f)
                       < 0.001f,
            "Existing project settings changed while adding Mono Safe");
    require(parameterById(legacyRestored, "monoSafe").getValue() < 0.5f,
            "Existing projects do not receive Mono Safe Off by default");
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

[[nodiscard]] juce::Colour backgroundAccent(int characterIndex) noexcept
{
    constexpr std::array<std::uint32_t, algorithmCases.size()> colours {
        0xff81bfc7, 0xffc89c83, 0xff829de0, 0xffb3a6c4, 0xff74c6a8, 0xff2f7fe0,
        0xff6672f2, 0xff1f9be0
    };
    return juce::Colour(colours[static_cast<std::size_t>(
        std::clamp(characterIndex, 0, lastCharacterIndex))]);
}

// Contrast ratio of two opaque colours as WCAG 2 defines it, from 1 to 21.
[[nodiscard]] double contrastRatio(juce::Colour first, juce::Colour second) noexcept
{
    const auto luminance = [](juce::Colour colour)
    {
        const auto linear = [](int channel)
        {
            const auto value = channel / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(colour.getRed()) + 0.7152 * linear(colour.getGreen())
             + 0.0722 * linear(colour.getBlue());
    };
    const auto lighter = std::max(luminance(first), luminance(second));
    const auto darker = std::min(luminance(first), luminance(second));
    return (lighter + 0.05) / (darker + 0.05);
}

// The centre of the Evolution dial on the editor's 960 x 640 canvas, round
// which the editor gathers its background.
constexpr auto editorFocalX = 614.0f / 960.0f;
constexpr auto editorFocalY = 296.0f / 640.0f;

[[nodiscard]] juce::Image renderBackgroundBase(int width, int height)
{
    juce::Image image(juce::Image::ARGB, width, height, true,
                      juce::SoftwareImageType {});
    juce::Graphics graphics(image);
    const auto bounds = image.getBounds().toFloat();
    juce::ColourGradient background(amanita::ui::OceanLookAndFeel::backgroundTop(),
                                    bounds.getTopLeft(),
                                    amanita::ui::OceanLookAndFeel::backgroundBottom(),
                                    bounds.getBottomLeft(),
                                    false);
    background.addColour(0.58, juce::Colour::fromRGB(9, 23, 26));
    graphics.setGradientFill(background);
    graphics.fillRect(bounds);
    return image;
}

[[nodiscard]] juce::Image paintBackgroundFrame(
    const amanita::ui::DeepCurrentRenderer& renderer,
    int width,
    int height)
{
    auto image = renderBackgroundBase(width, height);
    juce::Graphics graphics(image);
    renderer.paint(graphics, image.getBounds().toFloat());
    return image;
}

[[nodiscard]] juce::Image renderBackgroundFrame(int width,
                                                int height,
                                                int characterIndex,
                                                float evolution,
                                                bool frozen,
                                                double timeSeconds,
                                                juce::Colour accent,
                                                float currentFlowX = 0.0f,
                                                float currentFlowY = 0.0f,
                                                float currentStrength = 0.0f)
{
    amanita::ui::DeepCurrentRenderer renderer;
    renderer.setCurrentFieldSnapshot(currentFlowX, currentFlowY,
                                     currentStrength);
    renderer.setFocalPoint(editorFocalX, editorFocalY);
    renderer.reset(characterIndex, evolution, frozen, timeSeconds);
    renderer.setSize(width, height);
    renderer.render(accent);
    require(renderer.hasFrame(), "Deep Current did not create a CPU frame");
    require(std::abs(renderer.getTimeSeconds() - timeSeconds) <= 1.0e-9,
            "Deep Current reset did not preserve its explicit render time");
    return paintBackgroundFrame(renderer, width, height);
}

struct ImageDifference
{
    double normalisedMean = 0.0;
    double normalisedMaximum = 0.0;
    std::uint64_t changedPixels = 0;
    std::uint64_t perceptiblePixels = 0;
    std::uint64_t strongPixels = 0;
    std::uint64_t totalPixels = 0;
};

[[nodiscard]] ImageDifference measureImageDifferenceInRegion(
    const juce::Image& first,
    const juce::Image& second,
    juce::Rectangle<int> requestedRegion)
{
    require(first.isValid() && second.isValid(),
            "Deep Current comparison received an invalid image");
    require(first.getBounds() == second.getBounds(),
            "Deep Current comparison image sizes differ");

    const auto region = requestedRegion.getIntersection(first.getBounds());
    require(!region.isEmpty(), "Deep Current comparison region is empty");

    double summedDifference = 0.0;
    auto maximumDifference = 0;
    std::uint64_t changedPixels = 0;
    std::uint64_t perceptiblePixels = 0;
    std::uint64_t strongPixels = 0;
    const juce::Image::BitmapData firstPixels(first,
                                              juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData secondPixels(second,
                                               juce::Image::BitmapData::readOnly);
    for (auto y = region.getY(); y < region.getBottom(); ++y)
    {
        for (auto x = region.getX(); x < region.getRight(); ++x)
        {
            const auto firstPixel = firstPixels.getPixelColour(x, y);
            const auto secondPixel = secondPixels.getPixelColour(x, y);
            const auto redDifference = std::abs(static_cast<int>(firstPixel.getRed())
                                                - static_cast<int>(secondPixel.getRed()));
            const auto greenDifference = std::abs(static_cast<int>(firstPixel.getGreen())
                                                  - static_cast<int>(secondPixel.getGreen()));
            const auto blueDifference = std::abs(static_cast<int>(firstPixel.getBlue())
                                                 - static_cast<int>(secondPixel.getBlue()));
            const auto pixelDifference = redDifference + greenDifference + blueDifference;
            summedDifference += static_cast<double>(pixelDifference);
            maximumDifference = std::max(maximumDifference,
                                         std::max({ redDifference,
                                                    greenDifference,
                                                    blueDifference }));
            if (pixelDifference != 0)
                ++changedPixels;
            const auto maximumChannelDifference = std::max({ redDifference,
                                                              greenDifference,
                                                              blueDifference });
            if (maximumChannelDifference >= 2)
                ++perceptiblePixels;
            if (maximumChannelDifference >= 4)
                ++strongPixels;
        }
    }

    const auto sampleCount = static_cast<double>(region.getWidth())
                           * static_cast<double>(region.getHeight()) * 3.0;
    return { summedDifference / (sampleCount * 255.0),
             static_cast<double>(maximumDifference) / 255.0,
             changedPixels,
             perceptiblePixels,
             strongPixels,
             static_cast<std::uint64_t>(region.getWidth())
                 * static_cast<std::uint64_t>(region.getHeight()) };
}

[[nodiscard]] ImageDifference measureImageDifference(const juce::Image& first,
                                                     const juce::Image& second)
{
    return measureImageDifferenceInRegion(first, second, first.getBounds());
}

void testDeepCurrentBackgroundRenderer()
{
    constexpr auto width = AmanitaOceanAudioProcessorEditor::defaultWidth;
    constexpr auto height = AmanitaOceanAudioProcessorEditor::defaultHeight;
    constexpr auto defaultCharacter = 0;
    constexpr auto drift = 2;
    constexpr auto veil = 3;
    constexpr auto current = 4;
    constexpr auto fathom = 5;
    constexpr auto undertow = 6;
    constexpr auto spume = 7;
    constexpr auto evolution = 0.82f;
    constexpr auto laterTime = 19.0;
    const auto accent = backgroundAccent(drift);
    const auto currentAccent = backgroundAccent(current);
    const auto fathomAccent = backgroundAccent(fathom);
    const auto undertowAccent = backgroundAccent(undertow);
    const auto spumeAccent = backgroundAccent(spume);

    const auto initial = renderBackgroundFrame(width, height, drift, evolution, false,
                                               0.0, accent);
    const auto repeated = renderBackgroundFrame(width, height, drift, evolution, false,
                                                0.0, accent);
    const auto later = renderBackgroundFrame(width, height, drift, evolution, false,
                                             laterTime, accent);
    const auto afterTwoSeconds = renderBackgroundFrame(width, height, drift, evolution, false,
                                                       2.0, accent);
    const auto veilAtSameTime = renderBackgroundFrame(width, height, veil, evolution, false,
                                                      laterTime, accent);
    constexpr auto currentFlowX = 0.62f;
    constexpr auto currentFlowY = -0.28f;
    constexpr auto currentStrength = 0.78f;
    const auto currentInitial = renderBackgroundFrame(
        width, height, current, evolution, false, 0.0, currentAccent,
        currentFlowX, currentFlowY, currentStrength);
    const auto currentRepeated = renderBackgroundFrame(
        width, height, current, evolution, false, 0.0, currentAccent,
        currentFlowX, currentFlowY, currentStrength);
    const auto currentLater = renderBackgroundFrame(
        width, height, current, evolution, false, laterTime, currentAccent,
        currentFlowX, currentFlowY, currentStrength);
    const auto currentAtSameTime = renderBackgroundFrame(width, height, current, evolution,
                                                         false, laterTime, accent,
                                                         currentFlowX, currentFlowY,
                                                         currentStrength);
    const auto currentOppositeField = renderBackgroundFrame(
        width, height, current, evolution, false, laterTime, currentAccent,
        -currentFlowX, -currentFlowY, currentStrength);
    const auto fathomInitial = renderBackgroundFrame(width, height, fathom, evolution, false,
                                                     0.0, fathomAccent);
    const auto fathomRepeated = renderBackgroundFrame(width, height, fathom, evolution, false,
                                                      0.0, fathomAccent);
    const auto fathomLater = renderBackgroundFrame(width, height, fathom, evolution, false,
                                                   laterTime, fathomAccent);
    const auto fathomAtSameTime = renderBackgroundFrame(width, height, fathom, evolution, false,
                                                        laterTime, accent);
    const auto defaultAtSameTime = renderBackgroundFrame(width, height, defaultCharacter,
                                                         evolution, false, laterTime, accent);
    const auto undertowInitial = renderBackgroundFrame(width, height, undertow, evolution,
                                                       false, 0.0, undertowAccent);
    const auto undertowRepeated = renderBackgroundFrame(width, height, undertow, evolution,
                                                        false, 0.0, undertowAccent);
    const auto undertowLater = renderBackgroundFrame(width, height, undertow, evolution,
                                                     false, laterTime, undertowAccent);
    const auto undertowAtSameTime = renderBackgroundFrame(width, height, undertow, evolution,
                                                          false, laterTime, accent);
    const auto spumeInitial = renderBackgroundFrame(width, height, spume, evolution, false,
                                                    0.0, spumeAccent);
    const auto spumeRepeated = renderBackgroundFrame(width, height, spume, evolution, false,
                                                     0.0, spumeAccent);
    const auto spumeLater = renderBackgroundFrame(width, height, spume, evolution, false,
                                                  laterTime, spumeAccent);
    const auto spumeAtSameTime = renderBackgroundFrame(width, height, spume, evolution, false,
                                                       laterTime, accent);
    const auto base = renderBackgroundBase(width, height);

    const auto repeatDifference = measureImageDifference(initial, repeated);
    const auto motionDifference = measureImageDifference(initial, later);
    const auto characterDifference = measureImageDifference(later, veilAtSameTime);
    const auto currentRepeatDifference = measureImageDifference(currentInitial,
                                                                currentRepeated);
    const auto currentMotionDifference = measureImageDifference(currentInitial,
                                                                currentLater);
    const auto currentCharacterDifference = measureImageDifference(later,
                                                                   currentAtSameTime);
    const auto currentFieldDifference = measureImageDifference(
        currentLater, currentOppositeField);
    const auto fathomRepeatDifference = measureImageDifference(fathomInitial, fathomRepeated);
    const auto fathomMotionDifference = measureImageDifference(fathomInitial, fathomLater);
    const auto fathomDriftDifference = measureImageDifference(later, fathomAtSameTime);
    const auto fathomDefaultDifference = measureImageDifference(defaultAtSameTime,
                                                                fathomAtSameTime);
    const auto undertowRepeatDifference = measureImageDifference(undertowInitial,
                                                                 undertowRepeated);
    const auto undertowMotionDifference = measureImageDifference(undertowInitial,
                                                                 undertowLater);
    const auto undertowDriftDifference = measureImageDifference(later, undertowAtSameTime);
    const auto undertowDefaultDifference = measureImageDifference(defaultAtSameTime,
                                                                  undertowAtSameTime);
    const auto undertowFathomDifference = measureImageDifference(fathomAtSameTime,
                                                                 undertowAtSameTime);
    // What each of the two draws over the base, in the same accent.
    const auto fathomPresence = measureImageDifference(fathomAtSameTime, base);
    const auto undertowPresence = measureImageDifference(undertowAtSameTime, base);
    const auto spumeRepeatDifference = measureImageDifference(spumeInitial, spumeRepeated);
    const auto spumeMotionDifference = measureImageDifference(spumeInitial, spumeLater);
    const auto spumeDriftDifference = measureImageDifference(later, spumeAtSameTime);
    const auto spumeDefaultDifference = measureImageDifference(defaultAtSameTime,
                                                               spumeAtSameTime);
    const auto spumeFathomDifference = measureImageDifference(fathomAtSameTime,
                                                              spumeAtSameTime);
    const auto spumeUndertowDifference = measureImageDifference(undertowAtSameTime,
                                                                spumeAtSameTime);
    const auto spumePresence = measureImageDifference(spumeAtSameTime, base);
    const auto overlayDifference = measureImageDifference(initial, base);
    const auto topRegion = juce::Rectangle<int>(0, 0, width, height / 4);
    const auto bottomRegion = juce::Rectangle<int>(0, height * 3 / 4,
                                                   width, height / 4);
    const auto leftRegion = juce::Rectangle<int>(0, 0, width / 4, height);
    const auto rightRegion = juce::Rectangle<int>(width * 3 / 4, 0,
                                                  width / 4, height);
    const auto topMotion = measureImageDifferenceInRegion(initial, later, topRegion);
    const auto bottomMotion = measureImageDifferenceInRegion(initial, later, bottomRegion);
    const auto leftMotion = measureImageDifferenceInRegion(initial, later, leftRegion);
    const auto rightMotion = measureImageDifferenceInRegion(initial, later, rightRegion);
    const auto topPresence = measureImageDifferenceInRegion(initial, base, topRegion);
    const auto bottomPresence = measureImageDifferenceInRegion(initial, base, bottomRegion);

    require(repeatDifference.changedPixels == 0,
            "Deep Current is not pixel-deterministic for identical explicit inputs");
    require(motionDifference.normalisedMean > 1.0e-6
                && motionDifference.changedPixels > 1000,
            "Deep Current does not visibly evolve between fixed render times");
    require(motionDifference.normalisedMean < 0.02
                && motionDifference.normalisedMaximum < 0.20,
            "Deep Current fixed-time motion is too visually aggressive");
    require(characterDifference.normalisedMean > 1.0e-6
                && characterDifference.changedPixels > 1000,
            "Deep Current Drift and Veil frames are indistinguishable");
    require(currentRepeatDifference.changedPixels == 0,
            "Deep Current mode is not pixel-deterministic for identical inputs");
    require(currentMotionDifference.normalisedMean > 1.0e-6
                && currentMotionDifference.changedPixels > 1000,
            "Deep Current mode does not visibly evolve between fixed render times");
    require(currentMotionDifference.normalisedMean < 0.02
                && currentMotionDifference.normalisedMaximum < 0.20,
            "Deep Current mode fixed-time motion is too visually aggressive");
    require(currentCharacterDifference.normalisedMean > 1.0e-6
                && currentCharacterDifference.changedPixels > 1000,
            "Deep Current mode and Drift frames are indistinguishable");
    require(currentFieldDifference.normalisedMean > 1.0e-6
                && currentFieldDifference.changedPixels > 1000,
            "Deep Current renderer ignores the published DSP field direction");
    require(fathomRepeatDifference.changedPixels == 0,
            "Deep Current Fathom is not pixel-deterministic for identical inputs");
    require(fathomMotionDifference.normalisedMean > 1.0e-6
                && fathomMotionDifference.changedPixels > 1000,
            "Deep Current Fathom does not visibly evolve between fixed render times");
    require(fathomMotionDifference.normalisedMean < 0.02
                && fathomMotionDifference.normalisedMaximum < 0.20,
            "Deep Current Fathom fixed-time motion is too visually aggressive");
    require(fathomDriftDifference.normalisedMean > 1.0e-6
                && fathomDriftDifference.changedPixels > 1000,
            "Deep Current Fathom and Drift frames are indistinguishable");
    require(fathomDefaultDifference.normalisedMean > 1.0e-6
                && fathomDefaultDifference.changedPixels > 1000,
            "Deep Current Fathom and Default frames are indistinguishable");
    require(undertowRepeatDifference.changedPixels == 0,
            "Deep Current Undertow is not pixel-deterministic for identical inputs");
    require(undertowMotionDifference.normalisedMean > 1.0e-6
                && undertowMotionDifference.changedPixels > 1000,
            "Deep Current Undertow does not visibly evolve between fixed render times");
    require(undertowMotionDifference.normalisedMean < 0.02
                && undertowMotionDifference.normalisedMaximum < 0.20,
            "Deep Current Undertow fixed-time motion is too visually aggressive");
    require(undertowDriftDifference.normalisedMean > 1.0e-6
                && undertowDriftDifference.changedPixels > 1000,
            "Deep Current Undertow and Drift frames are indistinguishable");
    require(undertowDefaultDifference.normalisedMean > 1.0e-6
                && undertowDefaultDifference.changedPixels > 1000,
            "Deep Current Undertow and Default frames are indistinguishable");
    require(undertowFathomDifference.normalisedMean > 1.0e-6
                && undertowFathomDifference.changedPixels > 1000,
            "Deep Current Undertow and Fathom frames are indistinguishable");
    require(undertowPresence.normalisedMean > 0.0010
                && undertowPresence.normalisedMean < fathomPresence.normalisedMean,
            "Deep Current Undertow does not lie darker than Fathom over the base gradient");
    require(spumeRepeatDifference.changedPixels == 0,
            "Deep Current Spume is not pixel-deterministic for identical inputs");
    require(spumeMotionDifference.normalisedMean > 1.0e-6
                && spumeMotionDifference.changedPixels > 1000,
            "Deep Current Spume does not visibly evolve between fixed render times");
    require(spumeMotionDifference.normalisedMean < 0.02
                && spumeMotionDifference.normalisedMaximum < 0.20,
            "Deep Current Spume fixed-time motion is too visually aggressive");
    for (const auto& difference : { spumeDriftDifference, spumeDefaultDifference,
                                    spumeFathomDifference, spumeUndertowDifference })
        require(difference.normalisedMean > 1.0e-6 && difference.changedPixels > 1000,
                "Deep Current Spume frames are indistinguishable from those of Drift, Default, "
                "Fathom or Undertow");
    require(spumePresence.normalisedMean > fathomPresence.normalisedMean,
            "Deep Current Spume does not lie lighter than Fathom over the base gradient");
    require(overlayDifference.normalisedMean > 1.0e-6
                && overlayDifference.changedPixels > 1000,
            "Deep Current rendered no content over the base gradient");
    for (const auto& regionMotion : { topMotion, bottomMotion, leftMotion, rightMotion })
        require(regionMotion.normalisedMean > 0.0015
                    && regionMotion.changedPixels > 1000,
                "Deep Current motion does not cover the full editor bounds");
    require(topPresence.normalisedMean > 0.0010
                && bottomPresence.normalisedMean > 0.0010,
            "Deep Current is not visible behind both upper and lower controls");

    constexpr auto continuityWidth = 480;
    constexpr auto continuityHeight = 300;
    constexpr auto continuityFrames = 120;
    amanita::ui::DeepCurrentRenderer continuityRenderer;
    continuityRenderer.setFocalPoint(editorFocalX, editorFocalY);
    continuityRenderer.reset(drift, evolution, false, 11.0);
    continuityRenderer.setSize(continuityWidth, continuityHeight);
    continuityRenderer.render(accent);
    auto previousContinuityFrame = paintBackgroundFrame(continuityRenderer,
                                                        continuityWidth,
                                                        continuityHeight);
    std::vector<double> motionFrameDeltas;
    motionFrameDeltas.reserve(continuityFrames);
    for (auto frame = 0; frame < continuityFrames; ++frame)
    {
        require(continuityRenderer.advance(1.0 / 30.0, drift, evolution, false),
                "Deep Current stopped during its 30 FPS continuity test");
        continuityRenderer.render(accent);
        auto currentContinuityFrame = paintBackgroundFrame(continuityRenderer,
                                                           continuityWidth,
                                                           continuityHeight);
        motionFrameDeltas.push_back(measureImageDifference(previousContinuityFrame,
                                                            currentContinuityFrame)
                                        .normalisedMean);
        previousContinuityFrame = std::move(currentContinuityFrame);
    }

    const auto meanMotionFrameDelta = std::accumulate(motionFrameDeltas.begin(),
                                                       motionFrameDeltas.end(), 0.0)
                                    / static_cast<double>(motionFrameDeltas.size());
    const auto peakMotionFrameDelta = *std::max_element(motionFrameDeltas.begin(),
                                                        motionFrameDeltas.end());
    require(meanMotionFrameDelta > 1.0e-6,
            "Deep Current 30 FPS sequence contains no visible motion");
    require(peakMotionFrameDelta <= meanMotionFrameDelta * 1.75 + 1.0e-5,
            "Deep Current 30 FPS sequence contains an abrupt motion jump");
    for (std::size_t index = 1; index + 1 < motionFrameDeltas.size(); ++index)
        require(motionFrameDeltas[index]
                    <= 1.5 * std::max(motionFrameDeltas[index - 1],
                                      motionFrameDeltas[index + 1])
                       + 1.0e-5,
                "Deep Current 30 FPS sequence contains an isolated visual spike");

    constexpr std::array<int, 4> horizontalEdges { 0, width / 3, width * 2 / 3, width };
    constexpr std::array<int, 4> verticalEdges { 0, 144, 484, height };
    auto minimumLongMean = 1.0;
    auto minimumLongStrongCoverage = 1.0;
    auto minimumShortMean = 1.0;
    auto minimumShortPerceptibleCoverage = 1.0;
    for (auto row = 0; row < 3; ++row)
    {
        for (auto column = 0; column < 3; ++column)
        {
            const auto cell = juce::Rectangle<int>::leftTopRightBottom(
                horizontalEdges[static_cast<std::size_t>(column)],
                verticalEdges[static_cast<std::size_t>(row)],
                horizontalEdges[static_cast<std::size_t>(column + 1)],
                verticalEdges[static_cast<std::size_t>(row + 1)]);
            const auto longMotion = measureImageDifferenceInRegion(initial, later, cell);
            const auto shortMotion = measureImageDifferenceInRegion(initial,
                                                                    afterTwoSeconds,
                                                                    cell);
            const auto longStrongCoverage = static_cast<double>(longMotion.strongPixels)
                                          / static_cast<double>(longMotion.totalPixels);
            const auto shortPerceptibleCoverage
                = static_cast<double>(shortMotion.perceptiblePixels)
                / static_cast<double>(shortMotion.totalPixels);
            minimumLongMean = std::min(minimumLongMean, longMotion.normalisedMean);
            minimumLongStrongCoverage = std::min(minimumLongStrongCoverage,
                                                 longStrongCoverage);
            minimumShortMean = std::min(minimumShortMean, shortMotion.normalisedMean);
            minimumShortPerceptibleCoverage = std::min(minimumShortPerceptibleCoverage,
                                                       shortPerceptibleCoverage);
        }
    }
    require(minimumLongMean >= 0.0020 && minimumLongStrongCoverage >= 0.05,
            "Deep Current long-term motion does not cover every UI region visibly");
    require(minimumShortMean >= 0.0015 && minimumShortPerceptibleCoverage >= 0.10,
            "Deep Current motion is not perceptible across every UI region within two seconds");

    amanita::ui::DeepCurrentRenderer frozenRenderer;
    frozenRenderer.reset(drift, evolution, true, laterTime);
    require(!frozenRenderer.advance(1.0 / 24.0, drift, evolution, true),
            "Deep Current remains dirty after reaching a stable Freeze");
    require(frozenRenderer.advance(1.0 / 24.0, drift, evolution, false),
            "Deep Current does not resume smoothly after Freeze");

    amanita::ui::DeepCurrentRenderer deceleratingRenderer;
    deceleratingRenderer.reset(drift, evolution, false, laterTime);
    auto freezeSteps = 0;
    while (freezeSteps < 96
           && deceleratingRenderer.advance(1.0 / 24.0, drift, evolution, true))
        ++freezeSteps;
    const auto freezeStopSeconds = static_cast<double>(freezeSteps) / 24.0;
    require(freezeStopSeconds >= 1.20 && freezeStopSeconds <= 1.80,
            "Deep Current Freeze does not settle near its 1.5-second target");

    constexpr std::array<std::array<int, 4>, 3> sizes {{
        { AmanitaOceanAudioProcessorEditor::minimumWidth,
          AmanitaOceanAudioProcessorEditor::minimumHeight,
          804, 536 },
        { AmanitaOceanAudioProcessorEditor::defaultWidth,
          AmanitaOceanAudioProcessorEditor::defaultHeight,
          960, 640 },
        { AmanitaOceanAudioProcessorEditor::maximumWidth,
          AmanitaOceanAudioProcessorEditor::maximumHeight,
          1125, 750 }
    }};
    constexpr auto resizeTime = 7.25;
    constexpr auto resizeEvolution = 0.71f;
    const auto resizeReference = renderBackgroundFrame(width, height, drift,
                                                       resizeEvolution, false,
                                                       resizeTime, accent);
    amanita::ui::DeepCurrentRenderer resizedRenderer;
    resizedRenderer.setFocalPoint(editorFocalX, editorFocalY);
    resizedRenderer.reset(drift, resizeEvolution, false, resizeTime);
    for (const auto& size : sizes)
    {
        resizedRenderer.setSize(size[0], size[1]);
        require(resizedRenderer.getFrameWidth() == size[2]
                    && resizedRenderer.getFrameHeight() == size[3],
                "Deep Current cache resolution regressed while resizing");
        resizedRenderer.render(accent);
        require(resizedRenderer.hasFrame(),
                "Deep Current lost its CPU frame while resizing");
        const auto resizedImage = paintBackgroundFrame(resizedRenderer, size[0], size[1]);
        require(resizedImage.isValid()
                    && resizedImage.getWidth() == size[0]
                    && resizedImage.getHeight() == size[1],
                "Deep Current resize produced the wrong output dimensions");
        const auto resizedBase = renderBackgroundBase(size[0], size[1]);
        require(measureImageDifference(resizedImage, resizedBase).changedPixels > 1000,
                "Deep Current resize produced an empty overlay");
    }

    resizedRenderer.setSize(width, height);
    resizedRenderer.reset(drift, resizeEvolution, false, resizeTime);
    resizedRenderer.render(accent);
    const auto returnedToDefault = paintBackgroundFrame(resizedRenderer, width, height);
    const auto resizeReturnDifference = measureImageDifference(resizeReference,
                                                               returnedToDefault);
    require(resizeReturnDifference.changedPixels == 0,
            "Deep Current did not return deterministically after cache resizes");

    const auto testEvolutionSweep = [&](float start, float finish)
    {
        constexpr auto sweepFrames = 60;
        constexpr auto sweepWidth = 480;
        constexpr auto sweepHeight = 300;
        amanita::ui::DeepCurrentRenderer sweepRenderer;
        sweepRenderer.setFocalPoint(editorFocalX, editorFocalY);
        sweepRenderer.reset(drift, start, true, 11.0);
        sweepRenderer.setSize(sweepWidth, sweepHeight);
        sweepRenderer.render(accent);
        const auto firstFrame = paintBackgroundFrame(sweepRenderer,
                                                     sweepWidth,
                                                     sweepHeight);
        auto previousFrame = firstFrame;
        auto previousEvolution = sweepRenderer.getEvolution();
        std::vector<double> frameDeltas;
        frameDeltas.reserve(sweepFrames);

        for (auto frame = 1; frame <= sweepFrames; ++frame)
        {
            const auto position = static_cast<float>(frame)
                                / static_cast<float>(sweepFrames);
            const auto target = start + (finish - start) * position;
            require(sweepRenderer.advance(1.0 / 30.0, drift, target, true),
                    "Deep Current Evolution sweep stopped changing prematurely");
            const auto currentEvolution = sweepRenderer.getEvolution();
            require(finish > start ? currentEvolution > previousEvolution
                                   : currentEvolution < previousEvolution,
                    "Deep Current Evolution smoothing is not monotonic");
            require(sweepRenderer.needsHighRefresh(drift, target, true),
                    "Deep Current left high-refresh mode during an Evolution gesture");
            previousEvolution = currentEvolution;

            sweepRenderer.render(accent);
            auto currentFrame = paintBackgroundFrame(sweepRenderer,
                                                     sweepWidth,
                                                     sweepHeight);
            frameDeltas.push_back(measureImageDifference(previousFrame,
                                                         currentFrame).normalisedMean);
            previousFrame = std::move(currentFrame);
        }

        const auto endpointDelta = measureImageDifference(firstFrame,
                                                          previousFrame).normalisedMean;
        const auto peakFrameDelta = *std::max_element(frameDeltas.begin(),
                                                      frameDeltas.end());
        std::cout << "[METRIC] Evolution sweep " << start << " -> " << finish
                  << ": endpoint=" << endpointDelta
                  << ", peak frame=" << peakFrameDelta << '\n';
        require(endpointDelta > 1.0e-4,
                "Deep Current Evolution no longer changes the background visibly");
        require(peakFrameDelta <= endpointDelta * 0.15 + 1.0e-5,
                "Deep Current Evolution contains an abrupt frame-to-frame jump");
        for (std::size_t index = 1; index + 1 < frameDeltas.size(); ++index)
            require(frameDeltas[index]
                        <= 1.75 * std::max(frameDeltas[index - 1],
                                          frameDeltas[index + 1])
                           + 1.0e-5,
                    "Deep Current Evolution contains an isolated visual spike");

        auto settleFrames = 0;
        while (settleFrames < 120
               && sweepRenderer.needsHighRefresh(drift, finish, true))
        {
            static_cast<void>(sweepRenderer.advance(1.0 / 30.0,
                                                    drift,
                                                    finish,
                                                    true));
            ++settleFrames;
        }
        require(!sweepRenderer.needsHighRefresh(drift, finish, true),
                "Deep Current never returns from 30 FPS to its steady refresh rate");
    };

    testEvolutionSweep(0.0f, 1.0f);
    testEvolutionSweep(1.0f, 0.0f);

    std::cout << std::fixed << std::setprecision(8)
              << "[METRIC] Deep Current mean pixel delta: motion="
              << motionDifference.normalisedMean
              << ", Drift/Veil=" << characterDifference.normalisedMean
              << ", Drift/Current=" << currentCharacterDifference.normalisedMean
              << ", Current motion=" << currentMotionDifference.normalisedMean
              << ", Current field=" << currentFieldDifference.normalisedMean
              << ", Fathom motion=" << fathomMotionDifference.normalisedMean
              << ", Drift/Fathom=" << fathomDriftDifference.normalisedMean
              << ", Default/Fathom=" << fathomDefaultDifference.normalisedMean
              << ", Undertow motion=" << undertowMotionDifference.normalisedMean
              << ", Drift/Undertow=" << undertowDriftDifference.normalisedMean
              << ", Default/Undertow=" << undertowDefaultDifference.normalisedMean
              << ", Fathom/Undertow=" << undertowFathomDifference.normalisedMean
              << ", Fathom/Undertow presence=" << fathomPresence.normalisedMean << '/'
              << undertowPresence.normalisedMean
              << ", Spume motion=" << spumeMotionDifference.normalisedMean
              << ", Drift/Spume=" << spumeDriftDifference.normalisedMean
              << ", Default/Spume=" << spumeDefaultDifference.normalisedMean
              << ", Fathom/Spume=" << spumeFathomDifference.normalisedMean
              << ", Undertow/Spume=" << spumeUndertowDifference.normalisedMean
              << ", Spume presence=" << spumePresence.normalisedMean
              << ", overlay=" << overlayDifference.normalisedMean
              << ", motion changed pixels=" << motionDifference.changedPixels
              << ", top/bottom motion=" << topMotion.normalisedMean
              << '/' << bottomMotion.normalisedMean
              << ", left/right motion=" << leftMotion.normalisedMean
              << '/' << rightMotion.normalisedMean
              << ", top/bottom presence=" << topPresence.normalisedMean
              << '/' << bottomPresence.normalisedMean
              << ", 3x3 long min=" << minimumLongMean
              << " @" << minimumLongStrongCoverage * 100.0 << "% strong"
              << ", 3x3 2-s min=" << minimumShortMean
              << " @" << minimumShortPerceptibleCoverage * 100.0 << "% perceptible"
              << ", 30 FPS delta mean/peak=" << meanMotionFrameDelta
              << '/' << peakMotionFrameDelta
              << ", Freeze stop=" << freezeStopSeconds << " s\n";
}

[[nodiscard]] juce::Rectangle<int> boundsInEditor(juce::Component& editor,
                                                  const juce::String& componentId)
{
    auto* component = findDescendantById(editor, componentId);
    require(component != nullptr,
            "Custom editor is missing: " + componentId.toStdString());
    return editor.getLocalArea(component, component->getLocalBounds());
}

// What one component paints by itself, at one pixel per point unless more
// are asked for.
[[nodiscard]] juce::Image paintAlone(juce::Component& component, float pixelsPerPoint = 1.0f)
{
    return component.createComponentSnapshot(component.getLocalBounds(), false, pixelsPerPoint,
                                             juce::SoftwareImageType {});
}

// The left edge of the Evolution ring in the dial's own points: the first
// column on the row through its centre that the ring covers by half or more.
// The glow round the ring stays far below that.
[[nodiscard]] double ringLeftEdge(juce::Slider& dial, float pixelsPerPoint = 1.0f)
{
    const auto image = paintAlone(dial, pixelsPerPoint);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    const auto row = image.getHeight() / 2;
    for (auto x = 0; x < image.getWidth() / 2; ++x)
        if (pixels.getPixelColour(x, row).getAlpha() >= 128)
            return x / static_cast<double>(pixelsPerPoint);

    throw std::runtime_error("The Evolution dial painted no ring on its centre row");
}

// The rows of the text a component paints, in the pixels of its picture: from
// the first painted row to the baseline of the last line. Under that baseline
// only the tails of a few letters and commas and the overshoot of round glyphs
// are painted, on rows far emptier than the fullest row of the line.
[[nodiscard]] juce::Range<int> textRows(juce::Component& component,
                                        double canvasScale,
                                        float pixelsPerPoint = 1.0f)
{
    const auto image = paintAlone(component, pixelsPerPoint);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    std::vector<int> ink(static_cast<std::size_t>(image.getHeight()), 0);
    auto first = -1;
    auto last = -1;
    for (auto y = 0; y < image.getHeight(); ++y)
    {
        for (auto x = 0; x < image.getWidth(); ++x)
            ink[static_cast<std::size_t>(y)] += pixels.getPixelColour(x, y).getAlpha();
        if (ink[static_cast<std::size_t>(y)] != 0)
        {
            first = first < 0 ? y : first;
            last = y;
        }
    }
    require(first >= 0, "Component painted no text");

    // The last line lies within one line pitch above the last painted row.
    const auto lineTop = std::max(first,
                                  last - juce::roundToInt(14.0 * canvasScale * pixelsPerPoint));
    const auto fullest = *std::max_element(ink.begin() + lineTop, ink.begin() + last + 1);
    auto baseline = last;
    while (baseline > lineTop && 4 * ink[static_cast<std::size_t>(baseline)] < fullest)
        --baseline;
    return { first, baseline + 1 };
}

// The row of such a picture on which the capitals of the first line set in:
// the sharpest rise of ink within one design pixel under the first painted
// row. Round capitals rise a little over the flat ones, which begin together.
[[nodiscard]] int capitalsTopRow(juce::Component& component,
                                 double canvasScale,
                                 float pixelsPerPoint)
{
    const auto image = paintAlone(component, pixelsPerPoint);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    const auto inkOn = [&](int y)
    {
        auto ink = 0;
        for (auto x = 0; x < image.getWidth(); ++x)
            ink += pixels.getPixelColour(x, y).getAlpha();
        return ink;
    };
    auto first = 0;
    while (first < image.getHeight() && inkOn(first) == 0)
        ++first;
    require(first < image.getHeight(), "Component painted no text");

    const auto reach = std::min(image.getHeight(),
                                first + juce::roundToInt(canvasScale * pixelsPerPoint));
    auto top = first;
    auto sharpestRise = 0;
    auto inkAbove = 0;
    for (auto y = first; y < reach; ++y)
    {
        const auto ink = inkOn(y);
        if (ink - inkAbove > sharpestRise)
        {
            sharpestRise = ink - inkAbove;
            top = y;
        }
        inkAbove = ink;
    }
    return top;
}

// The rows of the description block that its hairline fills: the only rows
// painted from the first column to the last.
[[nodiscard]] juce::Range<int> descriptionHairlineRows(juce::Component& description)
{
    const auto image = paintAlone(description);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    juce::Range<int> rows;
    for (auto y = 0; y < image.getHeight(); ++y)
    {
        auto filled = true;
        for (auto x = 0; x < image.getWidth() && filled; ++x)
            filled = pixels.getPixelColour(x, y).getAlpha() >= 128;
        if (filled)
            rows = rows.isEmpty() ? juce::Range<int>(y, y + 1) : rows.withEnd(y + 1);
    }
    return rows;
}

// The first unbroken run of rows a component paints anything on.
[[nodiscard]] juce::Range<int> firstPaintedRows(juce::Component& component)
{
    const auto image = paintAlone(component);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    const auto paintsOn = [&](int y)
    {
        for (auto x = 0; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y).getAlpha() != 0)
                return true;
        return false;
    };
    auto first = 0;
    while (first < image.getHeight() && ! paintsOn(first))
        ++first;
    auto end = first;
    while (end < image.getHeight() && paintsOn(end))
        ++end;
    return { first, end };
}

// The number of unbroken runs of rows a component paints anything on above
// the row `end`: the lines of text that stand over that row.
[[nodiscard]] int paintedRunsAbove(juce::Component& component, int end)
{
    const auto image = paintAlone(component);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    auto runs = 0;
    auto paintedAbove = false;
    for (auto y = 0; y < std::min(end, image.getHeight()); ++y)
    {
        auto painted = false;
        for (auto x = 0; x < image.getWidth() && ! painted; ++x)
            painted = pixels.getPixelColour(x, y).getAlpha() != 0;
        runs += painted && ! paintedAbove ? 1 : 0;
        paintedAbove = painted;
    }
    return runs;
}

// The first and the last row a component paints anything on.
[[nodiscard]] juce::Range<int> paintedRows(juce::Component& component)
{
    const auto image = paintAlone(component);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    juce::Range<int> rows;
    for (auto y = 0; y < image.getHeight(); ++y)
        for (auto x = 0; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y).getAlpha() != 0)
            {
                rows = rows.isEmpty() ? juce::Range<int>(y, y + 1) : rows.withEnd(y + 1);
                break;
            }
    return rows;
}

// The unbroken runs of rows of a picture that anything is painted on, from
// the top down.
[[nodiscard]] std::vector<juce::Range<int>> paintedRuns(const juce::Image& image)
{
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    std::vector<juce::Range<int>> runs;
    auto paintedAbove = false;
    for (auto y = 0; y < image.getHeight(); ++y)
    {
        auto painted = false;
        for (auto x = 0; x < image.getWidth() && ! painted; ++x)
            painted = pixels.getPixelColour(x, y).getAlpha() != 0;
        if (painted && ! paintedAbove)
            runs.emplace_back(y, y + 1);
        else if (painted)
            runs.back().setEnd(y + 1);
        paintedAbove = painted;
    }
    return runs;
}

// Two colours that differ by no more than one step of 255 in any channel:
// one tone, whatever the rounding of the blend that painted it.
[[nodiscard]] bool sameTone(juce::Colour first, juce::Colour second) noexcept
{
    return std::abs(first.getRed() - second.getRed()) <= 1
        && std::abs(first.getGreen() - second.getGreen()) <= 1
        && std::abs(first.getBlue() - second.getBlue()) <= 1;
}

// The colour of the ink in some rows of a picture: that of its fully covered
// pixels, which show it unblended and must all show the same tone.
[[nodiscard]] juce::Colour inkColour(const juce::Image& image, juce::Range<int> rows)
{
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    juce::Colour ink;
    auto found = false;
    for (auto y = rows.getStart(); y < rows.getEnd(); ++y)
        for (auto x = 0; x < image.getWidth(); ++x)
        {
            const auto pixel = pixels.getPixelColour(x, y);
            if (pixel.getAlpha() != 255)
                continue;
            require(! found || sameTone(pixel, ink), "A line of text is painted in two tones");
            ink = found ? ink : pixel;
            found = true;
        }
    require(found, "A line of text has no fully covered pixel to read its colour from");
    return ink;
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
    require(std::abs(constrainer->getFixedAspectRatio() - 1.5) <= 1.0e-9,
            "Custom editor aspect ratio is wrong");

    for (int index = 0; index <= lastCharacterIndex; ++index)
        require(amanita::ui::characterAccent(index) == backgroundAccent(index),
                "Character accent palette changed");
    require(amanita::ui::characterAccent(-1) == amanita::ui::characterAccent(0)
                && amanita::ui::characterAccent(lastCharacterIndex + 1)
                    == amanita::ui::characterAccent(lastCharacterIndex),
            "Character accent does not clamp an out-of-range index");

    // The one table of the texts shown for the Characters.
    using Description = amanita::ui::CharacterDescription;
    for (const auto& algorithmCase : algorithmCases)
    {
        const auto& text = Description::text(algorithmCase.rawIndex);
        const juce::String subtitle(text.subtitle);
        const juce::String paragraph(text.paragraph);
        require(juce::String(text.name) == juce::String(algorithmCase.name).toUpperCase(),
                std::string("Character description does not carry the name of ")
                    + algorithmCase.name);
        require(subtitle.length() >= 12 && subtitle == subtitle.toUpperCase(),
                std::string("Character description has no capitalised subtitle for ")
                    + algorithmCase.name);
        require(paragraph.length() >= 120 && paragraph.endsWithChar('.')
                    && paragraph.contains("Evolution"),
                std::string("Character description has no complete paragraph for ")
                    + algorithmCase.name);
        const auto& approved
            = approvedDescriptions[static_cast<std::size_t>(algorithmCase.rawIndex)];
        require(std::strcmp(text.name, approved.name) == 0
                    && std::strcmp(text.subtitle, approved.subtitle) == 0
                    && std::strcmp(text.paragraph, approved.paragraph) == 0,
                std::string("Character description is not the approved text for ")
                    + algorithmCase.name);
    }
    require(&Description::text(-1) == &Description::text(0)
                && &Description::text(lastCharacterIndex + 1)
                    == &Description::text(lastCharacterIndex),
            "Character description does not clamp an out-of-range index");

    auto* characterSelector = dynamic_cast<juce::ComboBox*>(
        findDescendantById(*editor, "character-selector"));
    auto* characterDescription = dynamic_cast<Description*>(
        findDescendantById(*editor, "character-description"));
    auto* evolutionSlider = dynamic_cast<juce::Slider*>(
        findDescendantById(*editor, "evolution"));
    require(characterSelector != nullptr, "Character drop-down was not found");
    require(characterDescription != nullptr, "Character description block was not found");
    require(evolutionSlider != nullptr, "Evolution slider was not found");
    require(characterSelector->getNumItems() == static_cast<int>(algorithmCases.size()),
            "Character drop-down does not list exactly eight Characters");
    for (const auto& algorithmCase : algorithmCases)
        require(characterSelector->getItemText(algorithmCase.rawIndex) == algorithmCase.name
                    && characterSelector->getItemId(algorithmCase.rawIndex)
                        == algorithmCase.rawIndex + 1
                    && algorithmParameter(processor).choices[algorithmCase.rawIndex]
                        == algorithmCase.name,
                std::string("Character drop-down item order is wrong at ")
                    + algorithmCase.name);
    require(characterSelector->getSelectedItemIndex() == 0
                && characterSelector->getText() == "Default"
                && !characterSelector->isTextEditable(),
            "Character drop-down does not show the default Character");
    require(findDescendantById(*editor, "character-default") == nullptr
                && findDescendantById(*editor, "algorithm") == nullptr,
            "The segment selector is still part of the editor");

    constexpr std::array<const char*, 13> interactiveIds {
        "character-selector",
        "evolution", "preDelay", "size", "decay", "lowCut", "highDamping",
        "harmony", "width", "focus", "mix", "mono-safe", "freeze"
    };
    // Every control that takes room of its own in the editor.
    constexpr std::array<const char*, 14> placedIds {
        "character-selector", "character-description", "knob-evolution",
        "knob-preDelay", "knob-size", "knob-decay", "knob-lowCut", "knob-highDamping",
        "knob-harmony", "knob-width", "knob-focus", "knob-mix", "mono-safe", "freeze"
    };
    constexpr std::array<std::array<int, 2>, 3> editorSizes {{
        { AmanitaOceanAudioProcessorEditor::minimumWidth,
          AmanitaOceanAudioProcessorEditor::minimumHeight },
        { AmanitaOceanAudioProcessorEditor::defaultWidth,
          AmanitaOceanAudioProcessorEditor::defaultHeight },
        { AmanitaOceanAudioProcessorEditor::maximumWidth,
          AmanitaOceanAudioProcessorEditor::maximumHeight }
    }};

    // The Evolution control is read with a value whose digits stand on their
    // baseline with flat feet; round digits reach a little under it.
    evolutionSlider->setValue(21.0, juce::sendNotificationSync);

    for (const auto& size : editorSizes)
    {
        editor->setSize(size[0], size[1]);
        const auto sizeName = std::to_string(size[0]) + " x " + std::to_string(size[1]);
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

        std::array<juce::Rectangle<int>, placedIds.size()> placedBounds;
        for (std::size_t index = 0; index < placedIds.size(); ++index)
        {
            placedBounds[index] = boundsInEditor(*editor, placedIds[index]);
            require(!placedBounds[index].isEmpty()
                        && editor->getLocalBounds().contains(placedBounds[index]),
                    std::string("Custom editor control is empty or escapes its bounds: ")
                        + placedIds[index]);
            for (std::size_t other = 0; other < index; ++other)
                require(!placedBounds[index].intersects(placedBounds[other]),
                        std::string("Custom editor controls overlap at ") + sizeName + ": "
                            + placedIds[other] + " and " + placedIds[index]);
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

        // The drop-down sits on the window's vertical axis as the heading of
        // the group under it: the description block and the ring of the
        // Evolution dial keep the same distance from that axis, with clear
        // air between the drop-down and the ring and between the Evolution
        // value and the footer rule. The block stands round the middle of the
        // whole Evolution control: half way from the top of its ring to the
        // foot of its value's digits.
        constexpr auto finePixelsPerPoint = 8.0f;
        const auto selectorBounds = boundsInEditor(*editor, "character-selector");
        const auto descriptionBounds = boundsInEditor(*editor, "character-description");
        const auto evolutionBounds = boundsInEditor(*editor, "knob-evolution");
        const auto axis = 0.5 * static_cast<double>(size[0]);
        const auto canvasScale = static_cast<double>(size[0])
                               / AmanitaOceanAudioProcessorEditor::defaultWidth;
        require(std::abs(0.5 * (selectorBounds.getX() + selectorBounds.getRight()) - axis) <= 0.5,
                "Character drop-down is not centred on the window's vertical axis at "
                    + sizeName);
        require(std::abs(selectorBounds.getWidth() - 340.0 * canvasScale) <= 0.5
                    && std::abs(selectorBounds.getHeight() - 40.0 * canvasScale) <= 0.5,
                "Character drop-down is not 340 x 40 design pixels at " + sizeName);
        require(selectorBounds.getBottom() <= descriptionBounds.getY()
                    && selectorBounds.getBottom() <= evolutionBounds.getY()
                    && descriptionBounds.getRight() < axis
                    && evolutionBounds.getX() > axis,
                "Description block and Evolution knob are not under the drop-down on "
                "either side of the axis at " + sizeName);

        const auto dialCentreX = 0.5 * (sliderBounds.getX() + sliderBounds.getRight());
        const auto dialCentreY = 0.5 * (sliderBounds.getY() + sliderBounds.getBottom());
        const auto ringLeft = sliderBounds.getX() + ringLeftEdge(*evolutionSlider);
        const auto ringRadius = dialCentreX - ringLeft;
        const auto textGap = axis - descriptionBounds.getRight();
        const auto ringGap = ringLeft - axis;
        const auto airUnderSelector = dialCentreY - ringRadius - selectorBounds.getBottom();
        const auto airOverFooter = 484.0 * canvasScale - valueBounds.getBottom();
        // The same radius read at eight pixels per point, and the control
        // read alike.
        const auto fineRingRadius = 0.5 * sliderBounds.getWidth()
                                  - ringLeftEdge(*evolutionSlider, finePixelsPerPoint);
        const auto ringTop = dialCentreY - fineRingRadius;
        const auto valueFoot = valueBounds.getY()
                             + textRows(*evolutionValue, canvasScale, finePixelsPerPoint).getEnd()
                                   / static_cast<double>(finePixelsPerPoint);
        const auto controlMiddle = 0.5 * (ringTop + valueFoot);
        std::cout << "[METRIC] Editor " << sizeName << ": axis to text block " << textGap
                  << " px, axis to Evolution ring " << ringGap << " px, ring "
                  << 2.0 * ringRadius << " px across (radius " << fineRingRadius << " px of a "
                  << sliderBounds.getWidth() << " px dial), " << airUnderSelector
                  << " px between drop-down and ring, " << airOverFooter
                  << " px between Evolution value and footer rule; Evolution control from "
                  << ringTop << " to " << valueFoot << " px, its middle at " << controlMiddle
                  << " px; description block from " << descriptionBounds.getY() << " to "
                  << descriptionBounds.getBottom() << " px, "
                  << 484.0 * canvasScale - descriptionBounds.getBottom()
                  << " px over the footer rule\n";
        require(textGap > 0.0 && std::abs(textGap - ringGap) <= 1.0,
                "Description block and Evolution ring are not mirrored about the axis at "
                    + sizeName);
        require(std::abs(ringGap - 44.0 * canvasScale) <= 1.0
                    && std::abs(2.0 * ringRadius - 180.0 * canvasScale) <= 2.0,
                "Evolution ring is not 180 design pixels across and 44 from the axis at "
                    + sizeName);
        // The editor mirrors the block on a ring that keeps its share of the
        // dial at every size.
        require(std::abs(fineRingRadius - sliderBounds.getWidth() * 103.6 / 224.0) <= 0.125,
                "Evolution ring does not keep its share of the dial at " + sizeName);
        require(airUnderSelector >= 48.0 * canvasScale,
                "Evolution ring crowds the Character drop-down at " + sizeName);
        require(airOverFooter >= 32.0 * canvasScale,
                "Evolution value crowds the footer rule at " + sizeName);
        require(std::abs(descriptionBounds.getWidth() - 248.0 * canvasScale) <= 0.5
                    && std::abs(0.5 * (descriptionBounds.getY() + descriptionBounds.getBottom())
                                - controlMiddle)
                           <= 1.0,
                "Description block is not 248 design pixels wide round the middle of the "
                "whole Evolution control at " + sizeName);
        require(! descriptionHairlineRows(*characterDescription).isEmpty(),
                "Description block painted no hairline from edge to edge at " + sizeName);

        if (size[0] == AmanitaOceanAudioProcessorEditor::defaultWidth)
        {
            require(selectorBounds == juce::Rectangle<int>(310, 112, 340, 40),
                    "Character drop-down no longer follows the 4 px top grid");
            // The block's top is the whole pixel nearest to a middle that
            // hangs on the foot of the Evolution value's digits: a metric of
            // the system's sans-serif face. macOS's face puts it at 239; a
            // face an eighth of a pixel shorter rounds to 238, and the check
            // above holds every face to that middle.
           #if JUCE_MAC
            constexpr auto descriptionTop = 239;
           #else
            const auto descriptionTop = descriptionBounds.getY();
           #endif
            require(descriptionBounds
                        == juce::Rectangle<int>(188, descriptionTop, 248, 160),
                    "Description block no longer mirrors the Evolution ring round the "
                    "middle of the Evolution control");
            require(evolutionBounds == juce::Rectangle<int>(510, 199, 208, 238),
                    "Evolution hero is no longer placed by the centre of its dial");
            require(std::abs(sliderBounds.toFloat().getCentreX() - editorFocalX * 960.0f)
                            <= 1.0e-3f
                        && std::abs(sliderBounds.toFloat().getCentreY()
                                    - editorFocalY * 640.0f)
                               <= 1.0e-3f,
                    "Evolution dial is not centred where the background gathers");
            require(sliderBounds.getWidth() == 194 && valueBounds.getBottom() == 437,
                    "Evolution dial, name and value no longer stack to 238 design pixels");
        }

        // The list of the drop-down opens under its field at the field's
        // width, and its items and its text follow the editor's scale.
        auto& lookAndFeel = characterSelector->getLookAndFeel();
        auto* fieldLabel = dynamic_cast<juce::Label*>(characterSelector->getChildComponent(0));
        require(fieldLabel != nullptr, "Character drop-down has no text label");
        const auto fieldScale = static_cast<float>(selectorBounds.getHeight()) / 40.0f;
        const auto listGap = juce::roundToInt(4.0f * fieldScale);
        const auto listOptions = lookAndFeel.getOptionsForComboBoxPopupMenu(*characterSelector,
                                                                            *fieldLabel);
        const auto listPadding = lookAndFeel.getPopupMenuBorderSizeWithOptions(listOptions);
        require(listOptions.getTargetComponent() == characterSelector
                    && listOptions.getTargetScreenArea()
                        == characterSelector->getScreenBounds().expanded(0, listGap)
                    && listOptions.getItemThatMustBeVisible() == 0
                    && listOptions.getMaximumNumColumns() == 1,
                "Character list does not open under its field at " + sizeName);
        require(listOptions.getMinimumWidth() == selectorBounds.getWidth()
                    && listOptions.getInitiallySelectedItemId()
                        == characterSelector->getSelectedId(),
                "Character list does not start at its field's width on the selected item at "
                    + sizeName);
        require(listOptions.getStandardItemHeight() == juce::roundToInt(32.0f * fieldScale)
                    && listPadding == juce::roundToInt(6.0f * fieldScale)
                    && std::abs(lookAndFeel.getComboBoxFont(*characterSelector).getHeight()
                                - 12.5f * fieldScale)
                           <= 1.0e-3f
                    && lookAndFeel.getComboBoxFont(*characterSelector).isBold()
                    && fieldLabel->getJustificationType() == juce::Justification::centred,
                "Character drop-down text and list items do not follow the editor's scale at "
                    + sizeName);
        for (const auto& algorithmCase : algorithmCases)
        {
            auto itemWidth = 0;
            auto itemHeight = 0;
            lookAndFeel.getIdealPopupMenuItemSizeWithOptions(
                algorithmCase.name, false, listOptions.getStandardItemHeight(),
                itemWidth, itemHeight, listOptions);
            require(itemHeight == listOptions.getStandardItemHeight()
                        && itemWidth > 0
                        && itemWidth + 2 * listPadding <= selectorBounds.getWidth(),
                    std::string("Character list would grow wider than its field for ")
                        + algorithmCase.name + " at " + sizeName);
        }

        // All eight texts lie inside the block unshortened.
        for (const auto& algorithmCase : algorithmCases)
        {
            algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
            const auto& text = Description::text(algorithmCase.rawIndex);
            require(characterSelector->getSelectedItemIndex() == algorithmCase.rawIndex
                        && characterSelector->getText() == algorithmCase.name,
                    std::string("Host Character did not update the drop-down for ")
                        + algorithmCase.name);
            require(characterDescription->getTitle().contains(text.name)
                        && characterDescription->getDescription().contains(text.paragraph),
                    std::string("Host Character did not update the description block for ")
                        + algorithmCase.name);
            require(characterDescription->textFits(),
                    std::string("Character description does not fit its block for ")
                        + algorithmCase.name + " at " + sizeName);
            const auto rows = paintedRows(*characterDescription);
            require(rows.getStart() > 0 && rows.getLength() > descriptionBounds.getHeight() / 2
                        && rows.getEnd() < descriptionBounds.getHeight(),
                    std::string("Character description is cut off at the top or the foot of "
                                "its block for ")
                        + algorithmCase.name + " at " + sizeName);

            // The text begins with a name whose capitals are those of a 20 pt
            // bold face.
            const auto blockRows = textRows(*characterDescription, canvasScale);
            const auto nameRows = firstPaintedRows(*characterDescription);
            require(nameRows.getStart() == blockRows.getStart()
                        && nameRows.getLength() >= 12.0 * canvasScale
                        && nameRows.getLength() <= 15.0 * canvasScale,
                    std::string("Name of the Character description is not set in 20 pt for ")
                        + algorithmCase.name + " at " + sizeName);

            // From the top of the name's capitals to the last baseline the
            // text has its middle within half a design pixel of the middle of
            // the whole Evolution control, read at eight pixels per point.
            const auto capitalsTop = capitalsTopRow(*characterDescription, canvasScale,
                                                    finePixelsPerPoint)
                                   / static_cast<double>(finePixelsPerPoint);
            const auto lastBaseline = textRows(*characterDescription, canvasScale,
                                               finePixelsPerPoint).getEnd()
                                    / static_cast<double>(finePixelsPerPoint);
            const auto textMiddle = descriptionBounds.getY() + 0.5 * (capitalsTop + lastBaseline);
            std::cout << "[METRIC] Editor " << sizeName << ", " << algorithmCase.name
                      << ": description text " << lastBaseline - capitalsTop
                      << " px high, its middle at " << textMiddle << " px, "
                      << textMiddle - controlMiddle
                      << " px below the middle of the Evolution control\n";
            require(std::abs(textMiddle - controlMiddle) <= 0.5 * canvasScale,
                    std::string("Character description is not centred on the whole Evolution "
                                "control for ")
                        + algorithmCase.name + " at " + sizeName);

            // The hairline moves with the text: the name and the subtitle
            // stand over it, the paragraph under it.
            const auto hairlineRows = descriptionHairlineRows(*characterDescription);
            require(paintedRunsAbove(*characterDescription, hairlineRows.getStart()) == 2
                        && rows.getEnd() > hairlineRows.getEnd() + 14.0 * canvasScale,
                    std::string("Hairline of the Character description does not lie between "
                                "the subtitle and the paragraph for ")
                        + algorithmCase.name + " at " + sizeName);

            // The name, the subtitle and the paragraph are set in Ocean's
            // three text tones, each far enough from Ocean's background: the
            // lines over the hairline are the first two runs of painted rows,
            // the hairline the third, the paragraph everything under it.
            if (size[0] == AmanitaOceanAudioProcessorEditor::defaultWidth)
            {
                using LookAndFeel = amanita::ui::OceanLookAndFeel;
                const auto picture = paintAlone(*characterDescription, finePixelsPerPoint);
                const auto runs = paintedRuns(picture);
                require(runs.size() >= 4,
                        std::string("Character description does not paint a name, a subtitle, "
                                    "a hairline and a paragraph for ")
                            + algorithmCase.name);
                const auto nameColour = inkColour(picture, runs[0]);
                const auto subtitleColour = inkColour(picture, runs[1]);
                const auto paragraphColour = inkColour(
                    picture, { runs[3].getStart(), runs.back().getEnd() });
                const auto ground = LookAndFeel::backgroundTop();
                if (algorithmCase.rawIndex == 0)
                    std::cout << "[METRIC] Description block against Ocean's background: name "
                              << contrastRatio(nameColour, ground) << " : 1, subtitle "
                              << contrastRatio(subtitleColour, ground) << " : 1, paragraph "
                              << contrastRatio(paragraphColour, ground) << " : 1\n";
                require(sameTone(nameColour, LookAndFeel::primaryText())
                            && sameTone(subtitleColour, LookAndFeel::secondaryText())
                            && sameTone(paragraphColour,
                                        LookAndFeel::primaryText().interpolatedWith(
                                            LookAndFeel::secondaryText(), 0.45f)),
                        std::string("Character description is not set in Ocean's text tones for ")
                            + algorithmCase.name);
                require(contrastRatio(nameColour, ground) >= 7.0
                            && contrastRatio(subtitleColour, ground) >= 4.5
                            && contrastRatio(paragraphColour, ground) >= 7.0,
                        std::string("Character description does not stand out from Ocean's "
                                    "background for ")
                            + algorithmCase.name);
            }
        }
        algorithmParameter(processor).setValueNotifyingHost(algorithmCases.front().hostValue);

        constexpr std::array<const char*, 9> lowerRowIds {
            "preDelay", "size", "decay", "lowCut", "highDamping",
            "harmony", "width", "focus", "mix"
        };
        auto previousRight = -1;
        for (const auto* id : lowerRowIds)
        {
            auto* control = findDescendantById(
                *editor, juce::String("knob-") + id);
            require(control != nullptr, std::string("Lower-row control is missing: ") + id);
            const auto controlBounds = editor->getLocalArea(
                control, control->getLocalBounds());
            require(controlBounds.getX() >= previousRight,
                    std::string("Lower-row controls overlap at: ") + id);
            previousRight = controlBounds.getRight();
        }
    }

    // Between those sizes the block mirrors the ring as well: it ends on the
    // whole pixel nearest to the mirror image of the ring's left edge, which
    // is read at eight pixels per point.
    for (const auto width : { 852, 900, 1026, 1200, 1338 })
    {
        editor->setSize(width, width * AmanitaOceanAudioProcessorEditor::defaultHeight
                                   / AmanitaOceanAudioProcessorEditor::defaultWidth);
        const auto axis = 0.5 * width;
        const auto textGap = axis - boundsInEditor(*editor, "character-description").getRight();
        const auto ringGap = boundsInEditor(*editor, "evolution").getX()
                           + ringLeftEdge(*evolutionSlider, 8.0f) - axis;
        std::cout << "[METRIC] Editor " << width << " wide: axis to text block " << textGap
                  << " px, axis to Evolution ring " << ringGap << " px\n";
        require(textGap > 0.0 && std::abs(textGap - ringGap) <= 0.75,
                "Description block and Evolution ring are not mirrored about the axis at width "
                    + std::to_string(width));
    }

    parameterById(processor, "evolution").setValueNotifyingHost(0.73f);
    require(std::abs(evolutionSlider->getValue() - 73.0) <= 0.11,
            "Host Evolution did not update the custom slider");
    evolutionSlider->setValue(62.5, juce::sendNotificationSync);
    require(std::abs(parameterById(processor, "evolution").getValue() - 0.625f) <= 0.001f,
            "Custom Evolution slider did not update the host parameter");

    auto* mixValueLabel = dynamic_cast<juce::Label*>(
        findDescendantById(*editor, "mix-value"));
    require(mixValueLabel != nullptr, "Editable Mix value label was not found");
    require(static_cast<bool>(mixValueLabel->onEditorHide),
            "Editable value has no post-edit focus release");
    require(static_cast<bool>(mixValueLabel->getProperties().getWithDefault(
                juce::Identifier("suppressFocusOutline"), false)),
            "Editable value still allows a retained focus outline");
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

    auto* harmonySlider = dynamic_cast<juce::Slider*>(
        findDescendantById(*editor, "harmony"));
    require(harmonySlider != nullptr, "Harmony slider was not found");
    parameterById(processor, "harmony").setValueNotifyingHost(0.64f);
    require(std::abs(harmonySlider->getValue() - 64.0) <= 0.11,
            "Host Harmony did not update the custom slider");
    harmonySlider->setValue(37.5, juce::sendNotificationSync);
    require(std::abs(parameterById(processor, "harmony").getValue() - 0.375f) <= 0.001f,
            "Custom Harmony slider did not update the host parameter");

    auto* lowCutSlider = dynamic_cast<juce::Slider*>(
        findDescendantById(*editor, "lowCut"));
    auto* lowCutValueLabel = dynamic_cast<juce::Label*>(
        findDescendantById(*editor, "lowCut-value"));
    require(lowCutSlider != nullptr && lowCutValueLabel != nullptr,
            "Low Cut control or value label was not found");
    require(lowCutValueLabel->getText() == "80 Hz",
            "Initial Low Cut label is not integer-formatted: "
                + lowCutValueLabel->getText().toStdString());
    auto* lowCutParameter = dynamic_cast<juce::RangedAudioParameter*>(
        &parameterById(processor, "lowCut"));
    require(lowCutParameter != nullptr, "Low Cut is not a ranged parameter");
    lowCutParameter->setValueNotifyingHost(
        lowCutParameter->convertTo0to1(26.3896f));
    require(std::abs(lowCutSlider->getValue() - 26.3896) <= 0.001,
            "Host Low Cut did not update the custom slider continuously");
    require(lowCutValueLabel->getText() == "26 Hz",
            "Host Low Cut update exposed fractional hertz in the custom label: "
                + lowCutValueLabel->getText().toStdString());
    lowCutSlider->setValue(31.7284, juce::sendNotificationSync);
    require(lowCutValueLabel->getText() == "32 Hz",
            "Dragged Low Cut exposed fractional hertz in the custom label: "
                + lowCutValueLabel->getText().toStdString());

    require(characterSelector->getWantsKeyboardFocus()
                && characterSelector->getExplicitFocusOrder() == 1,
            "Character drop-down is not the first keyboard-focus stop");
    // No label stands over the drop-down; its accessible title says what it is.
    require(characterSelector->getTitle().containsIgnoreCase("character")
                && characterSelector->getDescription().isNotEmpty(),
            "Character drop-down has no accessible title that names it, or no description");

    characterSelector->setSelectedItemIndex(4, juce::sendNotificationSync);
    require(algorithmParameter(processor).getIndex() == 4,
            "Custom Character selector did not update the host parameter");
    characterSelector->setSelectedItemIndex(0, juce::sendNotificationSync);
    require(algorithmParameter(processor).getIndex() == 0,
            "Custom Character selector did not return the host parameter to Default");

    // The arrow keys step through the Characters in both directions, each
    // step one complete host gesture, without opening the list.
    const auto press = [&](int keyCode)
    {
        return characterSelector->keyPressed(juce::KeyPress(keyCode));
    };
    GestureProbe characterGestures;
    processor.addListener(&characterGestures);
    for (auto index = 1; index <= lastCharacterIndex; ++index)
        require(press(index % 2 == 0 ? juce::KeyPress::rightKey : juce::KeyPress::downKey)
                    && algorithmParameter(processor).getIndex() == index
                    && characterSelector->getSelectedItemIndex() == index,
                "Right and Down arrows do not step to the next Character in the host "
                "parameter");
    require(press(juce::KeyPress::rightKey)
                && algorithmParameter(processor).getIndex() == 0,
            "Right arrow does not wrap from the last Character to the first");
    require(press(juce::KeyPress::leftKey)
                && algorithmParameter(processor).getIndex() == lastCharacterIndex,
            "Left arrow does not wrap from the first Character to the last");
    for (auto index = lastCharacterIndex - 1; index >= 0; --index)
        require(press(index % 2 == 0 ? juce::KeyPress::leftKey : juce::KeyPress::upKey)
                    && algorithmParameter(processor).getIndex() == index
                    && characterSelector->getSelectedItemIndex() == index,
                "Left and Up arrows do not step to the previous Character in the host "
                "parameter");
    require(press(juce::KeyPress::upKey)
                && algorithmParameter(processor).getIndex() == lastCharacterIndex,
            "Up arrow does not wrap from the first Character to the last");
    require(press(juce::KeyPress::downKey)
                && algorithmParameter(processor).getIndex() == 0,
            "Down arrow does not wrap from the last Character to the first");
    processor.removeListener(&characterGestures);
    require(characterGestures.beginCount == 2 * lastCharacterIndex + 4
                && characterGestures.endCount == characterGestures.beginCount,
            "A Character key step did not produce one complete host gesture");
    require(!characterSelector->isPopupActive(),
            "An arrow key opened the Character list");

    for (const auto keyCode : { juce::KeyPress::returnKey, juce::KeyPress::spaceKey })
    {
        require(press(keyCode) && characterSelector->isPopupActive(),
                "Return and Space do not open the Character list");
        characterSelector->hidePopup();
        require(!characterSelector->isPopupActive(),
                "The Character list does not close again");
    }
    require(algorithmParameter(processor).getIndex() == 0,
            "Opening the Character list changed the Character");
    require(!press(juce::KeyPress::tabKey) && !press('a'),
            "Character drop-down takes keys it has no use for");

    // While the list is open the keys are the list's. It hands Left and Right
    // on to its drop-down, which leaves the Character alone behind the list.
    characterSelector->showPopup();
    auto* openList = juce::Component::getCurrentlyModalComponent();
    require(openList != nullptr && characterSelector->isPopupActive(),
            "The Character list did not open");
    GestureProbe listGestures;
    processor.addListener(&listGestures);
    for (const auto keyCode : { juce::KeyPress::rightKey, juce::KeyPress::rightKey,
                                juce::KeyPress::leftKey })
        openList->keyPressed(juce::KeyPress(keyCode));
    processor.removeListener(&listGestures);
    require(characterSelector->isPopupActive()
                && algorithmParameter(processor).getIndex() == 0
                && characterSelector->getSelectedItemIndex() == 0
                && listGestures.beginCount == 0,
            "Left or Right changed the Character behind its open list");
    characterSelector->hidePopup();
    require(!characterSelector->isPopupActive(), "The Character list does not close again");

    // Only the bare keys are the drop-down's. Pressed with a modifier they
    // are passed on: the Character stays and the list stays closed.
    for (const auto modifier : { juce::ModifierKeys::shiftModifier,
                                 juce::ModifierKeys::ctrlModifier,
                                 juce::ModifierKeys::altModifier,
                                 juce::ModifierKeys::commandModifier })
        for (const auto keyCode : { juce::KeyPress::upKey, juce::KeyPress::downKey,
                                    juce::KeyPress::leftKey, juce::KeyPress::rightKey,
                                    juce::KeyPress::spaceKey, juce::KeyPress::returnKey })
            require(!characterSelector->keyPressed(
                        juce::KeyPress(keyCode, juce::ModifierKeys(modifier), 0))
                        && algorithmParameter(processor).getIndex() == 0
                        && !characterSelector->isPopupActive(),
                    "Character drop-down takes a key that is pressed with a modifier");

    auto* freezeButton = dynamic_cast<juce::ToggleButton*>(
        findDescendantById(*editor, "freeze"));
    auto* monoSafeButton = dynamic_cast<juce::ToggleButton*>(
        findDescendantById(*editor, "mono-safe"));
    require(freezeButton != nullptr && monoSafeButton != nullptr,
            "Mono Safe or Freeze toggle was not found");
    editor->setSize(AmanitaOceanAudioProcessorEditor::defaultWidth,
                    AmanitaOceanAudioProcessorEditor::defaultHeight);
    const auto freezeBounds = editor->getLocalArea(freezeButton,
                                                    freezeButton->getLocalBounds());
    const auto monoSafeBounds = editor->getLocalArea(
        monoSafeButton, monoSafeButton->getLocalBounds());
    require(freezeBounds.getRight() == 928
                && freezeBounds.getWidth() >= 72
                && freezeBounds.getWidth() <= 88
                && freezeBounds.getWidth() % 4 == 0,
            "Freeze pill is not fitted to its text on the 4-pixel layout grid");
    require(monoSafeBounds.getRight() == freezeBounds.getX() - 8
                && monoSafeBounds.getWidth() >= 88
                && monoSafeBounds.getWidth() <= 136
                && monoSafeBounds.getWidth() % 4 == 0,
            "Mono Safe pill is not fitted beside Freeze on the 4-pixel grid");
    require(!monoSafeButton->getToggleState()
                && parameterById(processor, "monoSafe").getValue() < 0.5f,
            "Mono Safe must default to Off");
    monoSafeButton->setToggleState(true, juce::sendNotificationSync);
    require(parameterById(processor, "monoSafe").getValue() > 0.5f,
            "Mono Safe button did not update the host parameter");
    freezeButton->setToggleState(true, juce::sendNotificationSync);
    require(parameterById(processor, "freeze").getValue() > 0.5f,
            "Custom Freeze toggle did not update the host parameter");
}

// A paragraph of the description block does not end in a widow where giving
// up a little of the lines' width brings a word down to it. Here three words
// fill a line and a short fourth would stand alone under them; with a fifth
// word of their length the last line is no widow and the lines stay as wide
// as they were.
void testDescriptionParagraphGivesAWidowCompany()
{
    using Description = amanita::ui::CharacterDescription;
    const auto lineLength = [](const juce::TextLayout& layout, int line)
    {
        return layout.getLine(line).getLineBoundsX().getLength();
    };

    const auto threeWords = Description::paragraphLayout("level level level", 10000.0f);
    require(threeWords.getNumLines() == 1, "Three short words do not fit on one long line");
    const auto width = 1.02f * lineLength(threeWords, 0);

    const auto withWidow = Description::paragraphLayout("level level level a", width);
    require(withWidow.getNumLines() == 2
                && lineLength(withWidow, 1) >= Description::widowShare * width
                && lineLength(withWidow, 0) <= width,
            "A widow of the Character description is left alone on its line");

    const auto withoutWidow = Description::paragraphLayout("level level level level level",
                                                           width);
    require(withoutWidow.getNumLines() == 2
                && std::abs(lineLength(withoutWidow, 0) - lineLength(threeWords, 0)) <= 0.5f,
            "A Character description without a widow has its lines narrowed");

    std::cout << "[METRIC] Last paragraph line of the Character descriptions, share of the "
                 "block's width:";
    for (const auto& algorithmCase : algorithmCases)
    {
        const auto layout = Description::paragraphLayout(
            Description::text(algorithmCase.rawIndex).paragraph,
            static_cast<float>(Description::designWidth));
        std::cout << ' ' << algorithmCase.name << ' ' << layout.getNumLines() << " lines "
                  << lineLength(layout, layout.getNumLines() - 1) / Description::designWidth;
    }
    std::cout << '\n';
}

// How many pixels of a picture show a colour unblended, within 4 of 255 in
// every channel.
[[nodiscard]] int pixelsInColour(const juce::Image& image, juce::Colour colour)
{
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    auto count = 0;
    for (auto y = 0; y < image.getHeight(); ++y)
        for (auto x = 0; x < image.getWidth(); ++x)
        {
            const auto pixel = pixels.getPixelColour(x, y);
            if (pixel.getAlpha() >= 250
                && std::abs(pixel.getRed() - colour.getRed()) <= 4
                && std::abs(pixel.getGreen() - colour.getGreen()) <= 4
                && std::abs(pixel.getBlue() - colour.getBlue()) <= 4)
                ++count;
        }
    return count;
}

// An editor opened on a Character shows that Character's accent in the word
// OCEAN of its title, in the ring of the Evolution dial, in the border of the
// drop-down while its list is open and in the highlight of that list, over
// Ocean's own surface and text colours, and in no part of the description
// block. The highlighted item carries the text that stands out more from its
// fill, Ocean's darkest tone or white.
void testCharacterAccentInTitleRingAndList()
{
    using LookAndFeel = amanita::ui::OceanLookAndFeel;
    const auto darkText = LookAndFeel::backgroundBottom();
    const auto lightText = juce::Colours::white;
    for (const auto& algorithmCase : algorithmCases)
    {
        AmanitaOceanAudioProcessor processor;
        algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        require(editor != nullptr, "Processor did not create an editor");
        auto* selector = dynamic_cast<juce::ComboBox*>(
            findDescendantById(*editor, "character-selector"));
        auto* description = findDescendantById(*editor, "character-description");
        auto* dial = findDescendantById(*editor, "evolution");
        require(selector != nullptr && description != nullptr && dial != nullptr,
                "Character drop-down, description block or Evolution dial was not found");
        const auto accent = amanita::ui::characterAccent(algorithmCase.rawIndex);
        require(selector->getText() == algorithmCase.name,
                std::string("Editor does not open on the saved Character ")
                    + algorithmCase.name);

        const auto title = editor->createComponentSnapshot({ 32, 18, 300, 24 }, true, 1.0f,
                                                           juce::SoftwareImageType {});
        const auto titlePixels = pixelsInColour(title, accent);
        const auto ringPixels = pixelsInColour(paintAlone(*dial), accent);
        const auto descriptionPixels = pixelsInColour(paintAlone(*description), accent);
        require(titlePixels >= 60 && ringPixels >= 200 && descriptionPixels == 0,
                std::string("Title word and Evolution ring are not in the accent of ")
                    + algorithmCase.name + ", or the description block is");

        // The border of the engaged drop-down on the upper edge of its field
        // and the field inside it, as they lie over Ocean's background.
        const auto fieldAt = [&](int row)
        {
            return LookAndFeel::backgroundTop().overlaidWith(
                paintAlone(*selector).getPixelAt(selector->getWidth() / 4, row));
        };
        const auto restingBorder = fieldAt(0);
        selector->showPopup();
        const auto engagedBorder = fieldAt(0);
        const auto fieldSurface = fieldAt(selector->getHeight() / 2);
        selector->hidePopup();
        const auto apartFromAccent = [accent](juce::Colour colour)
        {
            return std::abs(colour.getRed() - accent.getRed())
                 + std::abs(colour.getGreen() - accent.getGreen())
                 + std::abs(colour.getBlue() - accent.getBlue());
        };
        std::cout << "[METRIC] Drop-down border in the accent of " << algorithmCase.name
                  << " while engaged: " << contrastRatio(engagedBorder, fieldSurface)
                  << " : 1 against its field (resting "
                  << contrastRatio(restingBorder, fieldSurface) << " : 1)\n";
        require(10 * apartFromAccent(engagedBorder) <= 3 * apartFromAccent(fieldSurface)
                    && contrastRatio(engagedBorder, fieldSurface) >= 3.0
                    && contrastRatio(restingBorder, fieldSurface) < 2.0,
                std::string("Engaged drop-down does not wear a border of at least 3 : 1 in the "
                            "accent of ")
                    + algorithmCase.name);

        auto& lookAndFeel = selector->getLookAndFeel();
        const auto highlight = lookAndFeel.findColour(
            juce::PopupMenu::highlightedBackgroundColourId);
        const auto listBackground = lookAndFeel.findColour(juce::PopupMenu::backgroundColourId);
        require(highlight.withAlpha(1.0f) == accent && highlight.getFloatAlpha() >= 0.8f,
                std::string("Highlight of the Character list is not in the accent of ")
                    + algorithmCase.name);
        require(listBackground.withAlpha(1.0f) == LookAndFeel::surface()
                    && listBackground.getAlpha() >= 250 && ! listBackground.isOpaque(),
                "Character list is not on Ocean's surface colour in a window that can "
                "round its corners");
        require(lookAndFeel.findColour(juce::PopupMenu::textColourId)
                        == LookAndFeel::primaryText()
                    && selector->findColour(juce::ComboBox::textColourId)
                        == LookAndFeel::primaryText(),
                "Character drop-down and its list do not use Ocean's text colours");

        // The fill of the highlighted item as it is painted, over the list.
        const auto fill = LookAndFeel::surface().overlaidWith(highlight);
        const auto highlightedText = lookAndFeel.findColour(
            juce::PopupMenu::highlightedTextColourId);
        const auto darkContrast = contrastRatio(fill, darkText);
        const auto lightContrast = contrastRatio(fill, lightText);
        require(highlightedText == (darkContrast >= lightContrast ? darkText : lightText),
                std::string("Highlighted item of the Character list does not carry the text "
                            "of higher contrast in the accent of ")
                    + algorithmCase.name);
        require(contrastRatio(fill, highlightedText) >= 4.5,
                std::string("Text on the highlighted item of the Character list is below "
                            "4.5 : 1 in the accent of ")
                    + algorithmCase.name);
        std::cout << "[METRIC] Character list highlighted in the accent of "
                  << algorithmCase.name << ": "
                  << (highlightedText == darkText ? "dark" : "white") << " text at "
                  << contrastRatio(fill, highlightedText) << " : 1 (the other tone "
                  << std::min(darkContrast, lightContrast) << " : 1)\n";
    }

    // The rule at its ends, in a look-and-feel no accent was given to, and at
    // every step of a morph from the first accent to the last.
    require(LookAndFeel::textOn(juce::Colours::white) == darkText
                && LookAndFeel::textOn(juce::Colours::black) == lightText,
            "Text on a white or on a black fill is not the opposite tone");
    LookAndFeel lookAndFeel;
    require(lookAndFeel.findColour(juce::PopupMenu::highlightedTextColourId)
                == LookAndFeel::textOn(LookAndFeel::surface().overlaidWith(
                       lookAndFeel.findColour(juce::PopupMenu::highlightedBackgroundColourId))),
            "A new look-and-feel does not choose the text of its highlighted item by contrast");
    for (auto step = 0; step <= 32; ++step)
    {
        lookAndFeel.setAccentColour(amanita::ui::characterAccent(0).interpolatedWith(
            amanita::ui::characterAccent(lastCharacterIndex), static_cast<float>(step) / 32.0f));
        const auto fill = LookAndFeel::surface().overlaidWith(
            lookAndFeel.findColour(juce::PopupMenu::highlightedBackgroundColourId));
        const auto text = lookAndFeel.findColour(juce::PopupMenu::highlightedTextColourId);
        require((text == darkText || text == lightText)
                    && contrastRatio(fill, text)
                        >= contrastRatio(fill, text == darkText ? lightText : darkText),
                "Text of the highlighted item does not follow the accent through a morph");
    }
}

// Without the shader the editor paints the Deep Current frame that gathers
// round the Evolution dial. With its controls hidden the editor paints nothing
// else under the footer rule left of the first divider, so there its picture
// and that frame agree pixel for pixel, and a frame gathered round the middle
// of the window does not. Nor does it paint anything of its own between the
// header rule and the Character drop-down, which stands without a label.
void testEditorBackgroundGathersRoundTheEvolutionDial()
{
    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    auto* dial = findDescendantById(*editor, "evolution");
    require(dial != nullptr, "Evolution dial was not found");
    for (auto* child : editor->getChildren())
        child->setVisible(false);

    constexpr std::array<int, 3> widths {
        AmanitaOceanAudioProcessorEditor::minimumWidth,
        AmanitaOceanAudioProcessorEditor::defaultWidth,
        AmanitaOceanAudioProcessorEditor::maximumWidth
    };
    for (const auto width : widths)
    {
        const auto height = width * AmanitaOceanAudioProcessorEditor::defaultHeight
                          / AmanitaOceanAudioProcessorEditor::defaultWidth;
        editor->setSize(width, height);
        const auto painted = editor->createComponentSnapshot(editor->getLocalBounds(), true,
                                                             1.0f, juce::SoftwareImageType {});
        const auto dialCentre = editor->getLocalArea(dial, dial->getLocalBounds())
                                    .toFloat().getCentre();

        amanita::ui::DeepCurrentRenderer renderer;
        renderer.reset(0, 0.35f, false);
        renderer.setSize(width, height);
        renderer.setFocalPoint(dialCentre.x / static_cast<float>(width),
                               dialCentre.y / static_cast<float>(height));
        renderer.render(backgroundAccent(0));
        const auto gathered = paintBackgroundFrame(renderer, width, height);
        renderer.setFocalPoint(0.5f, 0.5f);
        renderer.render(backgroundAccent(0));
        const auto centred = paintBackgroundFrame(renderer, width, height);

        const auto scale = static_cast<float>(width)
                         / AmanitaOceanAudioProcessorEditor::defaultWidth;
        const auto region = juce::Rectangle<float>(40.0f, 490.0f, 280.0f, 140.0f)
                                .transformedBy(juce::AffineTransform::scale(scale))
                                .toNearestInt();
        const auto againstGathered = measureImageDifferenceInRegion(painted, gathered, region);
        const auto againstCentred = measureImageDifferenceInRegion(painted, centred, region);
        std::cout << "[METRIC] Editor " << width << " x " << height
                  << " background under the footer rule: " << againstGathered.changedPixels
                  << " of " << againstGathered.totalPixels
                  << " pixels differ from the frame gathered round the Evolution dial, "
                  << againstCentred.changedPixels
                  << " from the frame gathered round the middle of the window\n";
        require(againstGathered.changedPixels == 0,
                "The editor's background does not gather round the Evolution dial at width "
                    + std::to_string(width));
        require(againstCentred.changedPixels > againstCentred.totalPixels / 100,
                "The background's focal point does not show under the footer rule at width "
                    + std::to_string(width));

        const auto overSelector = juce::Rectangle<float>(310.0f, 84.0f, 340.0f, 26.0f)
                                      .transformedBy(juce::AffineTransform::scale(scale))
                                      .toNearestInt();
        require(measureImageDifferenceInRegion(painted, gathered, overSelector).changedPixels
                    == 0,
                "The editor paints something of its own over the Character drop-down at width "
                    + std::to_string(width));
    }
}

// The control that key presses go to, by its component ID, and whether it is
// a value open for typing: the editor of a value is a child of its label and
// has no ID of its own.
[[nodiscard]] juce::String focusedControl()
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const auto isOpenValue = dynamic_cast<juce::TextEditor*>(focused) != nullptr;
    for (auto* component = focused; component != nullptr;
         component = component->getParentComponent())
        if (component->getComponentID().isNotEmpty())
            return component->getComponentID() + (isOpenValue ? " (open)" : "");

    return "nothing";
}

// The editor in a window of its own, which assistive technology and the
// keyboard need. Assistive technology is told what the Character drop-down
// is, which Character it shows, what the description block says and what the
// eight items of the open list are called. Tab walks from the drop-down through
// the Evolution dial and its value and the nine dials of the lower row, each
// with its value, to Mono Safe and Freeze and back to the drop-down;
// Shift+Tab walks the same way back. A value opens for typing when it is
// tabbed to. A value that is left hands the keyboard to its dial one message
// later, and only if no other control has taken it by then, so the focus is
// read a few messages after every key; Return in an open value ends the edit
// on its dial. The message loop of a process runs once: main() gives it to
// this test or to a live snapshot.
void testKeyboardAndAccessibilityInTheEditorWindow()
{
    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    require(editor->getPeer() != nullptr, "The editor got no window for its keyboard walk");
    editor->getPeer()->setAlpha(0.0f);
    editor->setVisible(true);
    auto* selector = dynamic_cast<juce::ComboBox*>(
        findDescendantById(*editor, "character-selector"));
    auto* description = findDescendantById(*editor, "character-description");
    require(selector != nullptr && description != nullptr,
            "Character drop-down or description block was not found");

    auto* selectorHandler = selector->getAccessibilityHandler();
    auto* descriptionHandler = description->getAccessibilityHandler();
    require(selectorHandler != nullptr && descriptionHandler != nullptr,
            "Character drop-down or description block is not accessible in its window");
    require(selectorHandler->getRole() == juce::AccessibilityRole::comboBox
                && selectorHandler->getTitle().containsIgnoreCase("character")
                && selectorHandler->getDescription().contains("Return or Space")
                && selectorHandler->getValueInterface() != nullptr,
            "Assistive technology is not told what the Character drop-down is and how its "
            "keys work");
    for (const auto& algorithmCase : algorithmCases)
    {
        algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
        const auto& approved
            = approvedDescriptions[static_cast<std::size_t>(algorithmCase.rawIndex)];
        require(selectorHandler->getValueInterface()->getCurrentValueAsString()
                    == algorithmCase.name,
                std::string("Assistive technology is not told that the drop-down shows ")
                    + algorithmCase.name);
        require(descriptionHandler->getRole() == juce::AccessibilityRole::staticText
                    && descriptionHandler->getTitle() == approved.name
                    && descriptionHandler->getDescription().contains(approved.subtitle)
                    && descriptionHandler->getDescription().contains(approved.paragraph),
                std::string("Assistive technology is not told what the description block says "
                            "of ")
                    + algorithmCase.name);
    }
    algorithmParameter(processor).setValueNotifyingHost(algorithmCases.front().hostValue);

    selector->showPopup();
    auto* list = juce::Component::getCurrentlyModalComponent();
    require(list != nullptr
                && list->getNumChildComponents() == static_cast<int>(algorithmCases.size()),
            "The Character list did not open with its eight items");
    for (const auto& algorithmCase : algorithmCases)
    {
        auto* itemHandler = list->getChildComponent(algorithmCase.rawIndex)
                                ->getAccessibilityHandler();
        require(itemHandler != nullptr
                    && itemHandler->getRole() == juce::AccessibilityRole::menuItem
                    && itemHandler->getTitle() == algorithmCase.name,
                std::string("Assistive technology is not told the name of the list item ")
                    + algorithmCase.name);
    }
    selector->hidePopup();

    selector->grabKeyboardFocus();
    if (! selector->hasKeyboardFocus(false))
    {
        std::cout << "[SKIP] Keyboard walk: this desktop gave the editor's window no "
                     "keyboard focus\n";
        return;
    }

    constexpr std::array<const char*, 10> dials {
        "evolution", "preDelay", "size", "decay", "lowCut", "highDamping",
        "harmony", "width", "focus", "mix"
    };
    std::vector<juce::String> forwardRound;
    for (const auto* dial : dials)
    {
        forwardRound.push_back(dial);
        forwardRound.push_back(juce::String(dial) + "-value (open)");
    }
    forwardRound.insert(forwardRound.end(), { "mono-safe", "freeze", "character-selector" });

    // The walk: once round forwards, once round backwards, then forwards
    // into the Evolution value and Return.
    const juce::KeyPress tab(juce::KeyPress::tabKey);
    const juce::KeyPress shiftTab(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
    std::vector<juce::KeyPress> keys(forwardRound.size(), tab);
    keys.insert(keys.end(), forwardRound.size(), shiftTab);
    keys.insert(keys.end(), { tab, tab, juce::KeyPress(juce::KeyPress::returnKey) });
    std::vector<juce::String> expectedStops { "character-selector" };
    expectedStops.insert(expectedStops.end(), forwardRound.begin(), forwardRound.end());
    expectedStops.insert(expectedStops.end(), forwardRound.rbegin() + 1, forwardRound.rend());
    expectedStops.insert(expectedStops.end(),
                         { "character-selector", "evolution", "evolution-value (open)",
                           "evolution" });

    // A turn of the walk reads where the key of the turn before has left the
    // focus, presses the next key and posts the next turn. What a key sets
    // off is done within two messages of it: Return closes a value by a
    // message, and the closed value hands the keyboard on by another. The
    // next turn is therefore posted through three messages in a row, each
    // behind everything the one before has posted.
    constexpr auto messagesBetweenTurns = 3;
    std::vector<juce::String> stops;
    std::function<void()> takeTurn;
    std::function<void(int)> postTurn = [&](int messagesToPass)
    {
        juce::MessageManager::callAsync([&postTurn, &takeTurn, messagesToPass]
        {
            if (messagesToPass > 1)
                postTurn(messagesToPass - 1);
            else
                takeTurn();
        });
    };
    takeTurn = [&]
    {
        stops.push_back(focusedControl());
        if (stops.size() > keys.size())
        {
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
        editor->getPeer()->handleKeyPress(keys[stops.size() - 1]);
        postTurn(messagesBetweenTurns);
    };

    struct Wait final : juce::Timer
    {
        void timerCallback() override { step(); }
        std::function<void()> step;
    };

    // The window comes to the front through messages of its own, which hand
    // the keyboard round once more: the walk starts when the drop-down has
    // kept it for a few turns of a timer.
    constexpr auto turnsAtRestWanted = 5;
    constexpr auto turnsToComeToRest = 300;
    auto turnsAtRest = 0;
    auto turnsWaited = 0;
    Wait wait;
    wait.step = [&]
    {
        ++turnsWaited;
        turnsAtRest = selector->hasKeyboardFocus(false) ? turnsAtRest + 1 : 0;
        if (turnsAtRest == 0)
            selector->grabKeyboardFocus();
        if (turnsAtRest < turnsAtRestWanted && turnsWaited < turnsToComeToRest)
            return;

        wait.stopTimer();
        if (turnsAtRest >= turnsAtRestWanted)
            postTurn(messagesBetweenTurns);
        else
            juce::MessageManager::getInstance()->stopDispatchLoop();
    };
    wait.startTimer(10);
    juce::MessageManager::getInstance()->runDispatchLoop();
    require(turnsAtRest >= turnsAtRestWanted,
            "The Character drop-down does not keep the keyboard focus in the editor's window");

    juce::String walk;
    for (const auto& stop : stops)
        walk << (walk.isEmpty() ? "" : " > ") << stop;
    require(stops == expectedStops,
            "The keyboard does not walk through the editor control by control: "
                + walk.toStdString());
    std::cout << "[METRIC] Keyboard walk in the editor's window: " << keys.size()
              << " keys, one round forwards: " << forwardRound.size() << " stops\n";
}

// A path of a snapshot as given on the command line, absolute or relative to
// the working directory.
[[nodiscard]] juce::File snapshotFile(const juce::String& path)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

void writePng(const juce::Image& image, const juce::String& path)
{
    require(image.isValid(), "Custom editor PNG snapshot is invalid");
    const auto output = snapshotFile(path);
    output.deleteFile();
    juce::FileOutputStream stream(output);
    require(stream.openedOk(), "Could not open custom editor PNG output");
    juce::PNGImageFormat png;
    require(png.writeImageToStream(image, stream), "Could not write custom editor PNG");
    stream.flush();
}

// A pointer event as JUCE hands it to a component: at a place in the
// component's own coordinates, with the mouse buttons that are held.
[[nodiscard]] juce::MouseEvent pointerEvent(juce::Component& component,
                                            juce::Point<float> position,
                                            juce::ModifierKeys buttons = {})
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(),
             position,
             buttons,
             juce::MouseInputSource::defaultPressure,
             juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation,
             juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY,
             &component,
             &component,
             now,
             position,
             now,
             1,
             false };
}

// What a snapshot of the editor shows: the Character, the editor's width, the
// host values of Freeze, Evolution and Focus, and the item of the Character
// list that is highlighted when the list is open (negative: the list closed).
struct SnapshotSettings
{
    int characterIndex = 0;
    int width = AmanitaOceanAudioProcessorEditor::defaultWidth;
    bool frozen = false;
    int highlightedListItem = -1;
    float evolution = 0.68f;
    float focus = 1.0f;
    // The step chevron the pointer rests on: negative the one that steps back,
    // positive the one that steps on, zero neither.
    int stepUnderPointer = 0;
};

// A processor with the settings of a snapshot and its editor at the snapshot's
// size.
struct SnapshotEditor
{
    explicit SnapshotEditor(const SnapshotSettings& settings)
        : character(std::clamp(settings.characterIndex, 0, lastCharacterIndex))
    {
        algorithmParameter(processor).setValueNotifyingHost(
            algorithmCases[static_cast<std::size_t>(character)].hostValue);
        parameterById(processor, "evolution").setValueNotifyingHost(settings.evolution);
        parameterById(processor, "focus").setValueNotifyingHost(settings.focus);
        parameterById(processor, "freeze").setValueNotifyingHost(settings.frozen ? 1.0f : 0.0f);

        editor.reset(processor.createEditor());
        require(editor != nullptr, "Could not create editor for PNG render");
        const auto width = std::clamp(settings.width,
                                      AmanitaOceanAudioProcessorEditor::minimumWidth,
                                      AmanitaOceanAudioProcessorEditor::maximumWidth);
        editor->setSize(width, width * AmanitaOceanAudioProcessorEditor::defaultHeight
                                   / AmanitaOceanAudioProcessorEditor::defaultWidth);
        selector = dynamic_cast<juce::ComboBox*>(
            findDescendantById(*editor, "character-selector"));
        require(selector != nullptr, "Character drop-down was not found for PNG render");
        if (settings.stepUnderPointer != 0)
        {
            stepUnderPointer = dynamic_cast<amanita::ui::CharacterStepButton*>(
                findDescendantById(*editor, settings.stepUnderPointer < 0 ? "character-previous"
                                                                          : "character-next"));
            require(stepUnderPointer != nullptr, "Step chevron was not found for PNG render");
            bringPointerToStep();
        }
    }

    // The pointer comes to the chevron it is to rest on. On no screen the
    // chevron takes the accent at once; in a window it eases to it.
    void bringPointerToStep()
    {
        if (stepUnderPointer == nullptr)
            return;

        stepUnderPointer->mouseExit(pointerEvent(*stepUnderPointer, { -4.0f, -4.0f }));
        stepUnderPointer->mouseEnter(pointerEvent(
            *stepUnderPointer, stepUnderPointer->getLocalBounds().toFloat().getCentre()));
    }

    AmanitaOceanAudioProcessor processor;
    int character;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::ComboBox* selector = nullptr;
    amanita::ui::CharacterStepButton* stepUnderPointer = nullptr;
};

// The picture of the open Character list and where it opened in the editor.
struct OpenList
{
    juce::Image image;
    juce::Rectangle<int> bounds;
};

// Opens the Character list, which is a window of its own on the desktop,
// moves its highlight to `highlightedItem`, checks that it opened under its
// field at the field's width, takes its picture and closes it again.
[[nodiscard]] OpenList paintOpenList(SnapshotEditor& snapshot,
                                     int highlightedItem,
                                     float pixelsPerPoint)
{
    snapshot.selector->showPopup();
    auto* list = juce::Component::getCurrentlyModalComponent();
    require(list != nullptr, "The Character list did not open");
    const auto steps = std::min(highlightedItem, lastCharacterIndex) - snapshot.character;
    for (auto step = 0; step < std::abs(steps); ++step)
        list->keyPressed(juce::KeyPress(steps > 0 ? juce::KeyPress::downKey
                                                  : juce::KeyPress::upKey));

    auto& editor = *snapshot.editor;
    const auto field = editor.getLocalArea(snapshot.selector,
                                           snapshot.selector->getLocalBounds());
    const auto bounds = list->getScreenBounds() - editor.getScreenPosition();
    std::cout << "[METRIC] Character list at editor width " << editor.getWidth() << ": field "
              << field.toString() << ", list " << bounds.toString() << ", "
              << list->getNumChildComponents() << " items of height "
              << list->getChildComponent(0)->getHeight() << '\n';
    require(bounds.getX() == field.getX()
                && bounds.getWidth() == field.getWidth()
                && bounds.getY() > field.getBottom()
                && bounds.getY() - field.getBottom() <= 8,
            "The Character list did not open under its field at the field's width");
    require(editor.getLocalBounds().contains(bounds),
            "The Character list is cut off by the editor's window");
    for (auto* item : list->getChildren())
        require(list->getLocalBounds().contains(item->getBounds()),
                "An item of the Character list lies outside the list");

    OpenList result { list->createComponentSnapshot(list->getLocalBounds(), false,
                                                    pixelsPerPoint,
                                                    juce::SoftwareImageType {}),
                      bounds };
    snapshot.selector->hidePopup();
    return result;
}

void layOver(juce::Image& image, const OpenList& list, float pixelsPerPoint)
{
    juce::Graphics graphics(image);
    graphics.drawImageAt(list.image,
                         juce::roundToInt(static_cast<float>(list.bounds.getX())
                                          * pixelsPerPoint),
                         juce::roundToInt(static_cast<float>(list.bounds.getY())
                                          * pixelsPerPoint));
}

// The editor as it paints itself without a window: the CPU background under
// its controls.
void renderEditorPng(const juce::String& path, const SnapshotSettings& settings)
{
    constexpr auto pixelsPerPoint = 2.0f;
    SnapshotEditor snapshot(settings);
    auto& editor = *snapshot.editor;
    const auto paintEditor = [&]
    {
        return editor.createComponentSnapshot(editor.getLocalBounds(), true, pixelsPerPoint,
                                              juce::SoftwareImageType {});
    };

    if (settings.highlightedListItem < 0)
    {
        writePng(paintEditor(), path);
        return;
    }

    // The field shows that its list is open only while it is.
    snapshot.selector->showPopup();
    auto image = paintEditor();
    snapshot.selector->hidePopup();
    layOver(image, paintOpenList(snapshot, settings.highlightedListItem, pixelsPerPoint),
            pixelsPerPoint);
    writePng(image, path);
}

// The editor as a host shows it: in a window of its own, where the OpenGL
// background runs under the controls. The window is fully transparent on the
// desktop. Once the shader paints, the picture is read back from the OpenGL
// context `secondsOfAnimation` later, at the pixels of the display. With a
// `shaderFramePath` the controls, painted as they are over the shader, are
// laid over that picture instead: a frame of the shader from another moment.
void renderLiveEditorPng(const juce::String& path,
                         const SnapshotSettings& settings,
                         double secondsOfAnimation,
                         const juce::String& shaderFramePath)
{
    SnapshotEditor snapshot(settings);
    auto& editor = *snapshot.editor;
    editor.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    require(editor.getPeer() != nullptr, "The editor got no window for its live picture");
    editor.getPeer()->setAlpha(0.0f);
    editor.setVisible(true);
    snapshot.bringPointerToStep();

    struct Capture final : juce::Timer
    {
        void timerCallback() override { step(); }
        std::function<void()> step;
    };

    std::vector<std::uint8_t> pixels;
    juce::Image controls;
    OpenList openList;
    auto pixelWidth = 0;
    auto pixelHeight = 0;
    auto shaderSeenAt = 0.0;
    std::string failure;
    const auto startedAt = juce::Time::getMillisecondCounterHiRes();
    Capture capture;
    capture.step = [&]
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        auto* context = juce::OpenGLContext::getContextAttachedTo(editor);
        // Over the shader the editor paints a thin wash; without it, every pixel.
        const auto probe = editor.createComponentSnapshot({ 8, editor.getHeight() / 2, 1, 1 },
                                                          true, 1.0f,
                                                          juce::SoftwareImageType {});
        if (context != nullptr && probe.getPixelAt(0, 0).getAlpha() < 255 && shaderSeenAt <= 0.0)
            shaderSeenAt = now;

        if (shaderSeenAt > 0.0 && now - shaderSeenAt >= 1000.0 * secondsOfAnimation)
        {
            const auto pixelsPerPoint = shaderFramePath.isNotEmpty()
                ? 2.0f
                : static_cast<float>(context->getRenderingScale());
            // The list closes by itself in a process that is not in front, so
            // it is opened, painted and closed in one go.
            if (settings.highlightedListItem >= 0)
                openList = paintOpenList(snapshot, settings.highlightedListItem, pixelsPerPoint);

            if (shaderFramePath.isNotEmpty())
            {
                controls = editor.createComponentSnapshot(editor.getLocalBounds(), true,
                                                          pixelsPerPoint,
                                                          juce::SoftwareImageType {});
            }
            else
            {
                context->executeOnGLThread([&](juce::OpenGLContext& glContext)
                {
                    using namespace juce::gl;
                    pixelWidth = juce::roundToInt(pixelsPerPoint
                                                  * static_cast<float>(editor.getWidth()));
                    pixelHeight = juce::roundToInt(pixelsPerPoint
                                                   * static_cast<float>(editor.getHeight()));
                    pixels.resize(static_cast<std::size_t>(pixelWidth * pixelHeight * 4));
                    glBindFramebuffer(GL_FRAMEBUFFER, glContext.getFrameBufferID());
                    glReadBuffer(GL_FRONT);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, pixelWidth, pixelHeight, GL_BGRA, GL_UNSIGNED_BYTE,
                                 pixels.data());
                    glReadBuffer(GL_BACK);
                }, true);
            }
        }
        else if (now - startedAt > 10000.0 + 1000.0 * secondsOfAnimation)
        {
            failure = "The OpenGL background did not start within ten seconds";
        }

        if (! pixels.empty() || controls.isValid() || ! failure.empty())
        {
            capture.stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    };
    capture.startTimer(40);
    juce::MessageManager::getInstance()->runDispatchLoop();
    require(failure.empty(), failure);

    juce::Image image;
    if (controls.isValid())
    {
        const auto shaderFrame = juce::ImageFileFormat::loadFrom(snapshotFile(shaderFramePath));
        require(shaderFrame.isValid(), "Could not read the shader frame for the live picture");
        pixelWidth = controls.getWidth();
        pixelHeight = controls.getHeight();
        image = juce::Image(juce::Image::ARGB, pixelWidth, pixelHeight, true,
                            juce::SoftwareImageType {});
        juce::Graphics graphics(image);
        graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        graphics.drawImage(shaderFrame, image.getBounds().toFloat());
        graphics.drawImageAt(controls, 0, 0);
    }
    else
    {
        // OpenGL keeps its rows from the bottom up.
        image = juce::Image(juce::Image::ARGB, pixelWidth, pixelHeight, true,
                            juce::SoftwareImageType {});
        const juce::Image::BitmapData target(image, juce::Image::BitmapData::writeOnly);
        for (auto y = 0; y < pixelHeight; ++y)
        {
            const auto* source = pixels.data()
                               + static_cast<std::size_t>((pixelHeight - 1 - y) * pixelWidth * 4);
            for (auto x = 0; x < pixelWidth; ++x)
                target.setPixelColour(x, y, juce::Colour(source[x * 4 + 2], source[x * 4 + 1],
                                                         source[x * 4]));
        }
    }

    const auto pixelsPerPoint = static_cast<float>(pixelWidth)
                              / static_cast<float>(editor.getWidth());
    std::cout << "[METRIC] Live editor " << editor.getWidth() << " x " << editor.getHeight()
              << (controls.isValid() ? " laid over a given shader frame at "
                                     : " read back from OpenGL at ")
              << pixelWidth << " x " << pixelHeight << " pixels, "
              << (juce::Time::getMillisecondCounterHiRes() - shaderSeenAt) * 0.001
              << " s after the shader's first frame; the Character drop-down "
              << (snapshot.selector->hasKeyboardFocus(true) ? "has" : "does not have")
              << " the keyboard focus";
    if (snapshot.stepUnderPointer != nullptr)
        std::cout << "; the step chevron under the pointer has eased to "
                  << snapshot.stepUnderPointer->getEmphasis() << " of the accent";
    std::cout << '\n';
    if (openList.image.isValid())
        layOver(image, openList, pixelsPerPoint);
    writePng(image, path);
}

void renderBackgroundPng(const juce::String& path,
                         int characterIndex,
                         float evolution,
                         double timeSeconds,
                         int requestedWidth)
{
    const auto safeCharacter = std::clamp(characterIndex, 0, lastCharacterIndex);
    const auto safeEvolution = std::isfinite(evolution)
        ? std::clamp(evolution, 0.0f, 1.0f)
        : 0.68f;
    const auto safeTime = std::isfinite(timeSeconds) ? timeSeconds : 0.0;
    const auto width = std::clamp(requestedWidth,
                                  AmanitaOceanAudioProcessorEditor::minimumWidth,
                                  AmanitaOceanAudioProcessorEditor::maximumWidth);
    const auto height = width * AmanitaOceanAudioProcessorEditor::defaultHeight
                      / AmanitaOceanAudioProcessorEditor::defaultWidth;
    const auto image = renderBackgroundFrame(width, height, safeCharacter,
                                             safeEvolution, false, safeTime,
                                             backgroundAccent(safeCharacter));

    juce::File output(path);
    output.deleteFile();
    juce::FileOutputStream stream(output);
    require(stream.openedOk(), "Could not open Deep Current PNG output");
    juce::PNGImageFormat png;
    require(png.writeImageToStream(image, stream),
            "Could not write Deep Current PNG");
    stream.flush();
}

void benchmarkBackgroundRenderer(int requestedWidth, int requestedFrames)
{
    const auto width = std::clamp(requestedWidth,
                                  AmanitaOceanAudioProcessorEditor::minimumWidth,
                                  AmanitaOceanAudioProcessorEditor::maximumWidth);
    const auto height = width * AmanitaOceanAudioProcessorEditor::defaultHeight
                      / AmanitaOceanAudioProcessorEditor::defaultWidth;
    const auto frames = std::clamp(requestedFrames, 12, 600);
    amanita::ui::DeepCurrentRenderer renderer;
    renderer.setFocalPoint(editorFocalX, editorFocalY);
    renderer.reset(2, 0.82f, false);
    renderer.setSize(width, height);

    for (auto frame = 0; frame < 30; ++frame)
    {
        static_cast<void>(renderer.advance(1.0 / 30.0, 2, 0.82f, false));
        renderer.render(backgroundAccent(2));
    }

    std::vector<double> milliseconds;
    milliseconds.reserve(static_cast<std::size_t>(frames));
    for (auto frame = 0; frame < frames; ++frame)
    {
        static_cast<void>(renderer.advance(1.0 / 30.0, 2, 0.82f, false));
        const auto started = std::chrono::steady_clock::now();
        renderer.render(backgroundAccent(2));
        const auto finished = std::chrono::steady_clock::now();
        milliseconds.push_back(std::chrono::duration<double, std::milli>(finished - started)
                                   .count());
    }

    std::sort(milliseconds.begin(), milliseconds.end());
    const auto total = std::accumulate(milliseconds.begin(), milliseconds.end(), 0.0);
    const auto percentileIndex = static_cast<std::size_t>(
        std::floor(0.95 * static_cast<double>(milliseconds.size() - 1)));
    std::cout << std::fixed << std::setprecision(3)
              << "[METRIC] Deep Current CPU render " << width << 'x' << height
              << " (cache " << renderer.getFrameWidth() << 'x'
              << renderer.getFrameHeight() << ')'
              << ": mean=" << total / static_cast<double>(milliseconds.size())
              << " ms, p95=" << milliseconds[percentileIndex]
              << " ms, max=" << milliseconds.back()
              << " ms over " << frames << " frames, one-core load at 30 FPS="
              << total / static_cast<double>(milliseconds.size()) * 3.0
              << "%\n";
}

// `fathomVoiceSeed` receives the seed the instance drew for the voice phase of
// the sixth Character.
std::vector<float> renderProcessor(const AlgorithmCase& algorithmCase,
                                   float evolution,
                                   float focus = 0.0f,
                                   float sizePercent = 100.0f,
                                   bool monoSafe = false,
                                   std::uint64_t* fathomVoiceSeed = nullptr)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 24000;
    constexpr auto blockSize = 512;

    AmanitaOceanAudioProcessor processor;
    if (fathomVoiceSeed != nullptr)
        *fathomVoiceSeed = processor.getFathomVoiceSeed();
    algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
    parameterById(processor, "mix").setValueNotifyingHost(1.0f);
    parameterById(processor, "preDelay").setValueNotifyingHost(0.0f);
    parameterById(processor, "evolution").setValueNotifyingHost(evolution);
    parameterById(processor, "focus").setValueNotifyingHost(focus);
    parameterById(processor, "size").setValueNotifyingHost(sizePercent / 200.0f);
    parameterById(processor, "monoSafe").setValueNotifyingHost(
        monoSafe ? 1.0f : 0.0f);
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
                             float focus = 0.0f,
                             float sizeScale = 1.0f,
                             bool monoSafe = false,
                             std::uint64_t fathomVoiceSeed = amanita::dsp::FathomEngine::defaultVoiceSeed)
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
    parameters.size = sizeScale;
    parameters.monoSafeStereo = monoSafe;

    amanita::dsp::FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.setFathomVoiceSeed(fathomVoiceSeed);
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
            // The direct render runs from the voice seed the instance drew.
            std::uint64_t fathomVoiceSeed = 0;
            const auto processorRender = renderProcessor(algorithmCase, evolution, 0.0f, 100.0f,
                                                         false, &fathomVoiceSeed);
            const auto directRender = renderDsp(algorithmCase, evolution, 0.0f, 1.0f, false,
                                                fathomVoiceSeed);
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

void testSizeParameterReachesDsp()
{
    struct SizeCase
    {
        float percent;
        float scale;
    };

    constexpr std::array sizeCases {
        SizeCase { 0.0f,   0.15f },
        SizeCase { 25.0f,  0.2875f },
        SizeCase { 50.0f,  0.5f },
        SizeCase { 100.0f, 1.0f },
        SizeCase { 200.0f, 2.0f }
    };

    const auto& algorithmCase = algorithmCases.front();
    for (const auto& sizeCase : sizeCases)
    {
        const auto processorRender = renderProcessor(
            algorithmCase, 0.35f, 0.0f, sizeCase.percent);
        const auto directRender = renderDsp(
            algorithmCase, 0.35f, 0.0f, sizeCase.scale);
        require(processorRender.size() == directRender.size(),
                "Size routing render has the wrong size");

        auto maximumDifference = 0.0f;
        for (std::size_t sample = 0; sample < processorRender.size(); ++sample)
        {
            require(std::isfinite(processorRender[sample]),
                    "Size routing produced NaN/Inf");
            maximumDifference = std::max(
                maximumDifference,
                std::abs(processorRender[sample] - directRender[sample]));
        }

        require(maximumDifference <= 2.0e-7f,
                "Host Size percent does not reach the expected DSP scale at "
                    + std::to_string(sizeCase.percent) + "%: maximum difference="
                    + std::to_string(maximumDifference));
    }
}

void testMonoSafeParameterReachesDsp()
{
    const auto& algorithmCase = algorithmCases.front();
    const auto processorRender = renderProcessor(
        algorithmCase, 0.35f, 0.0f, 100.0f, true);
    const auto directRender = renderDsp(
        algorithmCase, 0.35f, 0.0f, 1.0f, true);
    const auto openRender = renderProcessor(
        algorithmCase, 0.35f, 0.0f, 100.0f, false);
    require(processorRender.size() == directRender.size()
                && processorRender.size() == openRender.size(),
            "Mono Safe routing render has the wrong size");

    auto maximumDifference = 0.0f;
    auto referenceEnergy = 0.0;
    auto voicingDifferenceEnergy = 0.0;
    for (std::size_t sample = 0; sample < processorRender.size(); ++sample)
    {
        require(std::isfinite(processorRender[sample]),
                "Mono Safe routing produced NaN/Inf");
        maximumDifference = std::max(
            maximumDifference,
            std::abs(processorRender[sample] - directRender[sample]));
        const auto reference = static_cast<double>(openRender[sample]);
        const auto difference = static_cast<double>(
            processorRender[sample] - openRender[sample]);
        referenceEnergy += reference * reference;
        voicingDifferenceEnergy += difference * difference;
    }

    const auto normalisedDifference = std::sqrt(
        voicingDifferenceEnergy / std::max(referenceEnergy, 1.0e-20));
    require(maximumDifference <= 2.0e-7f,
            "Host Mono Safe parameter does not reach the expected DSP path");
    require(normalisedDifference >= 0.05 && normalisedDifference <= 2.0,
            "Mono Safe host parameter does not select a distinct stereo voicing");
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

[[nodiscard]] std::vector<float> renderAutoHarmony(bool throughProcessor,
                                                   float harmony)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 144000;
    constexpr auto blockSize = 127;

    AmanitaOceanAudioProcessor processor;
    amanita::dsp::FDNReverb direct;
    if (throughProcessor)
    {
        parameterById(processor, "mix").setValueNotifyingHost(1.0f);
        parameterById(processor, "preDelay").setValueNotifyingHost(0.0f);
        parameterById(processor, "focus").setValueNotifyingHost(0.0f);
        parameterById(processor, "harmony").setValueNotifyingHost(harmony);
        processor.prepareToPlay(sampleRate, blockSize);
    }
    else
    {
        amanita::dsp::ReverbParameters parameters;
        parameters.mix = 1.0f;
        parameters.preDelayMs = 0.0f;
        parameters.harmony = harmony;
        parameters.autoHarmony = true;
        direct.setParameters(parameters);
        direct.prepare(sampleRate, blockSize);
    }

    std::vector<float> render(static_cast<std::size_t>(sampleCount * 2), 0.0f);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (auto offset = 0; offset < sampleCount; offset += blockSize)
    {
        const auto samplesThisBlock = std::min(blockSize, sampleCount - offset);
        buffer.setSize(2, samplesThisBlock, false, false, true);
        for (auto sample = 0; sample < samplesThisBlock; ++sample)
        {
            const auto absoluteSample = offset + sample;
            const auto time = static_cast<double>(absoluteSample) / sampleRate;
            const auto envelope = absoluteSample < 93600
                ? 1.0
                : absoluteSample < 96000
                    ? static_cast<double>(96000 - absoluteSample) / 2400.0
                    : 0.0;
            const auto c = std::sin(2.0 * juce::MathConstants<double>::pi
                                    * 261.625565 * time);
            const auto e = std::sin(2.0 * juce::MathConstants<double>::pi
                                    * 329.627557 * time + 0.31);
            const auto g = std::sin(2.0 * juce::MathConstants<double>::pi
                                    * 391.995436 * time + 0.57);
            const auto left = static_cast<float>(
                0.045 * envelope * (c + 0.72 * e + 0.41 * g));
            const auto right = static_cast<float>(
                0.045 * envelope * (0.38 * c + 0.76 * e + g));
            buffer.setSample(0, sample, left);
            buffer.setSample(1, sample, right);
        }

        if (throughProcessor)
            processor.processBlock(buffer, midi);
        else
            direct.process(buffer.getWritePointer(0), buffer.getWritePointer(1),
                           samplesThisBlock);

        for (auto sample = 0; sample < samplesThisBlock; ++sample)
        {
            const auto index = static_cast<std::size_t>((offset + sample) * 2);
            render[index] = buffer.getSample(0, sample);
            render[index + 1] = buffer.getSample(1, sample);
        }
    }
    return render;
}

void testHarmonyParameterReachesAutoDsp()
{
    const auto processorRender = renderAutoHarmony(true, 1.0f);
    const auto directRender = renderAutoHarmony(false, 1.0f);
    const auto bypassRender = renderAutoHarmony(true, 0.0f);
    require(processorRender.size() == directRender.size()
                && processorRender.size() == bypassRender.size(),
            "Harmony routing render has the wrong size");

    auto maximumDifference = 0.0f;
    auto peak = 0.0f;
    auto referenceEnergy = 0.0;
    auto effectEnergy = 0.0;
    for (std::size_t sample = 0; sample < processorRender.size(); ++sample)
    {
        require(std::isfinite(processorRender[sample]),
                "Auto Harmony routing produced NaN/Inf");
        maximumDifference = std::max(
            maximumDifference,
            std::abs(processorRender[sample] - directRender[sample]));
        peak = std::max(peak, std::abs(processorRender[sample]));
        const auto reference = static_cast<double>(bypassRender[sample]);
        const auto difference = static_cast<double>(
            processorRender[sample] - bypassRender[sample]);
        referenceEnergy += reference * reference;
        effectEnergy += difference * difference;
    }

    const auto normalisedEffect = std::sqrt(
        effectEnergy / std::max(referenceEnergy, 1.0e-20));
    require(maximumDifference <= 2.0e-7f,
            "Host Harmony parameter does not reach the expected Auto DSP path");
    require(normalisedEffect >= 0.005 && normalisedEffect <= 0.50,
            "Host Harmony parameter is inaudible or excessive");
    require(peak < 4.0f, "Auto Harmony routing exceeded the safety range");
    std::cout << "[METRIC] Host Auto Harmony NRMS=" << normalisedEffect
              << ", peak=" << peak << '\n';
}

// The sixth Character with Ocean's own controls at the host values that take
// them out of its circuit, Width and Mix at 100 %.
void selectNeutralFathom(AmanitaOceanAudioProcessor& processor)
{
    algorithmParameter(processor).setValueNotifyingHost(algorithmCases[fathomIndex].hostValue);
    parameterById(processor, "mix").setValueNotifyingHost(1.0f);
    parameterById(processor, "width").setValueNotifyingHost(0.5f);
    parameterById(processor, "lowCut").setValueNotifyingHost(0.0f);
    parameterById(processor, "highDamping").setValueNotifyingHost(1.0f);
    parameterById(processor, "focus").setValueNotifyingHost(0.0f);
    parameterById(processor, "harmony").setValueNotifyingHost(0.0f);
    parameterById(processor, "monoSafe").setValueNotifyingHost(0.0f);
    parameterById(processor, "freeze").setValueNotifyingHost(0.0f);
}

// Bursts of a tone with an onset above the threshold of the reference's
// level stage, four per second.
[[nodiscard]] float fathomTestInput(int frame, double sampleRate, int channel)
{
    const auto period = static_cast<int>(sampleRate * 0.25);
    const auto position = frame % period;
    if (position >= period / 2)
        return 0.0f;

    const auto tone = static_cast<float>(std::sin(
        2.0 * juce::MathConstants<double>::pi * (channel == 0 ? 220.0 : 331.0)
        * static_cast<double>(frame) / sampleRate));
    return (channel == 0 ? 0.25f : -0.18f) * tone
         + (channel == 0 && position < period / 100 ? 0.45f : 0.0f);
}

// The processor's answer to the test input, which stops after `inputFrames`.
[[nodiscard]] std::vector<float> renderFathomProcessor(AmanitaOceanAudioProcessor& processor,
                                                       double sampleRate,
                                                       int frameCount,
                                                       int inputFrames)
{
    constexpr auto blockSize = 127;
    processor.prepareToPlay(sampleRate, blockSize);

    std::vector<float> render(static_cast<std::size_t>(frameCount * 2), 0.0f);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (auto offset = 0; offset < frameCount; offset += blockSize)
    {
        const auto framesThisBlock = std::min(blockSize, frameCount - offset);
        buffer.setSize(2, framesThisBlock, false, false, true);
        for (auto frame = 0; frame < framesThisBlock; ++frame)
            for (auto channel = 0; channel < 2; ++channel)
                buffer.setSample(channel, frame,
                                 offset + frame < inputFrames
                                     ? fathomTestInput(offset + frame, sampleRate, channel)
                                     : 0.0f);
        processor.processBlock(buffer, midi);
        for (auto frame = 0; frame < framesThisBlock; ++frame)
        {
            const auto index = static_cast<std::size_t>((offset + frame) * 2);
            render[index] = buffer.getSample(0, frame);
            render[index + 1] = buffer.getSample(1, frame);
        }
    }
    return render;
}

[[nodiscard]] bool sameRender(const std::vector<float>& first, const std::vector<float>& second)
{
    return first.size() == second.size()
        && std::memcmp(first.data(), second.data(), first.size() * sizeof(float)) == 0;
}

// From host values to audio: with Ocean's own controls neutral the processor
// returns the engine's wet through its level stage bit for bit, the engine
// holding the values the host parameters carry in the reference's units and
// the voice seed the instance drew.
void testFathomHostValuesReachTheEngine()
{
    constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        const auto frameCount = static_cast<int>(sampleRate * 0.5);
        AmanitaOceanAudioProcessor processor;
        selectNeutralFathom(processor);
        parameterById(processor, "decay").setValueNotifyingHost(0.42f);
        parameterById(processor, "size").setValueNotifyingHost(0.6f);
        parameterById(processor, "preDelay").setValueNotifyingHost(0.1f);
        parameterById(processor, "evolution").setValueNotifyingHost(1.0f);
        const auto processorRender = renderFathomProcessor(processor, sampleRate, frameCount,
                                                           frameCount);

        const auto rawValue = [&](const char* id)
        {
            return processor.getParameterState().getRawParameterValue(id)->load();
        };
        require(std::abs(rawValue("size") - 120.0f) < 1.0e-3f
                    && std::abs(rawValue("preDelay") - 25.0f) < 1.0e-3f
                    && std::abs(rawValue("lowCut") - 20.0f) <= 0.0f
                    && std::abs(rawValue("highDamping") - 20000.0f) <= 0.0f,
                "Fathom test host values did not reach the expected parameter values");

        amanita::dsp::FathomEngine::Parameters engineParameters;
        engineParameters.decaySeconds = rawValue("decay");
        engineParameters.sizeScale = rawValue("size") * 0.01f;
        engineParameters.preDelaySeconds = rawValue("preDelay") * 0.001f;
        engineParameters.macro = rawValue("evolution") * 0.01f;
        amanita::dsp::FathomEngine engine;
        amanita::dsp::FathomEngine::LevelStage levelStage;
        engine.setParameters(engineParameters);
        engine.setVoiceSeed(processor.getFathomVoiceSeed());
        engine.prepare(sampleRate);
        levelStage.prepare(sampleRate);

        auto peak = 0.0f;
        for (auto frame = 0; frame < frameCount; ++frame)
        {
            const auto left = fathomTestInput(frame, sampleRate, 0);
            const auto right = fathomTestInput(frame, sampleRate, 1);
            const auto wet = levelStage.process(left, right, engine.processSample(left, right));
            const auto index = static_cast<std::size_t>(frame * 2);
            require(std::memcmp(&processorRender[index], &wet.left, sizeof(float)) == 0
                        && std::memcmp(&processorRender[index + 1], &wet.right, sizeof(float)) == 0,
                    "Host values do not give the engine's own wet for the sixth Character at "
                        + std::to_string(static_cast<int>(sampleRate)) + " Hz, frame "
                        + std::to_string(frame));
            peak = std::max({ peak, std::abs(wet.left), std::abs(wet.right) });
        }
        require(peak > 0.01f, "Fathom host routing test rendered silence");
    }
}

// What the processor hands its DSP at the present values of its host
// parameters, for a Size of 50 % or more.
[[nodiscard]] amanita::dsp::ReverbParameters dspParametersOf(AmanitaOceanAudioProcessor& processor)
{
    const auto rawValue = [&](const char* id)
    {
        return processor.getParameterState().getRawParameterValue(id)->load();
    };

    amanita::dsp::ReverbParameters parameters;
    parameters.mode = algorithmCases[static_cast<std::size_t>(std::lround(rawValue("algorithm")))].mode;
    parameters.mix = rawValue("mix") * 0.01f;
    parameters.decaySeconds = rawValue("decay");
    parameters.size = rawValue("size") * 0.01f;
    parameters.preDelayMs = rawValue("preDelay");
    parameters.lowCutHz = rawValue("lowCut");
    parameters.highDampingHz = rawValue("highDamping");
    parameters.evolution = rawValue("evolution") * 0.01f;
    parameters.width = rawValue("width") * 0.01f;
    parameters.ducking = rawValue("focus") * 0.01f;
    parameters.harmony = rawValue("harmony") * 0.01f;
    parameters.autoHarmony = true;
    parameters.freeze = rawValue("freeze") >= 0.5f;
    parameters.monoSafeStereo = rawValue("monoSafe") >= 0.5f;
    return parameters;
}

// The DSP's answer to the test input at given parameters, from a given voice
// seed of the sixth Character.
[[nodiscard]] std::vector<float> renderFathomDsp(const amanita::dsp::ReverbParameters& parameters,
                                                 std::uint64_t voiceSeed,
                                                 double sampleRate,
                                                 int frameCount)
{
    amanita::dsp::FDNReverb reverb;
    reverb.setParameters(parameters);
    reverb.setFathomVoiceSeed(voiceSeed);
    reverb.prepare(sampleRate, 127);

    std::vector<float> render(static_cast<std::size_t>(frameCount * 2), 0.0f);
    juce::ScopedNoDenormals noDenormals;
    for (auto frame = 0; frame < frameCount; ++frame)
    {
        const auto index = static_cast<std::size_t>(frame * 2);
        render[index] = fathomTestInput(frame, sampleRate, 0);
        render[index + 1] = fathomTestInput(frame, sampleRate, 1);
        reverb.processSample(render[index], render[index + 1]);
    }
    return render;
}

// Two voice seeds drawn at random start their phases close together now and
// then: of 1500 pairs on the programme of these tests, one or two lay closer
// than 0.01 and none closer than 0.002, by `renderDistance`. Two instances
// count as two realisations above this distance, which a pair falls under
// about once in ten million.
constexpr auto leastDistanceOfTwoVoiceSeeds = 1.0e-4;

// Root mean square of the difference of two renders over that of the first.
[[nodiscard]] double renderDistance(const std::vector<float>& first, const std::vector<float>& second)
{
    auto differenceEnergy = 0.0;
    auto energy = 0.0;
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        const auto difference = static_cast<double>(first[index]) - static_cast<double>(second[index]);
        differenceEnergy += difference * difference;
        energy += static_cast<double>(first[index]) * first[index];
    }
    return std::sqrt(differenceEnergy / std::max(energy, 1.0e-300));
}

// A project saved with the sixth Character selected opens with the settings
// it was saved with and with a voice phase of its own, which no state holds.
// The restored instance renders what the DSP renders at the saved settings
// from that instance's seed: another realisation than the one that was saved.
// At Evolution 0, where no voice moves, the two render the same samples.
void testFathomStateRestoresItsSettings()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 36000;

    AmanitaOceanAudioProcessor source;
    selectNeutralFathom(source);
    parameterById(source, "mix").setValueNotifyingHost(0.64f);
    parameterById(source, "decay").setValueNotifyingHost(0.55f);
    parameterById(source, "size").setValueNotifyingHost(0.37f);
    parameterById(source, "preDelay").setValueNotifyingHost(0.2f);
    parameterById(source, "evolution").setValueNotifyingHost(0.8f);
    parameterById(source, "width").setValueNotifyingHost(0.7f);
    parameterById(source, "lowCut").setValueNotifyingHost(0.3f);
    parameterById(source, "highDamping").setValueNotifyingHost(0.6f);
    parameterById(source, "focus").setValueNotifyingHost(0.4f);
    parameterById(source, "monoSafe").setValueNotifyingHost(1.0f);

    juce::MemoryBlock data;
    source.getStateInformation(data);
    AmanitaOceanAudioProcessor restored;
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(algorithmParameter(restored).getIndex() == fathomIndex,
            "The sixth Character did not survive save/load");
    require(restored.getFathomVoiceSeed() != source.getFathomVoiceSeed(),
            "A restored instance has the voice seed of the instance that was saved");

    const auto savedParameters = dspParametersOf(source);
    const auto sourceRender = renderFathomProcessor(source, sampleRate, frameCount, frameCount);
    const auto restoredRender = renderFathomProcessor(restored, sampleRate, frameCount, frameCount);
    AmanitaOceanAudioProcessor untouched;
    const auto defaultRender = renderFathomProcessor(untouched, sampleRate, frameCount, frameCount);
    require(sameRender(sourceRender,
                       renderFathomDsp(savedParameters, source.getFathomVoiceSeed(), sampleRate,
                                       frameCount)),
            "The sixth Character does not render its settings from the voice seed of its instance");
    require(sameRender(restoredRender,
                       renderFathomDsp(savedParameters, restored.getFathomVoiceSeed(), sampleRate,
                                       frameCount)),
            "A restored state with the sixth Character does not render the saved settings from "
            "the voice seed of its own instance");
    const auto realisationDistance = renderDistance(sourceRender, restoredRender);
    std::cout << "[METRIC] Sixth Character saved and restored at Evolution 80 %: NRMS between the "
                 "two instances=" << realisationDistance << '\n';
    require(realisationDistance > leastDistanceOfTwoVoiceSeeds,
            "A restored instance repeats the voice phase of the instance that was saved");
    require(!sameRender(sourceRender, defaultRender),
            "The saved state with the sixth Character renders like the defaults");

    parameterById(source, "evolution").setValueNotifyingHost(0.0f);
    parameterById(restored, "evolution").setValueNotifyingHost(0.0f);
    const auto sourceAtRest = renderFathomProcessor(source, sampleRate, frameCount, frameCount);
    require(sameRender(sourceAtRest,
                       renderFathomProcessor(restored, sampleRate, frameCount, frameCount)),
            "A restored state with the sixth Character does not render what was saved at Evolution 0");
    require(!sameRender(sourceAtRest, sourceRender),
            "Evolution leaves the sixth Character as it is");
}

// Every instance draws a voice phase of its own for the sixth Character when
// it is created and keeps it for its lifetime; the state holds the thirteen
// parameters and nothing of it. Two instances with the same state therefore
// differ in the sixth Character above Evolution 0 and in nothing else, and one
// instance renders the same samples again after processing was stopped and
// after its own state was loaded back.
void testFathomVoicePhaseOfEachInstance()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 36000;

    AmanitaOceanAudioProcessor first;
    selectNeutralFathom(first);
    parameterById(first, "evolution").setValueNotifyingHost(1.0f);
    juce::MemoryBlock data;
    first.getStateInformation(data);
    AmanitaOceanAudioProcessor second;
    second.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    juce::MemoryBlock secondData;
    second.getStateInformation(secondData);
    require(first.getFathomVoiceSeed() != second.getFathomVoiceSeed(),
            "Two instances drew the same voice seed");
    require(data == secondData,
            "Two instances with the same settings and their own voice seeds save different states");

    const auto savedState = decodeState(data);
    require(savedState.getNumProperties() == 0
                && savedState.getNumChildren() == first.getParameters().size(),
            "The state holds more than the host parameters");
    for (const auto& child : savedState)
        require(child.getNumChildren() == 0 && child.getNumProperties() == 2
                    && child.hasProperty("value")
                    && findParameterById(first, child.getProperty("id").toString()) != nullptr,
                "The state holds something that is no host parameter");

    const auto firstRender = renderFathomProcessor(first, sampleRate, frameCount, frameCount);
    const auto secondRender = renderFathomProcessor(second, sampleRate, frameCount, frameCount);
    const auto instanceDistance = renderDistance(firstRender, secondRender);
    std::cout << "[METRIC] Sixth Character at Evolution 100 %, two instances with the same state: "
                 "NRMS between them=" << instanceDistance << '\n';
    require(instanceDistance > leastDistanceOfTwoVoiceSeeds,
            "Two instances with the same state render the same voice phase");

    first.releaseResources();
    require(sameRender(renderFathomProcessor(first, sampleRate, frameCount, frameCount),
                       firstRender),
            "An instance does not render the same voice phase again after processing was stopped");
    algorithmParameter(first).setValueNotifyingHost(algorithmCases.front().hostValue);
    parameterById(first, "evolution").setValueNotifyingHost(0.2f);
    require(!sameRender(renderFathomProcessor(first, sampleRate, frameCount, frameCount),
                        firstRender),
            "Another Character renders what the sixth does");
    first.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(sameRender(renderFathomProcessor(first, sampleRate, frameCount, frameCount),
                       firstRender),
            "An instance does not render the same voice phase again after its state was loaded back");

    for (const auto& algorithmCase : algorithmCases)
    {
        const auto evolution = algorithmCase.rawIndex == fathomIndex ? 0.0f : 1.0f;
        for (auto* processor : { &first, &second })
        {
            algorithmParameter(*processor).setValueNotifyingHost(algorithmCase.hostValue);
            parameterById(*processor, "evolution").setValueNotifyingHost(evolution);
        }
        const auto render = renderFathomProcessor(first, sampleRate, frameCount, frameCount);
        require(sameRender(render,
                           renderFathomProcessor(second, sampleRate, frameCount, frameCount)),
                std::string("Two instances with the same state differ in ") + algorithmCase.name
                    + " at Evolution " + std::to_string(evolution));
        require(*std::max_element(render.begin(), render.end()) > 0.01f,
                std::string("Two instances rendered silence in ") + algorithmCase.name);
    }
}

// The seeds that instances made one after another draw for themselves are all
// different, and the voice phases they give start where the campaign found
// the reference's: each start level inside its measured range, spread over
// it, the two in the same half of their ranges in 83 % of the instances.
void testFathomVoiceSeedsOfManyInstances()
{
    constexpr auto instances = 1000;
    constexpr std::array<double, 2> levelLow { 0.184, -0.007 };
    constexpr std::array<double, 2> levelHigh { 0.591, 0.433 };
    constexpr auto startSameHalf = 0.83;

    std::vector<std::uint64_t> seeds;
    std::array<double, 2> placeSum {};
    auto sameHalf = 0;
    auto rightHigh = 0;
    for (auto instance = 0; instance < instances; ++instance)
    {
        AmanitaOceanAudioProcessor processor;
        seeds.push_back(processor.getFathomVoiceSeed());

        amanita::dsp::FathomVoicePhase phase;
        phase.reset(seeds.back());
        std::array<double, 2> place {};
        for (std::size_t output = 0; output < place.size(); ++output)
        {
            place[output] = (phase.knot(output, 0).level - levelLow[output])
                          / (levelHigh[output] - levelLow[output]);
            require(place[output] >= 0.0 && place[output] <= 1.0,
                    "The voice phase of an instance starts outside the range the campaign measured");
            placeSum[output] += place[output];
        }
        sameHalf += (place[0] >= 0.5) == (place[1] >= 0.5) ? 1 : 0;
        rightHigh += place[1] >= 0.5 ? 1 : 0;
    }

    std::sort(seeds.begin(), seeds.end());
    require(std::adjacent_find(seeds.begin(), seeds.end()) == seeds.end(),
            "Two instances of one process drew the same voice seed");

    const auto sameShare = static_cast<double>(sameHalf) / instances;
    const auto rightHighShare = static_cast<double>(rightHigh) / instances;
    std::cout << "[METRIC] Voice seeds of " << instances << " instances: all different, mean place "
                 "of the start level left=" << placeSum[0] / instances << " right="
              << placeSum[1] / instances << " (uniform 0.5), right in its upper half="
              << rightHighShare << ", same half=" << sameShare << " (campaign " << startSameHalf
              << ")\n";
    // Six standard deviations of each figure over this many instances.
    require(std::abs(placeSum[0] / instances - 0.5) <= 0.055
                && std::abs(placeSum[1] / instances - 0.5) <= 0.055
                && std::abs(rightHighShare - 0.5) <= 0.095,
            "The voice phases of many instances do not start uniformly over their ranges");
    require(std::abs(sameShare - startSameHalf) <= 0.072,
            "The voice phases of many instances are not tied as the campaign found them");
}

// A state whose Character is no choice of this build: a number past the last
// choice opens as the last one, anything else as Default, and the processor
// renders either.
void testCharacterOutsideItsChoices()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto frameCount = 6000;

    struct StoredCharacter
    {
        const char* name;
        juce::var value;
        int expectedIndex;
    };
    const std::array storedCharacters {
        StoredCharacter { "the index after the last", lastCharacterIndex + 1, lastCharacterIndex },
        StoredCharacter { "a far larger number", 1.0e9, lastCharacterIndex },
        StoredCharacter { "a negative index", -3, 0 },
        StoredCharacter { "text", "abc", 0 },
        StoredCharacter { "no number", "nan", 0 }
    };

    for (const auto& stored : storedCharacters)
    {
        AmanitaOceanAudioProcessor source;
        juce::MemoryBlock data;
        source.getStateInformation(data);
        auto state = decodeState(data);
        auto algorithm = findParameterState(state, "algorithm");
        require(algorithm.isValid(), "Saved state has no Algorithm node");
        algorithm.setProperty("value", stored.value, nullptr);
        juce::MemoryBlock changed;
        if (const auto xml = state.createXml())
            juce::AudioProcessor::copyXmlToBinary(*xml, changed);
        require(!changed.isEmpty(), "Could not create a state with another Character");

        AmanitaOceanAudioProcessor restored;
        restored.setStateInformation(changed.getData(), static_cast<int>(changed.getSize()));
        require(algorithmParameter(restored).getIndex() == stored.expectedIndex,
                std::string("A state with ") + stored.name + " as its Character opens as choice "
                    + std::to_string(algorithmParameter(restored).getIndex()));

        auto peak = 0.0f;
        for (const auto sample : renderFathomProcessor(restored, sampleRate, frameCount, frameCount))
        {
            require(std::isfinite(sample),
                    std::string("A state with ") + stored.name + " as its Character renders NaN/Inf");
            peak = std::max(peak, std::abs(sample));
        }
        require(peak > 0.01f,
                std::string("A state with ") + stored.name + " as its Character renders silence");
    }
}

// The tail the host is told about covers the sixth Character where its margin
// is smallest: short Decay with the longest Pre-delay and the largest Size.
void testFathomTailLengthCoversItsDecay()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockFrames = 960;

    for (const auto decaySeconds : { 0.2f, 0.5f })
    {
        AmanitaOceanAudioProcessor processor;
        selectNeutralFathom(processor);
        auto& decay = parameterById(processor, "decay");
        decay.setValueNotifyingHost(decay.getValueForText(juce::String(decaySeconds)));
        parameterById(processor, "size").setValueNotifyingHost(1.0f);
        parameterById(processor, "preDelay").setValueNotifyingHost(1.0f);
        parameterById(processor, "evolution").setValueNotifyingHost(1.0f);
        const auto tailSeconds = processor.getTailLengthSeconds();
        require(std::abs(tailSeconds - (static_cast<double>(decaySeconds) + 0.5)) < 1.0e-3,
                "Reported tail length is not Decay plus half a second");

        // The input ends after one burst of 125 ms.
        const auto inputFrames = static_cast<int>(sampleRate * 0.125);
        const auto frameCount = inputFrames
                              + static_cast<int>(sampleRate * (tailSeconds + 0.5));
        const auto render = renderFathomProcessor(processor, sampleRate, frameCount, inputFrames);
        std::vector<double> blockEnergy;
        for (auto start = inputFrames; start + blockFrames <= frameCount; start += blockFrames)
        {
            auto energy = 0.0;
            for (auto frame = start; frame < start + blockFrames; ++frame)
            {
                const auto index = static_cast<std::size_t>(frame * 2);
                energy += static_cast<double>(render[index]) * render[index]
                        + static_cast<double>(render[index + 1]) * render[index + 1];
            }
            blockEnergy.push_back(energy);
        }

        const auto loudest = *std::max_element(blockEnergy.begin(), blockEnergy.end());
        require(loudest > 1.0e-8, "Tail length test rendered silence");
        auto lastAudibleBlock = 0;
        for (auto block = 0; block < static_cast<int>(blockEnergy.size()); ++block)
            if (blockEnergy[static_cast<std::size_t>(block)] > loudest * 1.0e-6)
                lastAudibleBlock = block;
        const auto sixtyDecibelSeconds = static_cast<double>((lastAudibleBlock + 1) * blockFrames)
                                       / sampleRate;
        std::cout << "[METRIC] Sixth Character at Decay " << decaySeconds
                  << " s: 60 dB down " << sixtyDecibelSeconds
                  << " s after the input, reported tail " << tailSeconds << " s\n";
        require(sixtyDecibelSeconds <= tailSeconds,
                "The sixth Character sounds longer than the tail the host is told about");
    }
}

// The state as the builds with six and with seven choices wrote it (0.22.0 and
// 0.23.0, 560 bytes): the thirteen parameters by their IDs, the Character as
// the index of its choice. It was saved at Evolution 62.5 % and Mix 73.1 %
// with the rest at its defaults. The states of those builds differ, from
// Character to Character and from one build to the other, in that index
// alone, which stands here as CHARACTER.
constexpr auto stateOfAnEarlierBuild =
    R"(<AmanitaOceanState><PARAM id="algorithm" value="CHARACTER"/>)"
    R"(<PARAM id="decay" value="5.000000476837158"/><PARAM id="evolution" value="62.5"/>)"
    R"(<PARAM id="focus" value="100.0"/><PARAM id="freeze" value="0.0"/>)"
    R"(<PARAM id="harmony" value="0.0"/><PARAM id="highDamping" value="9000.0009765625"/>)"
    R"(<PARAM id="lowCut" value="80.0"/><PARAM id="mix" value="73.09999847412109"/>)"
    R"(<PARAM id="monoSafe" value="0.0"/><PARAM id="preDelay" value="20.0"/>)"
    R"(<PARAM id="size" value="100.0"/><PARAM id="width" value="100.0"/></AmanitaOceanState>)";

// That state with a given index as its Character, as a host hands it over.
[[nodiscard]] juce::MemoryBlock stateOfAnEarlierBuildWith(int characterIndex)
{
    const auto xml = juce::parseXML(juce::String(stateOfAnEarlierBuild)
                                        .replace("CHARACTER",
                                                 juce::String(characterIndex) + ".0"));
    require(xml != nullptr, "The state of an earlier build is no XML");
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*xml, data);
    require(data.getSize() == 560,
            "The state of an earlier build is not the 560 bytes those builds wrote");
    return data;
}

// Two states hold the same: the same parameters by their IDs and nothing
// else, the Character as the same index, every other value the same to one
// part in 100000. The bytes are not compared. A value that was never touched
// is written as its default has come back through the parameter's own range,
// and the curves of Decay and High Damping go through the exp, log and pow of
// the platform's maths library: the state above holds a Decay of
// 5.000000476837158 and a High Damping of 9000.0009765625 where another
// library gives 5.0 and 9000.0, and the text of the state changes with them.
void requireSameStateContent(const juce::MemoryBlock& first,
                             const juce::MemoryBlock& second,
                             const std::string& what)
{
    const auto firstState = decodeState(first);
    const auto secondState = decodeState(second);
    require(firstState.getType() == secondState.getType()
                && firstState.getNumProperties() == 0 && secondState.getNumProperties() == 0
                && firstState.getNumChildren() == secondState.getNumChildren(),
            what + ": the two states do not hold the same parameters");
    for (const auto& child : firstState)
    {
        const auto id = child.getProperty("id").toString();
        const auto other = findParameterState(secondState, id);
        require(other.isValid() && child.getNumProperties() == 2 && other.getNumProperties() == 2
                    && child.hasProperty("value") && other.hasProperty("value"),
                what + ": one state lacks the value of " + id.toStdString());
        const auto value = static_cast<double>(child.getProperty("value"));
        const auto otherValue = static_cast<double>(other.getProperty("value"));
        const auto allowed = id == "algorithm" ? 0.0 : 1.0e-5 * std::max(1.0, std::abs(value));
        require(std::isfinite(value) && std::isfinite(otherValue)
                    && std::abs(value - otherValue) <= allowed,
                what + ": " + id.toStdString() + " is " + std::to_string(value) + " in one state and "
                    + std::to_string(otherValue) + " in the other");
    }
}

// A project saved by the build with six choices or by the one with seven
// opens with the Character and the settings it was saved with, and this build
// saves it again with the same content: a further choice changes nothing of
// what a state holds. The eighth Character is saved the same way, as the
// index after Undertow's, and opens again as itself; and a state this build
// saves with all its values moved comes back from a load with every one.
void testStatesOfEarlierBuildsAndTheEighthChoice()
{
    struct EarlierBuild
    {
        const char* name;
        int lastIndex;
    };
    for (const auto& build : { EarlierBuild { "six-choice", fathomIndex },
                               EarlierBuild { "seven-choice", undertowIndex } })
    {
        for (auto index = 0; index <= build.lastIndex; ++index)
        {
            const auto& algorithmCase = algorithmCases[static_cast<std::size_t>(index)];
            const auto saved = stateOfAnEarlierBuildWith(index);

            AmanitaOceanAudioProcessor restored;
            restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            const auto& restoredAlgorithm = algorithmParameter(restored);
            require(restoredAlgorithm.getIndex() == algorithmCase.rawIndex
                        && restoredAlgorithm.getCurrentChoiceName() == algorithmCase.name,
                    std::string("A state of the ") + build.name
                        + " build does not open with its Character " + algorithmCase.name
                        + ": it opens as "
                        + restoredAlgorithm.getCurrentChoiceName().toStdString());
            require(std::abs(parameterById(restored, "evolution").getValue() - 0.625f) < 0.001f
                        && std::abs(parameterById(restored, "mix").getValue() - 0.731f)
                               < 0.001f,
                    std::string("A state of the ") + build.name
                        + " build does not open with its settings for " + algorithmCase.name);

            juce::MemoryBlock savedAgain;
            restored.getStateInformation(savedAgain);
            requireSameStateContent(saved, savedAgain,
                                    std::string("A state of the ") + build.name
                                        + " build saved again for " + algorithmCase.name);
        }
    }

    const auto& spume = algorithmCases[spumeIndex];
    AmanitaOceanAudioProcessor source;
    algorithmParameter(source).setValueNotifyingHost(spume.hostValue);
    parameterById(source, "evolution").setValueNotifyingHost(0.625f);
    parameterById(source, "mix").setValueNotifyingHost(0.731f);
    juce::MemoryBlock data;
    source.getStateInformation(data);
    require(spume.rawIndex == undertowIndex + 1,
            "The eighth Character does not follow Undertow");
    requireSameStateContent(stateOfAnEarlierBuildWith(spume.rawIndex), data,
                            "The eighth Character saved as the index after Undertow's");

    AmanitaOceanAudioProcessor restored;
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(algorithmParameter(restored).getIndex() == spume.rawIndex
                && algorithmParameter(restored).getCurrentChoiceName() == spume.name
                && std::abs(parameterById(restored, "evolution").getValue() - 0.625f) < 0.001f
                && std::abs(parameterById(restored, "mix").getValue() - 0.731f) < 0.001f,
            "The eighth Character did not survive save/load");

    // Every parameter moved off its default, saved, loaded and saved again.
    AmanitaOceanAudioProcessor moved;
    auto position = 0.07f;
    for (auto* parameter : moved.getParameters())
    {
        parameter->setValueNotifyingHost(position);
        position += 0.071f;
    }
    juce::MemoryBlock movedData;
    moved.getStateInformation(movedData);
    AmanitaOceanAudioProcessor reloaded;
    reloaded.setStateInformation(movedData.getData(), static_cast<int>(movedData.getSize()));
    juce::MemoryBlock reloadedData;
    reloaded.getStateInformation(reloadedData);
    requireSameStateContent(movedData, reloadedData, "A state with every parameter moved");
    // As the host reads them: a switch keeps the host value it was given and
    // saves on or off, so the parameters are compared by what they show.
    for (auto index = 0; index < moved.getParameters().size(); ++index)
        require(moved.getParameters()[index]->getCurrentValueAsText()
                    == reloaded.getParameters()[index]->getCurrentValueAsText(),
                "A parameter did not come back from a save and a load: index "
                    + std::to_string(index));

    // A host that automates the Character of a VST3 stores the normalised
    // value. Where the positions of the builds with seven, six and five
    // choices land among eight is said here and pinned nowhere.
    std::cout << "[METRIC] Normalised Character values of eight choices:";
    for (const auto& algorithmCase : algorithmCases)
        std::cout << ' ' << algorithmCase.name << ' '
                  << algorithmParameter(source).convertTo0to1(
                         static_cast<float>(algorithmCase.rawIndex));
    for (const auto steps : { 6, 5, 4 })
    {
        std::cout << "; a lane of " << steps + 1 << " choices plays";
        for (auto step = 0; step <= steps; ++step)
        {
            const auto written = static_cast<float>(step) / static_cast<float>(steps);
            algorithmParameter(source).setValueNotifyingHost(written);
            std::cout << ' ' << written << " as "
                      << algorithmParameter(source).getCurrentChoiceName();
        }
    }
    std::cout << '\n';
}

// A play head as a host hands one to the processor: it answers with the
// position the test has set and counts how often it is asked.
struct TestPlayHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        ++positionsAsked;
        return position;
    }

    juce::Optional<PositionInfo> position;
    mutable int positionsAsked = 0;
};

// The seventh Character keeps time by the host. For every block the processor
// asks the play head once and hands the DSP what it says of the block's first
// frame: the position in quarter notes, the tempo and whether the transport
// runs. Without a play head, without a position or without quarter notes the
// DSP is told that the transport stands; without a tempo, or with one that is
// no positive number, that there is none. Given the same transport directly,
// the DSP renders what the processor does.
void testHostTransportReachesDsp()
{
    // Two seconds, in which every voice has taken a chunk more than once at
    // this tempo and at the one the DSP takes where it is told of none.
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;
    constexpr auto blockCount = 375;
    constexpr auto startQuarterNotes = 16.25;
    constexpr auto tempo = 168.0;
    const auto notANumber = std::numeric_limits<double>::quiet_NaN();

    // What the host says, and what of it the DSP is to be told.
    struct TransportCase
    {
        const char* name;
        bool hasPlayHead;
        bool hasPosition;
        bool hasQuarterNotes;
        double quarterNotesOffset;
        bool hasTempo;
        double bpm;
        bool playing;
        bool expectedPlaying;
        bool expectedHasTempo;
    };
    const std::array transportCases {
        TransportCase { "no play head", false, false, false, 0.0, false, 0.0, false, false, false },
        TransportCase { "a play head without a position",
                        true, false, false, 0.0, false, 0.0, false, false, false },
        TransportCase { "a running transport with its tempo",
                        true, true, true, 0.0, true, tempo, true, true, true },
        TransportCase { "a stopped transport with its tempo",
                        true, true, true, 0.0, true, tempo, false, false, true },
        TransportCase { "a running transport without quarter notes",
                        true, true, false, 0.0, true, tempo, true, false, true },
        TransportCase { "a running transport whose position is no number",
                        true, true, true, notANumber, true, tempo, true, false, true },
        TransportCase { "a running transport without a tempo",
                        true, true, true, 0.0, false, 0.0, true, true, false },
        TransportCase { "a running transport with a tempo of zero",
                        true, true, true, 0.0, true, 0.0, true, true, false },
        TransportCase { "a running transport whose tempo is no number",
                        true, true, true, 0.0, true, notANumber, true, true, false }
    };

    for (const auto& transportCase : transportCases)
    {
        AmanitaOceanAudioProcessor processor;
        algorithmParameter(processor).setValueNotifyingHost(
            algorithmCases[undertowIndex].hostValue);
        parameterById(processor, "mix").setValueNotifyingHost(1.0f);
        parameterById(processor, "evolution").setValueNotifyingHost(1.0f);
        parameterById(processor, "focus").setValueNotifyingHost(0.0f);
        TestPlayHead playHead;
        if (transportCase.hasPlayHead)
            processor.setPlayHead(&playHead);
        processor.prepareToPlay(sampleRate, blockSize);

        amanita::dsp::FDNReverb reverb;
        reverb.setParameters(dspParametersOf(processor));
        reverb.setFathomVoiceSeed(processor.getFathomVoiceSeed());
        reverb.prepare(sampleRate, blockSize);

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::AudioBuffer<float> direct(2, blockSize);
        juce::MidiBuffer midi;
        auto maximumDifference = 0.0f;
        auto peak = 0.0f;
        for (auto block = 0; block < blockCount; ++block)
        {
            // The transport runs on by the frames of the blocks before this one.
            const auto quarterNotes = startQuarterNotes + transportCase.quarterNotesOffset
                                    + (transportCase.playing
                                           ? static_cast<double>(block * blockSize) / sampleRate
                                                 * tempo / 60.0
                                           : 0.0);
            if (transportCase.hasPosition)
            {
                juce::AudioPlayHead::PositionInfo position;
                position.setIsPlaying(transportCase.playing);
                if (transportCase.hasQuarterNotes)
                    position.setPpqPosition(quarterNotes);
                if (transportCase.hasTempo)
                    position.setBpm(transportCase.bpm);
                playHead.position = position;
            }

            amanita::dsp::HostTransport expected;
            if (transportCase.hasPosition && transportCase.hasQuarterNotes
                && std::isfinite(quarterNotes))
                expected.quarterNotes = quarterNotes;
            if (transportCase.expectedHasTempo)
                expected.bpm = transportCase.bpm;
            expected.playing = transportCase.expectedPlaying;
            expected.hasTempo = transportCase.expectedHasTempo;

            for (auto frame = 0; frame < blockSize; ++frame)
                for (auto channel = 0; channel < 2; ++channel)
                {
                    const auto sample = fathomTestInput(block * blockSize + frame, sampleRate,
                                                        channel);
                    buffer.setSample(channel, frame, sample);
                    direct.setSample(channel, frame, sample);
                }
            processor.processBlock(buffer, midi);
            {
                juce::ScopedNoDenormals noDenormals;
                reverb.setHostTransport(expected);
                reverb.process(direct.getWritePointer(0), direct.getWritePointer(1), blockSize);
            }

            for (auto frame = 0; frame < blockSize; ++frame)
                for (auto channel = 0; channel < 2; ++channel)
                {
                    const auto sample = buffer.getSample(channel, frame);
                    require(std::isfinite(sample),
                            std::string("The seventh Character renders NaN/Inf under ")
                                + transportCase.name);
                    peak = std::max(peak, std::abs(sample));
                    maximumDifference = std::max(
                        maximumDifference, std::abs(sample - direct.getSample(channel, frame)));
                }
        }
        processor.setPlayHead(nullptr);

        require(playHead.positionsAsked == (transportCase.hasPlayHead ? blockCount : 0),
                std::string("The processor does not ask the play head once for every block "
                            "under ")
                    + transportCase.name + ": " + std::to_string(playHead.positionsAsked)
                    + " times for " + std::to_string(blockCount) + " blocks");
        require(peak > 0.01f,
                std::string("The seventh Character renders silence under ") + transportCase.name);
        require(maximumDifference <= 2.0e-7f,
                std::string("The DSP is not told of the host's transport under ")
                    + transportCase.name + ": maximum difference="
                    + std::to_string(maximumDifference * 1.0e9f) + "e-9");
    }
}

// The tail the host is told about. For six Characters it is Decay plus half a
// second whatever the host's tempo, and for the eighth four seconds more, at
// every tempo as well. The seventh goes on replaying the past after its input
// has stopped: its tail is longer by two of its longest chunks and four passes
// of the recirculation an octave up, 13.33 quarter notes at the tempo of the
// block processed last. That tempo is the host's, held between 20 and 999
// BPM, and 120 BPM before the first block and where the host gives no tempo
// or none that is a positive number. Where the margin of the reverb itself is
// smallest, the seventh Character has fallen 60 dB within that tail, under a
// running transport and without a play head.
void testUndertowTailLengthFollowsTheTempo()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockSize = 256;
    constexpr auto replayedQuarterNotes = 2.0 * 8.0 / 3.0 + 4.0 * 2.0;
    constexpr auto tempoWithoutHost = 120.0;
    constexpr auto undertow = undertowIndex;
    constexpr auto spume = spumeIndex;
    constexpr auto heldByTheDiffuserSeconds = 4.0;
    const auto notANumber = std::numeric_limits<double>::quiet_NaN();

    // The tail of every Character at the tempo the processor is to hold.
    const auto requireTails = [&](AmanitaOceanAudioProcessor& processor, double tailTempo,
                                  const std::string& moment)
    {
        const auto plainTail = static_cast<double>(processor.getParameterState()
                                                       .getRawParameterValue("decay")->load())
                             + 0.5;
        for (const auto& algorithmCase : algorithmCases)
        {
            algorithmParameter(processor).setValueNotifyingHost(algorithmCase.hostValue);
            const auto expected = algorithmCase.rawIndex == undertow
                ? plainTail + replayedQuarterNotes * 60.0 / tailTempo
                : algorithmCase.rawIndex == spume ? plainTail + heldByTheDiffuserSeconds
                                                  : plainTail;
            require(std::abs(processor.getTailLengthSeconds() - expected) < 1.0e-6,
                    std::string("Reported tail of ") + algorithmCase.name + " is "
                        + std::to_string(processor.getTailLengthSeconds()) + " s and not "
                        + std::to_string(expected) + " s " + moment);
        }
    };

    struct TempoCase
    {
        const char* name;
        bool hasPlayHead;
        bool hasTempo;
        double bpm;
        double tailTempo;
    };
    const std::array tempoCases {
        TempoCase { "no play head", false, false, 0.0, tempoWithoutHost },
        TempoCase { "a host without a tempo", true, false, 0.0, tempoWithoutHost },
        TempoCase { "60 BPM", true, true, 60.0, 60.0 },
        TempoCase { "174.5 BPM", true, true, 174.5, 174.5 },
        TempoCase { "5 BPM", true, true, 5.0, 20.0 },
        TempoCase { "2000 BPM", true, true, 2000.0, 999.0 },
        TempoCase { "a tempo of zero", true, true, 0.0, tempoWithoutHost },
        TempoCase { "a tempo that is no number", true, true, notANumber, tempoWithoutHost }
    };

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (const auto& tempoCase : tempoCases)
    {
        AmanitaOceanAudioProcessor processor;
        TestPlayHead playHead;
        juce::AudioPlayHead::PositionInfo position;
        position.setIsPlaying(true);
        position.setPpqPosition(8.0);
        if (tempoCase.hasTempo)
            position.setBpm(tempoCase.bpm);
        playHead.position = position;
        if (tempoCase.hasPlayHead)
            processor.setPlayHead(&playHead);
        processor.prepareToPlay(sampleRate, blockSize);
        requireTails(processor, tempoWithoutHost,
                     std::string("before the first block under ") + tempoCase.name);

        buffer.clear();
        processor.processBlock(buffer, midi);
        requireTails(processor, tempoCase.tailTempo,
                     std::string("after a block under ") + tempoCase.name);

        // The tail follows Decay as before, and a host that stops giving its
        // tempo is one without a tempo.
        parameterById(processor, "decay").setValueNotifyingHost(0.2f);
        requireTails(processor, tempoCase.tailTempo,
                     std::string("at another Decay under ") + tempoCase.name);
        position.setBpm(juce::nullopt);
        playHead.position = position;
        buffer.clear();
        processor.processBlock(buffer, midi);
        requireTails(processor, tempoWithoutHost,
                     std::string("after a block without a tempo that follows ") + tempoCase.name);
        processor.setPlayHead(nullptr);
    }

    // Short Decay with the longest Pre-delay and the largest Size, every voice
    // in. The input ends after one burst of 125 ms.
    struct SoundCase
    {
        const char* name;
        bool running;
        double bpm;
        float decaySeconds;
    };
    const std::array soundCases {
        SoundCase { "a transport running at 140 BPM", true, 140.0, 0.2f },
        SoundCase { "no play head", false, tempoWithoutHost, 0.5f }
    };
    constexpr auto energyFrames = 960;
    for (const auto& soundCase : soundCases)
    {
        AmanitaOceanAudioProcessor processor;
        algorithmParameter(processor).setValueNotifyingHost(
            algorithmCases[undertowIndex].hostValue);
        auto& decay = parameterById(processor, "decay");
        decay.setValueNotifyingHost(decay.getValueForText(juce::String(soundCase.decaySeconds)));
        parameterById(processor, "mix").setValueNotifyingHost(1.0f);
        parameterById(processor, "size").setValueNotifyingHost(1.0f);
        parameterById(processor, "preDelay").setValueNotifyingHost(1.0f);
        parameterById(processor, "evolution").setValueNotifyingHost(1.0f);
        parameterById(processor, "focus").setValueNotifyingHost(0.0f);
        TestPlayHead playHead;
        if (soundCase.running)
            processor.setPlayHead(&playHead);
        processor.prepareToPlay(sampleRate, blockSize);

        const auto expectedTail = static_cast<double>(soundCase.decaySeconds) + 0.5
                                + replayedQuarterNotes * 60.0 / soundCase.bpm;
        const auto inputFrames = static_cast<int>(sampleRate * 0.125);
        const auto frameCount = inputFrames
                              + static_cast<int>(sampleRate * (expectedTail + 0.5));
        std::vector<double> blockEnergy(
            static_cast<std::size_t>((frameCount - inputFrames) / energyFrames), 0.0);
        for (auto offset = 0; offset < frameCount; offset += blockSize)
        {
            juce::AudioPlayHead::PositionInfo position;
            position.setIsPlaying(true);
            position.setBpm(soundCase.bpm);
            position.setPpqPosition(static_cast<double>(offset) / sampleRate
                                    * soundCase.bpm / 60.0);
            playHead.position = position;

            const auto framesThisBlock = std::min(blockSize, frameCount - offset);
            buffer.setSize(2, framesThisBlock, false, false, true);
            for (auto frame = 0; frame < framesThisBlock; ++frame)
                for (auto channel = 0; channel < 2; ++channel)
                    buffer.setSample(channel, frame,
                                     offset + frame < inputFrames
                                         ? fathomTestInput(offset + frame, sampleRate, channel)
                                         : 0.0f);
            processor.processBlock(buffer, midi);
            for (auto frame = 0; frame < framesThisBlock; ++frame)
            {
                const auto block = (offset + frame - inputFrames) / energyFrames;
                if (offset + frame < inputFrames
                    || block >= static_cast<int>(blockEnergy.size()))
                    continue;
                const auto left = static_cast<double>(buffer.getSample(0, frame));
                const auto right = static_cast<double>(buffer.getSample(1, frame));
                require(std::isfinite(left) && std::isfinite(right),
                        std::string("The seventh Character renders NaN/Inf in its tail under ")
                            + soundCase.name);
                blockEnergy[static_cast<std::size_t>(block)] += left * left + right * right;
            }
        }
        const auto tailSeconds = processor.getTailLengthSeconds();
        processor.setPlayHead(nullptr);
        buffer.setSize(2, blockSize, false, false, true);
        require(std::abs(tailSeconds - expectedTail) < 1.0e-3,
                std::string("Reported tail of the seventh Character is not Decay plus half a "
                            "second plus what it replays under ")
                    + soundCase.name);

        const auto loudest = *std::max_element(blockEnergy.begin(), blockEnergy.end());
        require(loudest > 1.0e-8,
                std::string("Tail length test of the seventh Character rendered silence under ")
                    + soundCase.name);
        auto lastAudibleBlock = 0;
        for (auto block = 0; block < static_cast<int>(blockEnergy.size()); ++block)
            if (blockEnergy[static_cast<std::size_t>(block)] > loudest * 1.0e-6)
                lastAudibleBlock = block;
        const auto sixtyDecibelSeconds = static_cast<double>((lastAudibleBlock + 1) * energyFrames)
                                       / sampleRate;
        std::cout << "[METRIC] Seventh Character at Decay " << soundCase.decaySeconds
                  << " s under " << soundCase.name << ": 60 dB down " << sixtyDecibelSeconds
                  << " s after the input, reported tail " << tailSeconds << " s\n";
        require(sixtyDecibelSeconds <= tailSeconds,
                std::string("The seventh Character sounds longer than the tail the host is told "
                            "about under ")
                    + soundCase.name);
    }
}

// The tail the host is told about covers the eighth Character where the margin
// of the reverb itself is smallest: short Decay with the longest Pre-delay and
// the largest Size, the diffuser all in. It is Decay plus half a second plus
// the four seconds the diffuser in front of the network holds.
void testSpumeTailLengthCoversItsDiffuser()
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto blockFrames = 960;

    for (const auto decaySeconds : { 0.2f, 0.5f })
    {
        AmanitaOceanAudioProcessor processor;
        algorithmParameter(processor).setValueNotifyingHost(algorithmCases[spumeIndex].hostValue);
        auto& decay = parameterById(processor, "decay");
        decay.setValueNotifyingHost(decay.getValueForText(juce::String(decaySeconds)));
        parameterById(processor, "mix").setValueNotifyingHost(1.0f);
        parameterById(processor, "size").setValueNotifyingHost(1.0f);
        parameterById(processor, "preDelay").setValueNotifyingHost(1.0f);
        parameterById(processor, "evolution").setValueNotifyingHost(1.0f);
        parameterById(processor, "focus").setValueNotifyingHost(0.0f);
        const auto tailSeconds = processor.getTailLengthSeconds();
        require(std::abs(tailSeconds - (static_cast<double>(decaySeconds) + 4.5)) < 1.0e-3,
                "Reported tail of the eighth Character is not Decay plus four and a half "
                "seconds");

        // The input ends after one burst of 125 ms.
        const auto inputFrames = static_cast<int>(sampleRate * 0.125);
        const auto frameCount = inputFrames
                              + static_cast<int>(sampleRate * (tailSeconds + 0.5));
        const auto render = renderFathomProcessor(processor, sampleRate, frameCount, inputFrames);
        std::vector<double> blockEnergy;
        for (auto start = inputFrames; start + blockFrames <= frameCount; start += blockFrames)
        {
            auto energy = 0.0;
            for (auto frame = start; frame < start + blockFrames; ++frame)
            {
                const auto index = static_cast<std::size_t>(frame * 2);
                require(std::isfinite(render[index]) && std::isfinite(render[index + 1]),
                        "The eighth Character renders NaN/Inf in its tail");
                energy += static_cast<double>(render[index]) * render[index]
                        + static_cast<double>(render[index + 1]) * render[index + 1];
            }
            blockEnergy.push_back(energy);
        }

        const auto loudest = *std::max_element(blockEnergy.begin(), blockEnergy.end());
        require(loudest > 1.0e-8, "Tail length test of the eighth Character rendered silence");
        auto lastAudibleBlock = 0;
        for (auto block = 0; block < static_cast<int>(blockEnergy.size()); ++block)
            if (blockEnergy[static_cast<std::size_t>(block)] > loudest * 1.0e-6)
                lastAudibleBlock = block;
        const auto sixtyDecibelSeconds = static_cast<double>((lastAudibleBlock + 1) * blockFrames)
                                       / sampleRate;
        std::cout << "[METRIC] Eighth Character at Decay " << decaySeconds
                  << " s: 60 dB down " << sixtyDecibelSeconds
                  << " s after the input, reported tail " << tailSeconds << " s\n";
        require(sixtyDecibelSeconds <= tailSeconds,
                "The eighth Character sounds longer than the tail the host is told about");
    }
}

// The open list of the drop-down holds every Character and lies inside the
// editor's window at its smallest size, at its default size and at its
// largest: it begins a gap under its field and its last item ends above the
// window's lower edge, so that no item has to be scrolled to.
void testCharacterListLiesInsideTheEditor()
{
    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    auto* selector = dynamic_cast<juce::ComboBox*>(
        findDescendantById(*editor, "character-selector"));
    require(selector != nullptr, "Character drop-down was not found");
    auto* fieldLabel = dynamic_cast<juce::Label*>(selector->getChildComponent(0));
    require(fieldLabel != nullptr, "Character drop-down has no text label");
    require(selector->getNumItems() == static_cast<int>(algorithmCases.size()),
            "Character drop-down does not list every Character");

    for (const auto width : { AmanitaOceanAudioProcessorEditor::minimumWidth,
                              AmanitaOceanAudioProcessorEditor::defaultWidth,
                              AmanitaOceanAudioProcessorEditor::maximumWidth })
    {
        editor->setSize(width, width * AmanitaOceanAudioProcessorEditor::defaultHeight
                                   / AmanitaOceanAudioProcessorEditor::defaultWidth);
        auto& lookAndFeel = selector->getLookAndFeel();
        const auto options = lookAndFeel.getOptionsForComboBoxPopupMenu(*selector, *fieldLabel);
        const auto field = boundsInEditor(*editor, "character-selector");
        const auto listTop = field.getBottom() + options.getTargetScreenArea().getBottom()
                           - selector->getScreenBounds().getBottom();
        const auto listHeight = selector->getNumItems() * options.getStandardItemHeight()
                              + 2 * lookAndFeel.getPopupMenuBorderSizeWithOptions(options);
        std::cout << "[METRIC] Character list at editor width " << width << ": "
                  << selector->getNumItems() << " items of " << options.getStandardItemHeight()
                  << " px from " << listTop << " to " << listTop + listHeight << " px of "
                  << editor->getHeight() << '\n';
        require(listTop > field.getBottom() && listTop + listHeight <= editor->getHeight(),
                "The open Character list does not lie inside the editor's window at width "
                    + std::to_string(width));
    }
}

// The painted part of a picture: the box round every pixel covered by a share
// of 255 or more, in picture pixels, and the most covered pixel as it is
// painted.
struct PaintedPart
{
    juce::Rectangle<int> box;
    juce::Colour densest;
};

[[nodiscard]] PaintedPart paintedPart(const juce::Image& image, int leastCover = 128)
{
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    PaintedPart part;
    for (auto y = 0; y < image.getHeight(); ++y)
        for (auto x = 0; x < image.getWidth(); ++x)
        {
            const auto pixel = pixels.getPixelColour(x, y);
            if (pixel.getAlpha() > part.densest.getAlpha())
                part.densest = pixel;
            if (pixel.getAlpha() >= leastCover)
                part.box = part.box.isEmpty() ? juce::Rectangle<int>(x, y, 1, 1)
                                              : part.box.getUnion({ x, y, 1, 1 });
        }
    return part;
}

// The two chevrons beside the drop-down. They stand on either side of it,
// mirrored about the window's axis and as high as it, a clear gap from its
// border, and take the pointer in an area of at least 28 x 28 points at every
// size, which no other control shares. Each is the drop-down's own chevron on
// its side: 4.5 design pixels deep and 9 high in a stroke of 1.35, in the tone
// of that chevron at rest, over a dark line 2 design pixels wider that keeps
// its outline where the field behind it runs light. Under the pointer it takes
// the accent of the Character, and the pointer alone changes nothing. A press released on the
// right one steps to the next Character and one on the left one to the one
// before, round either end, each one complete host gesture; a press that is
// dragged off, and one of another button, is given up. They take no keyboard
// focus, and assistive technology finds two buttons whose titles say which
// way they step and name no Character.
void testStepChevronsBesideTheDropDown()
{
    using Step = amanita::ui::CharacterStepButton;
    using LookAndFeel = amanita::ui::OceanLookAndFeel;
    constexpr auto finePixelsPerPoint = 8.0f;
    constexpr auto depth = 4.5;
    constexpr auto halfHeight = 4.5;
    constexpr auto stroke = 1.35;
    constexpr auto keyline = 2.0;
    constexpr auto gapToTheDropDown = 4.0;
    // The line under a chevron is Ocean's darkest tone at 70 %, the chevron at
    // rest the secondary text tone at 88 % over it. Half way from the cover of
    // the one to that of the two lies the edge of the stroke, half way to no
    // cover the edge of the line.
    const auto keylineTone = LookAndFeel::backgroundBottom().withAlpha(0.70f);
    const auto restTone = keylineTone.overlaidWith(LookAndFeel::secondaryText().withAlpha(0.88f));
    const auto strokeCover = (keylineTone.getAlpha() + restTone.getAlpha()) / 2;
    const auto keylineCover = keylineTone.getAlpha() / 2;

    AmanitaOceanAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "Processor did not create an editor");
    auto* selector = dynamic_cast<juce::ComboBox*>(
        findDescendantById(*editor, "character-selector"));
    auto* previous = dynamic_cast<Step*>(findDescendantById(*editor, "character-previous"));
    auto* next = dynamic_cast<Step*>(findDescendantById(*editor, "character-next"));
    require(selector != nullptr && previous != nullptr && next != nullptr,
            "Character drop-down or one of its step chevrons was not found");
    for (auto* step : { previous, next })
        require(step->isVisible() && step->isAccessible()
                    && ! step->getWantsKeyboardFocus()
                    && ! step->getMouseClickGrabsKeyboardFocus()
                    && step->getExplicitFocusOrder() == 0,
                "A step chevron is hidden, not accessible or takes the keyboard focus");

    constexpr std::array<const char*, 14> otherPlacedIds {
        "character-selector", "character-description", "knob-evolution",
        "knob-preDelay", "knob-size", "knob-decay", "knob-lowCut", "knob-highDamping",
        "knob-harmony", "knob-width", "knob-focus", "knob-mix", "mono-safe", "freeze"
    };
    for (const auto width : { AmanitaOceanAudioProcessorEditor::minimumWidth,
                              AmanitaOceanAudioProcessorEditor::defaultWidth,
                              AmanitaOceanAudioProcessorEditor::maximumWidth })
    {
        editor->setSize(width, width * AmanitaOceanAudioProcessorEditor::defaultHeight
                                   / AmanitaOceanAudioProcessorEditor::defaultWidth);
        const auto sizeName = std::to_string(width) + " wide";
        const auto canvasScale = static_cast<double>(width)
                               / AmanitaOceanAudioProcessorEditor::defaultWidth;
        const auto axis = 0.5 * width;
        const auto field = boundsInEditor(*editor, "character-selector");
        const auto left = boundsInEditor(*editor, "character-previous");
        const auto right = boundsInEditor(*editor, "character-next");
        std::cout << "[METRIC] Step chevrons at editor " << sizeName << ": areas "
                  << left.toString() << " and " << right.toString() << " beside the drop-down "
                  << field.toString() << '\n';
        require(left.getY() == field.getY() && left.getHeight() == field.getHeight()
                    && right.getY() == field.getY() && right.getHeight() == field.getHeight(),
                "Step chevrons are not as high as the drop-down and level with it at "
                    + sizeName);
        require(std::abs(left.getWidth() - Step::designWidth * canvasScale) <= 0.5
                    && right.getWidth() == left.getWidth()
                    && std::min(left.getWidth(), left.getHeight()) >= 28,
                "Step chevrons do not take the pointer in 36 x 40 design pixels, 28 x 28 "
                "points or more, at " + sizeName);
        require(std::abs((field.getX() - left.getRight()) - gapToTheDropDown * canvasScale) <= 1.0
                    && std::abs((right.getX() - field.getRight()) - gapToTheDropDown * canvasScale)
                           <= 1.0
                    && left.getRight() < field.getX() && right.getX() > field.getRight()
                    && std::abs((axis - left.getCentreX()) - (right.getCentreX() - axis)) <= 1.0,
                "Step chevrons are not mirrored about the axis, a gap from the drop-down, at "
                    + sizeName);
        for (const auto* id : otherPlacedIds)
            require(! left.intersects(boundsInEditor(*editor, id))
                        && ! right.intersects(boundsInEditor(*editor, id)),
                    std::string("A step chevron shares its area with ") + id + " at " + sizeName);

        // The chevrons as painted: one the mirror image of the other, in the
        // middle of their areas.
        const auto glyphScale = static_cast<double>(left.getHeight()) / Step::designHeight;
        const auto leftPicture = paintAlone(*previous, finePixelsPerPoint);
        const auto rightPicture = paintAlone(*next, finePixelsPerPoint);
        const auto leftPart = paintedPart(leftPicture, strokeCover);
        const auto rightPart = paintedPart(rightPicture, strokeCover);
        const auto leftLine = paintedPart(leftPicture, keylineCover);
        const auto inkWidth = leftPart.box.getWidth() / static_cast<double>(finePixelsPerPoint);
        const auto inkHeight = leftPart.box.getHeight() / static_cast<double>(finePixelsPerPoint);
        std::cout << "[METRIC] Step chevron at editor " << sizeName << ": ink " << inkWidth
                  << " x " << inkHeight << " px ("
                  << inkWidth / glyphScale << " x " << inkHeight / glyphScale
                  << " design px) in an area of " << left.getWidth() << " x "
                  << left.getHeight() << " px, " << field.getX() - left.getRight()
                  << " px from the drop-down; from its ink to the drop-down's border "
                  << field.getX() - left.getX()
                         - leftPart.box.getRight() / static_cast<double>(finePixelsPerPoint)
                  << " px\n";
        require(std::abs(inkWidth - (depth + stroke) * glyphScale) <= 0.3
                    && std::abs(inkHeight - (2.0 * halfHeight + stroke) * glyphScale) <= 0.3,
                "A step chevron is not 4.5 x 9 design pixels in a stroke of 1.35 at "
                    + sizeName);
        require(std::abs(leftLine.box.getWidth() / static_cast<double>(finePixelsPerPoint)
                         - (depth + stroke + keyline) * glyphScale) <= 0.3
                    && std::abs(leftLine.box.getHeight() / static_cast<double>(finePixelsPerPoint)
                                - (2.0 * halfHeight + stroke + keyline) * glyphScale) <= 0.3
                    && leftLine.box.contains(leftPart.box),
                "A step chevron does not lie over a dark line 2 design pixels wider at "
                    + sizeName);
        require(std::abs(leftPart.box.toFloat().getCentreX() / finePixelsPerPoint
                         - 0.5f * static_cast<float>(left.getWidth())) <= 0.25f
                    && std::abs(leftPart.box.toFloat().getCentreY() / finePixelsPerPoint
                                - 0.5f * static_cast<float>(left.getHeight())) <= 0.25f,
                "A step chevron does not stand in the middle of its area at " + sizeName);
        require(rightPart.box.getWidth() == leftPart.box.getWidth()
                    && rightPart.box.getHeight() == leftPart.box.getHeight()
                    && rightPart.box.getY() == leftPart.box.getY()
                    && std::abs(rightPart.box.getX()
                                - (rightPicture.getWidth() - leftPart.box.getRight())) <= 1,
                "The two step chevrons are not mirror images of each other at " + sizeName);
        // The left one points left: a column near the left end of its ink
        // crosses its tip once, one near the right end its two arms.
        const juce::Image::BitmapData leftPixels(leftPicture, juce::Image::BitmapData::readOnly);
        const auto strokesInColumn = [&](int x)
        {
            auto strokes = 0;
            auto inInk = false;
            for (auto y = leftPart.box.getY(); y < leftPart.box.getBottom(); ++y)
            {
                const auto covered = leftPixels.getPixelColour(x, y).getAlpha() >= strokeCover;
                strokes += covered && ! inInk ? 1 : 0;
                inInk = covered;
            }
            return strokes;
        };
        require(strokesInColumn(leftPart.box.getX() + 2) == 1
                    && strokesInColumn(leftPart.box.getRight() - leftPart.box.getWidth() / 4)
                           == 2,
                "The chevron that steps back does not point left at " + sizeName);
    }

    // At rest, under the pointer and after it has left.
    editor->setSize(AmanitaOceanAudioProcessorEditor::defaultWidth,
                    AmanitaOceanAudioProcessorEditor::defaultHeight);
    // A picture holds a tone that is not opaque multiplied by its cover, in
    // steps of 255: read back it is within three steps of what was painted.
    const auto wearsTone = [&](Step& step, juce::Colour tone)
    {
        const auto densest = paintedPart(paintAlone(step, finePixelsPerPoint)).densest;
        return std::abs(densest.getRed() - tone.getRed()) <= 3
            && std::abs(densest.getGreen() - tone.getGreen()) <= 3
            && std::abs(densest.getBlue() - tone.getBlue()) <= 3
            && std::abs(densest.getAlpha() - tone.getAlpha()) <= 3;
    };
    const auto middle = [](Step& step) { return step.getLocalBounds().toFloat().getCentre(); };
    GestureProbe gestures;
    processor.addListener(&gestures);
    for (auto* step : { previous, next })
    {
        require(juce::approximatelyEqual(step->getEmphasis(), 0.0f) && wearsTone(*step, restTone),
                "A step chevron at rest is not in the tone of the drop-down's chevron over "
                "its dark line");
        step->mouseEnter(pointerEvent(*step, middle(*step)));
        require(juce::approximatelyEqual(step->getEmphasis(), 1.0f)
                    && wearsTone(*step, amanita::ui::characterAccent(0)),
                "A step chevron under the pointer is not in the accent of the Character");
        require(algorithmParameter(processor).getIndex() == 0 && gestures.beginCount == 0,
                "The pointer over a step chevron changed the Character");
        step->mouseExit(pointerEvent(*step, { -4.0f, -4.0f }));
        require(juce::approximatelyEqual(step->getEmphasis(), 0.0f) && wearsTone(*step, restTone),
                "A step chevron the pointer has left keeps the accent");
    }

    // Presses: forwards round the end, backwards round the start, and the
    // presses that are given up.
    const juce::ModifierKeys leftButton(juce::ModifierKeys::leftButtonModifier);
    const juce::ModifierKeys rightButton(juce::ModifierKeys::rightButtonModifier);
    const auto click = [&](Step& step)
    {
        step.mouseDown(pointerEvent(step, middle(step), leftButton));
        step.mouseUp(pointerEvent(step, middle(step), leftButton));
    };
    for (auto index = 1; index <= lastCharacterIndex; ++index)
    {
        click(*next);
        require(algorithmParameter(processor).getIndex() == index
                    && selector->getSelectedItemIndex() == index
                    && gestures.beginCount == index && gestures.endCount == index,
                "A press on the right chevron does not step to the next Character in one "
                "host gesture");
    }
    click(*next);
    require(algorithmParameter(processor).getIndex() == 0,
            "The right chevron does not step from the last Character round to the first");
    click(*previous);
    require(algorithmParameter(processor).getIndex() == lastCharacterIndex,
            "The left chevron does not step from the first Character round to the last");
    for (auto index = lastCharacterIndex - 1; index >= 0; --index)
    {
        click(*previous);
        require(algorithmParameter(processor).getIndex() == index
                    && selector->getSelectedItemIndex() == index,
                "A press on the left chevron does not step to the Character before");
    }
    const auto clicks = 2 * lastCharacterIndex + 2;
    require(gestures.beginCount == clicks && gestures.endCount == clicks,
            "A press on a step chevron did not produce one complete host gesture");

    next->mouseDown(pointerEvent(*next, middle(*next), leftButton));
    require(juce::approximatelyEqual(next->getEmphasis(), 1.0f),
            "A step chevron that is held down is not in the accent");
    next->mouseUp(pointerEvent(*next, { -6.0f, static_cast<float>(next->getHeight()) + 6.0f },
                               leftButton));
    next->mouseDown(pointerEvent(*next, middle(*next), rightButton));
    next->mouseUp(pointerEvent(*next, middle(*next), rightButton));
    processor.removeListener(&gestures);
    require(algorithmParameter(processor).getIndex() == 0 && gestures.beginCount == clicks
                && juce::approximatelyEqual(next->getEmphasis(), 0.0f),
            "A press that was dragged off a step chevron, or one without the left button, "
            "changed the Character");
    require(! selector->isPopupActive(), "A step chevron opened the Character list");

    // An editor opened on another Character shows that Character's accent
    // under the pointer.
    {
        AmanitaOceanAudioProcessor spumeProcessor;
        algorithmParameter(spumeProcessor).setValueNotifyingHost(
            algorithmCases[spumeIndex].hostValue);
        std::unique_ptr<juce::AudioProcessorEditor> spumeEditor(spumeProcessor.createEditor());
        auto* step = dynamic_cast<Step*>(findDescendantById(*spumeEditor, "character-next"));
        require(step != nullptr, "Step chevron was not found");
        step->mouseEnter(pointerEvent(*step, middle(*step)));
        require(wearsTone(*step, amanita::ui::characterAccent(spumeIndex)),
                "A step chevron under the pointer is not in the accent of the eighth Character");
    }

    // In a window of its own, for assistive technology: two buttons that step
    // when they are pressed.
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    require(editor->getPeer() != nullptr, "The editor got no window for its step chevrons");
    editor->getPeer()->setAlpha(0.0f);
    editor->setVisible(true);
    struct Expected
    {
        Step* step;
        const char* title;
        int index;
    };
    for (const auto& expected : { Expected { next, "Next", 1 },
                                  Expected { previous, "Previous", 0 } })
    {
        auto* handler = expected.step->getAccessibilityHandler();
        require(handler != nullptr && handler->getRole() == juce::AccessibilityRole::button
                    && handler->getTitle() == expected.title
                    && ! handler->getTitle().containsIgnoreCase("character")
                    && handler->getDescription().isNotEmpty(),
                std::string("Assistive technology is not told of the step chevron ")
                    + expected.title);
        require(handler->getActions().invoke(juce::AccessibilityActionType::press)
                    && algorithmParameter(processor).getIndex() == expected.index,
                std::string("Assistive technology cannot press the step chevron ")
                    + expected.title);
    }
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
        testDescriptionParagraphGivesAWidowCompany();
        testCharacterAccentInTitleRingAndList();
        testEditorBackgroundGathersRoundTheEvolutionDial();
        testDeepCurrentBackgroundRenderer();
        testUnifiedAlgorithmReachesDsp();
        testSizeParameterReachesDsp();
        testMonoSafeParameterReachesDsp();
        testFocusParameterReachesDsp();
        testHarmonyParameterReachesAutoDsp();
        testFathomHostValuesReachTheEngine();
        testFathomStateRestoresItsSettings();
        testFathomVoicePhaseOfEachInstance();
        testFathomVoiceSeedsOfManyInstances();
        testCharacterOutsideItsChoices();
        testFathomTailLengthCoversItsDecay();
        testStatesOfEarlierBuildsAndTheEighthChoice();
        testHostTransportReachesDsp();
        testUndertowTailLengthFollowsTheTempo();
        testSpumeTailLengthCoversItsDiffuser();
        testCharacterListLiesInsideTheEditor();
        testStepChevronsBesideTheDropDown();
        // --render-ui <png> [character] [width] [frozen] [list item] [step under pointer]
        // --render-ui-live <png> [character] [width] [frozen] [list item] [seconds]
        //                  [shader frame] [evolution] [focus] [step under pointer]
        const auto wantsSnapshot = argc >= 3 && std::strcmp(argv[1], "--render-ui") == 0;
        const auto wantsLiveSnapshot = argc >= 3
                                    && std::strcmp(argv[1], "--render-ui-live") == 0;
        // The message loop of a process runs once: a live snapshot needs it,
        // and the keyboard walk takes it otherwise.
        if (! wantsLiveSnapshot)
            testKeyboardAndAccessibilityInTheEditorWindow();
        if (wantsSnapshot || wantsLiveSnapshot)
        {
            SnapshotSettings settings;
            settings.characterIndex = argc >= 4 ? std::atoi(argv[3]) : settings.characterIndex;
            settings.width = argc >= 5 ? std::atoi(argv[4]) : settings.width;
            settings.frozen = argc >= 6 && std::atoi(argv[5]) != 0;
            settings.highlightedListItem = argc >= 7 ? std::atoi(argv[6])
                                                     : settings.highlightedListItem;
            if (wantsSnapshot)
            {
                settings.stepUnderPointer = argc >= 8 ? std::atoi(argv[7])
                                                      : settings.stepUnderPointer;
                renderEditorPng(argv[2], settings);
                std::cout << "[PASS] wrote custom editor PNG to " << argv[2] << '\n';
            }
            else
            {
                const auto secondsOfAnimation = argc >= 8 ? std::atof(argv[7]) : 1.0;
                const juce::String shaderFramePath(argc >= 9 ? argv[8] : "");
                settings.evolution = argc >= 10 ? static_cast<float>(std::atof(argv[9]))
                                                : settings.evolution;
                settings.focus = argc >= 11 ? static_cast<float>(std::atof(argv[10]))
                                            : settings.focus;
                settings.stepUnderPointer = argc >= 12 ? std::atoi(argv[11])
                                                       : settings.stepUnderPointer;
                renderLiveEditorPng(argv[2], settings, secondsOfAnimation, shaderFramePath);
                std::cout << "[PASS] wrote live editor PNG to " << argv[2] << '\n';
            }
        }
        if (argc >= 3 && std::strcmp(argv[1], "--render-background") == 0)
        {
            const auto characterIndex = argc >= 4 ? std::atoi(argv[3]) : 0;
            const auto evolution = argc >= 5
                ? static_cast<float>(std::atof(argv[4]))
                : 0.68f;
            const auto timeSeconds = argc >= 6 ? std::atof(argv[5]) : 0.0;
            const auto requestedWidth = argc >= 7
                ? std::atoi(argv[6])
                : AmanitaOceanAudioProcessorEditor::defaultWidth;
            renderBackgroundPng(argv[2], characterIndex, evolution,
                                timeSeconds, requestedWidth);
            std::cout << "[PASS] wrote Deep Current PNG to " << argv[2] << '\n';
        }
        if (argc >= 2 && std::strcmp(argv[1], "--benchmark-background") == 0)
        {
            const auto requestedWidth = argc >= 3
                ? std::atoi(argv[2])
                : AmanitaOceanAudioProcessorEditor::maximumWidth;
            const auto requestedFrames = argc >= 4 ? std::atoi(argv[3]) : 180;
            benchmarkBackgroundRenderer(requestedWidth, requestedFrames);
        }
        std::cout << "[PASS] Unified Algorithm/Evolution/Focus/Harmony state/UI/DSP routing\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] Unified Algorithm/Evolution/Focus/Harmony state/UI/DSP routing: "
                  << error.what() << '\n';
        return 1;
    }
}
