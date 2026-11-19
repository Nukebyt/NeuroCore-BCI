"""End-to-end: synthetic user -> filters/EMG/SSVEP -> FSM, following ROADMAP §5."""

from host.simulate import DEMO, run_scenario
from protocol.nip.nip import Flags, Intent


def _segment_starts() -> list[float]:
    t, starts = 0.0, []
    for dur, _ in DEMO:
        starts.append(t)
        t += dur
    return starts


def test_demo_scenario_drives_steers_stops_and_clears() -> None:
    outs = list(run_scenario(DEMO, seed=0))
    starts = _segment_starts()

    def during(i: int) -> list:
        lo, hi = starts[i] * 1000, starts[i] * 1000 + DEMO[i][0] * 1000
        return [o for t, o in outs if lo <= t < hi]

    rest0, plus, left, _, both, relax, minus, _ = (during(i) for i in range(len(DEMO)))
    assert all(o.level == 0 for o in rest0)
    assert all(not (o.flags & Flags.LATCHED_STOP) for o in rest0)  # no false STOP at rest
    assert max(o.level for o in plus) >= 1  # looked at "+": speed went up
    assert any(o.intent is Intent.LEFT for o in left)
    assert not any(o.intent is Intent.RIGHT for o in left)
    assert any(o.flags & Flags.LATCHED_STOP for o in both)  # both arms -> STOP latch
    assert all(o.intent is Intent.STOP for o in relax)  # stays latched while relaxing
    cleared = [o for o in minus if not (o.flags & Flags.LATCHED_STOP)]
    assert cleared and cleared[-1].level == 0  # "-" after 2 s relax clears to ARMED
    assert all(o.intent is not None for _, o in outs)  # no fault in this scenario
