#pragma once

#include "Job.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>

namespace revocean
{

/** One instance of the reference plug-in and everything that is done to it.

    The instance is hosted the way a DAW hosts it and the way the campaign's Analyzer does: stereo main
    buses, offline mode, prepareToPlay, reset, then blocks of the session's block size with a play head
    at 120 BPM and no MIDI. Nothing is processed before `start`, so the instance's own sample clock is
    at zero then; `framesProcessed` counts every frame it has been given since.

    `open`, `createEditor`, `start` and `close` belong to the message thread. Jobs run on a worker
    thread; parameters are set on the message thread, between two blocks.
*/
class Session final : private juce::Thread
{
public:
    explicit Session (Config configToUse);
    ~Session() override;

    /** Loads the plug-in, checks that it is the pinned reference and prepares one instance. */
    juce::Result open();

    /** Records why the session cannot go on and returns that as a failed result. */
    juce::Result fail (const juce::String& error);

    juce::AudioProcessorEditor* createEditor();

    /** Once the window with the editor is on screen: records the session as waiting for Start. */
    void windowShown (int numberOfTheWindow);

    /** Starts running the jobs of the queue folder. */
    void start();
    bool hasStarted() const                  { return started; }

    /** Ends the session; the first reason given is the one recorded. */
    void requestStop (const juce::String& reason);
    juce::String stopReason() const;
    double secondsSinceStopRequest() const;

    /** After the worker has ended or was never started: frees the instance and completes the record. */
    void close();

    /** Last resort when the plug-in does not return: records the reason and ends the process. */
    [[noreturn]] void abandon (const juce::String& reason);

    const Config& getConfig() const          { return config; }
    juce::Time createdAt() const             { return created; }
    double secondsSinceCreation() const;
    juce::int64 framesProcessed() const      { return frames; }
    juce::String phase() const;

    /** Called on the message thread when the worker has ended. */
    std::function<void()> onWorkerFinished;

private:
    /** 4/4, 120 BPM unless the session asks for another tempo; the position is the number of frames the
        instance has processed, plus the session's transport offset. A session may also ask for a stopped
        transport: then the play head says "not playing" and its position stays at the offset. */
    struct PlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override;

        double sampleRate = 0.0, bpm = 120.0;
        juce::int64 position = 0, offset = 0;
        bool playing = true;
    };

    void run() override;
    juce::File nextJobFile() const;
    void runJob (const juce::File& file);
    juce::Result checkParameters (const Job& job) const;
    juce::Result setParameters (const Step& step, juce::var& entry);
    juce::Result process (const Step& step, const juce::File& recording, juce::var& entry);

    void setPhase (const juce::String& text);
    bool hasFailed() const;
    juce::var describe() const;
    void writeRecord (const juce::String& status);

    const Config config;
    juce::VST3PluginFormat vst3Format;
    juce::AudioUnitPluginFormat audioUnitFormat;   // for a bundle that ends in .component
    juce::PluginDescription description;
    juce::String binarySha256;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::map<juce::String, juce::AudioProcessorParameter*> parameters;   // by the format's parameter ID
    PlayHead playHead;

    juce::Time created, startTime;
    double createdTicks;
    int windowNumber = 0;
    std::atomic<bool> started { false }, stopRequested { false };
    std::atomic<juce::int64> frames { 0 };
    std::atomic<int> latency { 0 };   // as the plug-in reported it after preparing and after the last job

    juce::CriticalSection lock;       // guards what follows
    juce::String currentPhase, reasonToStop, failure;
    double stopRequestTicks = 0.0;
    juce::Array<juce::var> finishedJobs;
};

} // namespace revocean
