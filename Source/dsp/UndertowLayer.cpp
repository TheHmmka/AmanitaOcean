#include "FathomExactArithmetic.h"

#include "UndertowLayer.h"
#include "FathomEngineConstants.h"
#include "UndertowConstants.h"

#include <algorithm>
#include <cmath>

namespace amanita::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr std::int64_t blockSamples = undertow::blockSamples;
constexpr double internalRate = fathom::internalRate;

// Memory, in internal samples and powers of two. A reversed reader reads two
// of its chunks back; the longest chunk is that of the octave-down reader at
// the slowest tempo. The recirculation holds one chunk of the octave-up
// reader, a grain reader its sweep and its shortest delay.
constexpr std::size_t historyCapacity = std::size_t { 1 } << 20;
constexpr std::size_t recirculationCapacity = std::size_t { 1 } << 19;
constexpr std::size_t streamCapacity = std::size_t { 1 } << 13;
constexpr double longestChunkSamples = 2.6666667461395264 * 60.0 / undertow::minimumTempo * internalRate;
constexpr double longestRecirculationSamples = undertow::recirculationSamplesAtWindowTempo
                                             * undertow::windowTempo / undertow::minimumTempo;
static_assert(2.0 * longestChunkSamples + 256.0 < static_cast<double>(historyCapacity));
static_assert(longestRecirculationSamples + 2.0 < static_cast<double>(recirculationCapacity));
static_assert(undertow::octaveUpRightReader.sweepSamples + undertow::octaveUpRightReader.shortestDelaySamples
              + 2.0 < static_cast<double>(streamCapacity));
static_assert(undertow::octaveDownReader.sweepSamples + undertow::octaveDownReader.shortestDelaySamples
              + 2.0 < static_cast<double>(streamCapacity));

// A smaller number counts as zero, long before it would turn denormal in
// single precision; so does what is no number.
constexpr double signalFloor = 1.0e-30;
// Ocean's own: a boundary that lies this far behind is not caught up with;
// the reader starts anew.
constexpr double lateLimitSamples = 4.0 * undertow::blockSamples;
// Ocean's own: a position beyond this is no position.
constexpr double positionLimitQuarters = 1.0e12;

constexpr std::array<double, UndertowLayer::voiceCount> chunkQuarters {
    static_cast<double>(undertow::unisonChunkQuarters),
    static_cast<double>(undertow::octaveUpChunkQuarters),
    static_cast<double>(undertow::octaveDownChunkQuarters)
};
constexpr std::array<undertow::Window, UndertowLayer::voiceCount> windows {
    undertow::unisonWindow, undertow::octaveUpWindow, undertow::octaveDownWindow
};
constexpr std::array<undertow::Ramp, UndertowLayer::voiceCount> ramps {
    undertow::unisonRamp, undertow::octaveUpRamp, undertow::octaveDownRamp
};
constexpr std::array<double, UndertowLayer::channelCount> octaveUpGain {
    undertow::octaveUpLeftGain, undertow::octaveUpRightGain
};
constexpr std::array<undertow::GrainReader, UndertowLayer::channelCount> octaveUpReader {
    undertow::octaveUpLeftReader, undertow::octaveUpRightReader
};

[[nodiscard]] double settled(double value) noexcept
{
    return std::isfinite(value) && std::abs(value) >= signalFloor ? value : 0.0;
}

// Samples of the fade of a chunk that is cut short.
[[nodiscard]] int cutShortFadeSamples() noexcept
{
    return static_cast<int>(std::lround(undertow::cutShortFadeSeconds * internalRate));
}

// The fitted lag of the reference's free-running clock at a position, in
// samples (hostclock.free_run_drift).
[[nodiscard]] double freeRunDrift(double quarters) noexcept
{
    if (!(quarters >= undertow::freeRunDriftFromQuarters) || quarters > positionLimitQuarters)
        return 0.0;

    auto total = 0.0;
    auto low = undertow::freeRunDriftFromQuarters;
    auto sign = 1.0;
    while (quarters >= 2.0 * low)
    {
        total += sign * undertow::freeRunDriftSamplesPerQuarter * low;
        low = 2.0 * low;
        sign = -sign;
    }
    return total + sign * undertow::freeRunDriftSamplesPerQuarter * (quarters - low);
}

// A single-precision phasor one sample on.
[[nodiscard]] float stepped(float phase, float increment) noexcept
{
    phase += increment;
    if (phase >= 1.0f)
        phase -= 1.0f;
    return phase;
}

[[nodiscard]] float steppedBy(float phase, float increment, std::int64_t steps) noexcept
{
    for (std::int64_t step = 0; step < steps; ++step)
        phase = stepped(phase, increment);
    return phase;
}

// Gain of a chunk at a position behind its mirror point.
template <typename Chunk>
[[nodiscard]] double windowAt(const Chunk& chunk, double behind) noexcept
{
    auto gain = 1.0;
    if (behind < chunk.fadeIn)
        gain = 0.5 - 0.5 * std::cos(pi * std::max(behind, 0.0) / chunk.fadeIn);
    const auto fadeStart = chunk.end - chunk.fadeOut;
    if (behind > fadeStart)
    {
        // A chunk of full length is past its fade-in here and the product is
        // the fade-out alone; a chunk that was given a nearer end has both.
        const auto along = std::clamp((behind - fadeStart) / chunk.fadeOut, 0.0, 1.0);
        gain *= 0.5 + 0.5 * std::cos(pi * along);
    }
    return gain;
}
} // namespace

void UndertowLayer::prepare(const FathomRateLattice& lattice, int hostRate)
{
    hostRate_ = std::max(1, hostRate);
    hostStep_ = lattice.hostStep;
    internalStep_ = lattice.internalStep;
    inputDelay_ = lattice.converts() ? lattice.inputDelay : 0;
    // The reference converts in blocks of its reported latency.
    tickFrames_ = std::max(1, lattice.latencyFrames);
    tickSamples_ = static_cast<double>(tickFrames_ * std::int64_t { fathom::internalRate })
                 / static_cast<double>(hostRate_);

    for (std::size_t channel = 0; channel < channelCount; ++channel)
    {
        history_[channel].assign(historyCapacity, 0.0f);
        recirculation_[channel].assign(recirculationCapacity, 0.0f);
        upStream_[channel].assign(streamCapacity, 0.0);
        downStream_[channel].assign(streamCapacity, 0.0);
    }
    shareStep_ = 1.0 - std::exp(-1.0 / (undertow::gainSmoothingSeconds * internalRate));
    prepared_ = true;
    reset();
}

void UndertowLayer::reset() noexcept
{
    if (!prepared_)
        return;

    reference_ = referenceWanted_;
    origins_ = originsWanted_;
    firstSample_ = origins_.firstFrame / internalStep_ * hostStep_;
    frames_ = 0;
    now_ = firstSample_;

    // Until the host says otherwise the transport stands and has no tempo.
    transportKnown_ = false;
    playing_ = false;
    hostPlays_ = false;
    trusted_ = true;
    blockFrame_ = 0;
    blockQuartersSingle_ = 0.0f;
    blockSample_ = sampleOfFrame(0);
    tempoWatched_ = false;
    tempoMovedLast_ = false;
    tempoMovedAt_ = blockSample_ - 2.0 * undertow::tempoSettleSeconds * internalRate;
    stray_ = 0.0;
    strayTime_ = 0.0;
    jumpsInARow_ = 0;
    jumpedLast_ = false;
    jumpedAt_ = blockSample_;
    steadySince_ = blockSample_;
    restarts_ = 0;
    ownSample_ = static_cast<double>(origins_.freeRun);
    ownQuarters_ = 0.0;
    hostSample_ = ownSample_;
    hostQuarters_ = 0.0;
    readers_ = {};
    tickRead_ = 0;
    tickCount_ = 0;

    // The phasors stood at their start value a little in front of their origin.
    const auto phasorSteps = std::max<std::int64_t>(
        0, now_ - (origins_.phasors - undertow::phasorLeadSamples));
    downPhase_ = steppedBy(undertow::phasorStart, undertow::octaveDownReader.increment, phasorSteps);
    for (std::size_t channel = 0; channel < channelCount; ++channel)
        upPhase_[channel] = steppedBy(undertow::phasorStart, octaveUpReader[channel].increment,
                                      phasorSteps);

    silent_ = true;
    takeTempo(undertow::fallbackTempo);
    silence();
}

void UndertowLayer::silence() noexcept
{
    historyFirst_ = now_;
    recirculationFirst_ = now_;
    streamFirst_ = now_;
    upSounded_ = now_ - static_cast<std::int64_t>(streamCapacity);
    downSounded_ = upSounded_;
    share_ = shareTarget_;
    recirculationDelay_ = recirculationWanted_;
    recirculationNext_ = recirculationWanted_;
    recirculationFadeLeft_ = 0;
    silent_ = true;
}

void UndertowLayer::setMacro(double macro, bool atOnce) noexcept
{
    const auto position = std::isfinite(macro) ? std::clamp(macro, 0.0, 1.0) : 0.0;
    for (std::size_t voice = 0; voice < voiceCount; ++voice)
        shareTarget_[voice] = std::clamp((position - ramps[voice].foot)
                                             / (ramps[voice].knee - ramps[voice].foot),
                                         0.0, 1.0);
    if (atOnce || silent_)
        share_ = shareTarget_;
}

void UndertowLayer::setReferenceArithmetic(bool reference) noexcept
{
    referenceWanted_ = reference;
}

void UndertowLayer::setClockOrigins(const ClockOrigins& origins) noexcept
{
    originsWanted_ = origins;
    originsWanted_.firstFrame = std::max<std::int64_t>(0, origins.firstFrame);
}

// Internal sample at which a host frame since reset() is centred: the inverse
// of the input converter's time base (FathomRateLattice).
double UndertowLayer::sampleOfFrame(std::int64_t frame) const noexcept
{
    return static_cast<double>(firstSample_)
         + static_cast<double>(hostStep_ * frame + inputDelay_) / static_cast<double>(internalStep_);
}

double UndertowLayer::quartersAt(double sample) const noexcept
{
    return playing_ ? hostQuarters_ + (sample - hostSample_) / samplesPerQuarter_
                    : ownQuarters_ + (sample - ownSample_) / samplesPerQuarter_;
}

// Internal sample at which chunk `index` of a reader begins: the place of a
// whole number of its note values on the clock that runs.
double UndertowLayer::boundaryOf(Voice voice, std::int64_t index) const noexcept
{
    const auto quarters = static_cast<double>(index) * chunkQuarters[voice];
    if (playing_)
        return hostSample_ + (quarters - hostQuarters_) * samplesPerQuarter_;

    const auto boundary = ownSample_ + (quarters - ownQuarters_) * samplesPerQuarter_;
    return reference_ ? boundary + freeRunDrift(quarters) : boundary;
}

// A chunk with its window at the tempo that runs.
UndertowLayer::Chunk UndertowLayer::chunkAt(Voice voice, double sum) const noexcept
{
    const auto scale = undertow::windowTempo / tempo_;
    Chunk chunk;
    chunk.sum = sum;
    chunk.mirror = sum / 2.0;
    chunk.fadeIn = windows[voice].fadeIn * scale;
    chunk.fadeOut = windows[voice].fadeOut * scale;
    chunk.end = windows[voice].end * scale;
    return chunk;
}

// What follows the tempo: the length of a quarter note and the recirculation
// delay. A layer that holds no signal takes the delay at once.
void UndertowLayer::takeTempo(double bpm) noexcept
{
    tempo_ = bpm;
    samplesPerQuarter_ = 60.0 / bpm * internalRate;
    recirculationWanted_ = static_cast<std::int64_t>(std::nearbyint(
        undertow::recirculationSamplesAtWindowTempo * (undertow::windowTempo / bpm)));
    if (silent_)
    {
        recirculationDelay_ = recirculationWanted_;
        recirculationNext_ = recirculationWanted_;
        recirculationFadeLeft_ = 0;
    }
}

// Ocean's own: what a change of the tempo is. Every change is followed at
// once; this only says when the readers begin anew for it. A change with no
// other one near it is a step, and the readers begin anew once that is known,
// a settle time behind it. Changes that follow each other within that time,
// or from one word of the host to the next, are a ramp and are only followed.
// The size of a single change does not decide, so a ramp is a ramp in host
// blocks of any length. Returns true when a step has settled.
bool UndertowLayer::watchTempo(double bpm, double sample) noexcept
{
    const auto settle = undertow::tempoSettleSeconds * internalRate;
    const auto moved = bpm < tempo_ || bpm > tempo_;
    auto settled = false;
    if (moved)
    {
        const auto ramping = tempoMovedLast_ || sample - tempoMovedAt_ < settle;
        if (!ramping)
            tempoBefore_ = tempo_;
        tempoWatched_ = !ramping;
        tempoMovedAt_ = sample;
    }
    else if (tempoWatched_ && sample - tempoMovedAt_ >= settle)
    {
        tempoWatched_ = false;
        settled = std::abs(tempo_ - tempoBefore_) > undertow::tempoStep * tempoBefore_;
    }
    tempoMovedLast_ = moved;
    return settled;
}

// Ocean's own: the reported position of a running transport against the one
// the tempo lets expect, which lies between where the old and where the new
// tempo put it when the tempo has moved. A position off that by more than two
// internal blocks, or by more than a share of the time since the host's last
// word if that is more, has jumped. What it strays by is summed, and so is the
// time it had for that, both with a memory of the two blocks at that share
// (50 ms); a sum beyond the two blocks and beyond the share of its time is a
// jump as well. So a position that runs faster or slower than the tempo by
// more than the share is found out in host blocks of any length, and one that
// runs off by less is followed. Returns true for a jump; `follow` says whether
// the host's line is to be laid anew through this position.
bool UndertowLayer::watchPosition(double quarters, double sample, double newSamplesPerQuarter,
                                  bool tempoMoved, bool& follow) noexcept
{
    const auto span = std::max(0.0, sample - blockSample_);
    const auto expected = hostQuarters_ + (sample - hostSample_) / samplesPerQuarter_;
    const auto atNewTempo = tempoMoved
        ? expected + span * (1.0 / newSamplesPerQuarter - 1.0 / samplesPerQuarter_)
        : expected;
    const auto low = std::min(expected, atNewTempo);
    const auto high = std::max(expected, atNewTempo);
    const auto strayedQuarters = quarters < low ? quarters - low
                               : quarters > high ? quarters - high
                                                 : 0.0;
    const auto strayed = strayedQuarters * samplesPerQuarter_;

    const auto jumpSamples = undertow::jumpBlocks * undertow::blockSamples;
    const auto memory = std::exp(-span * undertow::positionRate / jumpSamples);
    stray_ = stray_ * memory + strayed;
    strayTime_ = strayTime_ * memory + span;
    const auto jumped = std::abs(strayed) > std::max(jumpSamples, undertow::positionRate * span)
                     || std::abs(stray_) > std::max(jumpSamples, undertow::positionRate * strayTime_);
    follow = jumped || tempoMoved || std::abs(strayedQuarters) > undertow::consistentQuarters;
    if (!jumped)
        return false;

    stray_ = 0.0;
    strayTime_ = 0.0;
    const auto inARow = jumpsInARow_ > 0
                     && (jumpedLast_ || sample - jumpedAt_ < undertow::jumpRowSeconds * internalRate);
    jumpsInARow_ = inARow ? jumpsInARow_ + 1 : 1;
    jumpedAt_ = sample;
    return true;
}

void UndertowLayer::setTransport(const Transport& transport) noexcept
{
    if (!prepared_)
        return;

    // Ocean's own: no tempo is 120 BPM, a tempo outside 20 to 999 BPM is the
    // nearer end, and a position that is no number is a stopped transport.
    const auto tempo = transport.hasTempo && std::isfinite(transport.bpm)
        ? std::clamp(transport.bpm, undertow::minimumTempo, undertow::maximumTempo)
        : undertow::fallbackTempo;
    const auto plays = transport.playing && std::isfinite(transport.quarterNotes)
                    && std::abs(transport.quarterNotes) < positionLimitQuarters;
    const auto quarters = plays ? transport.quarterNotes : 0.0;
    const auto sample = sampleOfFrame(frames_);

    if (!transportKnown_)
    {
        // The first word of the host, in front of any sound: nothing changes.
        transportKnown_ = true;
        hostPlays_ = plays;
        playing_ = plays;
        takeTempo(tempo);
        hostSample_ = sample;
        hostQuarters_ = quarters;
    }
    else
    {
        // Ocean's own, everything here: what the layer makes of a transport
        // that does not simply run on. A start, a stop, a jump and a step of
        // the tempo begin every reader anew; a position or a tempo that
        // drifts or ramps is followed; a position that keeps jumping is left
        // alone until it runs again.
        const auto tempoMoved = tempo < tempo_ || tempo > tempo_;
        auto anew = watchTempo(tempo, sample);
        auto follow = false;
        auto jumped = false;
        if (plays && hostPlays_)
        {
            const auto newSamplesPerQuarter = 60.0 / tempo * internalRate;
            jumped = watchPosition(quarters, sample, newSamplesPerQuarter, tempoMoved, follow);
            if (trusted_)
            {
                // A jump begins the readers anew on the host's position. Too
                // many in a row and the position tells nothing: the layer
                // keeps time itself, as under a stopped transport.
                anew = anew || jumped;
                if (jumped && jumpsInARow_ >= undertow::distrustJumps)
                {
                    trusted_ = false;
                    steadySince_ = sample;
                }
            }
            else if (jumped)
            {
                steadySince_ = sample;
            }
            else if (sample - steadySince_ >= undertow::returnSeconds * internalRate)
            {
                // The position has run for a while: it is followed again.
                trusted_ = true;
                jumpsInARow_ = 0;
                follow = true;
            }
        }
        else if (plays != hostPlays_)
        {
            // A start or a stop: whatever the position did before is forgotten.
            trusted_ = true;
            jumpsInARow_ = 0;
            stray_ = 0.0;
            strayTime_ = 0.0;
            follow = plays;
        }
        jumpedLast_ = jumped;

        if (tempoMoved)
        {
            // The layer's own clock goes on from where it is at the new tempo.
            ownQuarters_ += (sample - ownSample_) / samplesPerQuarter_;
            ownSample_ = sample;
            takeTempo(tempo);
        }
        if (plays && follow)
        {
            hostSample_ = sample;
            hostQuarters_ = quarters;
        }
        const auto following = plays && trusted_;
        anew = anew || following != playing_;
        playing_ = following;
        hostPlays_ = plays;
        if (anew)
        {
            for (auto& reader : readers_)
                reader.restart = true;
            tickCount_ = 0;
        }
    }

    blockSample_ = sample;
    blockFrame_ = frames_;
    blockQuartersSingle_ = static_cast<float>(quarters);
}

void UndertowLayer::hostFrame() noexcept
{
    if (!prepared_)
        return;

    // The reference's arithmetic: a converter block takes the position of its
    // first frame, which is the single-precision position of the host block
    // plus the single-precision way from there.
    const auto frame = origins_.firstFrame + frames_;
    if (reference_ && playing_ && frame % tickFrames_ == 0)
    {
        if (tickCount_ == ticks_.size())
        {
            tickRead_ = (tickRead_ + 1) % ticks_.size();
            --tickCount_;
        }
        const auto ahead = static_cast<float>(
            static_cast<double>(frames_ - blockFrame_)
            * (tempo_ / 60.0 / static_cast<double>(hostRate_)));
        const auto stamp = blockQuartersSingle_ + ahead;
        auto& tick = ticks_[(tickRead_ + tickCount_) % ticks_.size()];
        tick.number = frame / tickFrames_;
        tick.stamp = static_cast<double>(stamp);
        tick.samplesPerQuarter = samplesPerQuarter_;
        ++tickCount_;
    }
    ++frames_;
}

void UndertowLayer::give(Voice voice, std::int64_t index, const Chunk& chunk,
                         std::int64_t from) noexcept
{
    auto& reader = readers_[voice];
    reader.next = chunk;
    reader.nextFrom = from;
    reader.waiting = true;
    reader.last = index;
    reader.synced = true;
    reader.given = { index, chunk.sum, reader.given.count + 1 };
}

// The chunk that begins at a boundary. With `start` the block that holds the
// boundary and `offset` the way from there, the reversed read has the sum
// 2 start - 2 + offset + whole(offset); the octave-up reader rounds the offset
// up, the other two down.
void UndertowLayer::startAtBoundary(Voice voice, std::int64_t index, double boundary) noexcept
{
    const auto start = static_cast<double>(blockSamples)
                     * std::floor(boundary / static_cast<double>(blockSamples));
    const auto offset = boundary - start;
    const auto whole = voice == octaveUp ? std::ceil(offset) : std::floor(offset);
    const auto chunk = chunkAt(voice, 2.0 * start - 2.0 + offset + whole);
    give(voice, index, chunk, static_cast<std::int64_t>(std::ceil(chunk.mirror)));
}

// Ocean's own: a reader begins anew at a block, wherever its grid stands. The
// chunk mirrors about the sample in front of the block and ends where the
// next one of the grid begins.
void UndertowLayer::restartReader(Voice voice, std::int64_t block) noexcept
{
    const auto index = static_cast<std::int64_t>(
        std::floor(quartersAt(static_cast<double>(block)) / chunkQuarters[voice]));
    auto chunk = chunkAt(voice, 2.0 * static_cast<double>(block) - 2.0);
    chunk.end = std::min(chunk.end, boundaryOf(voice, index + 1) - chunk.mirror);
    give(voice, index, chunk, block);
    readers_[voice].restart = false;
    ++restarts_;
}

// The schedule of a reader on a clock that is exact at every block: the next
// chunk is given in the block that holds its boundary.
void UndertowLayer::scheduleFromPosition(Voice voice, std::int64_t block) noexcept
{
    auto& reader = readers_[voice];
    if (!reader.synced)
    {
        // The newest chunk whose boundary lies in front of this block.
        reader.last = static_cast<std::int64_t>(
            std::ceil(quartersAt(static_cast<double>(block)) / chunkQuarters[voice])) - 1;
        reader.synced = true;
    }

    const auto boundary = boundaryOf(voice, reader.last + 1);
    if (!(boundary < static_cast<double>(block + blockSamples)))
        return;
    if (boundary < static_cast<double>(block) - lateLimitSamples)
    {
        reader.restart = true;
        return;
    }
    startAtBoundary(voice, reader.last + 1, boundary);
}

// The reference's schedule while its transport runs (hostclock.schedule): a
// converter block with number m belongs to the first internal block at or
// behind its own place less a fixed offset. A boundary less than a block ahead
// of the stamp, with a sample to spare, is taken there; one that was missed is
// taken in the next converter block.
void UndertowLayer::scheduleFromTicks(std::int64_t block,
                                      const std::array<bool, voiceCount>& held) noexcept
{
    while (tickCount_ > 0)
    {
        const auto tick = ticks_[tickRead_];
        const auto start = blockSamples * static_cast<std::int64_t>(std::ceil(
            (tickSamples_ * static_cast<double>(tick.number) - undertow::tickBlockOffset)
            / static_cast<double>(blockSamples)));
        if (start > block)
            return;

        tickRead_ = (tickRead_ + 1) % ticks_.size();
        --tickCount_;
        for (std::size_t number = 0; number < voiceCount; ++number)
        {
            const auto voice = static_cast<Voice>(number);
            auto& reader = readers_[voice];
            if (held[voice])
                continue;

            const auto length = chunkQuarters[voice];
            auto index = static_cast<std::int64_t>(std::floor(tick.stamp / length));
            while (static_cast<double>(index + 1) * length <= tick.stamp)
                ++index;
            while (static_cast<double>(index) * length > tick.stamp)
                --index;
            if (!reader.synced)
            {
                reader.last = index;
                reader.synced = true;
                continue;
            }

            const auto up = voice == octaveUp;
            auto taken = index;
            auto offset = 0.0;
            auto whole = 0.0;
            if (index > reader.last)
            {
                offset = (static_cast<double>(index) * length - tick.stamp) * tick.samplesPerQuarter;
                whole = offset >= 0.0 ? (up ? std::ceil(offset) : std::floor(offset))
                                      : (up ? 0.0 : -1.0);
            }
            else
            {
                taken = index + 1;
                offset = (static_cast<double>(taken) * length - tick.stamp) * tick.samplesPerQuarter;
                if (!(offset < undertow::tickCatchSamples) || taken <= reader.last)
                    continue;
                whole = up ? std::ceil(offset) : std::floor(offset);
            }
            const auto chunk = chunkAt(voice, 2.0 * static_cast<double>(start) - 2.0 + offset + whole);
            give(voice, taken, chunk, static_cast<std::int64_t>(std::ceil(chunk.mirror)));
        }
    }
}

void UndertowLayer::beginBlock(std::int64_t block) noexcept
{
    transportKnown_ = true;

    // Ocean's own: a reader that was asked to begin anew does so at the next
    // block at which it has no chunk left fading.
    std::array<bool, voiceCount> held {};
    for (std::size_t number = 0; number < voiceCount; ++number)
    {
        const auto voice = static_cast<Voice>(number);
        if (!readers_[voice].restart)
            continue;
        if (readers_[voice].fadeLeft > 0)
            held[voice] = true;
        else
            restartReader(voice, block);
    }

    if (reference_ && playing_)
    {
        scheduleFromTicks(block, held);
        return;
    }
    for (std::size_t number = 0; number < voiceCount; ++number)
        if (!held[number])
            scheduleFromPosition(static_cast<Voice>(number), block);
}

// The chunk a reader was given takes over at its first sample, or now if that
// has passed. Ocean's own: the chunk it replaces, if it still sounds, fades
// out beside it.
void UndertowLayer::advanceReaders() noexcept
{
    for (auto& reader : readers_)
    {
        if (!reader.waiting || now_ < reader.nextFrom)
            continue;

        if (reader.sounding && reader.fadeLeft == 0
            && windowAt(reader.current, static_cast<double>(now_) - reader.current.mirror)
                   > undertow::cutShortFloor)
        {
            reader.fading = reader.current;
            reader.fadeLeft = cutShortFadeSamples();
        }
        reader.current = reader.next;
        reader.sounding = true;
        reader.waiting = false;
    }
}

// What a sample of time moves, with or without signal: the three phasors, the
// fade of a chunk that was cut short, and the count of samples.
void UndertowLayer::finishSample() noexcept
{
    downPhase_ = stepped(downPhase_, undertow::octaveDownReader.increment);
    for (std::size_t channel = 0; channel < channelCount; ++channel)
        upPhase_[channel] = stepped(upPhase_[channel], octaveUpReader[channel].increment);
    for (auto& reader : readers_)
        if (reader.fadeLeft > 0)
            --reader.fadeLeft;
    ++now_;
}

double UndertowLayer::readHistory(std::size_t channel, std::int64_t index) const noexcept
{
    if (index < historyFirst_ || index > now_
        || now_ - index >= static_cast<std::int64_t>(historyCapacity))
        return 0.0;
    return static_cast<double>(
        history_[channel][static_cast<std::size_t>(index) & (historyCapacity - 1)]);
}

// The two taps of a grain reader, half a cycle apart. A tap at phase q reads
// the stream `shortest + sweep q` samples back, or `shortest + sweep (1 - q)`
// where the delay falls, under the window sin(pi q).
UndertowLayer::GrainTaps UndertowLayer::grainTaps(float phase, double sweep, double shortest,
                                                  bool falling) const noexcept
{
    GrainTaps taps;
    for (std::size_t tap = 0; tap < taps.back.size(); ++tap)
    {
        auto single = phase;
        if (tap == 1)
        {
            single = phase + 0.5f;
            if (single >= 1.0f)
                single -= 1.0f;
        }
        const auto cycle = static_cast<double>(single);
        const auto delay = shortest + sweep * (falling ? 1.0 - cycle : cycle);
        // The read lies between two samples: `back` to the older of them.
        const auto whole = std::floor(-delay);
        taps.fraction[tap] = -delay - whole;
        taps.back[tap] = static_cast<std::int64_t>(-whole);
        taps.window[tap] = cycle > 0.0 && cycle < 1.0 ? std::sin(pi * cycle) : 0.0;
    }
    return taps;
}

void UndertowLayer::process(double left, double right, double inputShare, double& addedLeft,
                            double& addedRight) noexcept
{
    addedLeft = 0.0;
    addedRight = 0.0;
    if (!prepared_)
        return;

    if (silent_)
    {
        // The first sample behind a rest: nothing in front of it counts,
        // whatever the memory still holds.
        historyFirst_ = now_;
        recirculationFirst_ = now_;
        streamFirst_ = now_;
        silent_ = false;
    }
    if (now_ % blockSamples == 0)
        beginBlock(now_);
    advanceReaders();

    const auto sample = static_cast<double>(now_);
    const auto slot = static_cast<std::size_t>(now_);
    // Ocean's own: what is played under the engine's hold is not kept, so no
    // reader plays it back when the hold ends. What the readers, the
    // recirculation and the grain readers hold from before runs out as it would.
    history_[0][slot & (historyCapacity - 1)] = static_cast<float>(settled(inputShare * left));
    history_[1][slot & (historyCapacity - 1)] = static_cast<float>(settled(inputShare * right));

    // Each gain follows its ramp through a one-pole and arrives.
    for (std::size_t voice = 0; voice < voiceCount; ++voice)
    {
        const auto distance = shareTarget_[voice] - share_[voice];
        if (distance != 0.0)
            share_[voice] = std::abs(distance) < undertow::gainFloor
                ? shareTarget_[voice]
                : share_[voice] + shareStep_ * distance;
    }

    // The reversed stream of a reader at this sample: the chunk that runs
    // and, for a while, one that was cut short.
    const auto reversed = [&](Voice voice, std::array<double, channelCount>& stream) noexcept
    {
        stream = {};
        const auto& reader = readers_[voice];
        const auto add = [&](const Chunk& chunk, double scale) noexcept
        {
            const auto window = scale * windowAt(chunk, sample - chunk.mirror);
            const auto position = chunk.sum - sample;
            if (window == 0.0 || !(position >= static_cast<double>(historyFirst_) - 1.0)
                || position > sample)
                return;
            const auto whole = std::floor(position);
            const auto fraction = position - whole;
            const auto index = static_cast<std::int64_t>(whole);
            for (std::size_t channel = 0; channel < channelCount; ++channel)
                stream[channel] += window * ((1.0 - fraction) * readHistory(channel, index)
                                             + fraction * readHistory(channel, index + 1));
        };
        if (reader.sounding)
            add(reader.current, 1.0);
        if (reader.fadeLeft > 0)
            add(reader.fading,
                0.5 - 0.5 * std::cos(pi * reader.fadeLeft / (cutShortFadeSamples() + 1.0)));
    };

    const auto readStream = [&](const std::vector<double>& stream, const GrainTaps& taps) noexcept
    {
        auto sum = 0.0;
        for (std::size_t tap = 0; tap < taps.back.size(); ++tap)
        {
            if (taps.window[tap] == 0.0)
                continue;
            const auto older = now_ - taps.back[tap];
            const auto at = [&](std::int64_t index) noexcept
            {
                return index >= streamFirst_
                    ? stream[static_cast<std::size_t>(index) & (streamCapacity - 1)] : 0.0;
            };
            sum += taps.window[tap] * ((1.0 - taps.fraction[tap]) * at(older)
                                       + taps.fraction[tap] * at(older + 1));
        }
        return sum;
    };

    std::array<double, channelCount> added {};
    std::array<double, channelCount> stream {};

    // At pitch: the reversed stream under its gain.
    if (share_[unison] != 0.0)
    {
        reversed(unison, stream);
        const auto gain = undertow::unisonGain * share_[unison];
        for (std::size_t channel = 0; channel < channelCount; ++channel)
            added[channel] = gain * stream[channel];
    }

    // Octave up: the reversed stream recirculates over one chunk at every
    // Macro; its gain sits in front of the grain reader, which has its own
    // phasor for each input.
    reversed(octaveUp, stream);
    if (recirculationFadeLeft_ == 0 && recirculationWanted_ != recirculationDelay_)
    {
        // Ocean's own: a new delay fades in beside the old one.
        recirculationNext_ = recirculationWanted_;
        recirculationFadeLeft_ = cutShortFadeSamples();
    }
    const auto incoming = recirculationFadeLeft_ > 0
        ? 0.5 + 0.5 * std::cos(pi * recirculationFadeLeft_ / (cutShortFadeSamples() + 1.0))
        : 0.0;
    for (std::size_t channel = 0; channel < channelCount; ++channel)
    {
        const auto& ring = recirculation_[channel];
        const auto at = [&](std::int64_t delay) noexcept
        {
            const auto index = now_ - delay;
            return index >= recirculationFirst_
                ? static_cast<double>(ring[static_cast<std::size_t>(index) & (recirculationCapacity - 1)])
                : 0.0;
        };
        auto fedBack = at(recirculationDelay_);
        if (recirculationFadeLeft_ > 0)
            fedBack += incoming * (at(recirculationNext_) - fedBack);
        const auto recirculated = stream[channel] + undertow::recirculationGain * fedBack;
        recirculation_[channel][slot & (recirculationCapacity - 1)]
            = static_cast<float>(settled(recirculated));

        const auto taken = settled(octaveUpGain[channel] * share_[octaveUp] * recirculated);
        upStream_[channel][slot & (streamCapacity - 1)] = taken;
        if (taken != 0.0)
            upSounded_ = now_;
    }
    if (recirculationFadeLeft_ > 0 && --recirculationFadeLeft_ == 0)
        recirculationDelay_ = recirculationNext_;
    if (now_ - upSounded_ < static_cast<std::int64_t>(streamCapacity))
        for (std::size_t channel = 0; channel < channelCount; ++channel)
            added[channel] += readStream(
                upStream_[channel],
                grainTaps(upPhase_[channel], octaveUpReader[channel].sweepSamples,
                          octaveUpReader[channel].shortestDelaySamples, true));

    // Octave down: the gain in front of one grain reader for both inputs.
    stream = {};
    if (share_[octaveDown] != 0.0)
        reversed(octaveDown, stream);
    for (std::size_t channel = 0; channel < channelCount; ++channel)
    {
        const auto taken = settled(undertow::octaveDownGain * share_[octaveDown] * stream[channel]);
        downStream_[channel][slot & (streamCapacity - 1)] = taken;
        if (taken != 0.0)
            downSounded_ = now_;
    }
    if (now_ - downSounded_ < static_cast<std::int64_t>(streamCapacity))
    {
        const auto taps = grainTaps(downPhase_, undertow::octaveDownReader.sweepSamples,
                                    undertow::octaveDownReader.shortestDelaySamples, false);
        for (std::size_t channel = 0; channel < channelCount; ++channel)
            added[channel] += readStream(downStream_[channel], taps);
    }

    finishSample();
    addedLeft = added[0];
    addedRight = added[1];
}

void UndertowLayer::idle() noexcept
{
    if (!prepared_)
        return;

    if (!silent_)
        silence();
    if (now_ % blockSamples == 0)
        beginBlock(now_);
    advanceReaders();
    finishSample();
}

bool UndertowLayer::atRest() const noexcept
{
    for (std::size_t voice = 0; voice < voiceCount; ++voice)
        if (share_[voice] != 0.0 || shareTarget_[voice] != 0.0)
            return false;
    const auto reach = static_cast<std::int64_t>(streamCapacity);
    return now_ - upSounded_ >= reach && now_ - downSounded_ >= reach;
}
} // namespace amanita::dsp
