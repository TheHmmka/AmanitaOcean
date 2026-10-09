"""Attended session with the reference's Audio Unit, mode switched WHILE probes run (the only way that is known to work):
identical probes until Abyss is heard and on to a fixed probe index T0, then a common sequence that starts at processed time
30*T0 seconds: the batch of probe3.py, its ring-out and its continuous recordings. Two sessions with the same T0 can be
compared frame for frame however long their owners took to switch.
Usage: probe5.py <label> switch new|<folder of a running session> [T0]"""
import sys, json, time
from pathlib import Path
import numpy as np
sys.path.insert(0, "/Users/nespesha/Workspace/AmanitaOcean/Analyzer/Campaigns/RevOceanCharacterization")
import revocean as ro, revocean_session as rs, datasets

label, variant = sys.argv[1], sys.argv[2]
OUT = Path(__file__).parent / f"session_{label}"; OUT.mkdir(exist_ok=True)
SR = 48000
PROBES = 100
INSTRUCTION = {
    "switch": "Press Demo, then press Start WITHOUT touching the mode. About 10 seconds after Start switch the mode "
              "to Abyss with the arrows and leave it there. Change nothing else.",
    "before": "Press Demo, select the mode Abyss with the arrows next to the mode name, change nothing else, then press Start.",
}[variant]
def at(s): return int(round(s * SR))
def pad(x, seconds): return np.concatenate([x, np.zeros((at(seconds), 2), np.float32)])
t = np.arange(at(7.5)) / SR; fade = np.minimum(1, np.minimum(t, t[::-1]) / 0.02)
probe = np.zeros((at(20), 2), np.float32)
probe[at(0.5):at(8.0), 0] = 0.2 * np.sin(2 * np.pi * 1000 * t) * fade
probe[at(0.5):at(8.0), 1] = 0.2 * np.sin(2 * np.pi * 3000 * t) * fade
probe[at(10.0), 0] = 0.5; probe[at(13.0), 1] = 0.5
f = np.fft.rfftfreq(at(6), 1 / SR)
def share(P, lo, hi): return float(10 * np.log10(P[(f >= lo) & (f < hi)].sum() / P.sum() + 1e-30))
def describe(y):
    y = y.astype(np.float64)
    PL = np.abs(np.fft.rfft(y[at(1.5):at(7.5), 0] * np.hanning(at(6)))) ** 2
    PR = np.abs(np.fft.rfft(y[at(1.5):at(7.5), 1] * np.hanning(at(6)))) ** 2
    h = y[at(10.0):at(13.0), 0]
    e = lambda a, b: float(10 * np.log10((h[at(a):at(b)] ** 2).sum() + 1e-30))
    return {"L1k_within3Hz": round(share(PL, 997, 1003), 1), "L_400to600": round(share(PL, 400, 600), 1), "L_below900": round(share(PL, 20, 900), 1),
            "L_above1100": round(share(PL, 1100, 20000), 1), "R_1400to1600": round(share(PR, 1400, 1600), 1), "R_below2700": round(share(PR, 20, 2700), 1),
            "imp_0to0p3": round(e(0, 0.3), 1), "imp_0p3to1": round(e(0.3, 1.0), 1), "imp_1to3": round(e(1.0, 3.0), 1),
            "level": round(float(10 * np.log10((y ** 2).mean() + 1e-30)), 2)}

rng = np.random.default_rng(20261008)
train = np.zeros((at(24), 2), np.float32)
for k in range(20): train[at(0.25 + k), 0] = 0.5
imp3 = np.zeros((at(30), 2), np.float32); imp3[at(0.25), 0] = 0.5; imp3[at(6.25), 1] = 0.5; imp3[at(12.25), :] = 0.4
prog = datasets.network_programme(SR, seed=4242)
noise = np.clip(rng.standard_normal((at(20), 2)) * 0.05, -0.5, 0.5).astype(np.float32)
t6 = np.arange(at(6)) / SR; fade6 = np.minimum(1, np.minimum(t6, t6[::-1]) / 0.01)
sine1k = np.zeros((at(6), 2), np.float32); sine1k[:, 0] = 0.25 * np.sin(2 * np.pi * 1000 * t6) * fade6
sine220 = np.zeros((at(6), 2), np.float32); sine220[:, :] = (0.25 * np.sin(2 * np.pi * 220 * t6) * fade6)[:, None]
burst = np.clip(rng.standard_normal((at(2), 2)) * 0.1, -0.5, 0.5).astype(np.float32)
def S(decay, macro=0.0, size=100.0):
    return {"decay": ro.normalised("decay", decay), "macro": macro, "size": ro.normalised("size", size)}
cases = [
    ("c01_macro0_decay0p5_train", train, S(0.5)),
    ("c02_macro100_decay0p5_train", train, S(0.5, 1.0)),
    ("c03_macro100_decay2_imp3", imp3, S(2.0, 1.0)),
    ("c04_macro100_decay2_prog", pad(prog, 16), S(2.0, 1.0)),
    ("c05_macro100_decay2_noise", pad(noise, 10), S(2.0, 1.0)),
    ("c06_macro100_decay2_sine1k", pad(sine1k, 12), S(2.0, 1.0)),
    ("c07_macro100_decay2_sine220", pad(sine220, 12), S(2.0, 1.0)),
    ("c08_macro50_decay2_imp3", imp3, S(2.0, 0.5)),
    ("c09_macro25_decay2_imp3", imp3, S(2.0, 0.25)),
    ("c10_macro75_decay2_imp3", imp3, S(2.0, 0.75)),
    ("c11_macro100_decay2_imp3_again", imp3, S(2.0, 1.0)),
    ("c12_macro100_decay2_size60_imp3", imp3, S(2.0, 1.0, 60.0)),
    ("c13_macro100_decay2_size150_imp3", imp3, S(2.0, 1.0, 150.0)),
    ("c14_macro100_decay8_imp3", imp3, S(8.0, 1.0)),
    ("c16_macro0_decay2_prog_late", prog, S(2.0)),
]
import datetime
folder = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != "new" else None
t0 = int(sys.argv[4]) if len(sys.argv) > 4 else None
if folder:
    session = rs.Session.attach(Path(folder))
else:
    session = rs.Session.open(instruction=INSTRUCTION, sample_rate=SR, plugin_format="AudioUnit")
print("window open:", session.folder, flush=True)
info = {"label": label, "sessionFolder": str(session.folder), "probes": [], "cases": [], "firstAbyssProbe": None, "t0": t0}
def save_info(): (OUT / "info_common.json").write_text(json.dumps(info, indent=1, default=str))
created = datetime.datetime.fromisoformat(session.record()["instanceCreatedAt"])
def seconds_left():   # until the host stops by itself, 19 minutes after the creation of the instance
    return 19 * 60 - (datetime.datetime.now(created.tzinfo) - created).total_seconds()
complete = True
try:
    if not folder:
        session.wait_until_started(1000.0)
    print("started", time.strftime("%H:%M:%S"), "seconds left", round(seconds_left()), flush=True)
    done = sorted(p.name for p in (session.folder / "results").glob("*-p[0-9][0-9][0-9]") if (p / "record.json").exists())
    k = int(done[-1][-3:]) + 1 if done else 0
    first = None
    while t0 is None or k < t0:
        if seconds_left() < 60:
            complete = False; print("OUT OF TIME in the probes", flush=True); break
        rec = session.run(rs.case(probe, S(2.0, 1.0), SR), label=f"p{k:03d}", timeout=120.0).recordings[0]
        d = describe(rec.output); d.update(index=k, wall=time.strftime("%H:%M:%S"), warmupSeconds=rec.warmup_seconds)
        if first is None and d["L_400to600"] > -40.0:
            first = k; info["firstAbyssProbe"] = k
            if t0 is None:
                t0 = -(-(k + 20) // 100) * 100
                if (t0 - k) * 0.85 + 130 > seconds_left() - 20:
                    t0 = -(-(k + 10) // 10) * 10
                info["t0"] = t0
            print("ABYSS from probe", k, "- the common sequence starts behind probe", t0 - 1, flush=True)
        if (first is not None and (k - first < 10 or k % 5 == 0 or k == t0 - 1)) or (first is None and k % 10 == 0):
            np.save(OUT / f"p{k:03d}.npy", rec.output.astype(np.float32))
        info["probes"].append(d); print(json.dumps(d), flush=True); k += 1
    save_info()
    session.screenshot(session.folder / "window-after-probes.png")
    if complete:
        for name, stim, settings in cases:
            if seconds_left() < 45:
                complete = False; print("OUT OF TIME before", name, flush=True); break
            rec = session.run(rs.case(stim, settings, SR), label=name, timeout=300.0).recordings[0]
            np.save(OUT / f"{name}.npy", rec.output.astype(np.float32)); np.save(OUT / f"{name}.stimulus.npy", stim)
            info["cases"].append({"name": name, "firstFrame": int(rec.first_frame), "warmupSeconds": rec.warmup_seconds, "latency": int(rec.latency),
                                  "settings": rec.settings, "frames": int(len(rec.output)), "peak": float(np.abs(rec.output).max())})
            print(name, "ok peak", round(info["cases"][-1]["peak"], 4), flush=True)
        save_info()
    if complete:
        s = S(2.0, 1.0)
        steps = rs.flush(s, SR) + [rs.play(burst), rs.set_parameters(dict(s, decay=0.0)), rs.silence(at(40), record=True)]
        res = session.run(steps, label="c15_ringout", timeout=300.0)
        for i, rec in enumerate(res.recordings):
            np.save(OUT / f"c15_ringout_{i}.npy", rec.output.astype(np.float32))
            info["cases"].append({"name": f"c15_ringout_{i}", "firstFrame": int(rec.first_frame), "frames": int(len(rec.output)), "peak": float(np.abs(rec.output).max())})
        np.save(OUT / "c15_ringout.stimulus.npy", burst); save_info()
        print("ring-out ok, seconds left", round(seconds_left()), flush=True)
    if complete and seconds_left() > 75:
        def keep(name, recordings, stimuli):
            for i, (rec, stim) in enumerate(zip(recordings, stimuli, strict=True)):
                tag = name if len(recordings) == 1 else f"{name}_{i:02d}"
                np.save(OUT / f"{tag}.npy", rec.output.astype(np.float32))
                info["cases"].append({"name": tag, "firstFrame": int(rec.first_frame), "warmupSeconds": rec.warmup_seconds,
                                      "settings": rec.settings, "frames": int(len(rec.output)), "peak": float(np.abs(rec.output).max())})
            np.save(OUT / f"{name}.stimulus.npy", stimuli[0])
            save_info()
            print(name, "ok", flush=True)
        def impulse_map(seconds, channel):
            x = np.zeros((at(seconds), 2), np.float32)
            n = 0
            while 1.0 + 4.0137 * n < seconds - 4.5:
                x[at(1.0 + 4.0137 * n), channel] = 0.5
                n += 1
            return x
        tt = np.arange(at(120)) / SR
        tone = np.zeros((at(140), 2), np.float32)
        tone[:at(120), 0] = 0.2 * np.sin(2 * np.pi * 1000 * tt) * np.minimum(1, np.minimum(tt, tt[::-1]) / 0.02)
        t20 = np.arange(at(20)) / SR
        tone20 = np.zeros((at(20), 2), np.float32); tone20[:, 0] = 0.2 * np.sin(2 * np.pi * 1000 * t20)   # whole cycles: joins without a click
        ts = np.arange(at(60)) / SR
        sweep = np.zeros((at(70), 2), np.float32)
        sweep[:at(60), 0] = 0.2 * np.sin(2 * np.pi * 50 * 60 / np.log(240) * (np.exp(ts / 60 * np.log(240)) - 1)) * np.minimum(1, np.minimum(ts, ts[::-1]) / 0.02)
        quiet = np.zeros((at(60), 2), np.float32)
        for name, stim, settings in (("x01_silence_macro100_decay0p5", quiet, S(0.5, 1.0)),
                                     ("x02_impulse_map_left_macro100_decay0p5", impulse_map(600, 0), S(0.5, 1.0)),
                                     ("x03_impulse_map_right_macro100_decay0p5", impulse_map(300, 1), S(0.5, 1.0)),
                                     ("x04_tone1k_120s_macro100_decay2", tone, S(2.0, 1.0)),
                                     ("x06_sweep_50_to_12k_macro100_decay2", sweep, S(2.0, 1.0))):
            keep(name, session.run(rs.case(stim, settings, SR), label=name, timeout=600.0).recordings, [stim])
        steps = rs.flush(S(2.0, 0.0), SR) + [rs.play(tone20)]
        for macro in (0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0):
            steps += [rs.set_parameters(S(2.0, macro)), rs.play(tone20)]
        keep("x05_macro_steps_tone1k_decay2", session.run(steps, label="x05_macro_steps", timeout=600.0).recordings, [tone20] * 11)
    session.screenshot(session.folder / "window-end.png")
finally:
    info["record"] = session.close()
    save_info()
    print("session closed:", str(info["record"])[:200], flush=True)
