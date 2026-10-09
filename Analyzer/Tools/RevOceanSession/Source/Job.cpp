#include "Job.h"

#include <cmath>
#include <limits>

namespace revocean
{
namespace
{

bool isNumber (const juce::var& value)
{
    return value.isInt() || value.isInt64() || value.isDouble();
}

bool isWholeNumber (const juce::var& value)
{
    return value.isInt() || value.isInt64();
}

juce::Result parseFile (const juce::File& file, const juce::String& schema, juce::var& root)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("no file " + file.getFullPathName());

    if (const auto parsed = juce::JSON::parse (file.loadFileAsString(), root); parsed.failed())
        return juce::Result::fail (file.getFileName() + ": " + parsed.getErrorMessage());

    if (root["schema"].toString() != schema)
        return juce::Result::fail (file.getFileName() + " is not of schema " + schema);

    return juce::Result::ok();
}

bool isFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;

    return true;
}

/** A stimulus is stereo 32-bit float at the session's rate, as the campaign's captures are. */
juce::Result readStimulus (const juce::File& file, const Config& config, juce::AudioBuffer<float>& buffer)
{
    auto stream = file.createInputStream();
    if (stream == nullptr)
        return juce::Result::fail ("cannot open " + file.getFullPathName());

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (stream.release(), true));
    if (reader == nullptr)
        return juce::Result::fail (file.getFileName() + " is not a WAV file");

    if (reader->numChannels != 2 || ! reader->usesFloatingPointData || reader->bitsPerSample != 32
        || std::abs (reader->sampleRate - config.sampleRate) > 0.01
        || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
        return juce::Result::fail (file.getFileName() + " must be a stereo float32 WAV at the session's sample rate");

    buffer.setSize (2, static_cast<int> (reader->lengthInSamples));
    if (! reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true))
        return juce::Result::fail ("cannot decode " + file.getFileName());

    if (! isFinite (buffer))
        return juce::Result::fail (file.getFileName() + " contains NaN or infinity");

    return juce::Result::ok();
}

juce::Result readStep (const juce::var& entry, const juce::File& jobFile, const Config& config, Step& step)
{
    const auto kind = entry["kind"].toString();

    if (kind == "set")
    {
        step.kind = Step::Kind::set;
        const auto* values = entry["parameters"].getArray();
        if (values == nullptr || values->isEmpty())
            return juce::Result::fail ("a set step needs a list of parameters");

        for (const auto& value : *values)
        {
            if (! isWholeNumber (value["id"]) || ! isNumber (value["value"]))
                return juce::Result::fail ("a parameter needs a whole-number id and a value");

            const auto normalised = static_cast<double> (value["value"]);
            if (! (normalised >= 0.0 && normalised <= 1.0))
                return juce::Result::fail ("parameter " + value["id"].toString() + " is not a normalised value");

            step.parameters.push_back ({ value["id"].toString(), static_cast<float> (normalised) });
        }

        return juce::Result::ok();
    }

    if (! entry["record"].isVoid() && ! entry["record"].isBool())
        return juce::Result::fail ("record must be true or false");
    step.record = static_cast<bool> (entry["record"]);

    if (kind == "silence")
    {
        step.kind = Step::Kind::silence;
        if (! isWholeNumber (entry["frames"]) || static_cast<juce::int64> (entry["frames"]) <= 0)
            return juce::Result::fail ("a silence step needs a positive whole number of frames");

        // One hour of audio is more than a demo instance can usefully spend on one step; a larger number
        // is a mistake of the caller, and a running step can only be ended by stopping the session.
        step.frames = static_cast<juce::int64> (entry["frames"]);
        if (step.frames > static_cast<juce::int64> (3600.0 * config.sampleRate))
            return juce::Result::fail ("a silence step may last one hour of audio at most");

        return juce::Result::ok();
    }

    if (kind == "wav")
    {
        step.kind = Step::Kind::wav;
        step.file = entry["file"].toString();
        if (step.file.isEmpty())
            return juce::Result::fail ("a wav step needs a file");

        const auto read = readStimulus (jobFile.getSiblingFile (step.file), config, step.input);
        step.frames = step.input.getNumSamples();
        return read;
    }

    return juce::Result::fail ("unknown kind of step: " + kind);
}

} // namespace

juce::Result Config::read (const juce::File& folder, Config& config)
{
    juce::var root;
    if (const auto parsed = parseFile (folder.getChildFile ("session.json"), "revocean.session/1", root); parsed.failed())
        return parsed;

    const auto plugin = root["plugin"];
    config.folder = folder;
    config.pluginBundle = juce::File::isAbsolutePath (plugin["path"].toString()) ? juce::File (plugin["path"].toString())
                                                                                  : juce::File();
    config.name = plugin["name"].toString();
    config.manufacturer = plugin["manufacturer"].toString();
    config.version = plugin["version"].toString();
    config.binarySha256 = plugin["binarySha256"].toString();
    config.title = root["title"].toString();
    config.instruction = root["instruction"].toString();

    if (config.pluginBundle == juce::File() || config.name.isEmpty() || config.manufacturer.isEmpty()
        || config.version.isEmpty() || config.binarySha256.isEmpty() || config.title.isEmpty()
        || config.instruction.isEmpty())
        return juce::Result::fail ("session.json needs plugin.path (absolute), plugin.name, plugin.manufacturer, "
                                   "plugin.version, plugin.binarySha256, title and instruction");

    if (! isWholeNumber (root["sampleRate"]) || ! isWholeNumber (root["blockSize"]))
        return juce::Result::fail ("session.json needs whole numbers sampleRate and blockSize");

    config.sampleRate = static_cast<double> (root["sampleRate"]);
    config.blockSize = static_cast<int> (root["blockSize"]);
    if (config.sampleRate < 8000.0 || config.sampleRate > 768000.0 || config.blockSize < 1 || config.blockSize > 16384)
        return juce::Result::fail ("sampleRate must be 8000 to 768000 and blockSize 1 to 16384");

    for (const auto& [key, target] : { std::pair { "stopAfterMinutes", &config.stopAfterMinutes },
                                       std::pair { "idleSeconds", &config.idleSeconds } })
    {
        if (root[key].isVoid())
            continue;

        if (! isNumber (root[key]) || static_cast<double> (root[key]) < 0.0)
            return juce::Result::fail (juce::String (key) + " must be a number of zero or more");

        *target = static_cast<double> (root[key]);
    }

    if (config.stopAfterMinutes <= 0.0)
        return juce::Result::fail ("stopAfterMinutes must be above zero");

    // Optional: what the play head tells the plug-in. The defaults are those of every session so far.
    if (! root["bpm"].isVoid())
    {
        if (! isNumber (root["bpm"]) || static_cast<double> (root["bpm"]) < 20.0 || static_cast<double> (root["bpm"]) > 999.0)
            return juce::Result::fail ("bpm must be a number from 20 to 999");

        config.bpm = static_cast<double> (root["bpm"]);
    }

    if (! root["transportOffsetSeconds"].isVoid())
    {
        if (! isNumber (root["transportOffsetSeconds"]) || static_cast<double> (root["transportOffsetSeconds"]) < 0.0
            || static_cast<double> (root["transportOffsetSeconds"]) > 86400.0)
            return juce::Result::fail ("transportOffsetSeconds must be a number from 0 to 86400");

        config.transportOffsetSeconds = static_cast<double> (root["transportOffsetSeconds"]);
    }

    if (! root["transportPlaying"].isVoid())
    {
        if (! root["transportPlaying"].isBool())
            return juce::Result::fail ("transportPlaying must be true or false");

        config.transportPlaying = static_cast<bool> (root["transportPlaying"]);
    }

    return juce::Result::ok();
}

juce::String Step::kindName() const
{
    return kind == Kind::set ? "set" : kind == Kind::silence ? "silence" : "wav";
}

juce::Result Job::read (const juce::File& file, const Config& config, Job& job)
{
    juce::var root;
    if (const auto parsed = parseFile (file, "revocean.session.job/1", root); parsed.failed())
        return parsed;

    const auto* steps = root["steps"].getArray();
    if (steps == nullptr)
        return juce::Result::fail ("a job needs a list of steps");

    if (! root["quit"].isVoid() && ! root["quit"].isBool())
        return juce::Result::fail ("quit must be true or false");
    job.quit = static_cast<bool> (root["quit"]);

    for (int index = 0; index < steps->size(); ++index)
    {
        Step step;
        if (const auto read = readStep (steps->getReference (index), file, config, step); read.failed())
            return juce::Result::fail ("step " + juce::String (index) + ": " + read.getErrorMessage());

        job.steps.push_back (std::move (step));
    }

    return juce::Result::ok();
}

} // namespace revocean
