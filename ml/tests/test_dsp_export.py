"""Generated firmware DSP headers must match the Python filter design."""

from ml.export import gen_dsp


def test_coeffs_header_is_up_to_date() -> None:
    assert gen_dsp.COEFFS.read_text(encoding="utf-8") == gen_dsp.render_coeffs()


def test_vectors_header_is_up_to_date() -> None:
    assert gen_dsp.VECTORS.read_text(encoding="utf-8") == gen_dsp.render_vectors()


def test_detector_sees_both_bursts() -> None:
    _, _, transitions = gen_dsp.expected()
    assert [s for _, s in transitions] == [1, 0, 1, 0]
    on_latency = [t - a for (t, s), (a, _) in zip(transitions[::2], gen_dsp.BURSTS, strict=True)]
    assert all(0 < lat <= 250 for lat in on_latency)
