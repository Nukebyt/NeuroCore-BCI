"""NeuroCore Intent Protocol v0 codec. Normative spec: protocol/nip/spec.md."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum, IntFlag

FRAME_LEN = 16
MAGIC = 0x4E50
VERSION = 0
LEVEL_MAX = 3
LINK_TIMEOUT_MS = 300
HEARTBEAT_HZ = 25

# magic, version, seq, t_ms, intent, level, confidence, flags, reserved (spec §1)
_BODY = struct.Struct("<HBBIBBBBH")
_FLAGS_DEFINED = 0x1F


class Intent(IntEnum):
    STOP = 0
    HOLD = 1
    FWD = 2
    REV = 3
    LEFT = 4
    RIGHT = 5
    MODE_NEXT = 6
    AUX_A = 7
    AUX_B = 8


class Flags(IntFlag):
    NONE = 0
    CALIBRATED = 1 << 0
    ARTIFACT = 1 << 1
    CHANNEL_BAD = 1 << 2
    LATCHED_STOP = 1 << 3
    TEST_MODE = 1 << 4


class Status(IntEnum):
    """Decode status codes, identical to nip_status_t in nip.h (spec §5)."""

    OK = 0
    LEN = 1
    MAGIC = 2
    VERSION = 3
    CRC = 4
    INTENT = 5
    LEVEL = 6
    FLAGS = 7
    RESERVED = 8


class NipError(ValueError):
    def __init__(self, status: Status) -> None:
        super().__init__(f"invalid NIP frame: {status.name}")
        self.status = status


@dataclass(frozen=True)
class Frame:
    seq: int
    t_ms: int
    intent: Intent | int
    level: int = 0
    confidence: int = 0
    flags: Flags | int = Flags.NONE


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0 (spec §4)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def _field_status(intent: int, level: int, flags: int, reserved: int) -> Status:
    if intent > Intent.AUX_B:
        return Status.INTENT
    if level > LEVEL_MAX:
        return Status.LEVEL
    if flags & ~_FLAGS_DEFINED & 0xFF:
        return Status.FLAGS
    if reserved != 0:
        return Status.RESERVED
    return Status.OK


def pack_unchecked(
    seq: int,
    t_ms: int,
    intent: int,
    level: int,
    confidence: int,
    flags: int,
    *,
    magic: int = MAGIC,
    version: int = VERSION,
    reserved: int = 0,
) -> bytes:
    """Pack any field values with a correct CRC. For building test vectors only."""
    body = _BODY.pack(magic, version, seq, t_ms, intent, level, confidence, flags, reserved)
    return body + struct.pack("<H", crc16_ccitt_false(body))


def encode(frame: Frame) -> bytes:
    for name, hi in (("seq", 0xFF), ("t_ms", 0xFFFFFFFF), ("confidence", 0xFF)):
        value = getattr(frame, name)
        if not 0 <= value <= hi:
            raise ValueError(f"{name}={value} out of range 0..{hi}")
    for name in ("intent", "level", "flags"):
        if int(getattr(frame, name)) < 0:
            raise ValueError(f"{name} must be non-negative")
    status = _field_status(int(frame.intent), frame.level, int(frame.flags), 0)
    if status is not Status.OK:
        raise NipError(status)
    return pack_unchecked(
        frame.seq, frame.t_ms, int(frame.intent), frame.level, frame.confidence, int(frame.flags)
    )


def decode(raw: bytes) -> Frame:
    """Decode one frame. Raises NipError with the first failing check (spec §5 order)."""
    if len(raw) != FRAME_LEN:
        raise NipError(Status.LEN)
    magic, version, seq, t_ms, intent, level, confidence, flags, reserved = _BODY.unpack(raw[:14])
    if magic != MAGIC:
        raise NipError(Status.MAGIC)
    if version != VERSION:
        raise NipError(Status.VERSION)
    (crc,) = struct.unpack("<H", raw[14:])
    if crc != crc16_ccitt_false(raw[:14]):
        raise NipError(Status.CRC)
    status = _field_status(intent, level, flags, reserved)
    if status is not Status.OK:
        raise NipError(status)
    return Frame(seq, t_ms, Intent(intent), level, confidence, Flags(flags))


def next_seq(seq: int) -> int:
    return (seq + 1) & 0xFF


def seq_is_newer(candidate: int, last: int) -> bool:
    """True if candidate is 1..127 steps after last, modulo 256 (spec §6)."""
    return 1 <= ((candidate - last) & 0xFF) <= 127
