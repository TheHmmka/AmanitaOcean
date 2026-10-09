#include "Session.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#include <unistd.h>

namespace revocean
{
namespace
{

constexpr int messageThreadDeadlineMs = 10000;
constexpr int workerDeadlineMs = 10000;

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

void set (juce::var& target, const char* name, const juce::var& value)
{
    target.getDynamicObject()->setProperty (name, value);
}

/** Written to a temporary file and moved into place, so that a reader never sees half a record. */
bool writeJson (const juce::File& file, const juce::var& value)
{
    return file.replaceWithText (juce::JSON::toString (value) + "\n", false, false, "\n");
}

juce::String now()
{
    return juce::Time::getCurrentTime().toISO8601 (true);
}

/** Runs `work` on the message thread and waits for it; false when it has not run by the deadline. */
bool callOnMessageThread (std::function<void()> work)
{
    auto done = std::make_shared<juce::WaitableEvent>();
    juce::MessageManager::callAsync ([task = std::move (work), done]
    {
        task();
        done->signal();
    });
    return done->wait (messageThreadDeadlineMs);
}

/** Stereo in and out on the main buses, every other input bus off. */
juce::Result useStereoMainBuses (juce::AudioPluginInstance& plugin)
{
    auto layout = plugin.getBusesLayout();
    if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty())
        return juce::Result::fail ("the plug-in has no audio input or output bus");

    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
    for (int bus = 1; bus < layout.inputBuses.size(); ++bus)
        layout.inputBuses.set (bus, juce::AudioChannelSet::disabled());

    if (plugin.checkBusesLayoutSupported (layout))
        plugin.setBusesLayout (layout);

    if (plugin.getTotalNumInputChannels() != 2 || plugin.getTotalNumOutputChannels() != 2)
        return juce::Result::fail ("the plug-in did not accept a plain stereo layout");

    return juce::Result::ok();
}

} // namespace

juce::Optional<juce::AudioPlayHead::PositionInfo> Session::PlayHead::getPosition() const
{
    PositionInfo info;
    const auto reported = playing ? position + offset : offset;
    const auto seconds = static_cast<double> (reported) / sampleRate;
    const auto quarters = seconds * bpm / 60.0;

    info.setTimeInSamples (reported);
    info.setTimeInSeconds (seconds);
    info.setBpm (bpm);
    info.setTimeSignature (TimeSignature { 4, 4 });
    info.setPpqPosition (quarters);
    info.setPpqPositionOfLastBarStart (std::floor (quarters / 4.0) * 4.0);
    info.setIsPlaying (playing);
    info.setIsRecording (false);
    info.setIsLooping (false);
    return info;
}

Session::Session (Config configToUse)
    : juce::Thread ("Rev OCEAN session"), config (std::move (configToUse)),
      created (juce::Time::getCurrentTime()), createdTicks (juce::Time::getMillisecondCounterHiRes())
{
    playHead.sampleRate = config.sampleRate;
    playHead.bpm = config.bpm;
    playHead.playing = config.transportPlaying;
    playHead.offset = static_cast<juce::int64> (std::llround (config.transportOffsetSeconds * config.sampleRate));
}

Session::~Session()
{
    close();
}

juce::Result Session::open()
{
    if (! config.queue().createDirectory() || ! config.results().createDirectory())
        return fail ("cannot create the queue and results folders in " + config.folder.getFullPathName());

    const auto binary = config.pluginBundle.getChildFile ("Contents/MacOS")
                                           .getChildFile (config.pluginBundle.getFileNameWithoutExtension());
    if (! binary.existsAsFile())
        return fail ("no plug-in binary at " + binary.getFullPathName());

    binarySha256 = juce::SHA256 (binary).toHexString();
    if (binarySha256 != config.binarySha256)
        return fail ("the plug-in binary has SHA-256 " + binarySha256 + ", the session is pinned to " + config.binarySha256);

    // The demo of the reference may count from the moment its code is loaded, so the session's
    // clock starts in front of the first call that can load it.
    created = juce::Time::getCurrentTime();
    createdTicks = juce::Time::getMillisecondCounterHiRes();

    const auto isWanted = [this] (const juce::PluginDescription& candidate)
    {
        return candidate.name == config.name && candidate.manufacturerName == config.manufacturer
               && candidate.version == config.version;
    };
    const auto notHeld = "the bundle does not hold " + config.name + " " + config.version + " by " + config.manufacturer;
    juce::String error;

    if (config.pluginBundle.hasFileExtension ("component"))
    {
        // An Audio Unit can only be described by an instance, so the one instance of the session
        // is made first and checked afterwards; a scan would make and discard another one.
        description.fileOrIdentifier = config.pluginBundle.getFullPathName();
        description.pluginFormatName = audioUnitFormat.getName();
        plugin = audioUnitFormat.createInstanceFromDescription (description, config.sampleRate, config.blockSize, error);
        if (plugin == nullptr)
            return fail ("the plug-in could not be instantiated: " + error);

        description = plugin->getPluginDescription();
        if (! isWanted (description))
        {
            plugin.reset();
            return fail (notHeld);
        }
    }
    else
    {
        juce::OwnedArray<juce::PluginDescription> found;
        vst3Format.findAllTypesForFile (found, config.pluginBundle.getFullPathName());

        const auto* wanted = std::find_if (found.begin(), found.end(),
                                           [&isWanted] (const juce::PluginDescription* candidate) { return isWanted (*candidate); });
        if (wanted == found.end())
            return fail (notHeld);

        description = **wanted;
        plugin = vst3Format.createInstanceFromDescription (description, config.sampleRate, config.blockSize, error);
        if (plugin == nullptr)
            return fail ("the plug-in could not be instantiated: " + error);
    }

    if (const auto buses = useStereoMainBuses (*plugin); buses.failed())
        return fail (buses.getErrorMessage());

    plugin->setNonRealtime (true);
    plugin->setRateAndBufferSizeDetails (config.sampleRate, config.blockSize);
    plugin->prepareToPlay (config.sampleRate, config.blockSize);
    plugin->reset();
    plugin->setPlayHead (&playHead);
    latency = plugin->getLatencySamples();

    for (int index = 0; index < plugin->getParameters().size(); ++index)
        if (auto* hosted = plugin->getHostedParameter (index))
            parameters[hosted->getParameterID()] = hosted;

    return juce::Result::ok();
}

juce::Result Session::fail (const juce::String& error)
{
    {
        const juce::ScopedLock scope (lock);
        failure = error;
    }

    writeRecord ("failed");
    return juce::Result::fail (error);
}

void Session::windowShown (int numberOfTheWindow)
{
    windowNumber = numberOfTheWindow;
    setPhase ("Waiting for Start.");
    writeRecord ("waiting_for_start");
}

juce::AudioProcessorEditor* Session::createEditor()
{
    return plugin != nullptr ? plugin->createEditorAndMakeActive() : nullptr;
}

void Session::start()
{
    if (plugin == nullptr || started.exchange (true))
        return;

    startTime = juce::Time::getCurrentTime();
    setPhase ("Waiting for a job.");
    writeRecord ("running");
    startThread();
}

void Session::requestStop (const juce::String& reason)
{
    const juce::ScopedLock scope (lock);
    if (reasonToStop.isNotEmpty())
        return;

    reasonToStop = reason;
    stopRequestTicks = juce::Time::getMillisecondCounterHiRes();
    stopRequested = true;
    notify();
}

juce::String Session::stopReason() const
{
    const juce::ScopedLock scope (lock);
    return reasonToStop;
}

double Session::secondsSinceStopRequest() const
{
    const juce::ScopedLock scope (lock);
    return reasonToStop.isEmpty() ? 0.0 : (juce::Time::getMillisecondCounterHiRes() - stopRequestTicks) / 1000.0;
}

void Session::close()
{
    if (plugin == nullptr)
        return;

    requestStop ("closed");
    writeRecord ("stopping");
    stopThread (workerDeadlineMs);
    plugin->setPlayHead (nullptr);
    plugin->releaseResources();
    parameters.clear();
    plugin.reset();
    writeRecord (hasFailed() ? "failed" : "stopped");
}

bool Session::hasFailed() const
{
    const juce::ScopedLock scope (lock);
    return failure.isNotEmpty();
}

void Session::abandon (const juce::String& reason)
{
    requestStop ("error");
    fail (reason);
    std::cerr << "Rev OCEAN session abandoned: " << reason << std::endl;
    juce::Process::terminate();
    std::abort();   // terminate() does not return; this tells the compiler so
}

double Session::secondsSinceCreation() const
{
    return (juce::Time::getMillisecondCounterHiRes() - createdTicks) / 1000.0;
}

juce::String Session::phase() const
{
    const juce::ScopedLock scope (lock);
    return currentPhase;
}

void Session::setPhase (const juce::String& text)
{
    const juce::ScopedLock scope (lock);
    currentPhase = text;
}

//==============================================================================
void Session::run()
{
    auto lastActivity = juce::Time::getMillisecondCounterHiRes();

    while (! stopRequested)
    {
        if (const auto file = nextJobFile(); file != juce::File())
        {
            runJob (file);
            setPhase ("Waiting for a job.");
            lastActivity = juce::Time::getMillisecondCounterHiRes();
        }
        else if (config.idleSeconds > 0.0
                 && juce::Time::getMillisecondCounterHiRes() - lastActivity > config.idleSeconds * 1000.0)
        {
            requestStop ("idle");
        }
        else
        {
            wait (50);
        }
    }

    juce::MessageManager::callAsync ([this]
    {
        if (onWorkerFinished != nullptr)
            onWorkerFinished();
    });
}

/** The job whose file name sorts first; the driver numbers its jobs. */
juce::File Session::nextJobFile() const
{
    auto files = config.queue().findChildFiles (juce::File::findFiles, false, "*.json");
    const auto first = std::min_element (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName() < b.getFileName();
    });
    return first != files.end() ? *first : juce::File();
}

void Session::runJob (const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension();
    const auto folder = config.results().getChildFile (name);
    setPhase ("Running job " + name + ".");

    Job job;
    juce::Array<juce::var> steps;
    auto status = juce::String ("rejected");
    const auto isNew = ! folder.exists();
    auto outcome = isNew ? folder.createDirectory() : juce::Result::fail ("a job named " + name + " has already run");
    if (outcome.wasOk())
        outcome = Job::read (file, config, job);
    if (outcome.wasOk())
        outcome = checkParameters (job);

    // Out of the queue whatever happens, or the same job would be taken again.
    if (! (isNew ? file.moveFileTo (folder.getChildFile ("job.json")) : file.deleteFile()))
    {
        fail ("cannot take " + file.getFullPathName() + " out of the queue");
        requestStop ("error");
        return;
    }

    if (outcome.wasOk())
    {
        status = "ok";

        for (size_t index = 0; index < job.steps.size() && outcome.wasOk(); ++index)
        {
            const auto& step = job.steps[index];
            const auto begun = juce::Time::getMillisecondCounterHiRes();
            auto entry = object();
            set (entry, "index", static_cast<int> (index));
            set (entry, "kind", step.kindName());
            set (entry, "firstFrame", frames.load());
            set (entry, "startedAt", now());
            set (entry, "secondsSinceCreation", secondsSinceCreation());

            outcome = step.kind == Step::Kind::set
                          ? setParameters (step, entry)
                          : process (step, folder.getChildFile ("step-" + juce::String (index).paddedLeft ('0', 3) + ".wav"), entry);

            set (entry, "wallSeconds", (juce::Time::getMillisecondCounterHiRes() - begun) / 1000.0);
            steps.add (entry);
        }

        if (outcome.failed())
            status = "failed";
    }

    latency = plugin->getLatencySamples();
    auto record = describe();
    set (record, "schema", "revocean.session.job-record/1");
    set (record, "job", name);
    set (record, "status", status);
    set (record, "error", outcome.getErrorMessage());
    set (record, "steps", steps);
    set (record, "framesProcessed", frames.load());
    set (record, "finishedAt", now());

    if (isNew && ! writeJson (folder.getChildFile ("record.json"), record))
        status = "failed";

    std::cout << "job " << name << ": " << status
              << (outcome.failed() ? " (" + outcome.getErrorMessage() + ")" : juce::String()) << std::endl;

    {
        const juce::ScopedLock scope (lock);
        auto finished = object();
        set (finished, "name", name);
        set (finished, "status", status);
        finishedJobs.add (finished);
    }

    if (status == "ok" && job.quit)
        requestStop ("quit");

    writeRecord ("running");
}

juce::Result Session::checkParameters (const Job& job) const
{
    for (const auto& step : job.steps)
        for (const auto& value : step.parameters)
            if (parameters.count (value.id) == 0)
                return juce::Result::fail ("the plug-in has no parameter with ID " + value.id);

    return juce::Result::ok();
}

/** Sets the step's parameters and reads them back, all on the message thread before the next block:
    the order in which the Analyzer sets the parameters of a case. */
juce::Result Session::setParameters (const Step& step, juce::var& entry)
{
    struct Readback
    {
        float normalised = 0.0f;
        juce::String text;
    };

    auto readbacks = std::make_shared<std::vector<Readback>> (step.parameters.size());

    const auto answered = callOnMessageThread ([this, values = step.parameters, readbacks]
    {
        for (const auto& value : values)
            parameters.at (value.id)->setValue (value.normalised);

        for (size_t index = 0; index < values.size(); ++index)
        {
            const auto* parameter = parameters.at (values[index].id);
            (*readbacks)[index] = { parameter->getValue(), parameter->getCurrentValueAsText() };
        }
    });

    if (! answered)
        abandon ("the message thread did not answer within " + juce::String (messageThreadDeadlineMs / 1000) + " s");

    juce::Array<juce::var> list;
    for (size_t index = 0; index < step.parameters.size(); ++index)
    {
        auto item = object();
        set (item, "id", step.parameters[index].id.getLargeIntValue());
        set (item, "requested", static_cast<double> (step.parameters[index].normalised));
        set (item, "readback", static_cast<double> ((*readbacks)[index].normalised));
        set (item, "text", (*readbacks)[index].text);
        list.add (item);
    }

    set (entry, "frames", 0);
    set (entry, "parameters", list);
    return juce::Result::ok();
}

/** Gives the instance the step's frames in blocks of the session's size, the last one shorter, as the
    Analyzer gives it the warm-up and the stimulus of a case. */
juce::Result Session::process (const Step& step, const juce::File& recording, juce::var& entry)
{
    std::unique_ptr<juce::AudioFormatWriter> writer;

    if (step.record)
    {
        using Options = juce::AudioFormatWriterOptions;
        const auto float32 = Options().withSampleRate (config.sampleRate).withNumChannels (2).withBitsPerSample (32)
                                      .withSampleFormat (Options::SampleFormat::floatingPoint);
        std::unique_ptr<juce::OutputStream> stream = recording.createOutputStream();
        if (stream != nullptr)
            writer = juce::WavAudioFormat().createWriterFor (stream, float32);
        if (writer == nullptr)
            return juce::Result::fail ("cannot write " + recording.getFullPathName());
    }

    juce::AudioBuffer<float> block (2, config.blockSize);
    juce::MidiBuffer midi;
    std::array<double, 2> peak {}, squares {};
    juce::int64 done = 0;
    auto outcome = juce::Result::ok();

    while (done < step.frames && outcome.wasOk())
    {
        if (stopRequested)
        {
            outcome = juce::Result::fail ("the session stopped: " + stopReason());
            break;
        }

        const auto count = static_cast<int> (std::min<juce::int64> (config.blockSize, step.frames - done));
        block.clear();
        if (step.kind == Step::Kind::wav)
            for (int channel = 0; channel < 2; ++channel)
                block.copyFrom (channel, 0, step.input, channel, static_cast<int> (done), count);

        juce::AudioBuffer<float> active (block.getArrayOfWritePointers(), 2, 0, count);
        midi.clear();
        playHead.position = frames;
        plugin->processBlock (active, midi);
        frames += count;
        done += count;

        auto finite = true;
        for (size_t channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < count; ++sample)
            {
                const auto value = static_cast<double> (active.getSample (static_cast<int> (channel), sample));
                finite = finite && std::isfinite (value);
                peak[channel] = std::max (peak[channel], std::abs (value));
                squares[channel] += value * value;
            }

        if (! finite)
            outcome = juce::Result::fail ("the plug-in produced NaN or infinity");
        else if (writer != nullptr && ! writer->writeFromAudioSampleBuffer (active, 0, count))
            outcome = juce::Result::fail ("cannot write " + recording.getFullPathName());
    }

    if (writer != nullptr && ! writer->flush() && outcome.wasOk())
        outcome = juce::Result::fail ("cannot write " + recording.getFullPathName());
    writer.reset();

    const auto mean = [done] (double sum) { return done > 0 ? std::sqrt (sum / static_cast<double> (done)) : 0.0; };
    set (entry, "frames", done);
    set (entry, "outputPeak", juce::Array<juce::var> { peak[0], peak[1] });
    set (entry, "outputRms", juce::Array<juce::var> { mean (squares[0]), mean (squares[1]) });
    if (step.kind == Step::Kind::wav)
        set (entry, "input", step.file);
    if (step.record)
        set (entry, "recorded", recording.getFileName());
    return outcome;
}

//==============================================================================
/** What every record says about the instance. */
juce::var Session::describe() const
{
    auto pluginInfo = object();
    set (pluginInfo, "path", config.pluginBundle.getFullPathName());
    set (pluginInfo, "name", description.name);
    set (pluginInfo, "manufacturer", description.manufacturerName);
    set (pluginInfo, "version", description.version);
    set (pluginInfo, "format", description.pluginFormatName);
    set (pluginInfo, "binarySha256", binarySha256);

    auto root = object();
    set (root, "plugin", pluginInfo);
    set (root, "sampleRate", static_cast<int> (config.sampleRate));
    set (root, "blockSize", config.blockSize);
    set (root, "reportedLatencySamples", latency.load());
    set (root, "bpm", config.bpm);
    set (root, "transportOffsetSeconds", config.transportOffsetSeconds);
    set (root, "transportPlaying", config.transportPlaying);
    set (root, "instanceCreatedAt", created.toISO8601 (true));
    return root;
}

/** The record of the whole session: its state now, and in the end why it stopped. */
void Session::writeRecord (const juce::String& status)
{
    const juce::ScopedLock scope (lock);
    auto root = describe();
    set (root, "schema", "revocean.session.record/1");
    set (root, "status", status);
    set (root, "stopReason", reasonToStop);
    set (root, "error", failure);
    set (root, "processId", static_cast<int> (::getpid()));
    set (root, "windowNumber", windowNumber);
    set (root, "stopAfterMinutes", config.stopAfterMinutes);
    set (root, "writtenAt", now());
    set (root, "secondsSinceCreation", secondsSinceCreation());
    set (root, "framesProcessed", frames.load());
    set (root, "jobs", finishedJobs);
    if (started)
        set (root, "startedAt", startTime.toISO8601 (true));

    if (! writeJson (config.record(), root))
        std::cerr << "cannot write " << config.record().getFullPathName() << std::endl;
}

} // namespace revocean
