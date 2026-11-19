"""Host <-> USB bridge serial framing: COBS(type || payload) + 0x00 (ADR-0003)."""

from __future__ import annotations

from dataclasses import dataclass

TYPE_NIP = 0x01
TYPE_STATUS = 0x02
MAX_PAYLOAD = 64
# type byte + payload, plus COBS overhead (1 per 254) and slack.
_MAX_ENCODED = 1 + MAX_PAYLOAD + 2


def cobs_encode(data: bytes) -> bytes:
    """COBS-encode `data`. Output contains no 0x00 and excludes the delimiter."""
    out = bytearray([0])
    code_idx: int | None = 0
    code = 1
    n = len(data)
    for i, byte in enumerate(data):
        if byte == 0:
            assert code_idx is not None
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)
            code = 1
            continue
        if code_idx is None:  # pragma: no cover - only after a final full block
            code_idx = len(out)
            out.append(0)
        out.append(byte)
        code += 1
        if code == 0xFF:
            out[code_idx] = code
            code = 1
            if i + 1 < n:
                code_idx = len(out)
                out.append(0)
            else:
                code_idx = None
    if code_idx is not None:
        out[code_idx] = code
    return bytes(out)


def cobs_decode(enc: bytes) -> bytes:
    """Inverse of cobs_encode. Raises ValueError on malformed input."""
    if not enc:
        raise ValueError("empty COBS frame")
    out = bytearray()
    i, n = 0, len(enc)
    while i < n:
        code = enc[i]
        if code == 0:
            raise ValueError("zero byte inside COBS frame")
        i += 1
        end = i + code - 1
        if end > n:
            raise ValueError("COBS block runs past end of frame")
        block = enc[i:end]
        if 0 in block:
            raise ValueError("zero byte inside COBS block")
        out += block
        i = end
        if code != 0xFF and i < n:
            out.append(0)
    return bytes(out)


def encode_message(msg_type: int, payload: bytes, *, check_size: bool = True) -> bytes:
    if check_size and len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} > {MAX_PAYLOAD} bytes")
    return cobs_encode(bytes([msg_type]) + payload) + b"\x00"


@dataclass(frozen=True)
class Message:
    type: int
    payload: bytes


class Deframer:
    """Stream deframer. Feed arbitrary chunks; returns complete messages."""

    def __init__(self) -> None:
        self._buf = bytearray()
        self._overflow = False
        self.errors = 0

    def feed(self, chunk: bytes) -> list[Message]:
        msgs: list[Message] = []
        for byte in chunk:
            if byte != 0:
                if len(self._buf) < _MAX_ENCODED:
                    self._buf.append(byte)
                else:
                    self._overflow = True
                continue
            frame, overflow = bytes(self._buf), self._overflow
            self._buf.clear()
            self._overflow = False
            if not frame and not overflow:
                continue  # back-to-back delimiters are harmless
            try:
                if overflow:
                    raise ValueError("oversized")
                raw = cobs_decode(frame)
                if not raw or len(raw) - 1 > MAX_PAYLOAD:
                    raise ValueError("bad length")
            except ValueError:
                self.errors += 1
                continue
            msgs.append(Message(raw[0], raw[1:]))
        return msgs
