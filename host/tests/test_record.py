"""T2.1 recorder: synthetic stream round trip, monotonic timestamps, §11.4 schema."""

from datetime import date
from pathlib import Path

import numpy as np
import pytest

from host.record import SyntheticSource, record
from host.session import REQUIRED_META, SessionWriter, load_session

META = {k: "unknown" for k in REQUIRED_META} | {"consent_id": "self"}


class FakeClock:
    def __init__(self, t: float = 5000.0) -> None:
        self.t = t

    def now(self) -> float:
        return self.t

    def sleep(self, s: float) -> None:
        self.t += s


def _writer(tmp_path: Path, **kw) -> SessionWriter:
    args = {
        "root": tmp_path,
        "subject": "S01",
        "session": "rest",
        "channels": ["E1", "E2"],
        "srate_hz": 250.0,
        "meta": META,
        "day": date(2026, 11, 19),
    } | kw
    return SessionWriter(**args)


def test_synthetic_round_trip(tmp_path: Path) -> None:
    clock = FakeClock()
    src = SyntheticSource(["E1", "E2", "M1"], srate_hz=250.0, clock=clock.now, seed=1)
    with _writer(tmp_path, channels=src.channels) as w:
        record(src, w, duration_s=2.0, sleep_s=clock.sleep, poll_s=0.05)
        w.add_event(0.5, 1.0, "clench_L", "test")
        w.write_calib({"emg_on_uv": 42.0})

    s = load_session(tmp_path / "S01" / "20261119_rest")
    assert s.channels == ["E1", "E2", "M1"]
    assert s.x_uv.shape[1] == 3
    assert abs(len(s.t_s) - 500) <= 1
    assert s.t_s[0] == 0.0
    assert np.all(np.diff(s.t_s) > 0)
    np.testing.assert_allclose(np.diff(s.t_s), 1 / 250.0, atol=1e-6)
    assert s.events == [(0.5, 1.0, "clench_L", "test")]
    assert s.calib == {"emg_on_uv": 42.0}
    for key in (*REQUIRED_META, "channels", "srate_hz", "units", "n_samples"):
        assert key in s.meta
    assert s.meta["units"] == "uV"
    assert s.meta["n_samples"] == len(s.t_s)


def test_synthetic_source_has_requested_tone() -> None:
    clock = FakeClock()
    src = SyntheticSource(["E1"], srate_hz=250.0, clock=clock.now, seed=0, tones_hz={"E1": 10.0})
    clock.sleep(8.0)
    _, x = src.pull()
    spectrum = np.abs(np.fft.rfft(x[:, 0]))
    freqs = np.fft.rfftfreq(len(x), 1 / 250.0)
    assert abs(freqs[np.argmax(spectrum[1:]) + 1] - 10.0) < 0.2


def test_timestamps_must_increase(tmp_path: Path) -> None:
    with _writer(tmp_path) as w:
        w.write_samples(np.array([1.0, 1.004]), np.zeros((2, 2)))
        with pytest.raises(ValueError, match="monotonic"):
            w.write_samples(np.array([1.004, 1.008]), np.zeros((2, 2)))
        with pytest.raises(ValueError, match="monotonic"):
            w.write_samples(np.array([2.0, 1.9]), np.zeros((2, 2)))


def test_shape_mismatch_rejected(tmp_path: Path) -> None:
    with _writer(tmp_path) as w, pytest.raises(ValueError, match="shape"):
        w.write_samples(np.array([1.0]), np.zeros((1, 3)))


@pytest.mark.parametrize("subject", ["nuke", "Priyanuj", "S1", "S01/../x", ""])
def test_subject_must_be_pseudonymous(tmp_path: Path, subject: str) -> None:
    with pytest.raises(ValueError, match="subject"):
        _writer(tmp_path, subject=subject)


def test_missing_meta_and_overwrite_refused(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="meta"):
        _writer(tmp_path, meta={"consent_id": "x"})
    _writer(tmp_path).close()
    with pytest.raises(FileExistsError):
        _writer(tmp_path)
