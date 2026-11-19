# ADR-0005: RTL intent watchdog and verification flow

- Status: accepted
- Date: 2026-11-19
- Phase: 7
- Safety-relevant: yes (S1 on the FPGA path)

## Context
ROADMAP §7.2 asks for `intent_watchdog.sv` with a SymbiYosys proof that "no valid input for N cycles ⇒ output = STOP" (T7.1), cocotb tests, and a SystemVerilog subset that both Verilator and Quartus Lite accept.

## Decision
- `rtl/src/intent_watchdog.sv`: holds the last valid intent; after `TIMEOUT_CYCLES` clocks without `valid_in` (or since reset) outputs STOP (intent 0) and `timed_out`. Default 15 000 000 cycles = 300 ms at the DE10-Standard's 50 MHz clock. Synchronous active-low reset; reset state is timed out.
- **Fail-safe counter:** saturates with `<` and reports `timed_out` with `>=`, so out-of-range values (bit flips) read as timed out.
- **Formal** (`rtl/formal/intent_watchdog.sby`, `TIMEOUT_CYCLES` = 8): the main property uses an independent shift register of the last N `valid_in` bits, not the design's counter. Also proven: a valid input passes through on the next clock (the timeout isn't over-eager). Two cover statements (a live link times out without a reset; a non-STOP intent is carried). Engine `smtbmc z3`.
- **Simulation** (`rtl/tb/test_intent_watchdog.py`): cocotb 2.x via `cocotb_tools.runner` in pytest. `SIM=verilator` default (`make sim`), `SIM=icarus` supported.

## Evidence (2026-11-19, prep machine)
- SymbiYosys (YoWASP build) + z3: prove PASS (base case and induction), cover PASS (2/2 reached).
- Proof mutation check: an off-by-one timeout, removing STOP on timeout, and ignoring valid STOP frames each make the proof FAIL.
- cocotb + Icarus 14: reset, exact-timeout, recovery and 3 000-cycle random-model tests pass.
- verible lint and format check clean.
- Not yet run: Verilator simulation, Quartus synthesis (Fedora / DE10).

## Consequences
- Fedora needs `z3` for `make formal` (or change the engine line to yices).
- Next RTL modules (`spi_slave`, `delta_encoder`, `lif_layer`, `readout`) follow the same pattern: cocotb against a Python model; formal only for safety modules.
