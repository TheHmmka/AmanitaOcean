#include "PluginProcessor.h"
#include "ui/DeepCurrentRenderer.h"
#include "ui/PluginEditor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
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
    require(std::abs(focus->get() - 100.0f) <= 1.0e-6f,
            "Focus does not default to 100 percent");
    require(focus->getLabel() == "%", "Focus unit label changed");

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

[[nodiscard]] juce::Colour backgroundAccent(int characterIndex) noexcept
{
    constexpr std::array<std::uint32_t, 4> colours {
        0xff81bfc7, 0xffc89c83, 0xff829de0, 0xffb3a6c4
    };
    return juce::Colour(colours[static_cast<std::size_t>(std::clamp(characterIndex, 0, 3))]);
}

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
                                                juce::Colour accent)
{
    amanita::ui::DeepCurrentRenderer renderer;
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
    constexpr auto drift = 2;
    constexpr auto veil = 3;
    constexpr auto evolution = 0.82f;
    constexpr auto laterTime = 19.0;
    const auto accent = backgroundAccent(drift);

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
    const auto base = renderBackgroundBase(width, height);

    const auto repeatDifference = measureImageDifference(initial, repeated);
    const auto motionDifference = measureImageDifference(initial, later);
    const auto characterDifference = measureImageDifference(later, veilAtSameTime);
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

    constexpr std::array<int, 4> horizontalEdges { 0, width / 3, width * 2 / 3, width };
    constexpr std::array<int, 4> verticalEdges { 0, 126, 438, height };
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
          800, 500 },
        { AmanitaOceanAudioProcessorEditor::defaultWidth,
          AmanitaOceanAudioProcessorEditor::defaultHeight,
          960, 600 },
        { AmanitaOceanAudioProcessorEditor::maximumWidth,
          AmanitaOceanAudioProcessorEditor::maximumHeight,
          1200, 750 }
    }};
    constexpr auto resizeTime = 7.25;
    constexpr auto resizeEvolution = 0.71f;
    const auto resizeReference = renderBackgroundFrame(width, height, drift,
                                                       resizeEvolution, false,
                                                       resizeTime, accent);
    amanita::ui::DeepCurrentRenderer resizedRenderer;
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
              << ", Freeze stop=" << freezeStopSeconds << " s\n";
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

void renderBackgroundPng(const juce::String& path,
                         int characterIndex,
                         float evolution,
                         double timeSeconds,
                         int requestedWidth)
{
    const auto safeCharacter = std::clamp(characterIndex, 0, 3);
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
              << " ms over " << frames << " frames, one-core load="
              << total / static_cast<double>(milliseconds.size()) * 1.5
              << "% steady / "
              << total / static_cast<double>(milliseconds.size()) * 3.0
              << "% active\n";
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
        testDeepCurrentBackgroundRenderer();
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
