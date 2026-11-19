# Datasheet notes for NeuroCore Rev A

Every pin assignment and component value in `hardware/neurocore/gen/design.py` traces to a line below. PDFs were downloaded from the manufacturers on 2026-11-19. Page numbers are the PDF page.

| Part | File | Doc / revision |
|---|---|---|
| ADS1299-4 | `ads1299-sbas499.pdf` | TI SBAS499C (Jan 2017) |
| ADS131M04 | `ads131m04-sbas890.pdf` | TI SBAS890D (May 2021) |
| OPA2333 | `opa2333.pdf` | TI SBOS351E |
| ESP32-S3-WROOM-1 | `esp32-s3-wroom-1.pdf` | Espressif v1.8 |
| BMI270 | `bmi270.pdf` | Bosch BST-BMI270-DS000 |
| TPS7A20 | `tps7a20.pdf` | TI (rev on PDF p. 1) |
| TLV757P | `tlv757p.pdf` | TI SBVS322C (Mar 2024) |
| LM27761 | `lm27761.pdf` | TI SNVSA85D (Oct 2025) |
| BQ21040 | `bq21040.pdf` | TI SLUSCE2D |
| TPD4E02B04 | `tpd4e02b04.pdf` | TI SLVSD85B (Jul 2016) |
| TPD4EUSB30 | `tpd4eusb30.pdf` | TI SLVSAC2G (Jun 2021) |

ST datasheets (LSM6DSO, USBLC6) could not be downloaded (st.com refused automated requests), so those parts were replaced by BMI270 and TPD4EUSB30.

## ADS1299-4 (U1), TQFP-64 PAG 10 × 10 mm (p. 5)
- Pin numbers: §6 Pin Functions, p. 6–7.
- Unused analog inputs (IN5–IN8 on the -4): connect directly to AVDD (p. 6 note 2; §10.1.1 p. 61).
- Two-state pins (CLKSEL) via ≥ 10 kΩ; unused GPIO1–4 to DGND via ≥ 10 kΩ (p. 6).
- RESV1 → DGND (p. 6); NC 27/29 and RESERVED 64 open (p. 6); DAISY_IN → DGND, don't float digital inputs, SRB2/BIASIN/BIASREF may float (§10.1.1 p. 61).
- Decoupling: each AVDD 1 µF to AVSS; AVDD1 1 µF to AVSS1; DVDD 1 µF; VCAP1 100 µF, VCAP2 1 µF, VCAP3 1 µF ∥ 0.1 µF, VCAP4 1 µF to AVSS; VREFP ≥ 10 µF to VREFN (p. 6–7). Bulk 10 µF + 0.1 µF per supply; star AVDD1 to AVDD (§11 p. 70).
- Supplies: AVDD–AVSS 4.75–5.25 V; DVDD 1.8–3.6 V; AVDD–DVDD −2.1…3.6 V (§7.3 p. 8). AVSS to DGND abs max −3…0.2 V (§7.1 p. 7). Bipolar ±2.5 V + 3.3 V is a documented configuration (§11.3 p. 71). VREFN = AVSS (§7.3).
- Power-up: inputs low until supplies settle; reset after tPOR (§11.1 p. 70) → pull-downs on START/RESET/PWDN.
- Bias drive: BIASOUT–BIASINV 1 MΩ ∥ 1.5 nF (figures p. 28–29, p. 63).
- Layout: separate analog/digital, inputs with C0G differential caps, bypass caps without vias (§12.1 p. 72).

## ADS131M04 (U2), TSSOP-20 PW
- Pins: Table 5-1, p. 4. AVDD 1 µF to AGND; CAP 220 nF to DGND; DVDD 1 µF.
- AVDD 2.7–3.6 V, DVDD 2.7–3.6 V (§6.3 p. 6).
- Absolute input range: AGND − 1.3 V to AVDD (gain 1–4) or AVDD − 1.8 V (gain ≥ 8) (§6.3 p. 6). Abs max AGND − 1.6 V (§6.1 p. 5).
- CLKIN 0.3–8.4 MHz in high-resolution mode, 40–60 % duty (§6.3).

## OPA2333 (U5, U6), SOIC-8
- Pins p. 4; supply abs max 7 V; inputs to rails ± 0.3 V (§6.1 p. 5).

## ESP32-S3-WROOM-1-N16R8 (U3)
- Pin table: Table 3-1, p. 11–12. IO35–37 used by octal PSRAM (note b). Strapping pins IO0, IO3, IO45, IO46 (§4).
- VDD33 3.0–3.6 V, supply able to deliver ≥ 0.5 A (Table 6-2).
- EN RC 10 kΩ / 1 µF (§9, p. 41).

## BMI270 (U4), LGA-14 2.5 × 3.0 mm
- Pin-out and I2C connections: Table 22, p. 135 (CSB → VDDIO, SDO → GND, unused aux/OIS/INT2 not connected).
- 100 nF on VDD and VDDIO (§7.2 p. 136). VDD 1.71–3.6 V, VDDIO 1.2–3.6 V.

## Power
- TPS7A20 (U13 2.5 V, U14 3.3 V), SOT-23-5 DBV: IN 1, GND 2, EN 3, NC 4, OUT 5 (p. 4); CIN 1 µF, COUT ≥ 1 µF (ROC); 7 µVrms, 300 mA.
- TLV757P (U9, 3.3 V), DBV: IN 1, GND 2, EN 3, NC 4, OUT 5 (Table 4-1); ≥ 1 µF in/out.
- LM27761 (U7, −2.5 V), WSON-8 DSG 2 × 2 mm: pins Table 4-1 p. 3; VOUT = −1.22 V × (R1 + R2)/R2, R2 ≥ 50 kΩ (§7.2.2.4); C1 1 µF flying, C2 4.7 µF in, C3 4.7 µF CPOUT, C4 2.2 µF out (typical application p. 1, §7.2.2.5).
- BQ21040 (U8), SOT-23-6: pins p. 3; I_charge = KISET / RISET, KISET 540 AΩ typ (§7); TS → 10 kΩ to VSS if no NTC (pin table); VIN/OUT caps 1–10 µF.

## Protection
- TPD4E02B04 (U10–U12), USON-10 DQA: bidirectional, VRWM ±3.6 V, leakage ≤ 10 nA at ±2.5 V; pins IO1 1, IO2 2, GND 3/8, IO3 4, IO4 5, NC 6/7/9/10.
- TPD4EUSB30 (U15), DQA: D1+ 1, D1− 2, GND 3/8, D2+ 4, D2− 5 (Table 5-1 p. 3); IO 0–6 V.
- USB-C sink: Rd 5.1 kΩ on CC1/CC2 is from the USB Type-C specification, not a datasheet in this folder. Verify against the spec before ordering.
