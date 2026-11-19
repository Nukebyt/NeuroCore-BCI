"""Host <-> bridge serial framing (ADR-0003)."""

import pytest
from hypothesis import given
from hypothesis import strategies as st

from host.serial_link import (
    MAX_PAYLOAD,
    TYPE_NIP,
    Deframer,
    cobs_decode,
    cobs_encode,
    encode_message,
)

# Published COBS examples (Cheshire & Baker algorithm; encoded form without the 0x00 delimiter).
COBS_VECTORS = [
    (b"", b"\x01"),
    (b"\x00", b"\x01\x01"),
    (b"\x00\x00", b"\x01\x01\x01"),
    (b"\x00\x11\x00", b"\x01\x02\x11\x01"),
    (b"\x11\x22\x00\x33", b"\x03\x11\x22\x02\x33"),
    (b"\x11\x22\x33\x44", b"\x05\x11\x22\x33\x44"),
    (b"\x11\x00\x00\x00", b"\x02\x11\x01\x01\x01"),
    (bytes(range(1, 255)), b"\xff" + bytes(range(1, 255))),
    (bytes(range(0, 255)), b"\x01\xff" + bytes(range(1, 255))),
    (bytes(range(1, 256)), b"\xff" + bytes(range(1, 255)) + b"\x02\xff"),
    (bytes(range(2, 256)) + b"\x00", b"\xff" + bytes(range(2, 256)) + b"\x01\x01"),
    (bytes(range(3, 256)) + b"\x00\x01", b"\xfe" + bytes(range(3, 256)) + b"\x02\x01"),
]


@pytest.mark.parametrize(("raw", "encoded"), COBS_VECTORS)
def test_cobs_reference_vectors(raw: bytes, encoded: bytes) -> None:
    assert cobs_encode(raw) == encoded
    assert cobs_decode(encoded) == raw


@given(st.binary(max_size=600))
def test_cobs_round_trip_and_no_zeros(raw: bytes) -> None:
    enc = cobs_encode(raw)
    assert 0 not in enc
    assert cobs_decode(enc) == raw


@pytest.mark.parametrize("bad", [b"\x00", b"\x05\x11", b"\x02\x00", b"\x03\x11\x00"])
def test_cobs_decode_rejects_malformed(bad: bytes) -> None:
    with pytest.raises(ValueError):
        cobs_decode(bad)


def test_deframer_splits_stream_and_resyncs_after_garbage() -> None:
    a = encode_message(TYPE_NIP, b"\x01\x00\x02")
    b = encode_message(0x02, b"")
    d = Deframer()
    # Garbage before the first delimiter, then two messages split across chunks.
    stream = b"\x07\x07\x00" + a + b
    msgs = []
    for i in range(0, len(stream), 3):
        msgs += d.feed(stream[i : i + 3])
    assert [(m.type, m.payload) for m in msgs] == [(TYPE_NIP, b"\x01\x00\x02"), (0x02, b"")]
    assert d.errors == 1  # the garbage frame


def test_deframer_drops_oversized_messages() -> None:
    d = Deframer()
    big = encode_message(TYPE_NIP, bytes(MAX_PAYLOAD + 1), check_size=False)
    ok = encode_message(TYPE_NIP, b"\x10")
    msgs = d.feed(big + ok)
    assert [m.payload for m in msgs] == [b"\x10"]
    assert d.errors == 1


def test_encode_message_refuses_oversized_payload() -> None:
    with pytest.raises(ValueError):
        encode_message(TYPE_NIP, bytes(MAX_PAYLOAD + 1))
