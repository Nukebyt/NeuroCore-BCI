"""Session files in the ROADMAP §11.4 layout: data/<subject>/<YYYYMMDD>_<session>/.

raw.csv     t_s, then one column per channel in µV
events.csv  onset_s, duration_s, label, source
meta.json   REQUIRED_META plus channels, srate_hz, units, n_samples, duration_s
calib.json  thresholds from the calibration routine (optional)
"""

from __future__ import annotations

import csv
import json
import re
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path
from types import TracebackType
from typing import Any

import numpy as np

REQUIRED_META = (
    "hardware_rev",
    "firmware_hash",
    "montage",
    "electrode_type",
    "skin_prep",
    "impedances_kohm",
    "room_notes",
    "consent_id",
)
SUBJECT_RE = re.compile(r"^S\d{2,3}$")  # pseudonymous IDs only, never names
SESSION_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")


@dataclass
class Session:
    path: Path
    channels: list[str]
    t_s: np.ndarray
    x_uv: np.ndarray
    events: list[tuple[float, float, str, str]]
    meta: dict[str, Any]
    calib: dict[str, Any] | None = field(default=None)


class SessionWriter:
    """Streams samples to raw.csv; writes events and meta on close. Never overwrites."""

    def __init__(
        self,
        root: Path,
        subject: str,
        session: str,
        channels: list[str],
        srate_hz: float,
        meta: dict[str, Any],
        day: date | None = None,
    ) -> None:
        if not SUBJECT_RE.match(subject):
            raise ValueError(f"subject {subject!r} must be a pseudonymous ID like S01")
        if not SESSION_RE.match(session):
            raise ValueError(f"session {session!r} must be lowercase [a-z0-9_-]")
        missing = [k for k in REQUIRED_META if k not in meta]
        if missing:
            raise ValueError(f"meta is missing {missing}")
        day = day or date.today()
        self.path = Path(root) / subject / f"{day:%Y%m%d}_{session}"
        self.path.mkdir(parents=True, exist_ok=False)  # FileExistsError: never overwrite
        self.channels = list(channels)
        self.srate_hz = srate_hz
        self._meta = dict(meta)
        self._events: list[tuple[float, float, str, str]] = []
        self._t0: float | None = None
        self._last_t: float | None = None
        self._n = 0
        self._raw = (self.path / "raw.csv").open("w", newline="", encoding="utf-8")
        self._raw.write(",".join(["t_s", *self.channels]) + "\n")

    @property
    def t0(self) -> float | None:
        """Absolute timestamp of the first sample (session time zero)."""
        return self._t0

    def write_samples(self, t_abs_s: np.ndarray, x_uv: np.ndarray) -> None:
        t = np.asarray(t_abs_s, dtype=float)
        x = np.asarray(x_uv, dtype=float)
        if x.ndim != 2 or x.shape != (len(t), len(self.channels)):
            raise ValueError(f"shape {x.shape} != ({len(t)}, {len(self.channels)})")
        if len(t) == 0:
            return
        if np.any(np.diff(t) <= 0) or (self._last_t is not None and t[0] <= self._last_t):
            raise ValueError("timestamps must be strictly monotonic")
        if self._t0 is None:
            self._t0 = float(t[0])
        self._last_t = float(t[-1])
        rel = t - self._t0
        block = np.column_stack([rel, x])
        np.savetxt(self._raw, block, delimiter=",", fmt=["%.6f"] + ["%.3f"] * x.shape[1])
        self._n += len(t)

    def add_event(self, onset_s: float, duration_s: float, label: str, source: str) -> None:
        """onset_s is relative to the first sample (session time)."""
        self._events.append((float(onset_s), float(duration_s), label, source))

    def write_calib(self, calib: dict[str, Any]) -> None:
        (self.path / "calib.json").write_text(json.dumps(calib, indent=2) + "\n", encoding="utf-8")

    def close(self) -> None:
        if self._raw.closed:
            return
        self._raw.close()
        with (self.path / "events.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["onset_s", "duration_s", "label", "source"])
            w.writerows(sorted(self._events))
        duration = 0.0
        if self._t0 is not None and self._last_t is not None:
            duration = self._last_t - self._t0
        meta = self._meta | {
            "channels": self.channels,
            "srate_hz": self.srate_hz,
            "units": "uV",
            "n_samples": self._n,
            "duration_s": duration,
        }
        (self.path / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

    def __enter__(self) -> SessionWriter:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.close()


def load_session(path: Path) -> Session:
    path = Path(path)
    with (path / "raw.csv").open(encoding="utf-8") as f:
        channels = f.readline().strip().split(",")[1:]
    data = np.loadtxt(path / "raw.csv", delimiter=",", skiprows=1, ndmin=2)
    events = []
    with (path / "events.csv").open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            events.append(
                (float(row["onset_s"]), float(row["duration_s"]), row["label"], row["source"])
            )
    meta = json.loads((path / "meta.json").read_text(encoding="utf-8"))
    calib_path = path / "calib.json"
    calib = json.loads(calib_path.read_text(encoding="utf-8")) if calib_path.exists() else None
    return Session(path, channels, data[:, 0], data[:, 1:], events, meta, calib)
