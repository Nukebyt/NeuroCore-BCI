"""Scripted synthetic user -> §5 pipeline -> NIP frames (optionally to the USB bridge).

Lets the whole control chain run, and drive the real car, with no electrodes:
  python -m host.simulate --print
  python -m host.simulate --port /dev/ttyACM0      # needs the `teleop` extra (pyserial)

A scenario is a list of (seconds, action); actions: rest, left, right, both, plus, minus.
"""

from __future__ import annotations

import argparse
import time
from collections.abc import Callable, Iterator

import numpy as np

from host.serial_link import TYPE_NIP, encode_message
from ml.data import synthetic
from ml.intent.fsm import Output
from ml.intent.pipeline import (
    EEG_FS,
    EMG_FS,
    SSVEP_FREQS,
    TICK_MS,
    Calibration,
    Pipeline,
    calibrate,
)
from ml.models.classical.ssvep import Ssvep
from protocol.nip import nip

Scenario = list[tuple[float, str]]

DEMO: Scenario = [
    (3.0, "rest"),
    (4.0, "plus"),
    (1.5, "left"),
    (1.0, "rest"),
    (1.0, "both"),
    (3.0, "rest"),
    (4.0, "minus"),
    (1.0, "rest"),
]

SSVEP_UV = 4.0


def synthesize(scenario: Scenario, rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """Returns (emg [n, 2] at EMG_FS, eeg [n, 2] at EEG_FS) for the whole scenario."""
    total = sum(d for d, _ in scenario)
    left, right, t = [], [], 0.0
    eeg = synthetic.eeg(total, EEG_FS, 2, rng, mains_uv=5.0)
    for dur, action in scenario:
        if action in ("left", "both"):
            left.append((t, t + dur))
        if action in ("right", "both"):
            right.append((t, t + dur))
        if action in ("plus", "minus"):
            label = Ssvep.PLUS if action == "plus" else Ssvep.MINUS
            a, b = round(t * EEG_FS), round((t + dur) * EEG_FS)
            eeg[a:b] += synthetic.ssvep(dur, EEG_FS, 2, SSVEP_FREQS[label], rng, SSVEP_UV)
        t += dur
    emg = np.column_stack(
        [
            synthetic.emg(total, EMG_FS, left, rng, mains_uv=20.0),
            synthetic.emg(total, EMG_FS, right, rng, mains_uv=20.0),
        ]
    )
    return emg, eeg


def synthetic_calibration(rng: np.random.Generator) -> Calibration:
    """§5.6 routine on synthetic data: 20 s rest, 3 x 2 s clench per arm."""
    rest_emg = np.column_stack([synthetic.emg(20, EMG_FS, [], rng) for _ in range(2)])
    clench = [(0.5 + 3 * k, 2.5 + 3 * k) for k in range(3)]
    cl = np.column_stack(
        [synthetic.emg(9.5, EMG_FS, clench, rng), synthetic.emg(9.5, EMG_FS, [], rng)]
    )
    cr = np.column_stack(
        [synthetic.emg(9.5, EMG_FS, [], rng), synthetic.emg(9.5, EMG_FS, clench, rng)]
    )
    rest_eeg = synthetic.eeg(20, EEG_FS, 2, rng)
    return calibrate(rest_emg, cl, cr, rest_eeg)


def run_scenario(
    scenario: Scenario, seed: int = 0, calib: Calibration | None = None
) -> Iterator[tuple[int, Output]]:
    """Yields (t_ms, FSM output) every 40 ms of the scenario."""
    rng = np.random.default_rng(seed)
    calib = calib or synthetic_calibration(rng)
    emg, eeg = synthesize(scenario, rng)
    pipe = Pipeline(calib)
    emg_step, eeg_step = int(EMG_FS * TICK_MS / 1000), int(EEG_FS * TICK_MS / 1000)
    for k in range(len(eeg) // eeg_step):
        t_ms = k * TICK_MS
        out = pipe.tick(
            emg[k * emg_step : (k + 1) * emg_step], eeg[k * eeg_step : (k + 1) * eeg_step], t_ms
        )
        yield t_ms, out


def stream_frames(
    scenario: Scenario, write: Callable[[bytes], object], realtime: bool, seed: int = 0
) -> None:  # pragma: no cover - I/O
    rng = np.random.default_rng(seed)
    pipe_calib = synthetic_calibration(rng)
    pipe = Pipeline(pipe_calib)
    emg, eeg = synthesize(scenario, rng)
    emg_step, eeg_step = int(EMG_FS * TICK_MS / 1000), int(EEG_FS * TICK_MS / 1000)
    start = time.monotonic()
    for k in range(len(eeg) // eeg_step):
        t_ms = k * TICK_MS
        out = pipe.tick(
            emg[k * emg_step : (k + 1) * emg_step], eeg[k * eeg_step : (k + 1) * eeg_step], t_ms
        )
        frame = pipe.frame(out, t_ms)
        if frame is not None:
            write(encode_message(TYPE_NIP, nip.encode(frame)))
        if realtime:
            time.sleep(max(0.0, start + (k + 1) * TICK_MS / 1000 - time.monotonic()))


def main(argv: list[str] | None = None) -> None:  # pragma: no cover - CLI
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--port", help="USB bridge serial port; omit to just print")
    ap.add_argument("--print", action="store_true", help="print intent changes")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args(argv)
    if args.port:
        import serial

        with serial.Serial(args.port, 115200, timeout=0) as port:
            stream_frames(DEMO, port.write, realtime=True, seed=args.seed)
        return
    last = None
    for t_ms, out in run_scenario(DEMO, args.seed):
        key = (out.intent, out.level, bool(out.flags & nip.Flags.LATCHED_STOP))
        if key != last:
            name = out.intent.name if out.intent is not None else "-"
            print(f"{t_ms / 1000:6.2f} s  {name:6s} level {out.level}  flags {out.flags!r}")
            last = key


if __name__ == "__main__":  # pragma: no cover
    main()
