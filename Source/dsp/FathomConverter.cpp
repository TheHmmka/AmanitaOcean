#include "FathomExactArithmetic.h"

#include "FathomConverter.h"
#include "FathomEngineConstants.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace amanita::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// Branch tables above this size are not kept: a rate ratio with thousands of
// branches computes each branch as it is read.
constexpr std::size_t maximumBranchCoefficients = std::size_t { 1 } << 19;

// Modified Bessel function of the first kind and order zero, by its power series.
[[nodiscard]] double besselI0(double x) noexcept
{
    auto sum = 1.0;
    auto term = 1.0;
    for (auto k = 1; k < 40; ++k)
    {
        const auto half = x / (2.0 * k);
        term *= half * half;
        sum += term;
    }
    return sum;
}

// One wing of the kernel: cutoff * sinc(cutoff * i / 4096) under a Kaiser
// window that ends on the last entry, as single-precision values.
[[nodiscard]] std::vector<float> makeKernelTable()
{
    std::vector<float> table(static_cast<std::size_t>(fathom::converterTableEntries));
    const auto windowPeak = besselI0(fathom::converterKaiserBeta);
    for (std::size_t entry = 0; entry < table.size(); ++entry)
    {
        const auto index = static_cast<double>(entry);
        const auto angle = pi * (fathom::converterCutoff * index
                                 / fathom::converterEntriesPerCrossing);
        const auto sinc = entry == 0 ? 1.0 : std::sin(angle) / angle;
        const auto edge = index / (fathom::converterTableEntries - 1);
        const auto window = besselI0(fathom::converterKaiserBeta * std::sqrt(1.0 - edge * edge))
                          / windowPeak;
        table[entry] = static_cast<float>(fathom::converterCutoff * sinc * window);
    }
    return table;
}

// Samples a converter looks ahead in its source: 18 periods of the lower of the
// two rates, counted in source samples, and a margin of 10.
[[nodiscard]] std::int64_t lookahead(int sourceStep, int targetStep) noexcept
{
    const auto periods = std::int64_t { fathom::converterZeroCrossings + 1 };
    return periods * std::max(sourceStep, targetStep) / sourceStep
         + fathom::converterLookaheadMargin;
}

// The clock of a converter that works on blocks: its time in source frames, a
// double that starts at the look-ahead and advances by the rate ratio per
// target frame. A block of source frames yields target frames while the time
// lies short of its end; the time then steps back by the block, and whole
// frames it has crept past the look-ahead come off the next block. How the
// time has rounded against the exact ratio is the sign a converter reads its
// table with.
class BlockClock
{
public:
    BlockClock(int sourceStep, int targetStep) noexcept
        : sourceStep_(sourceStep),
          targetStep_(targetStep),
          step_(static_cast<double>(targetStep) / sourceStep),
          start_(lookahead(sourceStep, targetStep)),
          time_(static_cast<double>(start_))
    {
    }

    // Takes a block of source frames and returns the target frames it yields.
    [[nodiscard]] std::int64_t take(std::int64_t sourceFrames) noexcept
    {
        pending_ += sourceFrames;
        if (pending_ <= 0)
            return 0;

        const auto block = pending_;
        const auto end = time_ + static_cast<double>(block);
        std::int64_t yielded = 0;
        auto time = time_;
        while (time < end)
        {
            time += step_;
            ++yielded;
        }
        time_ = time - static_cast<double>(block);
        const auto creep = static_cast<std::int64_t>(time_) - start_;
        time_ -= static_cast<double>(creep);
        pending_ = -creep;
        targetFrames_ += yielded;
        sourceFrames_ += block + creep;
        return yielded;
    }

    // -1 when the time lies below the exact one, start + target frames times
    // the ratio - source frames, 0 on it and +1 above. Both sides are
    // compared times the denominator of the ratio: the exact side is then a
    // whole number, and the product on the other is taken with its rounding
    // error, which the fused operation returns exactly.
    [[nodiscard]] int errorSign() const noexcept
    {
        const auto denominator = static_cast<double>(sourceStep_);
        const auto exact = static_cast<double>((start_ - sourceFrames_) * sourceStep_
                                               + targetFrames_ * targetStep_);
        const auto scaled = time_ * denominator;
        const auto remainder = std::fma(time_, denominator, -scaled);
        const auto error = (scaled - exact) + remainder;
        return error > 0.0 ? 1 : error < 0.0 ? -1 : 0;
    }

private:
    std::int64_t sourceStep_;
    std::int64_t targetStep_;
    double step_;
    std::int64_t start_;
    double time_;
    std::int64_t pending_ = 0;
    std::int64_t targetFrames_ = 0;
    std::int64_t sourceFrames_ = 0;
};
} // namespace

FathomRateLattice FathomRateLattice::at(int hostRate) noexcept
{
    FathomRateLattice lattice;
    const auto divisor = std::gcd(hostRate, fathom::internalRate);
    lattice.internalStep = hostRate / divisor;
    lattice.hostStep = fathom::internalRate / divisor;
    // 44 internal samples at the host rate, rounded to a multiple of four frames.
    lattice.latencyFrames = 4 * static_cast<int>(
        (std::int64_t { fathom::latencyInternalSamples / 2 } * hostRate + fathom::internalRate)
        / (2 * fathom::internalRate));

    const auto host = std::int64_t { lattice.hostStep };
    const auto internal = std::int64_t { lattice.internalStep };
    if (!lattice.converts())
    {
        lattice.inputDelay = 0;
        lattice.outputDelay = lattice.latencyFrames * host;
        return lattice;
    }

    // The output is held back one block of the reported latency when the rate
    // ratio is a whole number, two otherwise.
    const auto heldBlocks = host == 1 || internal == 1 ? 1 : 2;
    lattice.inputDelay = (lookahead(lattice.hostStep, lattice.internalStep) + 1) * host - internal;
    lattice.outputDelay = lookahead(lattice.internalStep, lattice.hostStep) * internal
                        + heldBlocks * lattice.latencyFrames * host;

    for (const auto& measured : fathom::measuredClockSigns)
    {
        if (measured.hostRate == hostRate)
        {
            lattice.inputClockSign = measured.input;
            lattice.outputClockSign = measured.output;
            return lattice;
        }
    }

    // Where the signs were not measured they are those of two block clocks
    // that convert a second of host frames in blocks of the reported latency,
    // the second fed with what the first yields.
    BlockClock toInternal(lattice.hostStep, lattice.internalStep);
    BlockClock toHost(lattice.internalStep, lattice.hostStep);
    for (auto block = hostRate / lattice.latencyFrames; block > 0; --block)
        static_cast<void>(toHost.take(toInternal.take(lattice.latencyFrames)));
    lattice.inputClockSign = toInternal.errorSign();
    lattice.outputClockSign = toHost.errorSign();
    return lattice;
}

const std::vector<float>& FathomConverter::kernelTable()
{
    static const auto table = makeKernelTable();
    return table;
}

void FathomConverter::prepare(int sourceStep, int targetStep, std::int64_t delay, int clockSign,
                              int slackFrames)
{
    sourceStep_ = std::max(1, sourceStep);
    targetStep_ = std::max(1, targetStep);
    delay_ = delay;
    clockSign_ = clockSign;
    table_ = kernelTable().data();

    // Towards a lower rate the table is stepped by less than one zero crossing
    // per source frame and the sum is scaled by the rate ratio.
    const auto ratio = static_cast<double>(sourceStep_) / targetStep_;
    lowersRate_ = sourceStep_ < targetStep_;
    tableStep_ = lowersRate_ ? ratio * fathom::converterEntriesPerCrossing
                             : static_cast<double>(fathom::converterEntriesPerCrossing);
    gain_ = lowersRate_ ? ratio : 1.0;
    // One slot more than the quotient, so that its rounding never costs a
    // wing a tap.
    wingTaps_ = static_cast<int>(fathom::converterTableEntries / tableStep_) + 1;
    width_ = 2 * static_cast<std::size_t>(wingTaps_);

    scratch_.assign(width_, 0.0);
    branches_.clear();
    const auto branchCount = static_cast<std::size_t>(sourceStep_);
    if (branchCount * width_ <= maximumBranchCoefficients)
    {
        branches_.assign(branchCount * width_, 0.0);
        for (auto branch = 0; branch < sourceStep_; ++branch)
            fillBranch(branch, branches_.data() + static_cast<std::size_t>(branch) * width_);
    }

    capacity_ = 1;
    while (capacity_ < width_ + static_cast<std::size_t>(std::max(0, slackFrames)) + 1)
        capacity_ *= 2;
    left_.assign(2 * capacity_, 0.0);
    right_.assign(2 * capacity_, 0.0);
    reset();
}

void FathomConverter::reset() noexcept
{
    clear();
    written_ = 0;
    // Target frame 0 sits at -delay.
    base_ = -((delay_ + sourceStep_ - 1) / sourceStep_);
    branch_ = static_cast<int>(-delay_ - base_ * sourceStep_);
}

void FathomConverter::clear() noexcept
{
    std::fill(left_.begin(), left_.end(), 0.0);
    std::fill(right_.begin(), right_.end(), 0.0);
}

void FathomConverter::write(double left, double right) noexcept
{
    const auto slot = static_cast<std::size_t>(written_) & (capacity_ - 1);
    left_[slot] = left;
    left_[slot + capacity_] = left;
    right_[slot] = right;
    right_[slot + capacity_] = right;
    ++written_;
}

void FathomConverter::read(double& left, double& right) noexcept
{
    const auto* coefficients = scratch_.data();
    if (branches_.empty())
        fillBranch(branch_, scratch_.data());
    else
        coefficients = branches_.data() + static_cast<std::size_t>(branch_) * width_;

    const auto first = base_ + shiftOf(branch_) - wingTaps_ + 1;
    const auto slot = static_cast<std::size_t>(first) & (capacity_ - 1);
    const auto* sourceLeft = left_.data() + slot;
    const auto* sourceRight = right_.data() + slot;

    // Two partial sums per channel keep the order of the additions fixed and
    // their chains short.
    auto evenLeft = 0.0;
    auto oddLeft = 0.0;
    auto evenRight = 0.0;
    auto oddRight = 0.0;
    for (std::size_t tap = 0; tap < width_; tap += 2)
    {
        evenLeft += coefficients[tap] * sourceLeft[tap];
        oddLeft += coefficients[tap + 1] * sourceLeft[tap + 1];
        evenRight += coefficients[tap] * sourceRight[tap];
        oddRight += coefficients[tap + 1] * sourceRight[tap + 1];
    }
    left = evenLeft + oddLeft;
    right = evenRight + oddRight;
    advance();
}

void FathomConverter::advance() noexcept
{
    branch_ += targetStep_;
    if (branch_ >= sourceStep_)
    {
        base_ += branch_ / sourceStep_;
        branch_ %= sourceStep_;
    }
}

// Coefficients of one branch in `width_` slots, lowest source frame first: the
// wing of source frames at or before the target frame ends in the middle, the
// wing after it starts there. Unused slots are zero.
void FathomConverter::fillBranch(int branch, double* coefficients) const noexcept
{
    // How far the target frame lies past the source frame at or before it, and
    // how far short of the next one, in source frames. The drift stands for
    // the rounding of the converter's clock and decides the entry where a
    // distance falls exactly on one.
    auto past = static_cast<double>(branch) / sourceStep_
              + clockSign_ * fathom::converterClockDrift;
    if (shiftOf(branch) < 0)
        past = 1.0 - fathom::converterClockDrift;
    const auto ahead = 1.0 - past;
    const auto beforeLimit = fathom::converterTableEntries;
    const auto afterLimit = fathom::converterTableEntries - fathom::converterEntriesDroppedAfter;

    std::fill(coefficients, coefficients + width_, 0.0);
    auto* beforeWing = coefficients + wingTaps_ - 1;
    auto* afterWing = coefficients + wingTaps_;
    if (lowersRate_)
    {
        // The table position is accumulated, as the reference accumulates it.
        auto position = past * tableStep_;
        for (auto tap = 0; tap < wingTaps_ && static_cast<int>(position) < beforeLimit; ++tap)
        {
            beforeWing[-tap] = gain_ * static_cast<double>(table_[static_cast<int>(position)]);
            position += tableStep_;
        }
        position = ahead * tableStep_;
        for (auto tap = 0; tap < wingTaps_ && static_cast<int>(position) < afterLimit; ++tap)
        {
            afterWing[tap] = gain_ * static_cast<double>(table_[static_cast<int>(position)]);
            position += tableStep_;
        }
        return;
    }

    auto entry = static_cast<int>(past * fathom::converterEntriesPerCrossing);
    for (auto tap = 0; tap < wingTaps_ && entry < beforeLimit; ++tap)
    {
        beforeWing[-tap] = static_cast<double>(table_[entry]);
        entry += fathom::converterEntriesPerCrossing;
    }
    entry = static_cast<int>(ahead * fathom::converterEntriesPerCrossing);
    for (auto tap = 0; tap < wingTaps_ && entry < afterLimit; ++tap)
    {
        afterWing[tap] = static_cast<double>(table_[entry]);
        entry += fathom::converterEntriesPerCrossing;
    }
}
} // namespace amanita::dsp
