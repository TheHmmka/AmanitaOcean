#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace amanita::dsp
{
// Where host frames and the samples of Fathom's 44.1 kHz core meet. With
// hostRate / 44100 = internalStep / hostStep in lowest terms, host frame n sits
// at hostStep * n on a common lattice and internal sample k at
// internalStep * k, both counted from the first frame an instance processes.
struct FathomRateLattice
{
    [[nodiscard]] static FathomRateLattice at(int hostRate) noexcept;

    [[nodiscard]] bool converts() const noexcept { return hostStep != internalStep; }

    int hostStep = 1;
    int internalStep = 1;
    // Latency the reference reports, in host frames.
    int latencyFrames = 44;
    // Internal sample k reads the host stream at internalStep * k - inputDelay;
    // the reference's host frame N reads the core at hostStep * N - outputDelay.
    std::int64_t inputDelay = 0;
    std::int64_t outputDelay = 44;
    // Rounding of the two converters' clocks: -1 a hair low, 0 exact, +1 a hair
    // high. Measured at six host rates; elsewhere the campaign's simulation of
    // the reference's block clocks, which a capture confirms at 64 and 384 kHz.
    int inputClockSign = 0;
    int outputClockSign = 0;
};

// One of the reference's two rate converters: a polyphase filter whose taps are
// entries of one windowed-sinc table, read without interpolation. Source frame
// i sits at sourceStep * i on the lattice, target frame t at
// targetStep * t - delay. Frames are stereo; both channels share the taps.
class FathomConverter
{
public:
    // `slackFrames` is how many source frames may be written beyond
    // lastSourceNeeded() before the read that needs it is made.
    void prepare(int sourceStep, int targetStep, std::int64_t delay, int clockSign,
                 int slackFrames);
    // Back to source frame 0 and target frame 0, with silence behind them.
    void reset() noexcept;
    // Forgets the signal; the clocks keep their place.
    void clear() noexcept;

    void write(double left, double right) noexcept;
    // The next target frame. It may be read once the source frame
    // lastSourceNeeded() has been written.
    void read(double& left, double& right) noexcept;

    // One source or one target frame of time without signal.
    void skipSource() noexcept { ++written_; }
    void skipTarget() noexcept { advance(); }

    // Source frames so far, and the last one the next target frame may read.
    [[nodiscard]] std::int64_t written() const noexcept { return written_; }
    [[nodiscard]] std::int64_t lastSourceNeeded() const noexcept { return base_ + wingTaps_; }
    // Taps on either side of a target frame, at most.
    [[nodiscard]] int wingTaps() const noexcept { return wingTaps_; }

    // The table both converters read; built on first use.
    [[nodiscard]] static const std::vector<float>& kernelTable();

private:
    void advance() noexcept;
    void fillBranch(int branch, double* coefficients) const noexcept;
    [[nodiscard]] int shiftOf(int branch) const noexcept
    {
        return clockSign_ < 0 && branch == 0 ? -1 : 0;
    }

    int sourceStep_ = 1;
    int targetStep_ = 1;
    std::int64_t delay_ = 0;
    int clockSign_ = 0;
    bool lowersRate_ = false;
    double tableStep_ = 0.0;
    double gain_ = 1.0;
    int wingTaps_ = 0;
    std::size_t width_ = 0;

    // Target frame under the read: the source frame at or before it, and how
    // many lattice steps past that frame it lies (the polyphase branch).
    std::int64_t base_ = 0;
    int branch_ = 0;
    std::int64_t written_ = 0;

    // Coefficients of every branch, lowest source frame first; left empty when
    // the rate ratio has too many branches to tabulate. Then a read fills
    // `scratch_` with its own branch.
    std::vector<double> branches_;
    std::vector<double> scratch_;
    const float* table_ = nullptr;

    // Source history, each frame stored twice so that every window is contiguous.
    std::vector<double> left_;
    std::vector<double> right_;
    std::size_t capacity_ = 0;
};
} // namespace amanita::dsp
