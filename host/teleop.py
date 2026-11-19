"""Keyboard teleop: drive the car with NIP frames over the USB bridge (ROADMAP Phase 1).

Keys: W forward, S reverse (hold), A/D steer (hold), Space stop, 0-3 speed level, Esc quit.
Frames go out at 25 Hz (NIP heartbeat) with the test_mode flag set. Releasing every key
sends HOLD at level 0, so the car coasts. Quitting sends three STOP frames; after that the
car's 300 ms link watchdog takes over.

Usage: python -m host.teleop --port /dev/ttyACM0
Needs the `teleop` extra (pyserial, pygame).
"""

from __future__ import annotations

import argparse
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Protocol

from host.serial_link import TYPE_NIP, encode_message
from protocol.nip import nip
from protocol.nip.nip import Flags, Frame, Intent

PERIOD_MS = 1000 // nip.HEARTBEAT_HZ  # 40 ms
# The car only changes gear on FWD/REV frames (ADR-0002), so after a gear change send a
# few FWD/REV frames before steering intents, even if a steer key is already held.
GEAR_ASSERT_FRAMES = 3
EXIT_STOP_FRAMES = 3


@dataclass(frozen=True)
class Keys:
    fwd: bool = False
    rev: bool = False
    left: bool = False
    right: bool = False
    stop: bool = False


class TeleopCore:
    """Pure keys -> NIP frame logic. No I/O."""

    def __init__(self, level: int = 1) -> None:
        self.level = 0
        self.set_level(level)
        self._seq = 0
        self._gear = 0
        self._gear_frames = 0

    def set_level(self, level: int) -> None:
        self.level = max(0, min(nip.LEVEL_MAX, level))

    def _intent(self, keys: Keys) -> tuple[Intent, int]:
        if keys.stop or (keys.fwd and keys.rev):
            self._gear = 0
            return Intent.STOP, 0
        gear = 1 if keys.fwd else -1 if keys.rev else 0
        if gear != self._gear:
            self._gear, self._gear_frames = gear, 0
        level = self.level if gear else 0
        gear_intent = Intent.FWD if gear > 0 else Intent.REV
        if gear and self._gear_frames < GEAR_ASSERT_FRAMES:
            self._gear_frames += 1
            return gear_intent, level
        if keys.left != keys.right:
            return (Intent.LEFT if keys.left else Intent.RIGHT), level
        if gear:
            return gear_intent, level
        return Intent.HOLD, 0

    def next_frame(self, keys: Keys, t_ms: int) -> Frame:
        intent, level = self._intent(keys)
        frame = Frame(
            seq=self._seq,
            t_ms=t_ms & 0xFFFFFFFF,
            intent=intent,
            level=level,
            confidence=255,
            flags=Flags.TEST_MODE,
        )
        self._seq = nip.next_seq(self._seq)
        return frame

    def stop_frame(self, t_ms: int) -> Frame:
        return self.next_frame(Keys(stop=True), t_ms)


class Port(Protocol):
    def write(self, data: bytes, /) -> int | None: ...


def _send(port: Port, frame: Frame) -> None:
    port.write(encode_message(TYPE_NIP, nip.encode(frame)))


def run(
    port: Port,
    read_keys: Callable[[], Keys],
    should_quit: Callable[[], bool],
    now_ms: Callable[[], int],
    sleep_ms: Callable[[int], None],
    core: TeleopCore | None = None,
) -> None:
    """Send one frame every PERIOD_MS until should_quit(), then send STOP frames."""
    core = core or TeleopCore()
    next_t = now_ms()
    try:
        while not should_quit():
            now = now_ms()
            if now < next_t:
                sleep_ms(next_t - now)
                continue
            _send(port, core.next_frame(read_keys(), next_t))
            next_t += PERIOD_MS
            if now - next_t > PERIOD_MS:  # fell behind (e.g. laptop stalled): don't burst
                next_t = now + PERIOD_MS
    finally:
        for _ in range(EXIT_STOP_FRAMES):
            _send(port, core.stop_frame(now_ms()))


def main(argv: list[str] | None = None) -> None:  # pragma: no cover - needs a display + port
    import pygame
    import serial

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port of the USB bridge")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--level", type=int, default=1, help="initial speed level 0-3")
    args = ap.parse_args(argv)

    pygame.init()
    screen = pygame.display.set_mode((420, 160))
    pygame.display.set_caption("NeuroDrive teleop")
    font = pygame.font.Font(None, 28)
    core = TeleopCore(level=args.level)
    state = {"quit": False, "keys": Keys()}

    def read_keys() -> Keys:
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT or (ev.type == pygame.KEYDOWN and ev.key == pygame.K_ESCAPE):
                state["quit"] = True
            elif ev.type == pygame.KEYDOWN and pygame.K_0 <= ev.key <= pygame.K_3:
                core.set_level(ev.key - pygame.K_0)
        p = pygame.key.get_pressed()
        keys = Keys(
            fwd=p[pygame.K_w],
            rev=p[pygame.K_s],
            left=p[pygame.K_a],
            right=p[pygame.K_d],
            stop=p[pygame.K_SPACE] or not pygame.key.get_focused(),
        )
        screen.fill((20, 20, 24))
        lines = [f"level {core.level}   keys {keys}", "W/S drive  A/D steer  Space stop  Esc quit"]
        for i, text in enumerate(lines):
            screen.blit(font.render(text, True, (230, 230, 230)), (12, 20 + 34 * i))
        pygame.display.flip()
        return keys

    with serial.Serial(args.port, args.baud, timeout=0) as port:
        run(
            port,
            read_keys=read_keys,
            should_quit=lambda: bool(state["quit"]),
            now_ms=lambda: time.monotonic_ns() // 1_000_000,
            sleep_ms=lambda ms: time.sleep(ms / 1000),
            core=core,
        )
    pygame.quit()


if __name__ == "__main__":  # pragma: no cover
    main()
