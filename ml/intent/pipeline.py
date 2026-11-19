"""Rule-based hybrid control v1 (ROADMAP §5): EMG + SSVEP -> intent FSM -> NIP frames.

Python reference of what firmware/neurocore runs. Feed 40 ms chunks (one NIP frame each):
EMG at 1 kHz (2 channels: left, right forearm) and EEG at 250 Hz (2 channels: E1, E2).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from ml.features.emg import EnvelopeTracker, HysteresisDetector, Thresholds, rms_envelope
from ml.features.emg import thresholds as emg_thresholds
from ml.features.filters import StreamingFilter, emg_sos, notch_sos
from ml.intent.fsm import FsmState, Inputs, Output, step
from ml.models.classical.ssvep import (
    FbccaModel,
    Ssvep,
    SsvepDecoder,
    calibrate_idle_threshold,
)
from protocol.nip import nip
from protocol.nip.nip import Frame

EMG_FS = 1000.0
EEG_FS = 250.0
TICK_MS = 1000 // nip.HEARTBEAT_HZ
SSVEP_FREQS = {Ssvep.PLUS: 10.0, Ssvep.MINUS: 12.0}  # §5.3 candidates; LED box drives these


@dataclass(frozen=True)
class Calibration:
    emg_left: Thresholds
    emg_right: Thresholds
    ssvep_idle_threshold: float


def calibrate(
    emg_rest: np.ndarray,
    emg_clench_left: np.ndarray,
    emg_clench_right: np.ndarray,
    eeg_rest: np.ndarray,
) -> Calibration:
    """§5.6 routine. EMG arrays [n, 2] at EMG_FS (left, right); EEG rest [n, 2] at EEG_FS."""
    sos = emg_sos(EMG_FS)
    settle = int(EMG_FS)  # drop the first second of filter transient

    def env(x: np.ndarray, ch: int) -> np.ndarray:
        y = StreamingFilter(sos, 1).process(np.concatenate([x[:settle, ch], x[:, ch]]))[settle:]
        return rms_envelope(y[:, 0], EMG_FS)[1]

    left = emg_thresholds(env(emg_rest, 0), env(emg_clench_left, 0))
    right = emg_thresholds(env(emg_rest, 1), env(emg_clench_right, 1))
    model = FbccaModel(EEG_FS, SSVEP_FREQS)
    win = int(2.0 * EEG_FS)
    hop = int(0.5 * EEG_FS)
    rest_windows = [eeg_rest[i : i + win] for i in range(0, len(eeg_rest) - win + 1, hop)]
    return Calibration(left, right, calibrate_idle_threshold(model, rest_windows))


class Pipeline:
    def __init__(self, calib: Calibration) -> None:
        self._emg_filt = StreamingFilter(emg_sos(EMG_FS), 2)
        self._env = [EnvelopeTracker(EMG_FS), EnvelopeTracker(EMG_FS)]
        self._det = [HysteresisDetector(calib.emg_left), HysteresisDetector(calib.emg_right)]
        self._eeg_filt = StreamingFilter(notch_sos(EEG_FS), 2)
        self._ssvep = SsvepDecoder(FbccaModel(EEG_FS, SSVEP_FREQS), calib.ssvep_idle_threshold)
        self.state = FsmState()
        self._seq = 0
        self._ssvep_decision = Ssvep.NONE

    def tick(
        self,
        emg_chunk: np.ndarray,
        eeg_chunk: np.ndarray,
        t_ms: int,
        artifact: bool = False,
        channel_bad: bool = False,
        fault: bool = False,
    ) -> Output:
        y = self._emg_filt.process(emg_chunk)
        for ch in (0, 1):
            for env in self._env[ch].process(y[:, ch]):
                self._det[ch].update(env, t_ms)
        # The decoder decides every 250 ms; its latest decision holds between steps.
        decisions = self._ssvep.process(self._eeg_filt.process(eeg_chunk))
        if decisions:
            self._ssvep_decision = decisions[-1]
        ssvep = self._ssvep_decision
        inputs = Inputs(
            t_ms=t_ms,
            emg_left=self._det[0].active,
            emg_right=self._det[1].active,
            ssvep=ssvep,
            artifact=artifact,
            channel_bad=channel_bad,
            calibrated=True,
            fault=fault,
        )
        self.state, out = step(self.state, inputs)
        return out

    def frame(self, out: Output, t_ms: int) -> Frame | None:
        """NIP frame for an output, or None when the FSM has stopped emitting."""
        if out.intent is None:
            return None
        f = Frame(self._seq, t_ms & 0xFFFFFFFF, out.intent, out.level, 255, out.flags)
        self._seq = nip.next_seq(self._seq)
        return f
