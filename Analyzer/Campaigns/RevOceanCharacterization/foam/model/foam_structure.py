"""The Foam diffuser as a structure: S stages of four parallel Schroeder all-passes, a Hadamard mix between stages.

    AP(D, g):  y[n] = g x[n] + x[n - D] - g y[n - D]
    v = 0.5 (x, x, x, x);  for each stage s: v = AP_s(v) channel by channel;  between stages v = 0.5 H4 v;  out = sum of v

H4 is the Sylvester Hadamard matrix of order 4.  A structure is {"delays": [[4 ints] per stage], "gains": [g per stage]
or [[4 g] per stage]}, delays in CHANNEL order (channel 0 receives the sum row of the mix in front of it).
kernel(n, S) is its impulse response; diffuse(x, S) runs a signal through it (no truncation)."""
from __future__ import annotations
import json
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
H4 = np.array([[1, 1, 1, 1], [1, -1, 1, -1], [1, 1, -1, -1], [1, -1, -1, 1]], dtype=float)


def ap(x, D, g):
    D = int(D)
    if D >= len(x):
        return g * x
    y = g * x
    y[D:] += x[:-D]
    for s in range(D, len(x), D):
        e = min(s + D, len(x))
        y[s:e] -= g * y[s - D:e - D]
    return y


def stage_gains(S, s):
    g = S["gains"][s]
    return [g] * 4 if np.ndim(g) == 0 else list(g)


def diffuse(x, S, upto=None):
    """x (n,) -> (n,).  `upto`: number of stages that are real; the rest act as their direct gain 1 (for peeling)."""
    v = [0.5 * np.asarray(x, dtype=float)] * 4
    n_stages = len(S["delays"])
    for s in range(n_stages):
        g = stage_gains(S, s)
        v = [ap(v[c], S["delays"][s][c], g[c]) for c in range(4)]
        if s < n_stages - 1:
            v = [0.5 * sum(H4[r, c] * v[c] for c in range(4)) for r in range(4)]
    return v[0] + v[1] + v[2] + v[3]


def kernel(n, S):
    d = np.zeros(n); d[0] = 1.0
    return diffuse(d, S)


def load(name="structure_final.json"):
    return json.loads((HERE / name).read_text())
