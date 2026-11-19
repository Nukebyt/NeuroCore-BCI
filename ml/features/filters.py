"""Preprocessing filters (ROADMAP §5.1): 50 Hz notch + harmonics, band-pass, streaming state.

All filters are second-order sections so they port directly to biquads on the ESP32-S3.
"""

from __future__ import annotations

import numpy as np
from scipy import signal

MAINS_HZ = 50.0  # India


def notch_sos(fs: float, mains_hz: float = MAINS_HZ, q: float = 30.0) -> np.ndarray:
    """Notch at mains and every harmonic below Nyquist."""
    sections = []
    k = 1
    while k * mains_hz < 0.5 * fs - 1.0:
        b, a = signal.iirnotch(k * mains_hz, q, fs=fs)
        sections.append(signal.tf2sos(b, a))
        k += 1
    return np.vstack(sections)


def bandpass_sos(fs: float, lo_hz: float, hi_hz: float, order: int = 4) -> np.ndarray:
    return signal.butter(order, [lo_hz, hi_hz], btype="bandpass", fs=fs, output="sos")


def eeg_sos(fs: float) -> np.ndarray:
    """EEG chain: notch + 1-40 Hz band-pass."""
    return np.vstack([notch_sos(fs), bandpass_sos(fs, 1.0, 40.0)])


def emg_sos(fs: float) -> np.ndarray:
    """EMG chain: notch + 20-450 Hz band-pass. Needs fs >= 1 kHz (ROADMAP §5.1)."""
    if fs < 1000.0:
        raise ValueError("EMG lane needs >= 1 kSPS")
    return np.vstack([notch_sos(fs), bandpass_sos(fs, 20.0, 450.0)])


class StreamingFilter:
    """Applies an SOS cascade chunk by chunk; output equals filtering the whole signal."""

    def __init__(self, sos: np.ndarray, n_channels: int) -> None:
        self.sos = sos
        self._zi = np.zeros((sos.shape[0], 2, n_channels))

    def process(self, x: np.ndarray) -> np.ndarray:
        x = np.asarray(x, dtype=float)
        if x.ndim == 1:
            x = x[:, None]
        y, self._zi = signal.sosfilt(self.sos, x, axis=0, zi=self._zi)
        return y
