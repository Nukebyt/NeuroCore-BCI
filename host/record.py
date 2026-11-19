"""T2.1 recorder: sample stream -> session files (ROADMAP §11.4).

Usage:
  python -m host.record --subject S01 --session rest --duration 60 --source lsl:BioAmpDataStream
  python -m host.record --subject S01 --session dryrun --duration 10 --source synthetic

While recording, type a label and press Enter to add an event marker (e.g. `clench_L`).
Reminder (docs/safety.md): battery-only acquisition; laptop off its charger or via a USB
isolator while electrodes are on.
"""

from __future__ import annotations

import argparse
import queue
import sys
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import Protocol

import numpy as np

from host.session import REQUIRED_META, SessionWriter


class SampleSource(Protocol):
    channels: list[str]
    srate_hz: float

    def clock(self) -> float:
        """Current time in the same timebase as sample timestamps (seconds)."""
        ...

    def pull(self) -> tuple[np.ndarray, np.ndarray]:
        """All samples available now: (timestamps_s [n], x_uv [n, channels])."""
        ...


class SyntheticSource:
    """Deterministic fake stream: Gaussian noise plus optional per-channel tones (µV)."""

    def __init__(
        self,
        channels: list[str],
        srate_hz: float,
        clock: Callable[[], float],
        seed: int = 0,
        noise_uv: float = 5.0,
        tones_hz: dict[str, float] | None = None,
        tone_uv: float = 10.0,
    ) -> None:
        self.channels = list(channels)
        self.srate_hz = srate_hz
        self._clock = clock
        self._rng = np.random.default_rng(seed)
        self._noise = noise_uv
        self._tones = tones_hz or {}
        self._tone_uv = tone_uv
        self._t0 = clock()
        self._next = 0  # index of next sample to emit

    def clock(self) -> float:
        return self._clock()

    def pull(self) -> tuple[np.ndarray, np.ndarray]:
        due = int(np.floor((self._clock() - self._t0) * self.srate_hz))
        idx = np.arange(self._next, max(self._next, due))
        self._next = max(self._next, due)
        t = self._t0 + idx / self.srate_hz
        x = self._rng.normal(0.0, self._noise, (len(idx), len(self.channels)))
        for c, ch in enumerate(self.channels):
            if ch in self._tones:
                x[:, c] += self._tone_uv * np.sin(2 * np.pi * self._tones[ch] * (t - self._t0))
        return t, x


class LslSource:  # pragma: no cover - needs a live LSL stream
    """LSL inlet (e.g. Chords-Python's stream). Needs the `lsl` extra."""

    def __init__(self, name: str, timeout_s: float = 10.0) -> None:
        import pylsl

        self._pylsl = pylsl
        streams = pylsl.resolve_byprop("name", name, timeout=timeout_s)
        if not streams:
            raise RuntimeError(f"no LSL stream named {name!r}")
        self._inlet = pylsl.StreamInlet(streams[0], processing_flags=pylsl.proc_ALL)
        info = self._inlet.info()
        self.srate_hz = info.nominal_srate()
        self.channels = [f"ch{i}" for i in range(info.channel_count())]

    def clock(self) -> float:
        return self._pylsl.local_clock()

    def pull(self) -> tuple[np.ndarray, np.ndarray]:
        x, t = self._inlet.pull_chunk(timeout=0.0)
        return np.asarray(t, dtype=float), np.asarray(x, dtype=float).reshape(len(t), -1)


def record(
    source: SampleSource,
    writer: SessionWriter,
    duration_s: float,
    sleep_s: Callable[[float], None] = time.sleep,
    poll_s: float = 0.05,
    markers: queue.Queue[str] | None = None,
) -> None:
    """Pull from `source` into `writer` for duration_s (source clock)."""
    start = source.clock()
    while True:
        t, x = source.pull()
        writer.write_samples(t, x)
        while markers is not None and not markers.empty():
            label = markers.get_nowait()
            if writer.t0 is not None:
                writer.add_event(source.clock() - writer.t0, 0.0, label, "keyboard")
        if source.clock() - start >= duration_s:
            break
        sleep_s(poll_s)
    t, x = source.pull()
    writer.write_samples(t, x)


def _stdin_markers(q: queue.Queue[str]) -> None:  # pragma: no cover - interactive
    for line in sys.stdin:
        label = line.strip()
        if label:
            q.put(label)


def main(argv: list[str] | None = None) -> None:  # pragma: no cover - CLI
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--subject", required=True, help="pseudonymous ID, e.g. S01")
    ap.add_argument("--session", required=True)
    ap.add_argument("--duration", type=float, required=True, help="seconds")
    ap.add_argument("--source", default="synthetic", help="synthetic | lsl:<stream name>")
    ap.add_argument("--root", type=Path, default=Path("data"))
    for key in REQUIRED_META:
        ap.add_argument(f"--{key.replace('_', '-')}", default="unknown")
    args = ap.parse_args(argv)

    if args.source.startswith("lsl:"):
        src: SampleSource = LslSource(args.source[4:])
    else:
        src = SyntheticSource(["E1", "E2", "E3", "E4"], 250.0, clock=time.monotonic)
    meta = {k: getattr(args, k) for k in REQUIRED_META} | {"source": args.source}
    markers: queue.Queue[str] = queue.Queue()
    threading.Thread(target=_stdin_markers, args=(markers,), daemon=True).start()
    with SessionWriter(
        args.root, args.subject, args.session, src.channels, src.srate_hz, meta
    ) as w:
        print(f"recording {args.duration} s to {w.path}  (type a label + Enter for a marker)")
        record(src, w, args.duration, markers=markers)
    print("done")


if __name__ == "__main__":  # pragma: no cover
    main()
