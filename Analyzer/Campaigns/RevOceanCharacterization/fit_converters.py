#!/usr/bin/env python3
"""Regenerate tide_structural_data/converters.json: the evidence for converters.py at every host rate tested.

    python fit_converters.py            (about 90 seconds once the captures are cached, half an hour to render them)

Nothing is fitted. converters.py states the two rate converters of Rev OCEAN
as closed rules of the host rate; this script measures how well they hold.
Everything is rendered at Macro 0, Mix 100 %, the neutral baseline.

1. Reported latency at 48 host rates against `converters.reported_latency`.
2. The exact relation. At a 44.1 kHz host the plug-in does not convert, so for
   any stimulus x at a host rate

       capture(x, rate)  =  to_host( capture(to_internal(x), 44100) )

   holds sample for sample when both converters are right, whatever the
   network does. It is scored on a 14 s programme at 19 rates from 45 to
   384 kHz, with no network model and nothing fitted.
3. The same relation on a short probe with one element of the converters
   changed at a time (clock signs, the accumulated table position, the dropped
   last entry, a lattice step of delay, the table itself), with another
   warm-up, and with Decay, Size, Width and Pre-delay moved.
4. The converter clocks: the signs `converters.clock_signs` computes, how soon
   they settle, how many of the confirmed signs other block sizes would give,
   and whether the output depends on the block size of the host.
5. The packet's proof: the first-pass network of first_order_model.py driven
   through `to_internal` and `to_host`, scored on impulse trains (impulses
   1.0 to 1.3 s apart, Decay 0.5 s) in the window that corresponds to raw
   [1200, 2500) at 48 kHz. The trains with seed 7002 were rendered after the
   converters were fixed.

The locked holdouts are not read.
"""
from __future__ import annotations

import json
from contextlib import contextmanager
from fractions import Fraction
from pathlib import Path

import numpy as np

import converters as cv
import datasets
import first_order_model as model
import revocean

DATA = Path(__file__).resolve().parent / "tide_structural_data" / "converters.json"
CORE_RATE = cv.INTERNAL_RATE
AMPLITUDE = 0.5
TRAIN_SETTINGS = datasets.GRID_SETTINGS                       # Decay 0.5 s
PROGRAMME_SETTINGS = {"decay": revocean.normalised("decay", 1.5)}
PROGRAMME_SEED = 4242
PROGRAMME_LEVEL = 0.8                                         # keeps the converted 44.1 kHz stimulus below 0.5
PROBE_SECONDS = 1.0
PROBE_SEED = 4243
RELATION_RATES = (45000, 47250, 48000, 50000, 55125, 60000, 64000, 66150, 72000, 88200, 96000, 100000, 110250,
                  132300, 144000, 176400, 192000, 352800, 384000)
ABLATION_RATES = (48000, 66150, 88200, 96000, 132300, 192000)
LATENCY_PROBE_RATES = (44100, 45000, 45999, 46000, 46001, 46010, 47000, 49000, 50001, 50113, 50114, 51000, 52000,
                       54000, 58000, 58131, 58132, 62000, 65999, 66000, 66001, 66100, 66149, 70000, 74000, 78000,
                       90000, 100000, 110000, 110250, 154350, 250000)
# (rate, seconds) of the impulse trains; seed 7001 was used while the rules were worked out, 7002 only afterwards
TRAINS = {7001: ((44100, 60.0), (48000, 60.0), (50000, 40.0), (64000, 40.0), (66150, 40.0), (88200, 60.0), (96000, 60.0),
                 (132300, 40.0), (176400, 40.0), (192000, 40.0)),
          7002: ((88200, 30.0), (96000, 30.0), (176400, 20.0), (192000, 20.0))}
WINDOW_48K = (model.WINDOW_START, model.WINDOW_STOP)
FIXED_BLOCKS = tuple(range(8, 513, 4)) + (640, 768, 1024, 2048)   # conversion blocks tried at every rate alike
# (rate, its warm-up, warm-up of the 44.1 kHz capture): whole numbers of samples that are not whole conversion blocks
OTHER_WARMUPS = ((96000, 4.3125, 4.0), (48000, 59.0625, 58.5), (96000, 58.71875, 58.5), (192000, 57.3125, 57.0))
HOST_BLOCKS = (64, 960)                                       # host block sizes compared with the default 512
HOST_BLOCK_RATES = (88200, 96000, 192000)
UNMEASURED_RATES = (22050, 32000, 705600, 768000)             # constants the rules give where nothing was captured
TWIN_RATE = 192000                                            # where a shifted description was tried first
MOVED_SETTINGS = {"decay": 7.3, "size": 62.0, "width": 130.0}   # display values, the same in both captures


# ---------------------------------------------------------------- stimuli

def train(seed: int, rate: int, seconds: float) -> tuple:
    """(times, channels): impulses 1.0 to 1.3 s apart at any sample position, inputs alternating."""
    generator = np.random.default_rng(seed)
    times = [int(0.5 * rate) + int(generator.integers(0, int(0.3 * rate)))]
    while True:
        following = times[-1] + rate + int(generator.integers(0, int(0.3 * rate)))
        if following >= (seconds - 0.35) * rate:
            break
        times.append(following)
    times = np.array(times)
    return times, (np.arange(len(times)) + seed) % 2


def train_capture(seed: int, rate: int, seconds: float) -> tuple:
    """(times, channels, capture) of one impulse train."""
    times, channels = train(seed, rate, seconds)
    stimulus = revocean.impulses(seconds, [(int(t), int(c), AMPLITUDE) for t, c in zip(times, channels)], sample_rate=rate)
    return times, channels, revocean.capture(stimulus, TRAIN_SETTINGS, sample_rate=rate)


def programme(rate: int) -> np.ndarray:
    return (PROGRAMME_LEVEL * datasets.network_programme(rate, PROGRAMME_SEED)).astype(np.float32)


def probe(rate: int) -> np.ndarray:
    """One second: an impulse on each input, then a 30 ms noise burst on both."""
    generator = np.random.default_rng(PROBE_SEED)
    stimulus = np.zeros((int(PROBE_SECONDS * rate), 2))
    stimulus[int(0.05 * rate), 0] = 0.4
    stimulus[int(0.15 * rate), 1] = -0.4
    burst = generator.standard_normal((int(0.03 * rate), 2))
    start = int(0.25 * rate)
    stimulus[start:start + len(burst)] = 0.4 * burst / np.abs(burst).max()
    return stimulus.astype(np.float32)


# ---------------------------------------------------------------- the exact relation

def relation(rate: int, stimulus: np.ndarray, settings: dict, warmup: float = revocean.WARMUP_SECONDS,
             core_warmup: float = revocean.WARMUP_SECONDS) -> float:
    """Null in dB of to_host(capture at 44.1 kHz of to_internal(stimulus)) against the capture at `rate`."""
    reference = revocean.capture(stimulus, settings, sample_rate=rate, warmup=warmup).output.astype(np.float64)
    origin = int(round(warmup * rate))
    core_origin = int(round(core_warmup * CORE_RATE))
    streams = [cv.to_internal(stimulus[:, channel], rate, origin) for channel in (0, 1)]
    first = streams[0][0]
    if first < core_origin and max(np.abs(samples[:core_origin - first]).max() for _, samples in streams) > 0.0:
        raise ValueError("the stimulus starts before the 44.1 kHz capture can")
    core_stimulus = np.zeros((first + len(streams[0][1]) - core_origin, 2), np.float32)
    for channel, (_, samples) in enumerate(streams):
        core_stimulus[max(first - core_origin, 0):, channel] = samples[max(core_origin - first, 0):]
    core = revocean.capture(core_stimulus, settings, sample_rate=CORE_RATE, warmup=core_warmup).output.astype(np.float64)
    L, M = cv.lattice(rate)
    core_first = core_origin - cv.output_delay(CORE_RATE)        # internal index of the network output in core[0]
    covered = (L * (core_first + len(core) - cv.ZERO_CROSSINGS - 2) + cv.output_delay(rate)) // M - origin
    frames = min(len(reference), covered)
    predicted = np.stack([cv.to_host(core[:, channel], rate, core_first, origin, origin + frames) for channel in (0, 1)], axis=1)
    return revocean.null_db(predicted, reference[:frames])


@contextmanager
def changed(**values):
    """The converters module with some of its constants or functions replaced."""
    saved = {name: getattr(cv, name) for name in values}
    table, branches = (saved.get(name, getattr(cv, name)) for name in ("kernel_table", "_branches"))
    try:
        for name, value in values.items():
            setattr(cv, name, value)
        table.cache_clear()
        branches.cache_clear()
        yield
    finally:
        for name, value in saved.items():
            setattr(cv, name, value)
        table.cache_clear()
        branches.cache_clear()


def exact_wing_down(phase: float, step: float, limit: int) -> list:
    """Table entries of a down-converter wing at the mathematically exact positions (phase + k) * step."""
    exact_step = Fraction(step).limit_denominator(1 << 20)
    entries = []
    while int((Fraction(phase) + len(entries)) * exact_step) < limit:
        entries.append(int((Fraction(phase) + len(entries)) * exact_step))
    return entries


def variant_table(cutoff: float = cv.CUTOFF, beta: float = cv.KAISER_BETA, single: bool = True, window_end: int = 1):
    """A kernel_table with another cut-off, window or precision, sized by the module's current constants.

    The window of the module reaches its end value at the last entry (`window_end` 1); 0 puts the end one entry later.
    """
    def table() -> np.ndarray:
        index = np.arange(cv.WING)
        window = np.i0(beta * np.sqrt(1.0 - (index / (cv.WING - window_end)) ** 2)) / np.i0(beta)
        values = cutoff * np.sinc(cutoff * index / cv.ENTRIES) * window
        return values.astype(np.float32).astype(np.float64) if single else values
    return table


def at_rate(name: str, rate: int, value):
    """A stand-in for converters.<name> that gives `value` at `rate` and the module's own answer elsewhere."""
    original = getattr(cv, name)
    return lambda host_rate: value if host_rate == rate else original(host_rate)


def sign_variants(rate: int) -> dict:
    """The two other values of each clock sign."""
    into, back = cv.clock_signs(rate)
    names = {-1: "low", 0: "exact", 1: "high"}
    variants = {}
    for other in (-1, 0, 1):
        if other != into:
            variants[f"input clock taken as {names[other]}"] = {"clock_signs": at_rate("clock_signs", rate, (other, back))}
        if other != back:
            variants[f"output clock taken as {names[other]}"] = {"clock_signs": at_rate("clock_signs", rate, (into, other))}
    return variants


def element_variants(rate: int) -> dict:
    """One element of the converters changed at a time; delays and clock signs stay those of the rate."""
    a, b = cv.input_delay(rate), cv.output_delay(rate)
    into, back = cv.clock_signs(rate)
    fixed = {"input_delay": at_rate("input_delay", rate, a), "output_delay": at_rate("output_delay", rate, b),
             "clock_signs": at_rate("clock_signs", rate, (into, back))}
    twin = {}
    if rate == TWIN_RATE:      # scores like converters.py behind the network model; two lattice steps = two clock signs
        twin["delays (a + 2, b - 2) with both clock signs reversed"] = {
            "input_delay": at_rate("input_delay", rate, a + 2), "output_delay": at_rate("output_delay", rate, b - 2),
            "clock_signs": at_rate("clock_signs", rate, (-into, -back))}
    return {
        **twin,
        "table position of the down-converter not accumulated (exact)": {"_wing_down": exact_wing_down},
        "last table entry kept on the wing after the output sample": {"DROPPED_AFTER": 0},
        "input delay + 1 lattice step": {"input_delay": at_rate("input_delay", rate, a + 1)},
        "output delay + 1 lattice step": {"output_delay": at_rate("output_delay", rate, b + 1)},
        "input and output delay moved by one internal sample, sum unchanged": {
            "input_delay": at_rate("input_delay", rate, a + cv.lattice(rate)[0]),
            "output_delay": at_rate("output_delay", rate, b - cv.lattice(rate)[0])},
        "table in double precision": {"kernel_table": variant_table(single=False)},
        "window ending one entry after the table (i / 69632)": {"kernel_table": variant_table(window_end=0)},
        "table of 2048 entries per crossing": {"ENTRIES": 2048, "WING": 17 * 2048},
        "table of 8192 entries per crossing": {"ENTRIES": 8192, "WING": 17 * 8192},
        "table of 262144 entries per crossing (as good as the exact kernel)": {"ENTRIES": 262144, "WING": 17 * 262144},
        "Kaiser beta 5.9": {"kernel_table": variant_table(beta=5.9)},
        "cut-off 0.899": {"kernel_table": variant_table(cutoff=0.899)},
        "16 zero crossings": {"ZERO_CROSSINGS": 16, "WING": 16 * cv.ENTRIES, **fixed},
    }


def ablations(rate: int, variants: dict) -> dict:
    """Null of the exact relation on the probe with each variant; variants that change nothing are left out."""
    stimulus = probe(rate)
    result = {"converters.py": relation(rate, stimulus, TRAIN_SETTINGS)}
    reference = [cv.to_internal(stimulus[:, 0], rate, 0)[1], cv.to_host(stimulus[:, 0], rate, 0, 0, len(stimulus))]
    for name, patch in variants.items():
        with changed(**patch):
            trial = [cv.to_internal(stimulus[:, 0], rate, 0)[1], cv.to_host(stimulus[:, 0], rate, 0, 0, len(stimulus))]
            if all(len(new) == len(old) and np.array_equal(new, old) for new, old in zip(trial, reference)):
                continue
            result[name] = relation(rate, stimulus, TRAIN_SETTINGS)
    return result


# ---------------------------------------------------------------- measurements

def latency_table() -> dict:
    measured = {}
    for rate in LATENCY_PROBE_RATES:
        measured[rate] = train_capture(101, rate, 2.0)[2].latency
    for rate in RELATION_RATES:
        measured[rate] = revocean.capture(programme(rate), PROGRAMME_SETTINGS, sample_rate=rate).latency
    rows = {str(rate): {"measured": measured[rate], "rule": cv.reported_latency(rate)} for rate in sorted(measured)}
    return {"rule": "4 * floor(11 * rate / 44100 + 0.5) host samples",
            "rates": len(rows), "mismatches": sum(row["measured"] != row["rule"] for row in rows.values()), "table": rows}


def clock_report(rate: int) -> dict:
    """Signs of the two clocks, the first block from which they stay, and the signs after 100 s."""
    L, M = cv.lattice(rate)
    block = cv.reported_latency(rate)
    into = cv.Clock(Fraction(L, M), cv.lookahead(Fraction(L, M)))
    back = cv.Clock(Fraction(M, L), cv.lookahead(Fraction(M, L)))
    history = []
    for _ in range(int(100.0 * rate / block)):
        back.call(into.call(block))
        history.append((into.error_sign(), back.error_sign()))
    history = np.array(history)
    changes = np.nonzero(np.any(history != history[-1], axis=1))[0]
    return {"input": int(history[-1][0]), "output": int(history[-1][1]), "blockHostSamples": block,
            "settledAfterBlocks": int(changes[-1]) + 1 if len(changes) else 0,
            "sameAsConverters": tuple(int(v) for v in history[-1]) == cv.clock_signs(rate)}


def block_size_test() -> dict:
    """Rates with a drifting clock whose confirmed pair of clock signs a converter run in other blocks would give."""
    drifting = [rate for rate in RELATION_RATES if cv.clock_signs(rate) != (0, 0)]

    def matches(block_of) -> int:
        return sum(cv.clock_signs(rate, block_of(rate), 0.25) == cv.clock_signs(rate) for rate in drifting)

    fixed = {block: matches(lambda rate: block) for block in FIXED_BLOCKS}
    best = max(fixed.values())
    return {
        "ratesWithDriftingClock": len(drifting),
        "reportedLatency": matches(cv.reported_latency),
        "reportedLatencyTimes": {f"{num}/{den}": matches(lambda rate: cv.reported_latency(rate) * num // den)
                                 for num, den in ((1, 4), (1, 2), (3, 4), (5, 4), (3, 2), (2, 1), (3, 1), (4, 1))},
        "reportedLatencyPlus": {str(offset): matches(lambda rate: cv.reported_latency(rate) + offset)
                                for offset in (-8, -4, -2, -1, 1, 2, 4, 8)},
        "oneMillisecondRounded": matches(lambda rate: int(round(rate / 1000))),
        "bestFixedBlock": {"matches": best, "blocks": [block for block, count in fixed.items() if count == best],
                           "blocksTried": len(fixed)},
    }


def primed_clock_signs(rate: int) -> tuple:
    """Clock signs when each converter starts with only its look-ahead of zeros and is then primed: the input
    converter with one more sample (its one output is not an internal sample), the output converter with none."""
    L, M = cv.lattice(rate)
    clocks = [cv.Clock(Fraction(L, M), cv.lookahead(Fraction(L, M))), cv.Clock(Fraction(M, L), cv.lookahead(Fraction(M, L)))]
    for clock, extra in zip(clocks, (1, 0)):
        clock.pending = -clock.start
        clock.call(clock.start + extra)
    for _ in range(int(0.5 * rate / cv.reported_latency(rate))):
        clocks[1].call(clocks[0].call(cv.reported_latency(rate)))
    return clocks[0].error_sign(), clocks[1].error_sign()


def other_warmups() -> list:
    rows = []
    for rate, warmup, core_warmup in OTHER_WARMUPS:
        samples = int(round(warmup * rate))
        rows.append({"hostRateHz": rate, "warmupSeconds": warmup, "warmupSamples": samples,
                     "warmupModuloBlock": samples % cv.reported_latency(rate), "coreWarmupSeconds": core_warmup,
                     "relationNullDb": relation(rate, probe(rate), TRAIN_SETTINGS, warmup, core_warmup)})
    return rows


def host_block_test() -> dict:
    """Is the output bit-identical when the host calls the plug-in with other block sizes?"""
    result = {}
    for rate in HOST_BLOCK_RATES:
        reference = revocean.capture(probe(rate), TRAIN_SETTINGS, sample_rate=rate).output
        result[str(rate)] = {}
        for block in HOST_BLOCKS:                         # the Analyzer renders whole blocks, so the lengths differ
            output = revocean.capture(probe(rate), TRAIN_SETTINGS, sample_rate=rate, block_size=block).output
            frames = min(len(output), len(reference))
            result[str(rate)][str(block)] = bool(np.array_equal(output[:frames], reference[:frames]))
    return result


def moved_settings(rate: int) -> dict:
    """The relation with Decay, Size and Width moved in both captures."""
    settings = {key: revocean.normalised(key, value) for key, value in MOVED_SETTINGS.items()}
    return {"hostRateHz": rate, "settings": MOVED_SETTINGS, "relationNullDb": relation(rate, probe(rate), settings)}


# ---------------------------------------------------------------- the first-pass network through the converters

def window(rate: int) -> tuple:
    """Raw window at `rate` that holds the network times of raw [1200, 2500) at 48 kHz."""
    shift = [cv.fixed_delay_host_samples(rate) + (edge - cv.fixed_delay_host_samples(48000)) * rate / 48000 for edge in WINDOW_48K]
    return int(np.ceil(shift[0])), int(np.ceil(shift[1]))


def predict(network: model.Network, tank_predelay: int, rate: int, origin: int, channel: int, span: tuple) -> np.ndarray:
    """Raw response [span) of both outputs to a unit impulse on `channel` at absolute host sample `origin`.

    The network writes internal sample m into its lines at m + tank_predelay.
    """
    first, stream = cv.to_internal(np.ones(1), rate, origin)
    written = first + np.arange(len(stream)) + tank_predelay
    outputs = [network.output(written, stream, channel, group, model.WINDOW_FRAMES) for group in (0, 1)]
    return np.stack([cv.to_host(output, rate, int(written[0]), origin + span[0], origin + span[1]) for output in outputs], axis=1)


def first_pass(seed: int, rate: int, seconds: float, constants: dict) -> dict:
    times, channels, capture = train_capture(seed, rate, seconds)
    warmup = int(round(revocean.WARMUP_SECONDS * rate))
    L, M = cv.lattice(rate)
    network = model.Network(constants, (warmup + int(seconds * rate)) * M // L + 8192)
    span = window(rate)
    difference, energy = [], []
    for time, channel in zip(times, channels):
        reference = capture.output[int(time) + span[0]:int(time) + span[1]].astype(np.float64) / AMPLITUDE
        predicted = predict(network, constants["tank_predelay_internal_samples"], rate, warmup + int(time), int(channel), span)
        difference.append(np.sum((predicted - reference) ** 2))
        energy.append(np.sum(reference ** 2))
    each = 10 * np.log10(np.array(difference) / np.array(energy))
    return {"hostRateHz": rate, "seed": seed, "seconds": seconds, "impulses": int(len(times)), "window": list(span),
            "nullDb": float(10 * np.log10(np.sum(difference) / np.sum(energy))),
            "bestDb": float(each.min()), "medianDb": float(np.median(each)), "worstDb": float(each.max())}


def first_pass_without_rules(rate: int, seconds: float, constants: dict) -> dict:
    """The working train scored with the lattice delays only: clocks taken as exact, nothing accumulated or dropped."""
    with changed(clock_signs=lambda r: (0, 0), _wing_down=exact_wing_down, DROPPED_AFTER=0):
        return first_pass(7001, rate, seconds, constants)


# ---------------------------------------------------------------- the data file

def main() -> None:
    revocean.identity()
    constants = model.load_constants()
    data = {
        "kernel": {"cutoff": cv.CUTOFF, "zeroCrossings": cv.ZERO_CROSSINGS, "kaiserBeta": cv.KAISER_BETA,
                   "entriesPerCrossing": cv.ENTRIES, "interpolation": "none (entry at or below)",
                   "entriesSinglePrecision": "from findings/first_order.md; not resolved here (see the ablations)",
                   "lastEntryNotUsedAfterOutputSample": cv.DROPPED_AFTER, "lookaheadMarginSamples": cv.LOOKAHEAD_MARGIN},
        "operatingPoint": {"macro_percent": 0, "mix_percent": 100, "warmup_seconds": revocean.WARMUP_SECONDS,
                           "trains": "Decay 0.5 s", "programme": "Decay 1.5 s, datasets.network_programme seed 4242 at 0.8"},
        "latency": latency_table(),
    }
    print(f"latency: {data['latency']['rates']} rates, {data['latency']['mismatches']} mismatches", flush=True)
    rates = {}
    for rate in (CORE_RATE,) + RELATION_RATES:
        rates[str(rate)] = cv.describe(rate)
    for rate in RELATION_RATES:
        rates[str(rate)]["relationProgrammeNullDb"] = relation(rate, programme(rate), PROGRAMME_SETTINGS)
        rates[str(rate)]["clock"] = clock_report(rate)
        rates[str(rate)]["clockSignTestsProbeNullDb"] = ablations(rate, sign_variants(rate))
        print(f"{rate}: relation {rates[str(rate)]['relationProgrammeNullDb']:.2f} dB; "
              + "; ".join(f"{name} {value:.1f}" for name, value in rates[str(rate)]["clockSignTestsProbeNullDb"].items()), flush=True)
    data["rates"] = rates
    data["ablations"] = {str(rate): ablations(rate, element_variants(rate)) for rate in ABLATION_RATES}
    for rate, rows in data["ablations"].items():
        print(f"{rate}: " + "; ".join(f"{name} {value:.1f}" for name, value in rows.items()), flush=True)
    data["clockBlockSize"] = block_size_test()
    data["clockBlockSize"]["ratesWithSameSignsFromPrimedStart"] = sum(primed_clock_signs(rate) == cv.clock_signs(rate)
                                                                      for rate in RELATION_RATES)
    data["otherWarmups"] = other_warmups()
    data["movedSettings"] = moved_settings(96000)
    data["hostBlockSizeBitIdentical"] = host_block_test()
    for name in ("clockBlockSize", "otherWarmups", "movedSettings", "hostBlockSizeBitIdentical"):
        print(name, json.dumps(data[name]), flush=True)
    data["firstPass"] = {"network": "first_order_model.Network with tide_structural_data/first_order.json, unchanged",
                         "window": "network times of raw [1200, 2500) at 48 kHz",
                         "workingTrains": [first_pass(7001, rate, seconds, constants) for rate, seconds in TRAINS[7001]],
                         "freshTrains": [first_pass(7002, rate, seconds, constants) for rate, seconds in TRAINS[7002]],
                         "latticeDelaysOnly": [first_pass_without_rules(rate, seconds, constants)
                                               for rate, seconds in TRAINS[7001] if rate != CORE_RATE]}
    for name in ("workingTrains", "freshTrains", "latticeDelaysOnly"):
        for row in data["firstPass"][name]:
            print(f"{name} {row['hostRateHz']}: {row['nullDb']:.2f} dB (median {row['medianDb']:.2f}, worst {row['worstDb']:.2f}, "
                  f"{row['impulses']} impulses, window {row['window']})", flush=True)
    data["unmeasuredRatesInferred"] = {str(rate): cv.describe(rate) for rate in UNMEASURED_RATES}
    DATA.parent.mkdir(parents=True, exist_ok=True)
    DATA.write_text(json.dumps(data, indent=1))
    print(f"wrote {DATA}")


if __name__ == "__main__":
    main()
