"""Synthetic EEG/EMG for tests and dry runs. Not physiological models, just shaped noise."""

from __future__ import annotations

import numpy as np
from scipy import signal


def pink_noise(n: int, rng: np.random.Generator, rms: float = 1.0) -> np.ndarray:
    """1/f noise via spectral shaping, scaled to `rms`."""
    spec = np.fft.rfft(rng.normal(size=n))
    f = np.arange(len(spec), dtype=float)
    f[0] = 1.0
    x = np.fft.irfft(spec / np.sqrt(f), n)
    return x * (rms / (np.std(x) + 1e-12))


def eeg(
    seconds: float,
    fs: float,
    n_channels: int,
    rng: np.random.Generator,
    noise_uv: float = 10.0,
    mains_uv: float = 0.0,
) -> np.ndarray:
    """Background EEG-like noise [n, channels] in µV, optional 50 Hz mains pickup."""
    n = round(seconds * fs)
    x = np.column_stack([pink_noise(n, rng, noise_uv) for _ in range(n_channels)])
    if mains_uv:
        t = np.arange(n) / fs
        x += mains_uv * np.sin(2 * np.pi * 50.0 * t)[:, None]
    return x


def ssvep(
    seconds: float,
    fs: float,
    n_channels: int,
    freq_hz: float,
    rng: np.random.Generator,
    amp_uv: float = 2.0,
    harmonics: int = 2,
) -> np.ndarray:
    """SSVEP-like response: fundamental + decaying harmonics, random phase per channel."""
    n = round(seconds * fs)
    t = np.arange(n) / fs
    out = np.zeros((n, n_channels))
    for c in range(n_channels):
        for h in range(1, harmonics + 1):
            phase = rng.uniform(0, 2 * np.pi)
            out[:, c] += (amp_uv / h) * np.sin(2 * np.pi * h * freq_hz * t + phase)
    return out


def emg(
    seconds: float,
    fs: float,
    bursts_s: list[tuple[float, float]],
    rng: np.random.Generator,
    rest_uv: float = 3.0,
    clench_uv: float = 80.0,
    mains_uv: float = 0.0,
) -> np.ndarray:
    """Single-channel surface-EMG-like signal: 20-450 Hz noise with amplitude bursts."""
    n = round(seconds * fs)
    sos = signal.butter(4, [20.0, min(450.0, 0.45 * fs)], btype="bandpass", fs=fs, output="sos")
    carrier = signal.sosfilt(sos, rng.normal(size=n))
    carrier /= np.std(carrier) + 1e-12
    t = np.arange(n) / fs
    gain = np.full(n, rest_uv)
    ramp = 0.01  # 10 ms onset/offset
    for start, end in bursts_s:
        up = np.clip((t - start) / ramp, 0, 1)
        down = np.clip((end - t) / ramp, 0, 1)
        gain = np.maximum(gain, rest_uv + (clench_uv - rest_uv) * np.minimum(up, down))
    x = carrier * gain
    if mains_uv:
        x += mains_uv * np.sin(2 * np.pi * 50.0 * t)
    return x
