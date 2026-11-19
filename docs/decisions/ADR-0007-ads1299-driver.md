# ADR-0007: ADS1299-4 driver design and NeuroCore default configuration

- Status: accepted (host-tested only; not yet run on hardware)
- Date: 2026-11-19
- Phase: 4 (task card T4.1), written ahead of hardware
- Safety-relevant: no (acquisition settings only; battery-only rule unaffected)

## Context
NeuroCore Rev A (ADR-0006) wires the ADS1299-4 as follows:
- internal oscillator (CLKSEL pulled high)
- START, RESET and PWDN driven by the ESP32
- four electrode inputs on the positive inputs; the negative inputs are not connected
- the REF electrode on SRB1
- BIASOUT to the bias electrode

All facts come from TI SBAS499C (`docs/datasheets/ads1299-sbas499.pdf`). The constants in `ads1299_regs.h` cite their section.

## Decisions
1. **Platform-free driver with a 4-function HAL** (CS, transfer, pin, delay). The same code runs on the ESP32 and in host tests. The tests use a simulated chip (`test/host/fake_ads1299.c`) that models the register file, RDATAC/SDATAC behaviour and every timing rule the datasheet states (§7.6, §9.4.2, §9.5.3.1, §11.1). Any timing breach counts as a test failure.
2. **Worst-case clock for all delays.** Delays assume tCLK = 666 ns, the slowest allowed (§7.6), so they hold for any valid master clock. Example: tPOR = 2¹⁸ × 666 ns = 175 ms.
3. **Byte spacing computed from SCLK.** Multi-byte commands are sent one byte at a time. A gap is inserted only when a byte is shorter than 4 tCLK (§9.5.3.1): 1 µs at 4 MHz, 3 µs at 16 MHz. SCLK above 20 MHz is rejected (§7.6).
4. **Power-up follows §11.1 and Figure 67:**
   1. PWDN and RESET high.
   2. Wait max(tPOR, `vcap1_settle_ms`).
   3. RESET pulse, then wait 18 tCLK.
   4. SDATAC, then check the ID (must read as an ADS1299-4).
   5. Enable the internal reference and wait 150 ms (§7.5).
   6. Write every register and read each one back.
   7. Set START high and send RDATAC.
   
   Any failure leaves START low and returns an error.
5. **Default configuration (`ads1299_default_config`):**
   - 250 SPS, gain 24 on all channels (full scale ±187.5 mV).
   - MISC1.SRB1 = 1: referential montage to the REF electrode.
   - Bias on: BIASREF internal, derived from the positive inputs only (BIAS_SENSP = 0x0F, BIAS_SENSN = 0). The negative inputs all sit on SRB1, so adding them would only feed REF back in.
   - DC lead-off on the positive inputs: 6 nA, 95 % threshold. Status comes back in every frame.
6. **Scaling to µV in float**, 1 LSB = 2·VREF/gain/2²⁴ with VREF = 4.5 V (§9.5.1). The ESP32-S3 has a hardware FPU, and the DSP chain (ADR-0004) already works in float µV.

## Verification
- 17 host tests, 174 checks, passing under ASan/UBSan (gcc 13, WSL).
- Mutation check: 10 of 11 mutants killed. The survivor removes the explicit SDATAC in init; `read_regs` sends SDATAC itself, so behaviour doesn't change.

## Open items
- **VCAP1 charge time** (§11.1, Figure 25) depends on the 100 µF VCAP1 capacitor. It has not been read from the figure yet. The firmware must set `vcap1_settle_ms` once it is checked; the default is 0, meaning tPOR only.
- The DC lead-off current (6 nA) flows continuously. Measure its effect on the noise floor in Phase 4 and switch to on-demand lead-off checks if it matters.
- Shared SPI with the ADS131M04 (ADR-0006 open issue 2): the driver releases CS between transactions. Bus arbitration is the application's job.
- Not yet run on a real chip.
