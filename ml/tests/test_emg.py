"""§5.2 EMG steering against the §1 targets on synthetic data."""

import itertools

import numpy as np
import pytest

from ml.data import synthetic
from ml.features.emg import (
    EnvelopeTracker,
    HysteresisDetector,
    Thresholds,
    rms_envelope,
    td_features,
    thresholds,
)
from ml.features.filters import StreamingFilter, emg_sos

FS = 1000.0


def _calibrate(rng: np.random.Generator) -> Thresholds:
    rest = synthetic.emg(20, FS, [], rng, mains_uv=20)
    clench = synthetic.emg(6, FS, [(0.0, 2.0), (2.0, 4.0), (4.0, 6.0)], rng, mains_uv=20)
    f = StreamingFilter(emg_sos(FS), 1)
    _, rest_env = rms_envelope(f.process(rest)[:, 0][2000:], FS)  # drop filter transient
    _, clench_env = rms_envelope(f.process(clench)[:, 0], FS)
    return thresholds(rest_env, clench_env)


def _detect(x: np.ndarray, th: Thresholds) -> list[tuple[int, bool]]:
    """Streams x through filter -> envelope -> detector; returns (t_ms, state) per hop."""
    filt = StreamingFilter(emg_sos(FS), 1)
    env = EnvelopeTracker(FS)
    det = HysteresisDetector(th)
    out, n = [], 0
    for i in range(0, len(x), 50):
        chunk = filt.process(x[i : i + 50])[:, 0]
        for k, e in enumerate(env.process(chunk)):
            t_ms = int((i + (k + 1) * env.hop) * 1000 / FS)
            out.append((t_ms, det.update(e, t_ms)))
        n += len(chunk)
    return out


def test_envelope_streaming_matches_batch() -> None:
    rng = np.random.default_rng(3)
    x = rng.normal(size=3000)
    _, batch = rms_envelope(x, FS)
    tracker = EnvelopeTracker(FS)
    stream = [v for i in range(0, len(x), 77) for v in tracker.process(x[i : i + 77])]
    np.testing.assert_allclose(stream, batch[: len(stream)], rtol=1e-9)
    assert len(stream) >= len(batch) - 1


def test_thresholds_formula_and_bad_calibration() -> None:
    th = thresholds(np.full(100, 10.0), np.full(100, 50.0))
    assert th.on_uv == pytest.approx(30.0)
    assert th.off_uv == pytest.approx(18.0)
    with pytest.raises(ValueError):
        thresholds(np.full(10, 50.0), np.full(10, 10.0))


def test_hits_false_triggers_and_latency() -> None:
    rng = np.random.default_rng(42)
    th = _calibrate(rng)

    # 50 clenches of 0.6-1.2 s with 1.5-3 s gaps.
    bursts, t = [], 2.0
    for _ in range(50):
        d = rng.uniform(0.6, 1.2)
        bursts.append((t, t + d))
        t += d + rng.uniform(1.5, 3.0)
    x = synthetic.emg(t + 1.0, FS, bursts, rng, mains_uv=20)
    states = _detect(x, th)

    hits, latencies = 0, []
    for start, end in bursts:
        on = [tm for tm, s in states if s and start * 1000 <= tm <= end * 1000 + 300]
        if on:
            hits += 1
            latencies.append(on[0] - start * 1000)
    assert hits / len(bursts) >= 0.98  # §1 target
    assert np.max(latencies) <= 250  # leaves margin in the 300 ms EMG-to-wheel budget

    # 60 s of rest: <= 1 false trigger per minute (§1 target).
    rest_states = _detect(synthetic.emg(62.0, FS, [], rng, mains_uv=20), th)
    rest_states = [(tm, s) for tm, s in rest_states if tm > 2000]
    rises = sum(1 for (_, a), (_, b) in itertools.pairwise(rest_states) if b and not a)
    assert rises <= 1


def test_debounce_ignores_short_spikes() -> None:
    det = HysteresisDetector(Thresholds(30.0, 18.0), debounce_ms=80)
    assert not det.update(100.0, 0)
    assert not det.update(100.0, 50)
    assert not det.update(5.0, 75)  # spike shorter than debounce
    assert not det.update(100.0, 100)
    assert det.update(100.0, 180)  # held 80 ms
    assert det.update(20.0, 200)  # above OFF: stays on
    assert det.update(5.0, 220)
    assert not det.update(5.0, 300)


def test_td_features() -> None:
    f = td_features(np.array([0.0, 2.0, -2.0, 2.0, -2.0]))
    assert f["mav"] == pytest.approx(1.6)
    assert f["wl"] == pytest.approx(14.0)
    assert f["zc"] == 3
    assert f["ssc"] == 3
