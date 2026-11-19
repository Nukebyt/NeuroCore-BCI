"""T5.1 intent FSM: scenario tests plus hypothesis property tests (ROADMAP §5.5)."""

from __future__ import annotations

from hypothesis import given, settings
from hypothesis import strategies as st

from ml.intent.fsm import CONFIG, FsmState, Inputs, Mode, Output, step
from ml.models.classical.ssvep import Ssvep
from protocol.nip.nip import Flags, Intent

TICK = 40


def run(inputs: list[Inputs]) -> list[tuple[FsmState, Output]]:
    s, out = FsmState(), []
    for i in inputs:
        s, o = step(s, i)
        out.append((s, o))
    return out


def ticks(t0: int, ms: int, **kw) -> list[Inputs]:
    return [Inputs(t_ms=t, **kw) for t in range(t0, t0 + ms, TICK)]


# ------------------------------------------------------------------ scenarios


def test_drive_steer_stop_and_clear() -> None:
    seq = ticks(0, 400)  # armed, idle
    seq += ticks(400, 40, ssvep=Ssvep.PLUS)  # speed 1
    seq += ticks(440, 400)
    seq += ticks(840, 400, emg_left=True)
    seq += ticks(1240, 400, emg_left=True, emg_right=True)  # STOP
    seq += ticks(1640, 2000)  # relax
    seq += ticks(3640, 40, ssvep=Ssvep.PLUS)  # wrong key: stays latched
    seq += ticks(3680, 40, ssvep=Ssvep.MINUS)  # clears
    res = run(seq)
    outs = [o for _, o in res]

    assert outs[0].intent is Intent.FWD  # gear asserted when arming
    i_plus = 10
    assert outs[i_plus].level == 1
    assert outs[i_plus].intent is Intent.FWD
    steer = [o.intent for o in outs[21:31]]
    assert steer[-1] is Intent.LEFT
    stopped = [o for s, o in res if s.mode is Mode.STOPPED]
    assert stopped and all(o.intent is Intent.STOP for o in stopped)
    assert all(o.flags & Flags.LATCHED_STOP for o in stopped)
    assert outs[-2].intent is Intent.STOP  # PLUS doesn't clear
    assert res[-1][0].mode is Mode.ARMED
    assert outs[-1].intent is Intent.FWD and outs[-1].level == 0


def test_double_clench_toggles_reverse_only_at_level_zero() -> None:
    pulse = 120  # < stop_hold_ms
    seq = ticks(0, 200)
    seq += ticks(200, pulse, emg_left=True, emg_right=True) + ticks(320, 200)
    seq += ticks(520, pulse, emg_left=True, emg_right=True) + ticks(640, 400)
    res = run(seq)
    assert res[-1][0].reverse
    assert res[-1][0].mode is Mode.ARMED  # never latched

    # Same gesture while moving: ignored.
    seq2 = ticks(0, 40, ssvep=Ssvep.PLUS) + ticks(40, 200)
    seq2 += ticks(240, pulse, emg_left=True, emg_right=True) + ticks(360, 200)
    seq2 += ticks(560, pulse, emg_left=True, emg_right=True) + ticks(680, 200)
    res2 = run(seq2)
    assert not res2[-1][0].reverse
    assert res2[-1][1].level == 1


def test_double_clench_gap_boundary() -> None:
    def toggles(gap: int) -> bool:
        seq = ticks(0, 200) + ticks(200, 120, emg_left=True, emg_right=True)
        seq += ticks(320, gap) + ticks(320 + gap, 120, emg_left=True, emg_right=True)
        seq += ticks(440 + gap, 40)
        return run(seq)[-1][0].reverse

    assert toggles(CONFIG.double_clench_gap_ms)
    assert not toggles(CONFIG.double_clench_gap_ms + TICK)


def test_fault_sends_one_stop_then_silence() -> None:
    seq = ticks(0, 200, ssvep=Ssvep.PLUS) + ticks(200, 40, fault=True) + ticks(240, 400)
    outs = [o for _, o in run(seq)]
    k = 5
    assert outs[k].intent is Intent.STOP and outs[k].flags & Flags.LATCHED_STOP
    assert all(o.intent is None for o in outs[k + 1 :])


def test_uncalibrated_never_moves() -> None:
    seq = ticks(0, 2000, calibrated=False, ssvep=Ssvep.PLUS, emg_left=True)
    assert all(o.intent is Intent.STOP for _, o in run(seq))


def looks(n: int, look_ms: int, away_ms: int, label: Ssvep = Ssvep.PLUS, **kw) -> list[Inputs]:
    seq, t = [], 0
    for _ in range(n):
        seq += ticks(t, look_ms, ssvep=label, **kw) + ticks(t + look_ms, away_ms, **kw)
        t += look_ms + away_ms
    return seq


def test_constant_gaze_gives_one_step() -> None:
    # Fix for ADR-0004 limitation 1: lagging decisions can't keep raising the speed.
    levels = [o.level for _, o in run(ticks(0, 6000, ssvep=Ssvep.PLUS))]
    assert max(levels) == 1


def test_each_look_is_one_step_with_refractory_and_cap() -> None:
    levels = [o.level for _, o in run(looks(5, 400, 600))]
    assert max(levels) == 3
    first_at = {lvl: levels.index(lvl) * TICK for lvl in (1, 2, 3)}
    assert first_at[2] - first_at[1] >= CONFIG.level_refractory_ms
    assert [o.level for _, o in run(looks(3, 1000, 600, Ssvep.PLUS))][-1] == 3
    assert [o.level for _, o in run(looks(5, 400, 600, channel_bad=True))][-1] == 1


def test_brief_dropouts_inside_a_look_do_not_rearm() -> None:
    # Decoder flicker: PLUS with 200 ms NONE gaps (< ssvep_rearm_ms) counts as one look.
    levels = [o.level for _, o in run(looks(6, 800, 200))]
    assert max(levels) == 1


# ------------------------------------------------------------------ properties

segment = st.tuples(
    st.integers(1, 80),  # ticks in this segment
    st.sampled_from([(False, False)] * 4 + [(True, False), (False, True), (True, True)]),
    st.sampled_from([Ssvep.NONE] * 3 + [Ssvep.PLUS, Ssvep.MINUS]),
    st.integers(0, 5).map(lambda v: v == 0),  # artifact
    st.integers(0, 6).map(lambda v: v == 0),  # channel_bad
    st.integers(0, 12).map(lambda v: v != 0),  # calibrated
    st.integers(0, 40).map(lambda v: v == 0),  # fault (rare)
)


def expand(segments) -> list[Inputs]:
    out, t = [], 0
    for n, (left, right), ssvep, art, bad, cal, fault in segments:
        for k in range(n):
            out.append(
                Inputs(
                    t_ms=t,
                    emg_left=left,
                    emg_right=right,
                    ssvep=ssvep,
                    artifact=art,
                    channel_bad=bad,
                    calibrated=cal,
                    fault=fault and k == 0,
                )
            )
            t += TICK
    return out


@settings(max_examples=400, deadline=None)
@given(st.lists(segment, min_size=1, max_size=25))
def test_safety_properties(segments) -> None:
    inputs = expand(segments)
    res = run(inputs)
    states = [s for s, _ in res]
    outs = [o for _, o in res]
    first_fault = next((k for k, i in enumerate(inputs) if i.fault), None)

    for k, (i, o) in enumerate(zip(inputs, outs, strict=True)):
        # Fault wins: STOP at the fault, silence afterwards.
        if first_fault is not None and k >= first_fault:
            expected = Intent.STOP if k == first_fault else None
            assert o.intent is expected
            continue

        # Levels are bounded and rise by at most one step per tick (drops to 0 are fine).
        assert 0 <= o.level <= 3
        if k > 0 and outs[k - 1].intent is not None:
            assert o.level - outs[k - 1].level <= 1

        # S4: never increases while the artifact flag is set.
        if i.artifact and k > 0:
            assert o.level <= outs[k - 1].level

        # S3: channel_bad caps the level at 1.
        if i.channel_bad:
            assert o.level <= 1

        # Uncalibrated: no motion intents.
        if not i.calibrated and not (o.flags & Flags.LATCHED_STOP):
            assert o.intent is Intent.STOP

        # Both arms continuously on for >= stop_hold_ms -> latched STOP.
        j = k
        while j > 0 and inputs[j - 1].emg_left and inputs[j - 1].emg_right:
            j -= 1
        if i.emg_left and i.emg_right and i.t_ms - inputs[j].t_ms >= CONFIG.stop_hold_ms:
            assert o.intent is Intent.STOP and o.flags & Flags.LATCHED_STOP

        # S2: leaving the latch needs relax_clear_ms without EMG, then SSVEP "-".
        if k > 0 and states[k - 1].mode is Mode.STOPPED and states[k].mode is not Mode.STOPPED:
            r = k
            while r > 0 and not (inputs[r - 1].emg_left or inputs[r - 1].emg_right):
                r -= 1
            assert not (i.emg_left or i.emg_right)
            assert i.t_ms - inputs[r].t_ms >= CONFIG.relax_clear_ms
            assert i.ssvep is Ssvep.MINUS
        if states[k].mode is Mode.STOPPED:
            assert o.intent is Intent.STOP and o.flags & Flags.LATCHED_STOP

        # Reverse toggles only when the speed level was 0.
        if k > 0 and states[k].reverse != states[k - 1].reverse:
            assert states[k - 1].level == 0 or states[k].mode is Mode.ARMED

    # Edge-triggered speed: within an unbroken run of non-NONE decisions, at most one rise.
    end = first_fault if first_fault is not None else len(inputs)
    rises_in_run = 0
    for k in range(1, end):
        if inputs[k].ssvep is Ssvep.NONE:
            rises_in_run = 0
            continue
        if outs[k].level > outs[k - 1].level:
            rises_in_run += 1
            assert rises_in_run <= 1
