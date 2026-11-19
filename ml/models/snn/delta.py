"""Delta (UP/DOWN) spike encoder, integer reference for rtl/src/delta_encoder.sv (ROADMAP §7.1).

At most one spike per sample: UP when x - ref >= thresh (ref += thresh), DOWN when
ref - x >= thresh (ref -= thresh). ref starts at 0. The RTL must match this bit for bit.
"""

from __future__ import annotations

from collections.abc import Iterable


def delta_encode(x: Iterable[int], thresh: int) -> list[int]:
    """Returns +1 (UP), -1 (DOWN) or 0 per sample."""
    if thresh <= 0:
        raise ValueError("thresh must be positive")
    ref, out = 0, []
    for v in x:
        d = int(v) - ref
        if d >= thresh:
            out.append(1)
            ref += thresh
        elif d <= -thresh:
            out.append(-1)
            ref -= thresh
        else:
            out.append(0)
    return out


def delta_decode(spikes: Iterable[int], thresh: int) -> list[int]:
    """Reconstruct the encoder's reference signal from spikes."""
    ref, out = 0, []
    for s in spikes:
        ref += s * thresh
        out.append(ref)
    return out
