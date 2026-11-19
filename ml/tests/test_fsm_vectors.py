"""The C FSM's expected hashes must be current, and the sessions must exercise the FSM."""

from ml.intent import gen_fsm_vectors as g


def test_header_is_up_to_date() -> None:
    assert g.HEADER.read_text(encoding="utf-8") == g.render()


def test_xorshift_known_values() -> None:
    # Marsaglia xorshift32 (13, 17, 5) from seed 1.
    r = g.XorShift32(1)
    assert [r.next() for _ in range(3)] == [270369, 67634689, 2647435461]


def test_sessions_cover_key_behaviours() -> None:
    cov = g.coverage()
    assert all(v >= 3 for v in cov.values()), cov
