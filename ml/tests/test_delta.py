"""Delta spike encoder reference (rtl/src/delta_encoder.sv must match it)."""

import pytest
from hypothesis import given
from hypothesis import strategies as st

from ml.models.snn.delta import delta_decode, delta_encode


def test_known_sequence() -> None:
    assert delta_encode([0, 10, 25, 25, 5, -30], thresh=10) == [0, 1, 1, 0, -1, -1]


@given(st.integers(1, 200), st.lists(st.integers(-200, 200), min_size=1, max_size=300))
def test_tracks_slope_limited_signals(thresh: int, steps: list[int]) -> None:
    # If the signal moves at most `thresh` per sample, reconstruction error stays < thresh.
    x, v = [], 0
    for s in steps:
        v += max(-thresh, min(thresh, s))
        x.append(v)
    recon = delta_decode(delta_encode(x, thresh), thresh)
    assert all(abs(a - b) < thresh for a, b in zip(x, recon, strict=True))


def test_rejects_bad_threshold() -> None:
    with pytest.raises(ValueError):
        delta_encode([1, 2], 0)
