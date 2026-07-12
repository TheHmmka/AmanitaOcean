#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>

namespace
{
constexpr auto modeId = "mode";
constexpr auto mixId = "mix";
constexpr auto decayId = "decay";
constexpr auto sizeId = "size";
constexpr auto preDelayId = "preDelay";
constexpr auto lowCutId = "lowCut";
constexpr auto highDampingId = "highDamping";
constexpr auto modulationId = "modulation";
constexpr auto widthId = "width";
constexpr auto freezeId = "freeze";

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
    mixParameter_ = state_.getRawParameterValue(mixId);
    decayParameter_ = state_.getRawParameterValue(decayId);
    sizeParameter_ = state_.getRawParameterValue(sizeId);
    preDelayParameter_ = state_.getRawParameterValue(preDelayId);
    lowCutParameter_ = state_.getRawParameterValue(lowCutId);
    highDampingParameter_ = state_.getRawParameterValue(highDampingId);
    modulationParameter_ = state_.getRawParameterValue(modulationId);
    widthParameter_ = state_.getRawParameterValue(widthId);
    freezeParameter_ = state_.getRawParameterValue(freezeId);

    jassert(modeParameter_ != nullptr && mixParameter_ != nullptr
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
    if (const auto xml = state_.copyState().createXml())
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

    const auto hasMode = std::any_of(restored.begin(), restored.end(), [] (const auto& child)
    {
        return child.getProperty("id").toString() == modeId;
    });
    if (!hasMode)
    {
        juce::ValueTree modeState("PARAM");
        modeState.setProperty("id", modeId, nullptr);
        modeState.setProperty("value", 0.0f, nullptr);
        restored.appendChild(modeState, nullptr);
    }

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
        juce::StringArray { "Default", "Bloom" }, 0));

    return layout;
}

amanita::dsp::ReverbParameters AmanitaOceanAudioProcessor::readDspParameters() const noexcept
{
    jassert(modeParameter_ != nullptr && mixParameter_ != nullptr
            && decayParameter_ != nullptr && sizeParameter_ != nullptr
            && preDelayParameter_ != nullptr && lowCutParameter_ != nullptr
            && highDampingParameter_ != nullptr && modulationParameter_ != nullptr
            && widthParameter_ != nullptr && freezeParameter_ != nullptr);

    amanita::dsp::ReverbParameters parameters;
    parameters.mode = modeParameter_->load(std::memory_order_relaxed) >= 0.5f
        ? amanita::dsp::ReverbMode::bloom
        : amanita::dsp::ReverbMode::defaultMode;
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
