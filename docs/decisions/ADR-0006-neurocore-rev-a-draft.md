# ADR-0006: NeuroCore Rev A schematic and PCB (unverified draft)

- Status: draft — **not for ordering** until reviewed (owner asked for a full attempt including routing)
- Date: 2026-11-19
- Phase: 3
- Safety-relevant: yes (battery-only acquisition, electrode series resistors)

## Context
ROADMAP §3 specifies the board at block level. The owner asked for a full attempt, including routing. Datasheets were downloaded into `docs/datasheets/` and every pin and value is traced in `docs/datasheets/NOTES.md`. KiCad 9 and Freerouting were used headlessly; nothing was drawn by hand.

## Decisions
1. **Design as code.** `hardware/neurocore/gen/design.py` is the source of truth. It writes the schematic (net labels on every pin), a symbol library `nd.kicad_sym`, and `design.json`. `check_netlist.py` confirms KiCad reads every pin onto the intended net. `build_pcb.py` places and routes the board.
2. **ADS1299 on bipolar ±2.5 V** (TPS7A2025 +2.5 V, LM27761 −2.5 V), DVDD 3.3 V. Avoids a boost converter from the 1S cell. Body bias (BIASOUT) sits at 0 V = GND, so electrode signals are centred on ground.
3. **ADS131M04** on its own TPS7A2033 3.3 V analog rail. EMG goes straight in through 10 kΩ, because its inputs accept −1.3 V…+1.5 V around ground at gain ≥ 8. The shadow EEG lane (S1/S2) goes through OPA2333 followers on ±2.5 V, as the roadmap asks.
4. **Clock for ADS131M04** from ESP32 LEDC (8 MHz, within 0.3–8.4 MHz). No extra oscillator. Data rates scale by 8/8.192.
5. **Protection.** Bidirectional TPD4E02B04 ESD at the electrode connectors (unidirectional parts would conduct on negative excursions), then a 10 kΩ series resistor on every electrode line (safety rule 2), including BIAS and REF.
6. **IMU: BMI270** in I2C mode (the ST LSM6DSO datasheet could not be downloaded).
7. **Power path:** 1S cell → slide switch → VSYS. BQ21040 charges from USB-C VBUS at ~270 mA. The "USB connected — check isolation" LED runs straight from VBUS (safety rule 1 reminder).
8. **Board:** 86 × 55 mm, 4 layers (F.Cu signals / In1 GND plane / In2 signals / B.Cu signals). Analog on the left, ESP32 on the right with its antenna at the top edge, power along the bottom.
9. **Antenna keepout from the footprint.** The KiCad ESP32-S3-WROOM-1 footprint carries a copper/parts rule area (board x 51.5–86 mm, y 0–8.75 mm). Placement uses that box as its keepout, so H2 sits at (47.5, 3.5) and the Qwiic connector J4 moved to the right edge.
10. **GND pads connect solid to the pours** (no thermal spokes). This suits reflow assembly and avoids starved-thermal DRC errors. Hand soldering GND pins will need more heat.
11. **Minimum drill 0.3 mm.** The library ESP32 footprint's 12 ground-pad thermal vias (0.6 mm pads, 0.2 mm drill) are drilled at 0.3 mm. Espressif's land pattern (DS v1.8 Fig. 11-1) shows the vias but no size, and 0.3 mm leaves a 0.15 mm ring, the same as the routing vias. Every hole is now within standard fab capability. The board copy of U3 therefore differs from the library on purpose (DRC warning `lib_footprint_mismatch`).
12. **Freerouting 2.1.0, optimizer off, 150 passes, checked by KiCad.** Newer releases need Java 25. 2.1.0 sometimes writes a session file that is missing vias or wires while reporting a complete route: with the optimizer on, one file had 77 of 232 vias; with it off, about one run in three is short. `route.sh` therefore repeats place → route → import until KiCad DRC on the imported board finds 0 unconnected items (up to 5 attempts). Freerouting's settings file (`%TEMP%/freerouting/freerouting.json`) has telemetry on by default; it was switched off on the prep machine.
13. **Pre-strapped pins.** Before export, `build_pcb.py` joins neighbouring same-net SMD pins of one footprint (≤ 0.55 mm, same pad size, same axis) with a short F.Cu track. That gave 14 straps, mostly ADS1299 supply pins. Without them, the router sometimes left one AVDD pair open.
14. **Edge keepout.** 0.6 mm track/via rule areas along the outline, so the router respects the 0.5 mm copper-to-edge rule.

## Result (2026-11-19)
- Schematic: ERC 0 violations; `check_netlist.py` finds 453/453 connected pins on the intended net (after the D4/D5 clamps were added).
- PCB: 86 × 55 mm, 4 layers, 141 parts, 210 vias, 0.2 mm tracks / 0.15 mm clearance, smallest hole 0.3 mm.
- KiCad DRC (errors): **0 violations, 0 unconnected, 0 schematic-parity issues**.
- Warnings left: 36 silk-over-copper, 38 silk overlaps, 2 silk at the USB-C edge, and 1 "ESP32 footprint differs from library copy" (the board copy is the library footprint as loaded; not investigated).
- Renders: `docs/reports/img/neurocore-revA-{top,bottom}.png`. BOM: `hardware/neurocore/fab/neurocore-bom.csv`.
- No gerbers are generated, on purpose: the board is not for ordering until the open issues below are closed.
- Passing DRC means only that the geometry is manufacturable and matches the schematic. It does not show that the circuit is right.

## Open issues (must be resolved before ordering)
1. ~~**Shadow-lane buffer can exceed ADS131M04 abs max.**~~ **Fixed 2026-11-19:** BAW56 clamps (D4, D5) from S1_ADC, S2_ADC and SREF_ADC to GND hold the ADC inputs at ≥ −0.72 V, inside the −1.6 V abs max (ADS131M04 DS §6.1). A railed buffer drives ≈ 0.36 mA through the 4.99 kΩ resistor into the diode (BAW56 VF ≤ 715 mV at 1 mA). Clamp leakage near 0 V isn't specified; it would only show up as a DC offset on the shadow lane.
2. ~~**ADS131M04 DOUT sharing MISO with ADS1299 DOUT.**~~ **Resolved 2026-11-19:** both DOUTs are high-Z while their CS is high (ADS131M04 §8.5.1.1/§8.5.1.4, ADS1299 §9.5.2.1), and both use SPI mode 1. See ADR-0008.
3. ~~**LM27761 divider topology.**~~ **Verified 2026-11-19:** the typical-application figure (DS p. 1) shows R1 VOUT–VFB and R2 VFB–GND, matching the design. Eq. 5 gives −1.22 × 205/100 = −2.50 V.
4. ~~**TLV757P dropout.**~~ **Checked 2026-11-19:** about 145 mV typical at 0.5 A / 25 °C, 170 mV at 85 °C (Fig. 5-14); 425 mV max at 1 A (§5.5). The 3.3 V rail stays regulated through ESP32 current peaks while the cell is ≥ about 3.5 V. **Firmware:** read VBAT_SENSE (IO4) and shut down below a cutoff of 3.5 V or more under load.
5. ~~**USB-C Rd 5.1 kΩ.**~~ **Verified 2026-11-19:** USB Type-C Spec R2.0, Table 4-25 (sink Rd = 5.1 kΩ to GND, ±20 %). The spec is kept locally but not committed (copyright); see `docs/datasheets/NOTES.md`.
6. **JST-PH battery polarity** is not standardised; check the actual pack.
7. **Autorouted (J4 orientation unchecked).** The Qwiic connector's cable opening may not face the board edge. Analog input traces, the bias loop and the ADS1299 decoupling were not hand-routed. The ROADMAP §3.4 layout checklist is not satisfied. In particular, decoupling caps are placed by search, not hugging their pins.
8. **No footprint-to-datasheet land-pattern check** beyond package family and size; KiCad library footprints were used as-is.
9. Lead-off LEDs per channel (ROADMAP §3.2) were dropped to save GPIOs; lead-off status goes to firmware/UI instead.

## Consequences
- Regenerating is cheap: edit `design.py`, rerun the scripts, re-check ERC, netlist and DRC.
- The ROADMAP Phase 3 gate (reviewer sign-off) still applies before ordering.
