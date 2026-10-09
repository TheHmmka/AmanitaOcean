#pragma once

#include "FathomConverter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amanita::dsp
{
// The layer the Undertow Character puts in front of Fathom's network: three
// voices made from the converter's output, per input channel, and added to it.
//
//     u --+--------------------------------------------------------(+)--> network
//         |                                                         ^
//         +-> reversed, 4/3 quarter notes ------------> gain ------>|   at pitch
//         +-> reversed, 2 -> recirculation -> gain -> grain reader ->|   octave up
//         +-> reversed, 8/3 ---------------> gain -> grain reader ->|   octave down
//
// A reversed reader plays the input backwards in chunks of a note value: each
// chunk mirrors the input about the point where it begins. The chunks follow
// the host's position while its transport runs and a clock of the layer's own
// while it stands. Everything runs at the engine's internal rate; sample k of
// the layer's clocks is the k-th call of process() or idle() after reset().
//
// What follows the reference is in UndertowConstants.h with its source. What
// the reference was never seen to do is Ocean's own and marked as such in
// UndertowLayer.cpp: a transport that starts, stops, jumps or changes its
// tempo, a position that tells nothing, a host without a tempo, and tempi
// outside 20 to 999 BPM.
class UndertowLayer
{
public:
    static constexpr std::size_t channelCount = 2;

    enum Voice : std::size_t
    {
        unison,
        octaveUp,
        octaveDown,
        voiceCount
    };

    // What the host says at the first frame of a block.
    struct Transport
    {
        double quarterNotes = 0.0;   // position of the block's first frame
        double bpm = 120.0;          // read only when hasTempo
        bool playing = false;        // false: the position is not used
        bool hasTempo = false;
    };

    // Where the clocks of an instance stand at its first frame, for a render
    // that takes up the reference in the middle of a session. All zero is a
    // fresh instance.
    struct ClockOrigins
    {
        // Host frames processed in front of the first. Internal samples are
        // counted from the same moment, so this must be a multiple of the
        // lattice's internal step.
        std::int64_t firstFrame = 0;
        // Internal sample `phasorLeadSamples` in front of which the grain
        // phasors stood at their start value.
        std::int64_t phasors = 0;
        // Internal sample at which the free-running clock read zero.
        std::int64_t freeRun = 0;
    };

    // The chunk a reversed reader was given last: its number on the reader's
    // grid of note values and the sum of output index and read index of its
    // reversed read, twice its mirror point. For the tests.
    struct ChunkStart
    {
        std::int64_t index = 0;
        double mirrorSum = 0.0;
        std::int64_t count = 0;   // chunks given since reset()
    };

    // Allocates; everything below is allocation-free and safe on the audio thread.
    void prepare(const FathomRateLattice& lattice, int hostRate);
    // Clears every signal and returns the clocks to the first frame.
    void reset() noexcept;
    // Forgets the signal and takes the gains as they are; the clocks keep
    // their place.
    void silence() noexcept;

    // Macro, 0 to 1. Each voice gain moves to its new value through a one-pole
    // unless `atOnce`.
    void setMacro(double macro, bool atOnce) noexcept;
    // Position and tempo at the next host frame.
    void setTransport(const Transport& transport) noexcept;

    // Test hooks; both take effect at the next reset(). In the reference's
    // arithmetic the chunk clock reads the host's position as the reference
    // does: in single precision, once per host block and per converter block.
    void setReferenceArithmetic(bool reference) noexcept;
    void setClockOrigins(const ClockOrigins& origins) noexcept;

    // One host frame of time, in front of the internal samples it brings.
    void hostFrame() noexcept;
    // What the layer adds to one internal sample of each input.
    void process(double left, double right, double& addedLeft, double& addedRight) noexcept;
    // One internal sample of time without signal.
    void idle() noexcept;

    // True while the layer adds nothing whatever its input: every gain rests
    // at zero and the grain readers hold nothing.
    [[nodiscard]] bool atRest() const noexcept;
    // Internal samples in front of the first frame.
    [[nodiscard]] std::int64_t firstSample() const noexcept { return firstSample_; }
    [[nodiscard]] ChunkStart lastChunk(Voice voice) const noexcept { return readers_[voice].given; }
    // For the tests: how often a reader began anew since reset(), all three
    // counted, and whether the chunks follow the host's position now.
    [[nodiscard]] std::int64_t restartCount() const noexcept { return restarts_; }
    [[nodiscard]] bool followsHost() const noexcept { return playing_; }

private:
    // A chunk of a reversed reader: its output at sample m is the input at
    // `sum - m` under a window over `m - mirror`.
    struct Chunk
    {
        double sum = 0.0;
        double mirror = 0.0;
        double fadeIn = 1.0;
        double fadeOut = 1.0;
        double end = 0.0;
    };

    struct Reader
    {
        Chunk current;
        Chunk next;
        Chunk fading;
        bool sounding = false;
        bool waiting = false;
        std::int64_t nextFrom = 0;   // sample at which `next` takes over
        int fadeLeft = 0;            // samples `fading` still sounds
        // Number of the newest chunk on the reader's grid once the reader has
        // found its place there, and whether the transport asked for a new start.
        std::int64_t last = 0;
        bool synced = false;
        bool restart = false;
        ChunkStart given;
    };

    // A converter block of the host with the position the reference gives it.
    struct Tick
    {
        std::int64_t number = 0;
        double stamp = 0.0;
        double samplesPerQuarter = 0.0;
    };

    // Two taps of a grain reader at one sample: how far back each reads and
    // under which window.
    struct GrainTaps
    {
        std::array<std::int64_t, 2> back {};
        std::array<double, 2> fraction {};
        std::array<double, 2> window {};
    };

    [[nodiscard]] double sampleOfFrame(std::int64_t frame) const noexcept;
    [[nodiscard]] double quartersAt(double sample) const noexcept;
    [[nodiscard]] double boundaryOf(Voice voice, std::int64_t index) const noexcept;
    [[nodiscard]] Chunk chunkAt(Voice voice, double sum) const noexcept;
    [[nodiscard]] double readHistory(std::size_t channel, std::int64_t index) const noexcept;

    void takeTempo(double bpm) noexcept;
    [[nodiscard]] bool watchTempo(double bpm, double sample) noexcept;
    [[nodiscard]] bool watchPosition(double quarters, double sample, double newSamplesPerQuarter,
                                     bool tempoMoved, bool& follow) noexcept;
    void beginBlock(std::int64_t block) noexcept;
    void restartReader(Voice voice, std::int64_t block) noexcept;
    void scheduleFromPosition(Voice voice, std::int64_t block) noexcept;
    void scheduleFromTicks(std::int64_t block, const std::array<bool, voiceCount>& held) noexcept;
    void startAtBoundary(Voice voice, std::int64_t index, double boundary) noexcept;
    void give(Voice voice, std::int64_t index, const Chunk& chunk, std::int64_t from) noexcept;
    void advanceReaders() noexcept;
    void finishSample() noexcept;
    [[nodiscard]] GrainTaps grainTaps(float phase, double sweep, double shortest, bool falling) const noexcept;

    bool prepared_ = false;
    bool silent_ = true;

    // The lattice of the host rate, and the converter block of the reference.
    int hostRate_ = 48000;
    std::int64_t hostStep_ = 1;
    std::int64_t internalStep_ = 1;
    std::int64_t inputDelay_ = 0;
    std::int64_t tickFrames_ = 44;
    double tickSamples_ = 44.0;

    bool referenceWanted_ = false;
    bool reference_ = false;
    ClockOrigins originsWanted_;
    ClockOrigins origins_;
    std::int64_t firstSample_ = 0;

    // Host frames since reset(), and the internal sample that comes next,
    // counted from the instance's first.
    std::int64_t frames_ = 0;
    std::int64_t now_ = 0;

    // The transport of the host block that runs. `playing_` is the clock the
    // chunks are on: the host's position while its transport runs and its
    // position is trusted, the layer's own otherwise.
    bool transportKnown_ = false;
    bool playing_ = false;
    bool hostPlays_ = false;
    bool trusted_ = true;
    double tempo_ = 120.0;
    double samplesPerQuarter_ = 22050.0;
    std::int64_t blockFrame_ = 0;
    float blockQuartersSingle_ = 0.0f;
    double blockSample_ = 0.0;   // internal sample of the host's last word

    // A change of the tempo under watch: the tempo in front of it, when it
    // came, and whether the host's last word changed the tempo as well.
    bool tempoWatched_ = false;
    bool tempoMovedLast_ = false;
    double tempoBefore_ = 120.0;
    double tempoMovedAt_ = 0.0;

    // The reported position under watch: what it has strayed by of late, in
    // samples, and the time that sum is of; the jumps in a row, the last of
    // them and whether the host's last word was one; and since when it has
    // run without one.
    double stray_ = 0.0;
    double strayTime_ = 0.0;
    int jumpsInARow_ = 0;
    bool jumpedLast_ = false;
    double jumpedAt_ = 0.0;
    double steadySince_ = 0.0;
    std::int64_t restarts_ = 0;
    // The position as a line through a sample and its quarter note: the
    // host's while it plays, the layer's own while it stands.
    double hostSample_ = 0.0;
    double hostQuarters_ = 0.0;
    double ownSample_ = 0.0;
    double ownQuarters_ = 0.0;

    std::array<Reader, voiceCount> readers_ {};
    std::array<Tick, 16> ticks_ {};
    std::size_t tickRead_ = 0;
    std::size_t tickCount_ = 0;

    // Macro: the share of each voice gain that is in, and where it is going.
    std::array<double, voiceCount> share_ {};
    std::array<double, voiceCount> shareTarget_ {};
    double shareStep_ = 0.0;

    // The input of each channel, and the samples at or after `historyFirst_`
    // that count; older ones read as silence.
    std::array<std::vector<float>, channelCount> history_;
    std::int64_t historyFirst_ = 0;

    // Recirculation of the octave-up voice. A new delay fades in beside the
    // old one.
    std::array<std::vector<float>, channelCount> recirculation_;
    std::int64_t recirculationFirst_ = 0;
    std::int64_t recirculationDelay_ = 44100;
    std::int64_t recirculationNext_ = 44100;
    std::int64_t recirculationWanted_ = 44100;
    int recirculationFadeLeft_ = 0;

    // What the grain readers read, per channel, and the last sample at which
    // each took something other than silence.
    std::array<std::vector<double>, channelCount> upStream_;
    std::array<std::vector<double>, channelCount> downStream_;
    std::int64_t streamFirst_ = 0;
    std::int64_t upSounded_ = 0;
    std::int64_t downSounded_ = 0;
    // Phasors of the octave-down reader and of the two octave-up readers.
    float downPhase_ = 0.5f;
    std::array<float, channelCount> upPhase_ { 0.5f, 0.5f };
};
} // namespace amanita::dsp
