#pragma once

#include "SpumeDiffuser.h"

#include <cstdint>

namespace amanita::dsp
{
// The layer the Spume Character puts in front of Fathom's network: the input
// crossfaded with its diffused copy.
//
//     u --+---------------- x cos(pi/2 Macro) --(+)--> network
//         |                                      ^
//         +--> diffuser ---> x sin(pi/2 Macro) -->|
//
// The diffuser takes the input at every Macro, also at 0, so that opening
// Macro brings the wash of what was played before. The two gains, not Macro,
// are what moves: once per block of 44 internal samples each goes a fixed
// share of the way to its target and is then held, a one-pole of 10 ms run at
// the block rate. There is no clock in the layer and it does not hear the
// host's transport. Sample k of its block count is the k-th call of process()
// or idle() after reset().
//
// What follows the reference is in SpumeConstants.h with its source. Ocean's
// own is this: a gain that has come within a floor of its target is on it,
// what the diffuser holds under a floor is silence, and under the engine's
// hold the diffuser takes no new input.
class SpumeLayer
{
public:
    // Allocates; everything below is allocation-free and safe on the audio thread.
    void prepare();
    // Empties the diffuser, returns the block count to the first sample and
    // takes the gains at their targets.
    void reset() noexcept;
    // Empties the diffuser and takes the gains at their targets; the block
    // count keeps its place.
    void silence() noexcept;

    // Macro, 0 to 1. The gains move to their new targets block by block unless
    // `atOnce` or the layer holds no signal.
    void setMacro(double macro, bool atOnce) noexcept;
    // Test hook: internal samples in front of the first, for a render that
    // takes up the reference in the middle of a session. It takes effect at
    // the next reset().
    void setFirstSample(std::int64_t sample) noexcept;

    // One internal sample of each input, turned into what the network takes.
    // A layer at rest leaves the pair as it is. `inputShare` is the share of
    // this sample the diffuser takes: one, and none while the engine holds,
    // with the hold's own glide between the two. The plain input is not the
    // layer's to mute: the network takes or leaves it itself.
    void process(double& left, double& right, double inputShare) noexcept;
    void process(double& left, double& right) noexcept { process(left, right, 1.0); }
    // One internal sample of time without signal.
    void idle() noexcept;

    // True while the layer passes its input untouched: the plain gain rests
    // at one and the diffused one at zero.
    [[nodiscard]] bool atRest() const noexcept;

private:
    void moveGains() noexcept;

    SpumeDiffuser diffuser_;
    bool prepared_ = false;
    bool silent_ = true;

    // The gain of the plain input and of the diffused one, and where each is going.
    double plainGain_ = 1.0;
    double plainTarget_ = 1.0;
    double diffusedGain_ = 0.0;
    double diffusedTarget_ = 0.0;

    // The internal sample that comes next, counted from the instance's first.
    std::int64_t firstSample_ = 0;
    std::int64_t now_ = 0;
};
} // namespace amanita::dsp
