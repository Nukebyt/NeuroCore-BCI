"""SSVEP speed decoder (ROADMAP §5.3): CCA and FBCCA, sliding windows, idle detection.

FBCCA follows Chen et al., J. Neural Eng. 12 (2015) 046008: sub-band n starts at n x 8 Hz,
weights w(n) = n^-1.25 + 0.25, score = sum_n w(n) rho_n^2.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from enum import Enum

import numpy as np
from scipy import signal


class Ssvep(Enum):
    NONE = 0
    PLUS = 1
    MINUS = 2


def reference_signals(freq_hz: float, fs: float, n: int, harmonics: int = 3) -> np.ndarray:
    t = np.arange(n) / fs
    cols = []
    for h in range(1, harmonics + 1):
        cols += [np.sin(2 * np.pi * h * freq_hz * t), np.cos(2 * np.pi * h * freq_hz * t)]
    return np.column_stack(cols)


def cca_max_corr(x: np.ndarray, y: np.ndarray) -> float:
    """Largest canonical correlation between x [n, p] and y [n, q]."""
    x = np.asarray(x, dtype=float)
    x = (x - x.mean(axis=0)).reshape(len(x), -1)
    y = y - y.mean(axis=0)
    qx, _ = np.linalg.qr(x)
    qy, _ = np.linalg.qr(y)
    s = np.linalg.svd(qx.T @ qy, compute_uv=False)
    return float(np.clip(s[0], 0.0, 1.0))


@dataclass
class FbccaModel:
    fs: float
    freqs_hz: dict[Ssvep, float]
    harmonics: int = 3
    n_bands: int = 5
    _sos: list[np.ndarray] = field(init=False, repr=False)
    _w: np.ndarray = field(init=False, repr=False)

    def __post_init__(self) -> None:
        hi = min(90.0, 0.45 * self.fs)
        self._sos = [
            signal.cheby1(4, 0.5, [8.0 * n - 2.0, hi], btype="bandpass", fs=self.fs, output="sos")
            for n in range(1, self.n_bands + 1)
        ]
        n = np.arange(1, self.n_bands + 1)
        self._w = n**-1.25 + 0.25

    def scores(self, x: np.ndarray) -> dict[Ssvep, float]:
        """Weighted FBCCA score per stimulus for one window x [n, channels]."""
        x = np.asarray(x, dtype=float).reshape(len(x), -1)
        bands = [signal.sosfiltfilt(sos, x, axis=0) for sos in self._sos]
        out = {}
        for label, f in self.freqs_hz.items():
            ref = reference_signals(f, self.fs, len(x), self.harmonics)
            rho = np.array([cca_max_corr(b, ref) for b in bands])
            out[label] = float(np.sum(self._w * rho**2))
        return out


@dataclass
class SsvepDecoder:
    """Sliding-window decoder. Emits a decision only after `agree` consecutive windows agree."""

    model: FbccaModel
    idle_threshold: float
    window_s: float = 2.0
    step_s: float = 0.25
    agree: int = 2
    n_channels: int = 2

    def __post_init__(self) -> None:
        self._win = round(self.window_s * self.model.fs)
        self._step = round(self.step_s * self.model.fs)
        self._buf: deque[np.ndarray] = deque(maxlen=self._win)
        self._since = 0
        self._recent: deque[Ssvep] = deque(maxlen=self.agree)

    def classify(self, x: np.ndarray) -> Ssvep:
        s = self.model.scores(x)
        best = max(s, key=lambda k: s[k])
        return best if s[best] >= self.idle_threshold else Ssvep.NONE

    def process(self, x: np.ndarray) -> list[Ssvep]:
        """Feed samples [n, channels]; returns one decision per completed step."""
        out = []
        for row in np.asarray(x, dtype=float).reshape(-1, self.n_channels):
            self._buf.append(row)
            self._since += 1
            if len(self._buf) == self._win and self._since >= self._step:
                self._since = 0
                self._recent.append(self.classify(np.array(self._buf)))
                agreed = len(self._recent) == self.agree and len(set(self._recent)) == 1
                out.append(self._recent[-1] if agreed else Ssvep.NONE)
        return out


def calibrate_idle_threshold(model: FbccaModel, rest_windows: list[np.ndarray], k: float = 3.0):
    """Per-user idle threshold: mean + k * std of the best score while not looking at an LED."""
    best = [max(model.scores(w).values()) for w in rest_windows]
    return float(np.mean(best) + k * np.std(best))
