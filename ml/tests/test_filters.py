"""§5.1 preprocessing: notch depth, passband, streaming == batch."""

import numpy as np
import pytest
from scipy import signal

from ml.features.filters import StreamingFilter, eeg_sos, emg_sos, notch_sos


def gain_db(sos: np.ndarray, fs: float, f_hz: float) -> float:
    _, h = signal.sosfreqz(sos, worN=[f_hz], fs=fs)
    return float(20 * np.log10(abs(h[0])))


def test_eeg_chain_removes_mains_keeps_alpha() -> None:
    sos = eeg_sos(250.0)
    assert gain_db(sos, 250.0, 50.0) < -40
    assert abs(gain_db(sos, 250.0, 10.0)) < 1.0
    assert abs(gain_db(sos, 250.0, 20.0)) < 1.0


def test_emg_chain_removes_mains_harmonics() -> None:
    sos = emg_sos(1000.0)
    for f in (50.0, 100.0, 150.0, 250.0, 450.0):
        if f < 450.0:
            assert gain_db(sos, 1000.0, f) < -30, f
    assert abs(gain_db(sos, 1000.0, 120.0)) < 1.0


def test_notch_covers_harmonics_below_nyquist() -> None:
    assert notch_sos(250.0).shape[0] == 2  # 50, 100
    assert notch_sos(1000.0).shape[0] == 9  # 50..450


def test_emg_needs_1ksps() -> None:
    with pytest.raises(ValueError):
        emg_sos(500.0)


def test_streaming_equals_batch() -> None:
    rng = np.random.default_rng(0)
    x = rng.normal(size=(2000, 3))
    sos = eeg_sos(250.0)
    batch = signal.sosfilt(sos, x, axis=0)
    f = StreamingFilter(sos, 3)
    chunks = [f.process(x[i : i + 37]) for i in range(0, len(x), 37)]
    np.testing.assert_allclose(np.vstack(chunks), batch, atol=1e-9)
