"""Keyboard teleop core (ROADMAP Phase 1): keys -> NIP frames at 25 Hz."""

import itertools

from host.serial_link import TYPE_NIP, Deframer
from host.teleop import GEAR_ASSERT_FRAMES, Keys, TeleopCore, run
from protocol.nip import nip
from protocol.nip.nip import Flags, Intent


def intents(core: TeleopCore, keys: Keys, n: int) -> list[tuple[Intent, int]]:
    out = []
    for i in range(n):
        f = core.next_frame(keys, t_ms=40 * i)
        out.append((f.intent, f.level))
    return out


def test_idle_is_hold_level_zero() -> None:
    assert intents(TeleopCore(), Keys(), 3) == [(Intent.HOLD, 0)] * 3


def test_forward_then_steer_asserts_gear_first() -> None:
    core = TeleopCore(level=2)
    seq = intents(core, Keys(fwd=True, left=True), GEAR_ASSERT_FRAMES + 2)
    assert seq[:GEAR_ASSERT_FRAMES] == [(Intent.FWD, 2)] * GEAR_ASSERT_FRAMES
    assert seq[GEAR_ASSERT_FRAMES:] == [(Intent.LEFT, 2)] * 2


def test_reverse_and_release() -> None:
    core = TeleopCore(level=1)
    assert intents(core, Keys(rev=True), 3) == [(Intent.REV, 1)] * 3
    assert intents(core, Keys(), 1) == [(Intent.HOLD, 0)]


def test_stop_and_conflicting_keys_send_stop() -> None:
    core = TeleopCore(level=3)
    assert intents(core, Keys(fwd=True, stop=True), 1) == [(Intent.STOP, 0)]
    assert intents(core, Keys(fwd=True, rev=True), 1) == [(Intent.STOP, 0)]


def test_steer_without_throttle_and_both_steer_keys() -> None:
    core = TeleopCore()
    assert intents(core, Keys(right=True), 1) == [(Intent.RIGHT, 0)]
    assert intents(core, Keys(left=True, right=True), 1) == [(Intent.HOLD, 0)]


def test_level_clamped_and_frames_flagged_test_mode() -> None:
    core = TeleopCore()
    core.set_level(9)
    assert core.level == 3
    core.set_level(-2)
    assert core.level == 0
    f = core.next_frame(Keys(), t_ms=5)
    assert f.flags & Flags.TEST_MODE
    assert f.t_ms == 5


def test_seq_increments_and_wraps() -> None:
    core = TeleopCore()
    seqs = [core.next_frame(Keys(), 0).seq for _ in range(258)]
    assert seqs[:3] == [0, 1, 2]
    assert seqs[255:] == [255, 0, 1]


class FakeClock:
    def __init__(self) -> None:
        self.t = 1000

    def now_ms(self) -> int:
        return self.t

    def sleep_ms(self, ms: int) -> None:
        self.t += max(1, ms)


class FakePort:
    def __init__(self) -> None:
        self.data = bytearray()

    def write(self, b: bytes) -> int:
        self.data += b
        return len(b)


def test_run_loop_sends_25hz_valid_frames_and_stops_on_exit() -> None:
    clock, port = FakeClock(), FakePort()
    end = clock.t + 1000
    run(
        port,
        read_keys=lambda: Keys(fwd=True),
        should_quit=lambda: clock.t >= end,
        now_ms=clock.now_ms,
        sleep_ms=clock.sleep_ms,
        core=TeleopCore(level=1),
    )
    msgs = Deframer().feed(bytes(port.data))
    assert all(m.type == TYPE_NIP for m in msgs)
    frames = [nip.decode(m.payload) for m in msgs]
    driving, tail = frames[:-3], frames[-3:]
    assert 24 <= len(driving) <= 26  # 25 Hz for 1 s
    gaps = [b.t_ms - a.t_ms for a, b in itertools.pairwise(driving)]
    assert all(g == 40 for g in gaps)
    assert all(f.intent == Intent.FWD for f in driving)
    assert [f.intent for f in tail] == [Intent.STOP] * 3
    seqs = [f.seq for f in frames]
    assert all(nip.seq_is_newer(b, a) for a, b in itertools.pairwise(seqs))
