# Status

Living progress log. Work proceeds autonomously through everything that doesn't need hardware (owner's instruction, 2026-11-19).

## Phase 0 — Foundations: closed 2026-11-19
- Safety rules signed off, then trimmed by the owner to three (battery-only acquisition, electrode series resistors, car watchdog + STOP). See `docs/safety.md`.
- GitHub push deferred until there is something worth showing (owner decision). CI workflow is ready: lint, Python + C tests, RTL sim (Icarus), formal (YoWASP SymbiYosys + z3).

## Phase 1 — Car adapter + NIP over keyboard: software done, hardware blocked
- **T1.1 NIP codec** — `protocol/nip/`: normative spec, Python + C codecs, generated golden vectors.
- **T1.2 car adapter core** — `firmware/car/{safety,control}/`, 17 host tests incl. randomized 2 × 200 000 steps; mutation-checked. ADR-0002.
- **Serial link** — COBS framing (ADR-0003), `host/serial_link.py` + `firmware/bridge-usb/core/`.
- **Teleop** — `python -m host.teleop --port <bridge>`.
- **Blocked:** ESP-IDF projects (pins/PWM need datasheets), truck rewiring, 50-trial link-loss test.

## Phase 2 — Rev 0 signals: recorder done, recording blocked
- **T2.1 recorder** — `host/record.py` + `host/session.py` (§11.4 layout, pseudonymous IDs, no overwrite, monotonic timestamps, `load_session()`).
- **Blocked:** BioAmp hardware, recordings, montage photos.

## Phase 3 — NeuroCore Rev A: unverified draft schematic + PCB (not for ordering)
- Datasheets for every IC in `docs/datasheets/` (TI, Espressif, Bosch); pin/value traceability in `NOTES.md`.
- `hardware/neurocore/gen/`: `design.py` (schematic, symbol library, `design.json`), `check_netlist.py`, `build_pcb.py` (KiCad 9 placement + SES import + GND pours), `route.sh` (full pipeline with Freerouting 2.1.0).
- Results: ERC 0; netlist 453/453 pins on the intended net; 4-layer 86 × 55 mm board, 141 parts, fully autorouted; KiCad DRC 0 errors, 0 unconnected, 0 schematic-parity issues (silk warnings only). Renders in `docs/reports/img/`. No gerbers on purpose.
- BOM: `hardware/neurocore/fab/neurocore-bom.csv`.
- Quote (2026-11-19): PCB Power offer INQ663178, 5 pcs bare board, 4L FR4 1.6 mm ENIG 1/0.5 oz, ₹6,650 + 18 % GST, 7 WD to dispatch, valid 30 days. Their drill-tolerance table starts at 0.50 mm; the board now has no hole below 0.30 mm (confirm 0.30 mm vias with them). Re-quote needed after the design changes. Lion Circuits quote pending.
- **PCB fixes (2026-11-19):** BAW56 clamps D4/D5 on the shadow-lane ADC inputs (abs max); all holes ≥ 0.3 mm (ESP32 thermal vias redrilled); `route.sh` retries until KiCad DRC sees a complete route (Freerouting 2.1.0 sometimes drops vias/wires from its SES). Verified against datasheets: LM27761 divider, TLV757P dropout, USB-C Rd (Type-C spec Table 4-25). Board: 141 parts, ERC 0, DRC 0 errors / 0 unconnected / 0 parity.
- **Firmware requirement from the dropout check:** shut down below a VBAT cutoff of ≥ 3.5 V under load (VBAT_SENSE on IO4) to keep the 3.3 V rail regulated.
- **Still open before ordering (ADR-0006):** battery connector polarity (check the pack), J4 cable-entry direction, land patterns vs datasheets, autorouted analog section (ROADMAP §3.4 checklist), lead-off LEDs dropped. Reviewer sign-off required.

## Phase 4 prep — drivers written ahead of hardware
- **T4.1 ADS1299-4 driver** — `firmware/neurocore/drivers/ads1299/`: power-up per DS §11.1, register write + read-back verify, RDATAC frames, µV scaling, lead-off status, test-signal mode. 17 host tests against a simulated chip that enforces the datasheet timing rules; mutation check 10/11 killed (survivor is redundant code). ADR-0007.
- **T4.2 ADS131M04 driver** — `firmware/neurocore/drivers/ads131m04/`: reset/ID/config with ack + read-back, output-CRC-checked frames, SYNC FIFO clear, µV scaling, test signal. 18 host tests against a simulated chip; mutation check 12/13 killed. ADR-0008.
- Open: ADS1299 VCAP1 charge time (Fig. 25), hardware run; ADS131M04 CRC bit order on a real frame.

## Phase 5 — Hybrid control v1: Python reference + C port of FSM and EMG front end
- Python reference: filters, EMG detector, SSVEP (CCA/FBCCA), intent FSM, full pipeline, `host/simulate.py`. ADR-0004.
- **SSVEP overshoot fixed (ADR-0004 rev 2):** speed is now edge-triggered, one step per look, re-armed by ≥ 500 ms of NONE. Found and fixed a pipeline bug that fed NONE between decoder steps. Demo: one 4 s look → exactly one step.
- **C port, `firmware/neurocore/`:**
  - `intent/intent_fsm.{h,c}` — matches the Python FSM on 300 shared xorshift32 sessions (FNV-1a output hashes; `ml/intent/gen_fsm_vectors.py`). Coverage: STOP 144, latch cleared 30, reverse 7, fault 90, level 3 reached 4 sessions. Explicit S2 latch tests in C. Mutation check 4/4 killed (after adding a gap-boundary test that one mutant exposed).
  - `dsp/biquad.{h,c}`, `dsp/emg_detect.{h,c}` — coefficients generated from the Python design (`ml/export/gen_dsp.py` → `dsp_coeffs.h`). Max error vs Python: filter 1.8e-4 µV, envelope 2.7e-5 µV; detector transitions identical to the millisecond.
- Freshness tests fail if any generated header is stale.

## Phase 7 — SNN on FPGA: first modules done
- `rtl/src/intent_watchdog.sv` — S1 on the FPGA path; fail-safe saturating counter. **Formal proof passes** (base + induction, 2/2 covers); proof mutation check 3/3 fail as expected. cocotb (Icarus) tests pass. ADR-0005.
- `rtl/src/delta_encoder.sv` — UP/DOWN spike encoder, cycle-exact with `ml/models/snn/delta.py` over 4 000 samples with random valid gaps.
- Not done: `lif_layer` (ROADMAP says reuse the LIF core from the owner's event-camera repo), `spi_slave`, `readout`; Verilator run; Quartus synthesis.

## Next (no hardware needed)
1. `spi_slave.sv` + `readout.sv`; bring in the existing LIF core (owner to point at the files, or approve fetching from github.com/Nukebyt/FPGA-SNN-Event-Camera).
2. SSVEP CCA/FBCCA in C (needs small QR/SVD), then the full NeuroCore pipeline on host.
3. Phase 6: public-dataset loaders (Benchmark/BETA, Ninapro DB2/DB5) and `make bench`.
4. Phase 3: resolve ADR-0006 open issues; hand-route analog front end.

## Waiting on the owner
- Datasheets: motor driver (TB6612FNG or DRV8833), ESP32-C3 SuperMini, bridge ESP32 board; (NeuroCore datasheets are in).
- Truck pack voltage (1S/2S).
- Commit/branch: nothing committed yet; everything staged on `phase-0-foundations`.
- `LICENSE` file and hardware licence choice.
- Fedora: `dnf install z3` for `make formal` (engine is `smtbmc z3`).

## Decisions
ADR-0001 tooling · ADR-0002 car adapter · ADR-0003 serial link · ADR-0004 intent FSM + pipeline (rev 2) · ADR-0005 RTL watchdog · ADR-0006 NeuroCore Rev A draft · ADR-0007 ADS1299 driver · ADR-0008 ADS131M04 driver.

## Verification notes (prep machine, Windows + WSL)
- Python: 96 tests (Python 3.13; pins target 3.12). ruff + black clean.
- C: 7 host suites with gcc 13 + ASan/UBSan (WSL Ubuntu). clang-format 23 clean.
- RTL: cocotb 2.1 + Icarus 14; SymbiYosys via YoWASP + z3; verible lint/format clean.
- Not run here: `make` end to end on Fedora, Verilator, Quartus, ESP-IDF builds.
