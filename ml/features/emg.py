"""EMG steering features (ROADMAP §5.2): RMS envelope, hysteresis detector, TD features."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

WIN_MS = 125  # within the 100-150 ms range in §5.2
HOP_MS = 25
DEBOUNCE_MS = 80
OFF_FRACTION = 0.6


class EnvelopeTracker:
    """Streaming RMS envelope over WIN_MS windows, one value every HOP_MS."""

    def __init__(self, fs: float, win_ms: int = WIN_MS, hop_ms: int = HOP_MS) -> None:
        self.win = round(fs * win_ms / 1000)
        self.hop = round(fs * hop_ms / 1000)
        self._buf = np.zeros(self.win)
        self._filled = 0
        self._since_hop = 0

    def process(self, x: np.ndarray) -> list[float]:
        """Feed samples (1-D, filtered EMG in µV); returns envelope values completed."""
        out = []
        for v in np.asarray(x, dtype=float).ravel():
            self._buf = np.roll(self._buf, -1)
            self._buf[-1] = v
            self._filled = min(self.win, self._filled + 1)
            self._since_hop += 1
            if self._since_hop >= self.hop and self._filled == self.win:
                self._since_hop = 0
                out.append(float(np.sqrt(np.mean(self._buf**2))))
        return out


def rms_envelope(x: np.ndarray, fs: float, win_ms: int = WIN_MS, hop_ms: int = HOP_MS):
    """Batch envelope. Returns (t_s of each window end, envelope)."""
    x = np.asarray(x, dtype=float).ravel()
    win = round(fs * win_ms / 1000)
    hop = round(fs * hop_ms / 1000)
    ends = np.arange(win, len(x) + 1, hop)
    sq = np.concatenate([[0.0], np.cumsum(x**2)])
    env = np.sqrt((sq[ends] - sq[ends - win]) / win)
    return (ends - 1) / fs, env


@dataclass(frozen=True)
class Thresholds:
    on_uv: float
    off_uv: float


def thresholds(rest_env: np.ndarray, clench_env: np.ndarray) -> Thresholds:
    """§5.2: ON = rest_p95 + 0.5 (clench_p50 - rest_p95); OFF = 60 % of ON."""
    rest_p95 = float(np.percentile(rest_env, 95))
    clench_p50 = float(np.percentile(clench_env, 50))
    if clench_p50 <= rest_p95:
        raise ValueError("clench envelope not above rest: check electrodes and redo calibration")
    on = rest_p95 + 0.5 * (clench_p50 - rest_p95)
    return Thresholds(on, OFF_FRACTION * on)


class HysteresisDetector:
    """ON when envelope >= on for DEBOUNCE_MS; OFF when < off for DEBOUNCE_MS."""

    def __init__(self, th: Thresholds, debounce_ms: int = DEBOUNCE_MS) -> None:
        self.th = th
        self.debounce_ms = debounce_ms
        self.active = False
        self._pending_since: int | None = None

    def update(self, env_uv: float, t_ms: int) -> bool:
        wants = env_uv >= self.th.on_uv if not self.active else env_uv >= self.th.off_uv
        if wants == self.active:
            self._pending_since = None
        elif self._pending_since is None:
            self._pending_since = t_ms
        if self._pending_since is not None and t_ms - self._pending_since >= self.debounce_ms:
            self.active = wants
            self._pending_since = None
        return self.active


def td_features(window: np.ndarray, eps_uv: float = 1.0) -> dict[str, float]:
    """Classic EMG time-domain features: MAV, RMS, WL, ZC, SSC (for later ML)."""
    x = np.asarray(window, dtype=float).ravel()
    d = np.diff(x)
    zc = np.sum((x[:-1] * x[1:] < 0) & (np.abs(d) >= eps_uv))
    ssc = np.sum((d[:-1] * d[1:] < 0) & ((np.abs(d[:-1]) >= eps_uv) | (np.abs(d[1:]) >= eps_uv)))
    return {
        "mav": float(np.mean(np.abs(x))),
        "rms": float(np.sqrt(np.mean(x**2))),
        "wl": float(np.sum(np.abs(d))),
        "zc": float(zc),
        "ssc": float(ssc),
    }
