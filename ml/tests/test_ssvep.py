"""§5.3 SSVEP decoder on synthetic data (targets from ROADMAP §1)."""

import numpy as np
import pytest

from ml.data import synthetic
from ml.models.classical.ssvep import (
    FbccaModel,
    Ssvep,
    SsvepDecoder,
    calibrate_idle_threshold,
    cca_max_corr,
    reference_signals,
)

FS = 250.0
FREQS = {Ssvep.PLUS: 10.0, Ssvep.MINUS: 12.0}
WIN = int(2.0 * FS)


def test_cca_matches_least_squares_for_one_channel() -> None:
    # Independent check: with one channel, canonical correlation == multiple correlation R.
    rng = np.random.default_rng(0)
    y = reference_signals(10.0, FS, WIN)
    x = y @ rng.normal(size=6) + rng.normal(size=WIN) * 2.0
    xc, yc = x - x.mean(), y - y.mean(axis=0)
    beta, *_ = np.linalg.lstsq(yc, xc, rcond=None)
    r = np.corrcoef(yc @ beta, xc)[0, 1]
    assert cca_max_corr(x[:, None], y) == pytest.approx(r, abs=1e-9)


def test_cca_extremes() -> None:
    y = reference_signals(10.0, FS, WIN)
    assert cca_max_corr(y[:, :1], y) == pytest.approx(1.0, abs=1e-9)
    noise = np.random.default_rng(1).normal(size=(WIN, 2))
    assert cca_max_corr(noise, y) < 0.3


def _windows(rng, label: Ssvep | None, n: int, amp_uv: float) -> list[np.ndarray]:
    out = []
    for _ in range(n):
        x = synthetic.eeg(2.0, FS, 2, rng, noise_uv=10.0)
        if label is not None:
            x += synthetic.ssvep(2.0, FS, 2, FREQS[label], rng, amp_uv=amp_uv)
        out.append(x)
    return out


def test_three_class_accuracy_and_idle_false_positives() -> None:
    rng = np.random.default_rng(7)
    model = FbccaModel(FS, FREQS)
    threshold = calibrate_idle_threshold(model, _windows(rng, None, 30, 0))
    dec = SsvepDecoder(model, threshold)

    # 4 uV SSVEP over 10 uV pink background. Measured sweep (seed 7, 120 trials):
    # 2 uV 52 %, 3 uV 85 %, 4 uV 100 %. Every error was a miss (NONE), never the wrong LED:
    # the mean+3sd idle threshold trades sensitivity for no false speed changes.
    # Re-measure on real recordings (Phase 2/5); synthetic SNR is only a sanity check.
    trials = [(lab, w) for lab in (Ssvep.PLUS, Ssvep.MINUS) for w in _windows(rng, lab, 40, 4.0)]
    trials += [(Ssvep.NONE, w) for w in _windows(rng, None, 40, 0)]
    correct = sum(dec.classify(w) == lab for lab, w in trials)
    assert correct / len(trials) >= 0.90  # §1 target: >= 90 % 3-class, 2 s window

    rest_pred = [dec.classify(w) for w in _windows(rng, None, 60, 0)]
    assert sum(p != Ssvep.NONE for p in rest_pred) <= 3


def test_streaming_needs_two_agreeing_windows() -> None:
    rng = np.random.default_rng(11)
    model = FbccaModel(FS, FREQS)
    dec = SsvepDecoder(
        model, idle_threshold=calibrate_idle_threshold(model, _windows(rng, None, 20, 0))
    )
    x = synthetic.eeg(4.0, FS, 2, rng) + synthetic.ssvep(4.0, FS, 2, 12.0, rng, amp_uv=4.0)
    decisions = dec.process(x)
    # First decision needs a full 2 s window, then one per 250 ms step.
    assert len(decisions) == int((4.0 - 2.0) / 0.25) + 1
    assert decisions[0] == Ssvep.NONE  # only one window so far: no agreement yet
    assert decisions[-1] == Ssvep.MINUS
