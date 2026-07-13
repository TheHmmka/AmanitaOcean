#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto modeId = "mode";
constexpr auto driftModelId = "driftModel";
constexpr auto mixId = "mix";
constexpr auto decayId = "decay";
constexpr auto sizeId = "size";
constexpr auto preDelayId = "preDelay";
constexpr auto lowCutId = "lowCut";
constexpr auto highDampingId = "highDamping";
constexpr auto modulationId = "modulation";
constexpr auto widthId = "width";
constexpr auto freezeId = "freeze";
constexpr auto stateSchemaId = "schemaVersion";
constexpr auto currentStateSchema = 4;

[[nodiscard]] juce::NormalisableRange<float> skewedRange(float minimum,
                                                          float maximum,
                                                          float interval,
                                                          float centre)
{
    juce::NormalisableRange<float> range(minimum, maximum, interval);
    range.setSkewForCentre(centre);
    return range;
}
} // namespace

AmanitaOceanAudioProcessor::AmanitaOceanAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "AmanitaOceanState", createParameterLayout())
{
    modeParameter_ = state_.getRawParameterValue(modeId);
    driftModelParameter_ = state_.getRawParameterValue(driftModelId);
    mixParameter_ = state_.getRawParameterValue(mixId);
    decayParameter_ = state_.getRawParameterValue(decayId);
    sizeParameter_ = state_.getRawParameterValue(sizeId);
    preDelayParameter_ = state_.getRawParameterValue(preDelayId);
    lowCutParameter_ = state_.getRawParameterValue(lowCutId);
    highDampingParameter_ = state_.getRawParameterValue(highDampingId);
    modulationParameter_ = state_.getRawParameterValue(modulationId);
    widthParameter_ = state_.getRawParameterValue(widthId);
    freezeParameter_ = state_.getRawParameterValue(freezeId);

    jassert(modeParameter_ != nullptr && driftModelParameter_ != nullptr
            && mixParameter_ != nullptr
            && decayParameter_ != nullptr && sizeParameter_ != nullptr
            && preDelayParameter_ != nullptr && lowCutParameter_ != nullptr
            && highDampingParameter_ != nullptr && modulationParameter_ != nullptr
            && widthParameter_ != nullptr && freezeParameter_ != nullptr);
}

void AmanitaOceanAudioProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    reverb_.setParameters(readDspParameters());
    reverb_.prepare(sampleRate, maximumExpectedSamplesPerBlock);
}

void AmanitaOceanAudioProcessor::releaseResources()
{
    reverb_.reset();
}

bool AmanitaOceanAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AmanitaOceanAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels();
         channel < getTotalNumOutputChannels();
         ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }

    reverb_.setParameters(readDspParameters());
    reverb_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
}

juce::AudioProcessorEditor* AmanitaOceanAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

bool AmanitaOceanAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String AmanitaOceanAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AmanitaOceanAudioProcessor::acceptsMidi() const { return false; }
bool AmanitaOceanAudioProcessor::producesMidi() const { return false; }
bool AmanitaOceanAudioProcessor::isMidiEffect() const { return false; }
double AmanitaOceanAudioProcessor::getTailLengthSeconds() const
{
    const auto decay = decayParameter_ != nullptr
        ? decayParameter_->load(std::memory_order_relaxed)
        : 5.0f;
    return static_cast<double>(decay) + 0.5;
}

int AmanitaOceanAudioProcessor::getNumPrograms() { return 1; }
int AmanitaOceanAudioProcessor::getCurrentProgram() { return 0; }
void AmanitaOceanAudioProcessor::setCurrentProgram(int) {}
const juce::String AmanitaOceanAudioProcessor::getProgramName(int) { return {}; }
void AmanitaOceanAudioProcessor::changeProgramName(int, const juce::String&) {}

void AmanitaOceanAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    auto state = state_.copyState();
    state.setProperty(stateSchemaId, currentStateSchema, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destinationData);
}

void AmanitaOceanAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto restored = juce::ValueTree::fromXml(*xml);
    if (!restored.isValid() || !restored.hasType(state_.state.getType()))
        return;

    const auto sourceSchema = static_cast<int>(restored.getProperty(stateSchemaId, 0));
    const auto modeState = std::find_if(restored.begin(), restored.end(), [] (const auto& child)
    {
        return child.getProperty("id").toString() == modeId;
    });
    if (modeState == restored.end())
    {
        juce::ValueTree defaultModeState("PARAM");
        defaultModeState.setProperty("id", modeId, nullptr);
        defaultModeState.setProperty("value", 0.0f, nullptr);
        restored.appendChild(defaultModeState, nullptr);
    }
    else if (sourceSchema < 3)
    {
        auto modeChild = *modeState;
        if (std::abs(static_cast<float>(modeChild.getProperty("value")) - 1.0f) < 0.001f)
            modeChild.setProperty("value", 2.0f, nullptr);
    }

    const auto driftModelState = std::find_if(
        restored.begin(), restored.end(), [] (const auto& child)
        {
            return child.getProperty("id").toString() == driftModelId;
        });
    if (driftModelState == restored.end())
    {
        juce::ValueTree defaultDriftModelState("PARAM");
        defaultDriftModelState.setProperty("id", driftModelId, nullptr);
        defaultDriftModelState.setProperty("value", 0.0f, nullptr);
        restored.appendChild(defaultDriftModelState, nullptr);
    }

    restored.setProperty(stateSchemaId, currentStateSchema, nullptr);

    state_.replaceState(restored);
}

juce::AudioProcessorValueTreeState::ParameterLayout
AmanitaOceanAudioProcessor::createParameterLayout()
{
    using FloatAttributes = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { mixId, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { decayId, 1 }, "Decay",
        skewedRange(0.2f, 30.0f, 0.01f, 3.0f), 5.0f,
        FloatAttributes().withLabel("s")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { sizeId, 1 }, "Size",
        juce::NormalisableRange<float> { 50.0f, 200.0f, 0.1f }, 100.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { preDelayId, 1 }, "Pre-delay",
        juce::NormalisableRange<float> { 0.0f, 250.0f, 0.1f }, 20.0f,
        FloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lowCutId, 1 }, "Low Cut",
        skewedRange(20.0f, 1000.0f, 1.0f, 120.0f), 80.0f,
        FloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { highDampingId, 1 }, "High Damping",
        skewedRange(1000.0f, 20000.0f, 1.0f, 7000.0f), 9000.0f,
        FloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { modulationId, 1 }, "Modulation",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 20.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { widthId, 1 }, "Width",
        juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { freezeId, 1 }, "Freeze", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { modeId, 1 }, "Character",
        juce::StringArray { "Default", "Drift", "Bloom" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { driftModelId, 2 }, "Drift Model",
        juce::StringArray { "Original", "Drift 2" }, 0));

    return layout;
}

amanita::dsp::ReverbParameters AmanitaOceanAudioProcessor::readDspParameters() const noexcept
{
    jassert(modeParameter_ != nullptr && driftModelParameter_ != nullptr
            && mixParameter_ != nullptr
            && decayParameter_ != nullptr && sizeParameter_ != nullptr
            && preDelayParameter_ != nullptr && lowCutParameter_ != nullptr
            && highDampingParameter_ != nullptr && modulationParameter_ != nullptr
            && widthParameter_ != nullptr && freezeParameter_ != nullptr);

    amanita::dsp::ReverbParameters parameters;
    switch (static_cast<int>(std::lround(modeParameter_->load(std::memory_order_relaxed))))
    {
        case 1:
            parameters.mode = amanita::dsp::ReverbMode::drift;
            break;
        case 2:
            parameters.mode = amanita::dsp::ReverbMode::bloom;
            break;
        default:
            parameters.mode = amanita::dsp::ReverbMode::defaultMode;
            break;
    }
    parameters.driftModel = driftModelParameter_->load(std::memory_order_relaxed) >= 0.5f
        ? amanita::dsp::DriftModel::drift2
        : amanita::dsp::DriftModel::original;
    parameters.mix = mixParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.decaySeconds = decayParameter_->load(std::memory_order_relaxed);
    parameters.size = sizeParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.preDelayMs = preDelayParameter_->load(std::memory_order_relaxed);
    parameters.lowCutHz = lowCutParameter_->load(std::memory_order_relaxed);
    parameters.highDampingHz = highDampingParameter_->load(std::memory_order_relaxed);
    parameters.modulation = modulationParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.width = widthParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.freeze = freezeParameter_->load(std::memory_order_relaxed) >= 0.5f;
    return parameters;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmanitaOceanAudioProcessor();
}
