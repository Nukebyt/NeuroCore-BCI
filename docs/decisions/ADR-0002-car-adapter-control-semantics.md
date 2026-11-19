# ADR-0002: Car adapter control semantics

- Status: proposed (owner to confirm)
- Date: 2026-11-19
- Phase: 1
- Safety-relevant: yes (S1, S2)

## Context
ROADMAP §4.2 step 8 and T1.2 require an ESP-NOW NIP receiver with a 300 ms watchdog ("coast then brake"), a speed cap, a soft-start ramp and a bench mode. NIP carries one intent per frame, so the adapter must decide how gear, speed level and steering combine, and how it treats duplicates and reboots. None of this depends on hardware facts; pins and PWM come later from the motor-driver datasheet.

## Decision
Implemented in `firmware/car/control/car_ctrl.c` and `firmware/car/safety/link_watchdog.c`, tested in `firmware/car/test/host/`.

1. **Watchdog (S1).** The timeout is the protocol constant `NIP_LINK_TIMEOUT_MS` (300 ms), not a config value. It starts expired and latches expiry. On expiry the drive **coasts, then brakes after `brake_after_coast_ms` (default 200 ms)**, steering centres, gear goes to neutral.
2. **Only accepted frames feed the watchdog.** Accepted = decodes `NIP_OK` (strict v0, spec §5) **and** has a newer seq (spec §6). Duplicates and old frames are `STALE` and ignored. **Resync:** once the link has expired, the next valid frame is accepted whatever its seq, so a rebooted NeuroCore reconnects.
3. **Gear.** Starts neutral. `FWD`/`REV` set the gear; other intents keep it. `STOP`, the `latched_stop` flag and link loss reset it to neutral, so `HOLD` after a stop cannot restart motion.
4. **STOP (S2).** `STOP` intent or `latched_stop` flag → brake immediately, regardless of level.
5. **Speed caps and artifact freeze.** Effective level = min(frame level, `max_level`); capped at 1 in bench mode or when `channel_bad` is set; with `artifact` set it may not exceed the previous level (adapter-side copy of the FSM rule, defense in depth).
6. **Ramp.** Only increases in |duty| are ramped (`ramp_permille_per_s`, default 2000 → 0 to full in 0.5 s). Decreases are instant. A direction change drops to zero first.
7. **Steering.** `LEFT`/`RIGHT` steer while held and keep driving at `level` in the current gear; every other intent centres.
8. **Defaults.** `bench_mode = true`, `max_level = 2`, duty per level `{0, 350, 600, 850}` ‰. Duty values are placeholders to tune on the bench. An invalid config makes `init` fail and `tick` always brake.

## Consequences
- The adapter is safe even if NeuroCore's FSM has a bug in its cap or artifact-freeze handling.
- A sender reboot causes up to 300 ms of stop before resync, which is acceptable.
- `brake_after_coast_ms` and the duty table need confirming on the real truck (Phase 1 bench work).
- MODE_NEXT/AUX_A/AUX_B are no-ops for the car (drive continues, steering centres).
