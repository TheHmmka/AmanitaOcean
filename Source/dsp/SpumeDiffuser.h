#pragma once

#include "SpumeConstants.h"

#include <array>
#include <cstddef>
#include <vector>

namespace amanita::dsp
{
// The diffuser of the Spume Character, for the left and the right input: per
// input, seven stages of four parallel Schroeder all-passes with a Hadamard
// mix of the four between two stages.
//
//     u x 1/2 -+-> AP -+       +-> AP -+             +-> AP -+
//              +-> AP -+ 1/2 H +-> AP -+ 1/2 H  ...  +-> AP -+-> sum
//              +-> AP -+       +-> AP -+             +-> AP -+
//              +-> AP -+       +-> AP -+             +-> AP -+
//
// Nothing in it moves and nothing feeds back around it: an impulse becomes a
// wash that rises for a second and falls by 28 dB a second. It runs at the
// engine's internal rate, where every delay is a whole number of samples. The
// constants are in SpumeConstants.h with their source.
class SpumeDiffuser
{
public:
    // Allocates; everything below is allocation-free and safe on the audio thread.
    void prepare();
    // Forgets the signal. The memory is not touched: `held_` counts the frames
    // that may be read.
    void clear() noexcept;

    // The diffused pair for one internal sample of each input.
    void process(double left, double right, double& diffusedLeft, double& diffusedRight) noexcept;

private:
    static constexpr std::size_t allPassCount =
        static_cast<std::size_t>(spume::stageCount) * static_cast<std::size_t>(spume::pathCount);

    // The delay lines of all all-passes in one block: for each, as many frames
    // as its delay, left and right side by side.
    std::vector<double> memory_;
    // Where the line of an all-pass begins in the block, and the frame of it
    // that is read and then written next.
    std::array<std::size_t, allPassCount> lineStart_ {};
    std::array<int, allPassCount> position_ {};
    // Frames taken since the last clear, counted no further than the longest delay.
    int held_ = 0;
};
} // namespace amanita::dsp
