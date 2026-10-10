#include "dsp/FathomEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Offline renderer of the Fathom engine for the scoring scripts of
// Analyzer/Campaigns/RevOceanCharacterization.
//
//     AmanitaOceanFathomRender --input in.wav --output out.wav [options]
//     AmanitaOceanFathomRender --benchmark [options]
//
// The input is a stereo 32-bit float WAV file; the output has its format,
// rate and length. Frame n of the output is the engine's answer to frame n of
// the input, which is the reference's raw output at n plus its reported
// latency. With no outer law switched on the output is the engine's wet signal.
// With --ocean-mix in place of --mix the chain is the plug-in's at Ocean's
// neutral controls.
//
// With --layer undertow the engine carries the Undertow layer and the renderer
// plays the host: it reports the transport in front of every block. A job of
// a recorded session of the reference is reproduced by the same block layout,
// tempo and reported position, the reference's arithmetic and the origins of
// the instance's clocks.
//
// With --layer spume the engine carries the Spume layer, which hears nothing of
// the host. A job of a recorded session needs the origin of the oscillators
// and, where Macro moves, the block of 44 internal samples that used each new
// value first (--macro-at-blocks): which block that was is the reference's
// handling of host blocks, and the scoring script works it out.

namespace
{
using amanita::dsp::FathomEngine;

constexpr char usage[] =
    "usage: AmanitaOceanFathomRender --input <wav> --output <wav> [options]\n"
    "       AmanitaOceanFathomRender --benchmark [options]\n"
    "\n"
    "  --warmup-frames <n>    frames of silence processed before the input (default 0)\n"
    "  --idle-warmup          pass the warm-up through advanceIdle() instead of processing silence\n"
    "  --decay <seconds>      Decay (default 4)\n"
    "  --size <scale>         Size / 100 (default 1)\n"
    "  --predelay <seconds>   Pre-delay (default 0)\n"
    "  --macro <0..1>         Macro (default 0)\n"
    "  --macro-steps <list>   Macro set anew in front of frames of the input, warm-up included:\n"
    "                         frame:value pairs separated by commas\n"
    "  --macro-at-blocks <list>  Macro set anew so that the block of 44 internal samples which\n"
    "                         begins at a given internal sample (counted from the first frame the\n"
    "                         instance processed) is the first to use it: sample:value pairs\n"
    "  --low-cut <Hz>         Ocean's Low Cut; 20 takes it out of the circuit (default 20)\n"
    "  --high-damping <Hz>    Ocean's High Damping; 20000 takes it out of the circuit (default 20000)\n"
    "  --voice-seed <n>       seed of the voice phase\n"
    "  --voice-phase <file>   phase of the first voice of the left and of the right output at the\n"
    "                         start of each block of 44 internal samples, counted from the first\n"
    "                         warm-up frame: pairs of 64-bit floats in cycles, little-endian, in\n"
    "                         place of the engine's own phase\n"
    "\n"
    "  --layer <name>         what Macro drives: tide (default), undertow or spume\n"
    "\n"
    "  the host of the Undertow layer; frames count from the first one the instance processed:\n"
    "  --tempo <bpm>          tempo the host reports (default: none)\n"
    "  --stopped              the transport stands (default: it runs)\n"
    "  --playhead-frames <n>  reported position less processed frames (default 0)\n"
    "  --host-block <n>       frames per host block (default 512)\n"
    "  --block-starts <list>  frames at which the host began a new run of blocks, separated by\n"
    "                         commas (default: the first frame rendered)\n"
    "  --first-frame <n>      frames the instance processed in front of the first warm-up frame\n"
    "  --reference-arithmetic the chunk clock in the reference's arithmetic\n"
    "  --oscillator-origin <n>  internal sample at which the line oscillators were at their start\n"
    "  --phasor-origin <n>    internal sample two in front of which the grain phasors were at theirs\n"
    "  --free-run-origin <n>  internal sample at which the clock of a stopped transport read zero\n"
    "\n"
    "  outer laws of the reference, applied in this order when given:\n"
    "  --level-stage          the level stage, keyed by the input\n"
    "  --width <0..2>         Width, 1 = 100 %\n"
    "  --mix <0..1>           Mix with the input\n"
    "  --clip                 the output clipper\n"
    "\n"
    "  what the plug-in applies in place of --mix:\n"
    "  --ocean-mix <0..1>     Ocean's own Mix, dry + mix (wet - dry) with the dry signal bounded\n"
    "                         at 4; at 1 the wet as it is\n";

struct Options
{
    std::string input;
    std::string output;
    long long warmupFrames = 0;
    bool idleWarmup = false;
    FathomEngine::Parameters parameters;
    std::vector<std::pair<long long, float>> macroSteps;
    std::vector<std::pair<long long, float>> macroAtBlocks;
    std::uint64_t voiceSeed = FathomEngine::defaultVoiceSeed;
    std::string voicePhase;
    FathomEngine::Layer layer = FathomEngine::Layer::tide;
    bool hasTempo = false;
    double tempo = 120.0;
    bool playing = true;
    long long playheadFrames = 0;
    long long hostBlock = 512;
    std::vector<long long> blockStarts;
    bool referenceArithmetic = false;
    FathomEngine::ClockOrigins origins;
    bool levelStage = false;
    bool applyWidth = false;
    float width = 1.0f;
    bool applyMix = false;
    float mix = 1.0f;
    bool applyOceanMix = false;
    float oceanMix = 1.0f;
    bool clip = false;
    bool benchmark = false;
};

struct Audio
{
    int sampleRate = 0;
    std::vector<float> samples;   // left and right interleaved
};

[[nodiscard]] float parseFloat(const std::string& name, const char* text)
{
    char* end = nullptr;
    const auto value = std::strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(value))
        throw std::runtime_error(name + " needs a number, got '" + text + "'");
    return static_cast<float>(value);
}

[[nodiscard]] unsigned long long parseCount(const std::string& name, const char* text)
{
    char* end = nullptr;
    const auto value = std::strtoull(text, &end, 0);
    if (end == text || *end != '\0' || text[0] == '-')
        throw std::runtime_error(name + " needs a whole number, got '" + text + "'");
    return value;
}

[[nodiscard]] long long parseFrames(const std::string& name, const char* text)
{
    char* end = nullptr;
    const auto value = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0')
        throw std::runtime_error(name + " needs a whole number, got '" + text + "'");
    return value;
}

[[nodiscard]] std::vector<long long> parseFrameList(const std::string& name, const char* text)
{
    std::vector<long long> frames;
    const std::string list = text;
    for (std::size_t begin = 0; begin <= list.size();)
    {
        const auto comma = std::min(list.find(',', begin), list.size());
        frames.push_back(parseFrames(name, list.substr(begin, comma - begin).c_str()));
        begin = comma + 1;
    }
    std::sort(frames.begin(), frames.end());
    return frames;
}

[[nodiscard]] std::vector<std::pair<long long, float>> parseMacroSteps(const std::string& name,
                                                                      const char* text)
{
    std::vector<std::pair<long long, float>> steps;
    const std::string list = text;
    for (std::size_t begin = 0; begin <= list.size();)
    {
        const auto comma = std::min(list.find(',', begin), list.size());
        const auto pair = list.substr(begin, comma - begin);
        const auto colon = pair.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error(name + " needs frame:value pairs, got '" + pair + "'");
        steps.emplace_back(parseFrames(name, pair.substr(0, colon).c_str()),
                           parseFloat(name, pair.substr(colon + 1).c_str()));
        begin = comma + 1;
    }
    std::sort(steps.begin(), steps.end());
    return steps;
}

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
    Options options;
    for (auto index = 1; index < argc; ++index)
    {
        const std::string name = argv[index];
        const auto value = [&]() -> const char*
        {
            if (index + 1 >= argc)
                throw std::runtime_error(name + " needs a value");
            return argv[++index];
        };

        if (name == "--input")
            options.input = value();
        else if (name == "--output")
            options.output = value();
        else if (name == "--warmup-frames")
            options.warmupFrames = static_cast<long long>(parseCount(name, value()));
        else if (name == "--idle-warmup")
            options.idleWarmup = true;
        else if (name == "--decay")
            options.parameters.decaySeconds = parseFloat(name, value());
        else if (name == "--size")
            options.parameters.sizeScale = parseFloat(name, value());
        else if (name == "--predelay")
            options.parameters.preDelaySeconds = parseFloat(name, value());
        else if (name == "--macro")
            options.parameters.macro = parseFloat(name, value());
        else if (name == "--macro-steps")
            options.macroSteps = parseMacroSteps(name, value());
        else if (name == "--macro-at-blocks")
            options.macroAtBlocks = parseMacroSteps(name, value());
        else if (name == "--low-cut")
            options.parameters.lowCutHz = parseFloat(name, value());
        else if (name == "--high-damping")
            options.parameters.highDampingHz = parseFloat(name, value());
        else if (name == "--voice-seed")
            options.voiceSeed = parseCount(name, value());
        else if (name == "--voice-phase")
            options.voicePhase = value();
        else if (name == "--layer")
        {
            const std::string layer = value();
            if (layer != "tide" && layer != "undertow" && layer != "spume")
                throw std::runtime_error("--layer is tide, undertow or spume, got '" + layer + "'");
            options.layer = layer == "undertow" ? FathomEngine::Layer::undertow
                          : layer == "spume" ? FathomEngine::Layer::spume
                                             : FathomEngine::Layer::tide;
        }
        else if (name == "--tempo")
        {
            options.tempo = static_cast<double>(parseFloat(name, value()));
            options.hasTempo = true;
        }
        else if (name == "--stopped")
            options.playing = false;
        else if (name == "--playhead-frames")
            options.playheadFrames = parseFrames(name, value());
        else if (name == "--host-block")
            options.hostBlock = std::max(1LL, parseFrames(name, value()));
        else if (name == "--block-starts")
            options.blockStarts = parseFrameList(name, value());
        else if (name == "--first-frame")
            options.origins.firstFrame = parseFrames(name, value());
        else if (name == "--reference-arithmetic")
            options.referenceArithmetic = true;
        else if (name == "--oscillator-origin")
            options.origins.oscillators = parseFrames(name, value());
        else if (name == "--phasor-origin")
            options.origins.phasors = parseFrames(name, value());
        else if (name == "--free-run-origin")
            options.origins.freeRun = parseFrames(name, value());
        else if (name == "--level-stage")
            options.levelStage = true;
        else if (name == "--width")
        {
            options.applyWidth = true;
            options.width = parseFloat(name, value());
        }
        else if (name == "--mix")
        {
            options.applyMix = true;
            options.mix = parseFloat(name, value());
        }
        else if (name == "--ocean-mix")
        {
            options.applyOceanMix = true;
            options.oceanMix = parseFloat(name, value());
        }
        else if (name == "--clip")
            options.clip = true;
        else if (name == "--benchmark")
            options.benchmark = true;
        else
            throw std::runtime_error("unknown option " + name);
    }

    if (!options.benchmark && (options.input.empty() || options.output.empty()))
        throw std::runtime_error("--input and --output are required");
    if (options.applyMix && options.applyOceanMix)
        throw std::runtime_error("--mix and --ocean-mix are two laws for one stage");
    return options;
}

// Ocean's Mix of one channel as the plug-in forms it for every Character
// (FDNReverb::processSample): the dry signal, under the bound of the FDN's
// input, crossfaded linearly into the wet. At 100 % the wet passes as it is.
[[nodiscard]] float oceanMix(float input, float wet, float mix) noexcept
{
    if (mix >= 1.0f)
        return wet;

    const auto dry = std::isfinite(input) ? std::clamp(input, -4.0f, 4.0f) : 0.0f;
    return dry + mix * (wet - dry);
}

[[nodiscard]] std::uint32_t readLittleEndian(const std::vector<char>& bytes, std::size_t offset,
                                             std::size_t count)
{
    if (offset + count > bytes.size())
        throw std::runtime_error("the WAV file ends inside a chunk");
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < count; ++byte)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + byte])) << (8 * byte);
    return value;
}

[[nodiscard]] Audio readWav(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open " + path);
    const std::vector<char> bytes { std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>() };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0
        || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        throw std::runtime_error(path + " is not a WAV file");

    constexpr std::uint32_t floatFormat = 3;
    constexpr std::uint32_t extensibleFormat = 0xfffe;
    Audio audio;
    auto formatFound = false;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();)
    {
        const auto size = static_cast<std::size_t>(readLittleEndian(bytes, offset + 4, 4));
        const auto body = offset + 8;
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0)
        {
            auto format = readLittleEndian(bytes, body, 2);
            // The extensible format names the sample type in the first two
            // bytes of its sub-format.
            if (format == extensibleFormat && size >= 26)
                format = readLittleEndian(bytes, body + 24, 2);
            if (format != floatFormat || readLittleEndian(bytes, body + 2, 2) != 2
                || readLittleEndian(bytes, body + 14, 2) != 32)
                throw std::runtime_error(path + " is not stereo 32-bit float");
            audio.sampleRate = static_cast<int>(readLittleEndian(bytes, body + 4, 4));
            formatFound = true;
        }
        else if (std::memcmp(bytes.data() + offset, "data", 4) == 0)
        {
            if (!formatFound)
                throw std::runtime_error(path + " has its samples in front of its format");
            const auto available = std::min(size, bytes.size() - body);
            audio.samples.resize(available / (2 * sizeof(float)) * 2);
            std::memcpy(audio.samples.data(), bytes.data() + body,
                        audio.samples.size() * sizeof(float));
            return audio;
        }
        offset = body + size + (size & 1);
    }
    throw std::runtime_error(path + " holds no samples");
}

void writeLittleEndian(std::ofstream& stream, std::uint32_t value, int count)
{
    for (auto byte = 0; byte < count; ++byte)
        stream.put(static_cast<char>((value >> (8 * byte)) & 0xffu));
}

void writeWav(const std::string& path, const Audio& audio)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot write " + path);

    constexpr std::uint32_t channels = 2;
    constexpr std::uint32_t bytesPerSample = sizeof(float);
    const auto dataBytes = static_cast<std::uint32_t>(audio.samples.size() * bytesPerSample);
    const auto sampleRate = static_cast<std::uint32_t>(audio.sampleRate);
    stream.write("RIFF", 4);
    writeLittleEndian(stream, 36 + dataBytes, 4);
    stream.write("WAVEfmt ", 8);
    writeLittleEndian(stream, 16, 4);
    writeLittleEndian(stream, 3, 2);
    writeLittleEndian(stream, channels, 2);
    writeLittleEndian(stream, sampleRate, 4);
    writeLittleEndian(stream, sampleRate * channels * bytesPerSample, 4);
    writeLittleEndian(stream, channels * bytesPerSample, 2);
    writeLittleEndian(stream, 8 * bytesPerSample, 2);
    stream.write("data", 4);
    writeLittleEndian(stream, dataBytes, 4);
    stream.write(reinterpret_cast<const char*>(audio.samples.data()),
                 static_cast<std::streamsize>(dataBytes));
    if (!stream)
        throw std::runtime_error("cannot write " + path);
}

// A prescribed voice phase, one value per block for each output.
struct VoicePhase
{
    std::vector<double> left;
    std::vector<double> right;
};

[[nodiscard]] VoicePhase readVoicePhase(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open " + path);
    const std::vector<char> bytes { std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>() };
    constexpr auto pairBytes = 2 * sizeof(double);
    if (bytes.empty() || bytes.size() % pairBytes != 0)
        throw std::runtime_error(path + " does not hold pairs of 64-bit floats");

    VoicePhase phase;
    phase.left.resize(bytes.size() / pairBytes);
    phase.right.resize(phase.left.size());
    for (std::size_t block = 0; block < phase.left.size(); ++block)
    {
        std::memcpy(&phase.left[block], bytes.data() + block * pairBytes, sizeof(double));
        std::memcpy(&phase.right[block], bytes.data() + block * pairBytes + sizeof(double),
                    sizeof(double));
        if (!std::isfinite(phase.left[block]) || !std::isfinite(phase.right[block]))
            throw std::runtime_error(path + " holds a phase that is not a number");
    }
    return phase;
}

// The host of the Undertow layer: blocks of a fixed length that begin anew at
// given frames, and the position it reports at the first frame of each. In
// front of the first of those frames the grid of that one runs backwards.
class Host
{
public:
    Host(const Options& options, int sampleRate)
        : options_(options),
          sampleRate_(static_cast<double>(sampleRate)),
          starts_(options.blockStarts)
    {
        if (starts_.empty())
            starts_.push_back(options.origins.firstFrame);
    }

    // What the host says in front of a frame of the instance's count, if a
    // block begins there. The first frame rendered is told of in any case: a
    // render that sets in inside a block takes that frame for the block's first.
    void announce(FathomEngine& engine, long long frame) noexcept
    {
        while (run_ + 1 < starts_.size() && starts_[run_ + 1] <= frame)
            ++run_;
        const auto into = (frame - starts_[run_]) % options_.hostBlock;
        if (into != 0 && announced_)
            return;
        announced_ = true;

        FathomEngine::Transport transport;
        transport.hasTempo = options_.hasTempo;
        transport.bpm = options_.tempo;
        transport.playing = options_.playing;
        // A stopped transport stays where it is.
        const auto reported = options_.playing ? frame + options_.playheadFrames
                                               : options_.playheadFrames;
        const auto seconds = static_cast<double>(reported) / sampleRate_;
        transport.quarterNotes = seconds * options_.tempo / 60.0;
        engine.setTransport(transport);
    }

private:
    const Options& options_;
    double sampleRate_;
    std::vector<long long> starts_;
    std::size_t run_ = 0;
    bool announced_ = false;
};

void render(const Options& options)
{
    auto audio = readWav(options.input);
    VoicePhase voicePhase;
    FathomEngine engine;
    FathomEngine::LevelStage levelStage;
    engine.setParameters(options.parameters);
    engine.setVoiceSeed(options.voiceSeed);
    if (!options.voicePhase.empty())
    {
        voicePhase = readVoicePhase(options.voicePhase);
        engine.setVoicePhaseForTesting(voicePhase.left.data(), voicePhase.right.data(),
                                       voicePhase.left.size());
    }
    const auto undertow = options.layer == FathomEngine::Layer::undertow;
    engine.setLayer(options.layer);
    engine.setReferenceArithmetic(options.referenceArithmetic);
    engine.setClockOriginsForTesting(options.origins);
    engine.prepare(static_cast<double>(audio.sampleRate));
    levelStage.prepare(static_cast<double>(audio.sampleRate));

    Host host(options, audio.sampleRate);
    auto hostFrame = options.origins.firstFrame;
    auto parameters = options.parameters;
    std::size_t macroStep = 0;
    std::size_t macroBlock = 0;
    long long rendered = 0;
    constexpr long long gainBlockSamples = 44;
    const auto moveMacro = [&]
    {
        for (; macroStep < options.macroSteps.size() && options.macroSteps[macroStep].first <= rendered;
             ++macroStep)
        {
            parameters.macro = options.macroSteps[macroStep].second;
            engine.setParameters(parameters);
        }
        // The layer takes a new Macro at the block that begins next. A value
        // meant for the block at a given sample is therefore handed over once
        // the block in front of it has begun.
        for (; macroBlock < options.macroAtBlocks.size()
               && engine.nextCoreSampleForTesting()
                      > options.macroAtBlocks[macroBlock].first - gainBlockSamples;
             ++macroBlock)
        {
            parameters.macro = options.macroAtBlocks[macroBlock].second;
            engine.setParameters(parameters);
        }
        ++rendered;
    };
    for (long long frame = 0; frame < options.warmupFrames; ++frame)
    {
        moveMacro();
        if (undertow)
            host.announce(engine, hostFrame++);
        if (options.idleWarmup)
            engine.advanceIdle();
        else
            static_cast<void>(engine.processSample(0.0f, 0.0f));
    }

    auto dryGain = 0.0f;
    auto wetGain = 1.0f;
    if (options.applyMix)
        FathomEngine::mixGains(options.mix, dryGain, wetGain);
    const auto oceanMixAmount = std::clamp(options.oceanMix, 0.0f, 1.0f);
    for (std::size_t index = 0; index + 1 < audio.samples.size(); index += 2)
    {
        const auto left = audio.samples[index];
        const auto right = audio.samples[index + 1];
        moveMacro();
        if (undertow)
            host.announce(engine, hostFrame++);
        auto wet = engine.processSample(left, right);
        if (options.levelStage)
            wet = levelStage.process(left, right, wet);
        if (options.applyWidth)
            wet = FathomEngine::applyWidth(wet, options.width);
        if (options.applyMix)
        {
            wet.left = FathomEngine::mix(dryGain, left, wetGain, wet.left);
            wet.right = FathomEngine::mix(dryGain, right, wetGain, wet.right);
        }
        if (options.applyOceanMix)
        {
            wet.left = oceanMix(left, wet.left, oceanMixAmount);
            wet.right = oceanMix(right, wet.right, oceanMixAmount);
        }
        if (options.clip)
        {
            wet.left = FathomEngine::clip(wet.left);
            wet.right = FathomEngine::clip(wet.right);
        }
        audio.samples[index] = wet.left;
        audio.samples[index + 1] = wet.right;
    }
    writeWav(options.output, audio);
}

// Time of processSample on noise and of advanceIdle at the given parameters,
// as the share of one core a stereo stream takes.
void benchmark(const Options& options)
{
    constexpr std::array<int, 3> sampleRates { 44100, 48000, 96000 };
    constexpr auto seconds = 20;
    for (const auto sampleRate : sampleRates)
    {
        FathomEngine engine;
        engine.setParameters(options.parameters);
        engine.setVoiceSeed(options.voiceSeed);
        const auto undertow = options.layer == FathomEngine::Layer::undertow;
        engine.setLayer(options.layer);
        engine.setReferenceArithmetic(options.referenceArithmetic);
        engine.prepare(static_cast<double>(sampleRate));
        Host host(options, sampleRate);
        std::uint32_t noiseState = 0x0ebbbe7cu;
        auto checksum = 0.0;
        const auto frames = static_cast<long long>(sampleRate) * seconds;
        const auto started = std::chrono::steady_clock::now();
        for (long long frame = 0; frame < frames; ++frame)
        {
            if (undertow)
                host.announce(engine, frame);
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto left = 0.25f * static_cast<float>(static_cast<std::int32_t>(noiseState))
                            / 2147483648.0f;
            noiseState = noiseState * 1664525u + 1013904223u;
            const auto right = 0.25f * static_cast<float>(static_cast<std::int32_t>(noiseState))
                             / 2147483648.0f;
            const auto wet = engine.processSample(left, right);
            checksum += static_cast<double>(wet.left) + static_cast<double>(wet.right);
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;

        const auto idleStarted = std::chrono::steady_clock::now();
        for (long long frame = 0; frame < frames; ++frame)
            engine.advanceIdle();
        const std::chrono::duration<double> idleElapsed = std::chrono::steady_clock::now() - idleStarted;

        std::cout << sampleRate << " Hz: processSample " << 1.0e9 * elapsed.count() / static_cast<double>(frames)
                  << " ns per frame, " << 100.0 * elapsed.count() / seconds
                  << " % of one core; advanceIdle "
                  << 1.0e9 * idleElapsed.count() / static_cast<double>(frames) << " ns per frame, "
                  << 100.0 * idleElapsed.count() / seconds << " % (checksum " << checksum << ")\n";
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto options = parseOptions(argc, argv);
        if (options.benchmark)
            benchmark(options);
        else
            render(options);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "AmanitaOceanFathomRender: " << error.what() << '\n' << usage;
        return 1;
    }
}
