#include "PluginProcessor.h"
#include "ui/PluginEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <random>

namespace
{
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);

constexpr auto algorithmId = "algorithm";
constexpr auto mixId = "mix";
constexpr auto decayId = "decay";
constexpr auto sizeId = "size";
constexpr auto preDelayId = "preDelay";
constexpr auto lowCutId = "lowCut";
constexpr auto highDampingId = "highDamping";
constexpr auto evolutionId = "evolution";
constexpr auto widthId = "width";
constexpr auto focusId = "focus";
constexpr auto freezeId = "freeze";
constexpr auto harmonyId = "harmony";
constexpr auto monoSafeId = "monoSafe";
constexpr float legacyMinimumSizePercent = 50.0f;

// The place of Undertow among the choices of the Character parameter.
constexpr int undertowChoice = 6;
// What Undertow replays after its input has stopped, in quarter notes: two of
// its longest chunks, of 8/3 each, and four passes of the recirculation an
// octave up, of 2 each.
constexpr double undertowTailQuarterNotes = 2.0 * 8.0 / 3.0 + 4.0 * 2.0;
// The tempo that tail is reckoned at: the host's within the range Undertow
// follows, and this one where the host gives none.
constexpr double slowestTailTempo = 20.0;
constexpr double fastestTailTempo = 999.0;
constexpr float tailTempoWithoutHost = 120.0f;

// The place of Spume among those choices, and what its diffuser in front of
// the network still holds after its input has stopped, in seconds.
constexpr int spumeChoice = 7;
constexpr double spumeTailSeconds = 4.0;

[[nodiscard]] float sizeScaleFromPercent(float percent) noexcept
{
    const auto safePercent = std::isfinite(percent)
        ? std::clamp(percent, 0.0f, 200.0f)
        : 100.0f;

    if (safePercent >= legacyMinimumSizePercent)
        return safePercent * 0.01f;

    const auto normalizedCompactRange = safePercent / legacyMinimumSizePercent;
    constexpr auto minimum = amanita::dsp::FDNReverb::minimumSizeScale;
    // This compact-range curve reaches 0.5 with the same slope as the legacy
    // branch, so automation crosses 50% without a speed kink.
    return minimum
         + (0.5f - 2.0f * minimum) * normalizedCompactRange
         + minimum * normalizedCompactRange * normalizedCompactRange;
}

[[nodiscard]] juce::NormalisableRange<float> skewedRange(float minimum,
                                                          float maximum,
                                                          float interval,
                                                          float centre)
{
    juce::NormalisableRange<float> range(minimum, maximum, interval);
    range.setSkewForCentre(centre);
    return range;
}

[[nodiscard]] juce::NormalisableRange<float> smoothLogarithmicRange(float minimum,
                                                                     float maximum,
                                                                     float centre)
{
    jassert(minimum > 0.0f && minimum < centre && centre < maximum);
    const auto fullLogSpan = std::log(static_cast<double>(maximum / minimum));
    const auto centreLogSpan = std::log(static_cast<double>(centre / minimum));
    const auto quadratic = 2.0 * fullLogSpan - 4.0 * centreLogSpan;
    const auto linear = 4.0 * centreLogSpan - fullLogSpan;
    jassert(linear > 0.0 && linear + 2.0 * quadratic > 0.0);

    return {
        minimum,
        maximum,
        [quadratic, linear](float rangeStart, float rangeEnd, float position)
        {
            if (position <= 0.0f)
                return rangeStart;
            if (position >= 1.0f)
                return rangeEnd;

            const auto p = static_cast<double>(position);
            return static_cast<float>(static_cast<double>(rangeStart)
                                      * std::exp(quadratic * p * p + linear * p));
        },
        [quadratic, linear](float rangeStart, float rangeEnd, float value)
        {
            if (!std::isfinite(value) || value <= rangeStart)
                return 0.0f;
            if (value >= rangeEnd)
                return 1.0f;

            const auto safeValue = static_cast<double>(value);
            const auto logValue = std::log(safeValue / static_cast<double>(rangeStart));
            const auto discriminant = juce::jmax(0.0,
                                                  linear * linear
                                                      + 4.0 * quadratic * logValue);
            const auto denominator = linear + std::sqrt(discriminant);
            return denominator > 1.0e-12
                ? static_cast<float>(2.0 * logValue / denominator)
                : 0.0f;
        }
    };
}

[[nodiscard]] juce::String limitHostText(juce::String text, int maximumLength)
{
    return maximumLength > 0 ? text.substring(0, maximumLength) : text;
}

[[nodiscard]] juce::String decayText(float value, int maximumLength)
{
    return limitHostText(juce::String(value, value < 10.0f ? 2 : 1), maximumLength);
}

[[nodiscard]] juce::String lowCutText(float value, int maximumLength)
{
    return limitHostText(juce::String(juce::roundToInt(value)), maximumLength);
}

// The output function of SplitMix64: one-to-one on 64 bits, and every bit of
// its argument reaches every bit of its result.
[[nodiscard]] std::uint64_t scrambleBits(std::uint64_t bits) noexcept
{
    bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
    bits = (bits ^ (bits >> 27)) * 0x94d049bb133111ebULL;
    return bits ^ (bits >> 31);
}

// A seed for the voice phase of Fathom in a new instance. The system's entropy
// is mixed with the time and with the number of instances this process has
// made, so that a source of entropy that fails or repeats itself still gives
// every instance a seed of its own: the count tells the instances of one
// process apart even when they are made at the same moment, the time those of
// two processes. Called while an instance is constructed, never while it
// processes audio.
[[nodiscard]] std::uint64_t drawFathomVoiceSeed()
{
    static std::atomic<std::uint64_t> instancesMade { 0 };

    std::uint64_t entropy = 0;
    try
    {
        std::random_device device;
        entropy = (static_cast<std::uint64_t>(device()) << 32) | device();
    }
    catch (const std::exception&)
    {
        // No entropy from the system: the time and the count remain.
    }

    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto instance = instancesMade.fetch_add(1, std::memory_order_relaxed) + 1;
    return scrambleBits(scrambleBits(scrambleBits(entropy) + ticks)
                        + instance * 0x9e3779b97f4a7c15ULL);
}
} // namespace

AmanitaOceanAudioProcessor::AmanitaOceanAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "AmanitaOceanState", createParameterLayout()),
      fathomVoiceSeed_(drawFathomVoiceSeed()),
      tailTempoBpm_(tailTempoWithoutHost)
{
    reverb_.setFathomVoiceSeed(fathomVoiceSeed_);

    characterParameter_ = state_.getRawParameterValue(algorithmId);
    mixParameter_ = state_.getRawParameterValue(mixId);
    decayParameter_ = state_.getRawParameterValue(decayId);
    sizeParameter_ = state_.getRawParameterValue(sizeId);
    preDelayParameter_ = state_.getRawParameterValue(preDelayId);
    lowCutParameter_ = state_.getRawParameterValue(lowCutId);
    highDampingParameter_ = state_.getRawParameterValue(highDampingId);
    evolutionParameter_ = state_.getRawParameterValue(evolutionId);
    widthParameter_ = state_.getRawParameterValue(widthId);
    focusParameter_ = state_.getRawParameterValue(focusId);
    freezeParameter_ = state_.getRawParameterValue(freezeId);
    harmonyParameter_ = state_.getRawParameterValue(harmonyId);
    monoSafeParameter_ = state_.getRawParameterValue(monoSafeId);

    jassert(characterParameter_ != nullptr && mixParameter_ != nullptr
            && decayParameter_ != nullptr && sizeParameter_ != nullptr
            && preDelayParameter_ != nullptr && lowCutParameter_ != nullptr
            && highDampingParameter_ != nullptr && evolutionParameter_ != nullptr
            && widthParameter_ != nullptr && focusParameter_ != nullptr
            && freezeParameter_ != nullptr && harmonyParameter_ != nullptr
            && monoSafeParameter_ != nullptr);
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

    // A block without frames changes nothing. Reading the parameters for it
    // would restart the Character crossfades with no sample in between.
    if (buffer.getNumSamples() <= 0)
        return;

    reverb_.setParameters(readDspParameters());
    const auto transport = readHostTransport();
    reverb_.setHostTransport(transport);
    // The tail length is asked for off this thread; it finds the tempo here.
    tailTempoBpm_.store(transport.hasTempo
                            ? static_cast<float>(std::clamp(transport.bpm, slowestTailTempo,
                                                            fastestTailTempo))
                            : tailTempoWithoutHost,
                        std::memory_order_relaxed);
    reverb_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());

    const auto& currentFrame = reverb_.getCurrentFieldFrame();
    // Acquire on the opening revision prevents the following field stores
    // from becoming visible before readers can observe the odd revision.
    currentVisualRevision_.fetch_add(1, std::memory_order_acq_rel);
    currentVisualFlowX_.store(currentFrame.flowX, std::memory_order_relaxed);
    currentVisualFlowY_.store(currentFrame.flowY, std::memory_order_relaxed);
    currentVisualStrength_.store(reverb_.getCurrentFieldStrength(),
                                 std::memory_order_relaxed);
    currentVisualRevision_.fetch_add(1, std::memory_order_release);
}

juce::AudioProcessorEditor* AmanitaOceanAudioProcessor::createEditor()
{
    return new AmanitaOceanAudioProcessorEditor(*this);
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
    const auto tail = static_cast<double>(decay) + 0.5;

    // Undertow goes on replaying the past after its input has stopped, at the
    // tempo of the block processed last; Spume empties its diffuser, whatever
    // the tempo.
    const auto character = characterParameter_ != nullptr
        ? static_cast<int>(std::lround(characterParameter_->load(std::memory_order_relaxed)))
        : 0;
    if (character == undertowChoice)
        return tail + undertowTailQuarterNotes * 60.0
                          / static_cast<double>(tailTempoBpm_.load(std::memory_order_relaxed));
    if (character == spumeChoice)
        return tail + spumeTailSeconds;
    return tail;
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

    const auto restored = juce::ValueTree::fromXml(*xml);
    if (!restored.isValid() || !restored.hasType(state_.state.getType()))
        return;

    state_.replaceState(restored);
    updateHostDisplay(
        juce::AudioProcessorListener::ChangeDetails {}.withProgramChanged(true));
}

juce::AudioProcessorValueTreeState& AmanitaOceanAudioProcessor::getParameterState() noexcept
{
    return state_;
}

const juce::AudioProcessorValueTreeState&
AmanitaOceanAudioProcessor::getParameterState() const noexcept
{
    return state_;
}

std::uint64_t AmanitaOceanAudioProcessor::getFathomVoiceSeed() const noexcept
{
    return fathomVoiceSeed_;
}

AmanitaOceanAudioProcessor::CurrentVisualSnapshot
AmanitaOceanAudioProcessor::getCurrentVisualSnapshot() const noexcept
{
    CurrentVisualSnapshot snapshot;
    for (;;)
    {
        const auto revisionBefore =
            currentVisualRevision_.load(std::memory_order_acquire);
        if ((revisionBefore & 1u) != 0u)
            continue;

        snapshot.flowX = currentVisualFlowX_.load(std::memory_order_relaxed);
        snapshot.flowY = currentVisualFlowY_.load(std::memory_order_relaxed);
        snapshot.strength =
            currentVisualStrength_.load(std::memory_order_relaxed);
        const auto revisionAfter =
            currentVisualRevision_.load(std::memory_order_acquire);
        if (revisionBefore == revisionAfter)
            return snapshot;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
AmanitaOceanAudioProcessor::createParameterLayout()
{
    using FloatAttributes = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { algorithmId, 1 }, "Character",
        juce::StringArray { "Default", "Bloom", "Drift", "Veil", "Current", "Fathom",
                            "Undertow", "Spume" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { mixId, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { decayId, 1 }, "Decay",
        smoothLogarithmicRange(0.2f, 30.0f, 3.0f), 5.0f,
        FloatAttributes().withLabel("s").withStringFromValueFunction(decayText)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { sizeId, 1 }, "Size",
        juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { preDelayId, 1 }, "Pre-delay",
        juce::NormalisableRange<float> { 0.0f, 250.0f, 0.1f }, 20.0f,
        FloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lowCutId, 1 }, "Low Cut",
        smoothLogarithmicRange(20.0f, 1000.0f, 120.0f), 80.0f,
        FloatAttributes().withLabel("Hz").withStringFromValueFunction(lowCutText)));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { highDampingId, 1 }, "High Damping",
        skewedRange(1000.0f, 20000.0f, 1.0f, 7000.0f), 9000.0f,
        FloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { evolutionId, 1 }, "Evolution",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 35.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { widthId, 1 }, "Width",
        juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 100.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { focusId, 1 }, "Focus",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { freezeId, 1 }, "Freeze", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { harmonyId, 1 }, "Harmony",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f,
        FloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { monoSafeId, 1 }, "Mono Safe", false));

    return layout;
}

amanita::dsp::ReverbParameters AmanitaOceanAudioProcessor::readDspParameters() const noexcept
{
    jassert(characterParameter_ != nullptr && mixParameter_ != nullptr
            && decayParameter_ != nullptr && sizeParameter_ != nullptr
            && preDelayParameter_ != nullptr && lowCutParameter_ != nullptr
            && highDampingParameter_ != nullptr && evolutionParameter_ != nullptr
            && widthParameter_ != nullptr && focusParameter_ != nullptr
            && freezeParameter_ != nullptr && harmonyParameter_ != nullptr
            && monoSafeParameter_ != nullptr);

    amanita::dsp::ReverbParameters parameters;
    switch (static_cast<int>(std::lround(
        characterParameter_->load(std::memory_order_relaxed))))
    {
        case 1:
            parameters.mode = amanita::dsp::ReverbMode::bloom;
            break;
        case 2:
            parameters.mode = amanita::dsp::ReverbMode::drift;
            break;
        case 3:
            parameters.mode = amanita::dsp::ReverbMode::veil;
            break;
        case 4:
            parameters.mode = amanita::dsp::ReverbMode::current;
            break;
        case 5:
            parameters.mode = amanita::dsp::ReverbMode::fathom;
            break;
        case undertowChoice:
            parameters.mode = amanita::dsp::ReverbMode::undertow;
            break;
        case spumeChoice:
            parameters.mode = amanita::dsp::ReverbMode::spume;
            break;
        default:
            parameters.mode = amanita::dsp::ReverbMode::defaultMode;
            break;
    }
    parameters.mix = mixParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.decaySeconds = decayParameter_->load(std::memory_order_relaxed);
    parameters.size = sizeScaleFromPercent(
        sizeParameter_->load(std::memory_order_relaxed));
    parameters.preDelayMs = preDelayParameter_->load(std::memory_order_relaxed);
    parameters.lowCutHz = lowCutParameter_->load(std::memory_order_relaxed);
    parameters.highDampingHz = highDampingParameter_->load(std::memory_order_relaxed);
    parameters.evolution = evolutionParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.width = widthParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.ducking = focusParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.harmony = harmonyParameter_->load(std::memory_order_relaxed) * 0.01f;
    parameters.autoHarmony = true;
    parameters.freeze = freezeParameter_->load(std::memory_order_relaxed) >= 0.5f;
    parameters.monoSafeStereo
        = monoSafeParameter_->load(std::memory_order_relaxed) >= 0.5f;
    return parameters;
}

amanita::dsp::HostTransport AmanitaOceanAudioProcessor::readHostTransport() const noexcept
{
    amanita::dsp::HostTransport transport;
    const auto* playHead = getPlayHead();
    if (playHead == nullptr)
        return transport;

    // The play head answers by value from what the host gave for this block.
    const auto position = playHead->getPosition();
    if (! position.hasValue())
        return transport;

    // A tempo that is no positive number is no tempo, and without a position
    // in quarter notes that is a number the transport reads as stopped.
    if (const auto bpm = position->getBpm(); bpm.hasValue() && std::isfinite(*bpm) && *bpm > 0.0)
    {
        transport.bpm = *bpm;
        transport.hasTempo = true;
    }
    if (const auto quarterNotes = position->getPpqPosition();
        quarterNotes.hasValue() && std::isfinite(*quarterNotes))
    {
        transport.quarterNotes = *quarterNotes;
        transport.playing = position->getIsPlaying();
    }
    return transport;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmanitaOceanAudioProcessor();
}
