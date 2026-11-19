"""T1.1 acceptance tests for the Python NIP codec (spec: protocol/nip/spec.md)."""

import json
from pathlib import Path

import pytest
from hypothesis import given
from hypothesis import strategies as st

from protocol.nip import gen_vectors, nip
from protocol.nip.nip import Flags, Frame, Intent, NipError, Status

NIP_DIR = Path(__file__).resolve().parents[1]

frames = st.builds(
    Frame,
    seq=st.integers(0, 255),
    t_ms=st.integers(0, 0xFFFFFFFF),
    intent=st.sampled_from(list(Intent)),
    level=st.integers(0, 3),
    confidence=st.integers(0, 255),
    flags=st.integers(0, 0x1F).map(Flags),
)


# --- independent anchors (don't depend on the code under test) -------------------------


def test_crc_check_value() -> None:
    # CRC-16/CCITT-FALSE catalogue check value (spec §4).
    assert nip.crc16_ccitt_false(b"123456789") == 0x29B1


def test_layout_matches_spec_by_hand() -> None:
    frame = Frame(
        seq=1,
        t_ms=0x01020304,
        intent=Intent.FWD,
        level=2,
        confidence=200,
        flags=Flags.CALIBRATED,
    )
    raw = nip.encode(frame)
    assert len(raw) == 16
    # magic LE, version, seq, t_ms LE, intent, level, confidence, flags, reserved
    assert raw[:14] == bytes.fromhex("504E 00 01 04030201 02 02 C8 01 0000")
    crc = nip.crc16_ccitt_false(raw[:14])
    assert raw[14:] == crc.to_bytes(2, "little")


# --- round trip and rejection -----------------------------------------------------------


@given(frames)
def test_round_trip(frame: Frame) -> None:
    assert nip.decode(nip.encode(frame)) == frame


@given(frames, st.integers(0, 16 * 8 - 1))
def test_any_single_bit_flip_is_rejected(frame: Frame, bit: int) -> None:
    raw = bytearray(nip.encode(frame))
    raw[bit // 8] ^= 1 << (bit % 8)
    with pytest.raises(NipError):
        nip.decode(bytes(raw))


def test_crc_mismatch_rejected() -> None:
    raw = bytearray(nip.encode(Frame(seq=7, t_ms=1000, intent=Intent.HOLD, level=1)))
    raw[15] ^= 0xFF
    with pytest.raises(NipError) as err:
        nip.decode(bytes(raw))
    assert err.value.status is Status.CRC


@pytest.mark.parametrize("length", [0, 15, 17, 32])
def test_wrong_length_rejected(length: int) -> None:
    with pytest.raises(NipError) as err:
        nip.decode(bytes(length))
    assert err.value.status is Status.LEN


@pytest.mark.parametrize(
    ("field", "value", "status"),
    [("intent", 9, Status.INTENT), ("level", 4, Status.LEVEL), ("flags", 0x20, Status.FLAGS)],
)
def test_encoder_refuses_invalid_fields(field: str, value: int, status: Status) -> None:
    kwargs = {"seq": 0, "t_ms": 0, "intent": Intent.STOP, "level": 0, field: value}
    with pytest.raises(NipError) as err:
        nip.encode(Frame(**kwargs))
    assert err.value.status is status


@pytest.mark.parametrize(("field", "value"), [("seq", 256), ("t_ms", 1 << 32), ("confidence", -1)])
def test_encoder_refuses_out_of_range_ints(field: str, value: int) -> None:
    kwargs = {"seq": 0, "t_ms": 0, "intent": Intent.STOP, "level": 0, field: value}
    with pytest.raises(ValueError):
        nip.encode(Frame(**kwargs))


# --- sequence numbers -------------------------------------------------------------------


def test_seq_wraps_in_encoder_sequence() -> None:
    seqs = [
        nip.decode(nip.encode(Frame(seq=s, t_ms=0, intent=Intent.HOLD))).seq for s in (254, 255, 0)
    ]
    assert seqs == [254, 255, 0]
    assert nip.next_seq(255) == 0
    assert nip.seq_is_newer(0, 255)
    assert not nip.seq_is_newer(255, 0)


@given(st.integers(0, 255), st.integers(0, 255))
def test_seq_is_newer_is_antisymmetric(a: int, b: int) -> None:
    diff = (a - b) % 256
    if diff in (0, 128):
        assert not (nip.seq_is_newer(a, b) and nip.seq_is_newer(b, a))
    else:
        assert nip.seq_is_newer(a, b) != nip.seq_is_newer(b, a)


# --- golden vectors ---------------------------------------------------------------------


def _vectors() -> dict:
    return json.loads((NIP_DIR / "vectors.json").read_text(encoding="utf-8"))


def test_vectors_files_are_up_to_date() -> None:
    json_text, c_header = gen_vectors.render()
    assert (NIP_DIR / "vectors.json").read_text(encoding="utf-8") == json_text
    assert (NIP_DIR / "tests" / "c" / "nip_vectors.h").read_text(encoding="utf-8") == c_header


def test_valid_vectors() -> None:
    vectors = _vectors()["valid"]
    assert len(vectors) >= 10
    for v in vectors:
        f = v["fields"]
        frame = Frame(**{**f, "intent": Intent(f["intent"]), "flags": Flags(f["flags"])})
        raw = bytes.fromhex(v["bytes"])
        assert nip.encode(frame) == raw, v["name"]
        assert nip.decode(raw) == frame, v["name"]


def test_invalid_vectors() -> None:
    vectors = _vectors()["invalid"]
    assert {v["error"] for v in vectors} == {s.name for s in Status if s is not Status.OK}
    for v in vectors:
        with pytest.raises(NipError) as err:
            nip.decode(bytes.fromhex(v["bytes"]))
        assert err.value.status.name == v["error"], v["name"]


def test_seq_vectors() -> None:
    for v in _vectors()["seq_is_newer"]:
        assert nip.seq_is_newer(v["candidate"], v["last"]) is v["newer"], v
