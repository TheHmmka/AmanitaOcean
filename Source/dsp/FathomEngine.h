#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace amanita::dsp
{
// Structural model of the reference reverb measured in
// Analyzer/Campaigns/RevOceanCharacterization (see its SPEC.md): a 44.1 kHz
// core of two 16-line networks between two rate converters, with the Tide
// layer (input comb and output voices) driven by `macro`.
//
// Timing contract: the engine reports no latency. The frame returned for the
// n-th input frame after prepare() or reset() equals the reference's wet
// output at index n + L, where L is the latency the reference reports at this
// sample rate; the reference delays its dry path by the same L. All clocks of
// the model (line oscillators, comb delay, voice phase) count frames from
// prepare() or reset(), whichever came last.
//
// The same core carries a second layer in place of Tide: Undertow, the model
// of the reference's Abyss mode (UndertowLayer.h). It makes three voices from
// the converter's output and adds them in front of the network, whose own
// Macro then rests at 0; `macro` drives the voices, and their chunks follow
// the host's transport.
//
// And a third: Spume, the model of the reference's Foam mode (SpumeLayer.h).
// It crossfades the converter's output with a diffused copy of it in front of
// the network, whose own Macro rests at 0 here as well; `macro` is the
// crossfade. Nothing in it follows the host's transport.
//
// Ocean's own for all three layers: while the network is held (`freeze`) it
// takes no input, and neither does the layer, by the network's own glide:
// not the comb of Tide inside the network, and not Undertow or Spume in
// front of it. What is played under the hold is nowhere when the hold ends;
// what a layer took before runs out as it would.
class FathomEngine
{
public:
    // What `macro` drives.
    enum class Layer
    {
        tide,
        undertow,
        spume
    };

    // What the host says about its transport at the first frame of a block.
    struct Transport
    {
        double quarterNotes = 0.0;   // position of the block's first frame
        double bpm = 120.0;          // tempo; read only when hasTempo
        bool playing = false;        // false: the position is not used
        bool hasTempo = false;       // false: the host gave no tempo
    };

    // Test hook of the Undertow and the Spume layer: where the clocks of an
    // instance stand at its first frame, for a render that takes up a session
    // of the reference in its middle. Internal samples and host frames count
    // from the first frame the instance ever processed; all zero is a fresh
    // one. Spume has the first two only: its blocks of 44 internal samples
    // count from the first frame, and the oscillators are the network's.
    struct ClockOrigins
    {
        // Host frames processed in front of the first; a multiple of the
        // number of host frames after which the two rates meet again.
        std::int64_t firstFrame = 0;
        // Internal sample at which the 32 line oscillators were at their
        // start phases, counted where the lines take their input.
        std::int64_t oscillators = 0;
        // Internal sample two samples in front of which the grain phasors
        // stood at their start value.
        std::int64_t phasors = 0;
        // Internal sample at which the clock of a stopped transport read zero.
        std::int64_t freeRun = 0;
    };

    struct Frame
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    struct Parameters
    {
        float decaySeconds = 4.0f;       // reference Decay
        float sizeScale = 1.0f;          // reference Size / 100
        float preDelaySeconds = 0.0f;    // reference Pre Delay
        float macro = 0.0f;              // reference Macro, 0 to 1
        float lowCutHz = 20.0f;          // Ocean's own in-loop low cut; 20 Hz takes it out of the circuit
        float highDampingHz = 20000.0f;  // Ocean's own in-loop damping; 20 kHz takes it out of the circuit
        bool freeze = false;             // Ocean's own hold; not a measured behaviour of the reference
    };

    // Phase of the two voices of each output above Macro 0. The reference draws
    // it at random per instance; a fixed seed makes renders repeatable.
    static constexpr std::uint64_t defaultVoiceSeed = 0x45626245ULL;

    FathomEngine();
    ~FathomEngine();
    FathomEngine(const FathomEngine&) = delete;
    FathomEngine& operator=(const FathomEngine&) = delete;

    // Allocates; everything below is allocation-free and safe on the audio thread.
    void prepare(double sampleRate);
    void reset() noexcept;
    void setVoiceSeed(std::uint64_t seed) noexcept;   // takes effect at the next reset()

    // Test hook: the phase of the first voice of the left and of the right
    // output, in cycles, at the start of every block of 44 internal samples
    // counted from prepare() or reset(), in place of the engine's own random
    // phase. The two arrays of `blockCount` values must outlive their use;
    // later blocks follow the engine's own phase, and null pointers return to
    // it altogether. With a prescribed phase the voices follow their laws at
    // Macro 0 as well, where those laws give the resting voice.
    void setVoicePhaseForTesting(const double* left, const double* right,
                                 std::size_t blockCount) noexcept;

    // The layer `macro` drives; it takes effect at the next prepare(), which
    // allocates the memory of the Undertow or of the Spume layer. With
    // Layer::tide the engine is what it is without this call.
    void setLayer(Layer layer) noexcept;

    // Undertow: the host's transport at the next frame. Call it in front of
    // every host block. Without a call the transport stands and has no tempo.
    void setTransport(const Transport& transport) noexcept;

    // Test hooks of the Undertow layer; both take effect at the next reset().
    // In the reference's arithmetic the chunk clock reads the position as the
    // reference does, in single precision once per host block and once per
    // converter block, and repeats what follows from that; the engine's own
    // arithmetic keeps the position in double precision at every internal
    // block. With clock origins set, reset() walks the oscillators and the
    // phasors to their places and takes as long as that needs.
    void setReferenceArithmetic(bool reference) noexcept;
    void setClockOriginsForTesting(const ClockOrigins& origins) noexcept;

    // Test hook: the internal sample the core computes next, counted from the
    // first frame the instance ever processed. The Spume layer moves its
    // gains once per block of 44 of these and takes a new Macro at the block
    // that begins next; a render that is to repeat a recording of the
    // reference hands Macro over in front of the block the reference used it
    // in first, which the reference's handling of host blocks decides.
    [[nodiscard]] std::int64_t nextCoreSampleForTesting() const noexcept;

    void setParameters(const Parameters& parameters) noexcept;

    // Wet output for one input frame: Width 100 %, Return 0 dB, before the level stage.
    [[nodiscard]] Frame processSample(float left, float right) noexcept;

    // One frame of time with no input and no output wanted: the clocks advance
    // and the networks are cleared, so the engine costs next to nothing while
    // another Character is selected and starts from silence when it returns.
    void advanceIdle() noexcept;

    // The reference's outer laws. The plug-in takes Width, the clipper and the
    // level stage from here and leaves through Ocean's own Mix; the Mix law
    // below serves the offline renderer and the tests.

    // Mid/side width on the wet signal. widthScale 0 to 2 is the reference's
    // Width knob travel (1 = 100 %, 2 = its maximum of 150 %).
    [[nodiscard]] static Frame applyWidth(Frame wet, float widthScale) noexcept;

    // Dry and wet gains of the reference's Mix law for mix 0 to 1.
    static void mixGains(float mix, float& dryGain, float& wetGain) noexcept;

    // The Mix sum of one channel in single precision. Each product is rounded
    // on its own before the addition in every build, so the renderer and the
    // tests agree to the bit.
    [[nodiscard]] static float mix(float dryGain, float dry, float wetGain, float wet) noexcept;

    // The reference's output clipper: unity up to +8 dBFS, ceiling +12 dBFS.
    [[nodiscard]] static float clip(float sample) noexcept;

    // The reference's level stage, which is active even with its Ducking at 0 %:
    // the wet is turned down while the input exceeds about -5 dBFS.
    class LevelStage
    {
    public:
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;
        [[nodiscard]] Frame process(float dryLeft, float dryRight, Frame wet) noexcept;

    private:
        float attackCoefficient_ = 0.0f;
        float releaseCoefficient_ = 0.0f;
        float reductionDb_ = 0.0f;
    };

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace amanita::dsp
