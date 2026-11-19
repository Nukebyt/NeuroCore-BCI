# CLAUDE.md — NeuroDrive

Standing instructions for Claude Code in this repo. The full plan is in `ROADMAP.md`; progress is in `docs/STATUS.md`.

## What this project is
NeuroDrive builds **NeuroCore**, a battery-powered EEG + EMG acquisition board (ADS1299-4 + ADS131M04 + ESP32-S3 + IMU). NeuroCore decodes biosignals into **intents** and sends them over the **NeuroCore Intent Protocol (NIP)** to device adapters:
1. Toy RC truck (first).
2. BLE gamepad.
3. Smart plugs.
4. Nano drone.

There is also an SNN "Intent Engine" in SystemVerilog for the DE10-Standard (Cyclone V), with a later path to silicon.

Owner: Nuke. Works on Fedora, Python 3.12 venv, RTX 3050 Ti, Icarus/Verilator/cocotb/SymbiYosys, Quartus Prime Lite, KiCad.

## Hard rules (never break these)
Safety rules are deliberately minimal (owner's decision, 2026-11-19): see `docs/safety.md`, three rules only.

1. **Keep the link watchdog and STOP.** Don't remove the NIP 300 ms link watchdog (S1) or STOP / latched-stop handling (S2). Changes to `firmware/*/safety/`, `intent/` or `rtl/src/intent_watchdog.sv` keep tests proving S1/S2.
2. **Never invent hardware facts.** Register addresses, bit fields, timing, pinouts, footprints, power limits: take them only from datasheets in `docs/datasheets/`. Cite the section in a comment (`// ADS1299 DS §9.6.1.3`). If the source isn't in the repo, ask for it and work on something else meanwhile.
3. **Acquisition is battery-only.** Never suggest mains-powered acquisition, or a laptop on its charger connected by USB without an isolator. Never suggest removing electrode series resistors.
4. **No medical claims** in code, docs or UI text.
5. **Licences.** Don't paste code or layout from GPL, CC-BY-NC or CERN-OHL-S projects into this repo without asking. Permissive sources (MIT, Apache-2.0, CERN-OHL-P, TI reference designs) are fine with attribution in `NOTICE.md`.
6. **Never commit** `data/` raw recordings, personal info, or secrets.

## How to work
- One phase at a time, branch `phase-N-<slug>`. At the start, read the matching `ROADMAP.md` section and `docs/STATUS.md`. At the end, update `docs/STATUS.md` (done / open / decisions) and add an ADR in `docs/decisions/` for any non-obvious choice.
- **Tests first.** Each task card in `ROADMAP.md §11.5` lists acceptance tests; write them before the implementation.
- Prefer **simulation and replay** over live hardware:
  - BrainFlow synthetic/playback boards.
  - Recorded sessions in `data/` (or `ml/data/samples/`).
  - cocotb + Verilator.
  - Host-side firmware unit tests with simulated time.
- Keep `protocol/nip/spec.md` the single source of truth. C (`nip.h/.c`) and Python (`nip.py`) codecs must pass the same `vectors.json`.
- ML results only count if the **artifact sanity tests** pass (ROADMAP §6.3): relaxed-jaw rerun, shuffled labels, electrodes-off dummy, saliency check.
- Work autonomously through the phases, building everything possible without hardware. Don't stop to ask for confirmation between steps: pick a sensible default, record it in an ADR, and keep going. Ask only when truly blocked (e.g. a missing datasheet), and keep working on other tasks meanwhile.

## Commands
```bash
make check          # lint + format check (ruff, black, clang-format, verible)
make test           # pytest (host, ml, protocol) + firmware host tests
make sim            # cocotb + Verilator RTL tests
make formal         # SymbiYosys proofs (rtl/formal/*.sby)
make bench          # ML benchmark → docs/reports/bench.md
make fw-car         # idf.py build for firmware/car   (flash = human)
make fw-neurocore   # idf.py build for firmware/neurocore (flash = human)
make hw-check       # kicad-cli ERC/DRC + BOM + gerbers into hardware/neurocore/fab/
```

## Conventions
- **Python:**
  - Type hints, ruff + black.
  - pytest; property tests with hypothesis for the FSM and DSP.
  - Units in names (`emg_rms_uv`, `t_ms`).
- **Firmware:**
  - ESP-IDF ≥ 5.3, C11.
  - No dynamic allocation in ISR paths.
  - Every task has a watchdog feed.
  - Fixed-point where it matters, with documented Q-format.
- **RTL:**
  - SystemVerilog subset accepted by both Verilator and Quartus Lite.
  - Synchronous active-low reset.
  - Parameters for widths.
  - Every module has a cocotb test; safety modules also have SBY properties.
- **Data:** follow ROADMAP §11.4 (raw + events.csv + meta.json + calib.json).
- **Docs:** short, factual, with plots saved under `docs/reports/img/`.

## Current phase
See `docs/STATUS.md`. If it doesn't exist yet, start Phase 0 from ROADMAP §10.
