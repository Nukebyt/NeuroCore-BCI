"""Intent FSM (ROADMAP §5.5, T5.1). Python reference for firmware/neurocore/intent/.

Pure function: step(state, inputs, cfg) -> (state, output). Called once per NIP frame (25 Hz).
Inputs are already-debounced EMG states and the SSVEP decoder's agreed decision.
Behaviour choices are recorded in ADR-0004.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum

from ml.models.classical.ssvep import Ssvep
from protocol.nip.nip import LEVEL_MAX, Flags, Intent


class Mode(Enum):
    IDLE = "idle"
    ARMED = "armed"
    DRIVE = "drive"
    STEER_L = "steer_l"
    STEER_R = "steer_r"
    STOPPED = "stopped"  # latched (S2)
    FAULT = "fault"  # NeuroCore watchdog / brownout: STOP once, then silence


@dataclass(frozen=True)
class Config:
    stop_hold_ms: int = 300  # both arms this long -> latched STOP
    relax_clear_ms: int = 2000  # no EMG this long, then SSVEP "-" clears the latch
    double_clench_gap_ms: int = 600  # max gap between two short both-arm pulses
    level_refractory_ms: int = 1000  # min time between speed-level changes
    gear_assert_frames: int = 3  # FWD/REV frames sent before steering (car sets gear on these)
    ssvep_rearm_ms: int = 500  # look away this long before the next speed step


CONFIG = Config()


@dataclass(frozen=True)
class Inputs:
    t_ms: int
    emg_left: bool = False
    emg_right: bool = False
    ssvep: Ssvep = Ssvep.NONE
    artifact: bool = False
    channel_bad: bool = False
    calibrated: bool = True
    fault: bool = False


@dataclass(frozen=True)
class Output:
    intent: Intent | None  # None: stop emitting (after a fault)
    level: int
    flags: Flags


@dataclass(frozen=True)
class FsmState:
    mode: Mode = Mode.IDLE
    level: int = 0
    reverse: bool = False
    both_since_ms: int | None = None
    last_pulse_end_ms: int | None = None
    relax_since_ms: int | None = None
    last_level_change_ms: int | None = None
    gear_frames_left: int = 0
    fault_reported: bool = False
    ssvep_rearmed: bool = True  # NONE for ssvep_rearm_ms is needed between speed changes
    ssvep_none_since_ms: int | None = None


def step(s: FsmState, i: Inputs, cfg: Config = CONFIG) -> tuple[FsmState, Output]:
    flags = Flags.NONE
    if i.calibrated:
        flags |= Flags.CALIBRATED
    if i.artifact:
        flags |= Flags.ARTIFACT
    if i.channel_bad:
        flags |= Flags.CHANNEL_BAD
    latched = flags | Flags.LATCHED_STOP

    # 1. Fault always wins: one STOP, then stop emitting so adapters fall to their watchdog.
    if i.fault or s.mode is Mode.FAULT:
        if s.fault_reported:
            return s, Output(None, 0, flags)
        return replace(s, mode=Mode.FAULT, level=0, fault_reported=True), Output(
            Intent.STOP, 0, latched
        )

    any_emg = i.emg_left or i.emg_right
    both = i.emg_left and i.emg_right
    relax_since = (
        None if any_emg else (s.relax_since_ms if s.relax_since_ms is not None else i.t_ms)
    )

    both_since = s.both_since_ms
    short_pulse_start: int | None = None
    if both:
        both_since = i.t_ms if both_since is None else both_since
    elif both_since is not None:
        if i.t_ms - both_since < cfg.stop_hold_ms:
            short_pulse_start = both_since
        both_since = None
    s = replace(s, relax_since_ms=relax_since, both_since_ms=both_since)
    ssvep = i.ssvep

    # 2. STOP latch (S2): cleared only by relax_clear_ms without EMG, then SSVEP "-".
    if s.mode is Mode.STOPPED:
        relaxed = relax_since is not None and i.t_ms - relax_since >= cfg.relax_clear_ms
        if not (relaxed and ssvep is Ssvep.MINUS and i.calibrated):
            return replace(s, level=0), Output(Intent.STOP, 0, latched)
        s = replace(
            s,
            mode=Mode.ARMED,
            level=0,
            reverse=False,
            last_pulse_end_ms=None,
            last_level_change_ms=i.t_ms,
            gear_frames_left=cfg.gear_assert_frames,
            ssvep_rearmed=False,  # this look is consumed by the clear
        )
        ssvep = Ssvep.NONE
        cleared_now = True
    else:
        cleared_now = False

    if both_since is not None and i.t_ms - both_since >= cfg.stop_hold_ms:
        stopped = replace(s, mode=Mode.STOPPED, level=0, gear_frames_left=0, last_pulse_end_ms=None)
        return stopped, Output(Intent.STOP, 0, latched)

    # 3. Not calibrated: no motion.
    if not i.calibrated:
        idle = replace(s, mode=Mode.IDLE, level=0, reverse=False, gear_frames_left=0)
        return idle, Output(Intent.STOP, 0, flags)
    if s.mode is Mode.IDLE:
        s = replace(s, mode=Mode.ARMED, level=0, gear_frames_left=cfg.gear_assert_frames)

    # 4. Double both-arm clench toggles reverse, only at level 0.
    if short_pulse_start is not None:
        prev_end = s.last_pulse_end_ms
        if prev_end is not None and short_pulse_start - prev_end <= cfg.double_clench_gap_ms:
            if s.level == 0:
                s = replace(s, reverse=not s.reverse, gear_frames_left=cfg.gear_assert_frames)
            s = replace(s, last_pulse_end_ms=None)
        else:
            s = replace(s, last_pulse_end_ms=i.t_ms)

    # 5. SSVEP speed changes, edge-triggered: one step per look (ADR-0004 rev 2). A NONE
    #    decision re-arms. S4: no increase while the artifact flag is set.
    level = s.level
    last_change = s.last_level_change_ms
    gear_frames = s.gear_frames_left
    rearmed = s.ssvep_rearmed
    none_since = s.ssvep_none_since_ms
    if ssvep is Ssvep.NONE and not cleared_now:
        none_since = i.t_ms if none_since is None else none_since
        if i.t_ms - none_since >= cfg.ssvep_rearm_ms:
            rearmed = True
    elif ssvep is not Ssvep.NONE:
        none_since = None
    if cleared_now:
        none_since = None
    if (
        ssvep is not Ssvep.NONE
        and rearmed
        and (last_change is None or i.t_ms - last_change >= cfg.level_refractory_ms)
    ):
        new = level
        if ssvep is Ssvep.PLUS and not i.artifact:
            new = min(LEVEL_MAX, level + 1)
        elif ssvep is Ssvep.MINUS:
            new = max(0, level - 1)
        if new != level:
            last_change = i.t_ms
            rearmed = False
            if level == 0:
                gear_frames = cfg.gear_assert_frames
            level = new
    if i.channel_bad:
        level = min(level, 1)  # S3

    # 6. Mode and output.
    steer_l = i.emg_left and not i.emg_right
    steer_r = i.emg_right and not i.emg_left
    if steer_l:
        mode = Mode.STEER_L
    elif steer_r:
        mode = Mode.STEER_R
    else:
        mode = Mode.DRIVE if level > 0 else Mode.ARMED
    gear_intent = Intent.REV if s.reverse else Intent.FWD
    if gear_frames > 0:
        intent = gear_intent
        gear_frames -= 1
    elif steer_l:
        intent = Intent.LEFT
    elif steer_r:
        intent = Intent.RIGHT
    elif level > 0:
        intent = gear_intent
    else:
        intent = Intent.HOLD
    s = replace(
        s,
        mode=mode,
        level=level,
        last_level_change_ms=last_change,
        gear_frames_left=gear_frames,
        ssvep_rearmed=rearmed,
        ssvep_none_since_ms=none_since,
    )
    return s, Output(intent, level, flags)
