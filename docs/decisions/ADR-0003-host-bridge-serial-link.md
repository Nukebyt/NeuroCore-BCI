# ADR-0003: Host ↔ USB bridge serial link

- Status: accepted
- Date: 2026-11-19
- Phase: 1
- Safety-relevant: yes (S1)

## Context
`host/teleop.py` (and later the host-side simulator) sends NIP frames to `firmware/bridge-usb`, an ESP32 that forwards them over ESP-NOW (ROADMAP §10 Phase 1). USB CDC serial is a byte stream, so frames need delimiting. NIP spec §8 leaves transport framing to each transport.

## Options
1. **COBS + 0x00 delimiter**: constant overhead (1 byte per 254), resyncs at the next 0x00 after any glitch, trivial in C without allocation.
2. **SLIP**: similar, but overhead depends on content (escapes).
3. **Raw 16-byte frames**: no resync after a dropped byte.

## Decision
COBS (Cheshire & Baker) with a 0x00 delimiter. Each message is `COBS(type ‖ payload) 0x00`.

| type | direction | payload |
|---|---|---|
| `0x01` NIP | host → bridge | one 16-byte NIP frame, forwarded unchanged |
| `0x02` STATUS | bridge → host | reserved for later (ESP-NOW send result, counters) |

- Max payload 64 bytes; longer messages are dropped as errors.
- The bridge forwards a NIP message only if `nip_decode` returns `NIP_OK`.
- **The bridge never generates, repeats or buffers NIP frames.** If the host stops sending, the car's 300 ms watchdog (S1) fires. This is what makes "pull the bridge's power" and "kill teleop" both valid failsafe tests.

## Consequences
- Python (`host/serial_link.py`) and C (`firmware/bridge-usb/core/`) share COBS test vectors taken from the published algorithm examples.
- The STATUS message format is defined when the bridge firmware is built.
