#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace revocean
{

/** The demo of the reference lasts this long from the creation of an instance: measured, the plug-in
    handed its input back untouched between 19:55.5 and 20:00.4 (see README.md, "Time"). */
constexpr double demoMinutes = 20.0;

/** A session stops by itself this long after the instance was created unless session.json says otherwise:
    one minute in front of the limit. */
constexpr double defaultStopAfterMinutes = 19.0;

/** What session.json says about the one plug-in instance of a session. */
struct Config
{
    juce::File folder;                 // holds session.json, queue/, results/ and session-record.json
    juce::File pluginBundle;
    juce::String name, manufacturer, version, binarySha256;
    double sampleRate = 0.0;
    int blockSize = 0;
    juce::String title, instruction;
    double stopAfterMinutes = defaultStopAfterMinutes;   // counted from the creation of the instance
    double idleSeconds = 0.0;          // longest wait for the next job; 0 waits until the session stops
    double bpm = 120.0;                // tempo the play head reports
    double transportOffsetSeconds = 0.0;   // the play head's position when the instance has processed nothing
    bool transportPlaying = true;      // false: the play head says stopped and stays at the offset

    juce::File queue() const      { return folder.getChildFile ("queue"); }
    juce::File results() const    { return folder.getChildFile ("results"); }
    juce::File record() const     { return folder.getChildFile ("session-record.json"); }

    static juce::Result read (const juce::File& folder, Config& config);
};

struct ParameterValue
{
    juce::String id;                   // parameter ID of the hosted format in decimal
    float normalised = 0.0f;
};

struct Step
{
    enum class Kind { set, silence, wav };

    Kind kind = Kind::silence;
    std::vector<ParameterValue> parameters;   // set
    juce::int64 frames = 0;                   // silence, wav
    juce::String file;                        // wav: the name the job gives
    juce::AudioBuffer<float> input;           // wav
    bool record = false;                      // silence, wav

    juce::String kindName() const;
};

struct Job
{
    std::vector<Step> steps;
    bool quit = false;

    /** Reads and checks a whole job, its stimuli included, so that a job runs completely or not at all. */
    static juce::Result read (const juce::File& file, const Config& config, Job& job);
};

} // namespace revocean
