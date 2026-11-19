# ADR-0008: ADS131M04 driver design and NeuroCore default configuration

- Status: accepted (host-tested only; not yet run on hardware)
- Date: 2026-11-19
- Phase: 4 (task card T4.2), written ahead of hardware
- Safety-relevant: no

## Context
On NeuroCore Rev A (ADR-0006) the ADS131M04 (U2) has this wiring:
- **Channels:** CH0 = EMG pair 1 and CH1 = EMG pair 2, through 10 kΩ series resistors. CH2 and CH3 are the shadow-EEG lane (S1, S2 buffered, against SREF).
- **Clock:** CLKIN comes from the ESP32 at 8 MHz.
- **Control pins:** SYNC/RESET, CS and DRDY go to ESP32 GPIOs.
- **SPI:** SCLK, MOSI and MISO are shared with the ADS1299.

All facts come from TI SBAS890D (`docs/datasheets/ads131m04-sbas890.pdf`), and the constants in `ads131m04_regs.h` cite their sections.

## Decisions
1. **Same structure as the ADS1299 driver (ADR-0007):** a 4-function HAL (CS, transfer, SYNC/RESET pin, delay), plus host tests against a simulated chip (`test/host/fake_ads131m04.c`).
   The simulated chip models:
   - the response-in-the-next-frame pipeline (§8.5.1.7)
   - WREG/RREG/RESET responses (§8.5.1.10)
   - the full-frame rule (§8.5.1.11)
   - tPOR / tREGACQ (§6.7)
   - the SYNC-versus-RESET pulse width (§6.6, §8.5.2)
   - the SPI timeout (§8.5.1.7)
2. **24-bit words, input CRC off, output CRC always checked.** The output CRC is CCITT with seed FFFFh (§8.3.12) over the response and the four channel words. A bad CRC returns an error and increments `crc_errors`. Input CRC can be added later if the shared bus turns out to be noisy.
3. **Single-register RREG only.** Multi-register reads use a different response format (§8.5.1.10.7.2) that would add complexity for little gain at init time.
4. **Writes are acknowledged, then read back.** Each WREG must come back as `010a aaaa a000 0000` (one register written), then a read-back check follows. Only the register-map CRC space 02h–1Ch is writable (§8.3.13).
5. **Init sequence:**
   1. Wait tPOR.
   2. Hold SYNC/RESET low for 2048 tCLKIN (computed from the configured CLKIN), then wait tREGACQ.
   3. Check the ID (`24xxh`).
   4. Write and verify MODE, CLOCK, GAIN1, THRSHLD_LSB and CH0..3_CFG.
   5. Check that STATUS.RESET has cleared.
   6. Send a 2-tCLKIN SYNC pulse to empty the FIFOs (§8.5.1.9.1, Figure 8-20).
6. **Defaults:**
   - OSR 4096 at CLKIN 8 MHz gives 976.6 SPS (8.192 MHz would give exactly 1 kSPS, Table 8-2).
   - Gain 4 (±300 mV) on every channel. This leaves room for electrode DC offset on the directly coupled EMG inputs. It's a design choice, not a datasheet figure; revisit with real recordings.
   - DC-block filter off, because the host DSP high-passes anyway.
   - OSR code 111b is rejected: the register table says 16256 but Table 8-2 says 16384.
7. **Datasheet facts that close ADR-0006 open issue 2 (shared MISO).** Both converters put DOUT in high impedance whenever their CS is high: ADS131M04 §8.5.1.1 and §8.5.1.4, ADS1299 §9.5.2.1. Both also use SPI mode 1 (CPOL 0, CPHA 1): ADS131M04 §8.5.1, ADS1299 §9.5.2.2.

## Verification
- 18 host tests passing under ASan/UBSan.
- Mutation check: 12 of 13 mutants killed. The survivor removes the tPOR wait, which the following 2048-tCLKIN reset pulse already covers (about 244 µs at 8.4 MHz, 256 µs at 8 MHz).

## Open items
- **CRC bit order:** polynomial and seed are from §8.3.12; the bit order is MSB first, as data is shifted (§8.5.1.4). Confirm against a real frame at bring-up.
- **Unclear datasheet values:**
  - UNLOCK is given as both 0655h (Table 8-11) and 0666h (§8.5.1.10.6). The driver doesn't use LOCK/UNLOCK.
  - OSR 111b is rejected (see decision 6).
- Not yet run on a real chip.
