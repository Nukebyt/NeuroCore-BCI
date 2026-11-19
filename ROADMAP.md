# NeuroDrive — Hybrid EEG + EMG Control Platform

**Roadmap, research notes and build guide for Claude Code**
Owner: Nuke (Priyanuj Boruah) · Started: Nov 2026 · Status: Phase 0

> **One-line summary:** Build a reusable biosignal "intent engine" called **NeuroCore** (my own PCB, firmware, models and RTL) that turns brain and muscle signals into a small set of safe, standard *intents*. The first device it controls is the toy RC truck. The same intents then drive a PC game (BLE gamepad), smart plugs and a drone, without changing NeuroCore.

---

## 0. Read this first: what exists and what doesn't

**The basic idea is already demonstrated.** In Sep 2025 Upside Down Labs (Delhi) published a brain-controlled robot car on their Neuro PlayGround Lite board:
- EEG beta-power "focus" drives the car forward.
- Left and right arm EMG steer.
- Flexing both arms reverses.
- Relaxing stops the car.

They list **speed modulation, obstacle avoidance, multi-user and adaptive ML** as *future work*. Copying their demo is a good weekend build, not a novel project. This roadmap aims at the gaps instead:

| Gap (open end) | How NeuroDrive goes after it |
|---|---|
| Speed control by brain is unreliable (focus is slow and confounded) | **SSVEP-based speed** (look at a flickering LED), with "focus" kept as an ML experiment |
| DIY boards are either cheap but noisy (12-bit ADC) or good but expensive (ADS1299) | A **side-by-side cost/noise study** on one PCB: ADS1299-4 (~$30) vs ADS131M04 (~$3) |
| Arm clenches leak muscle noise into EEG | **Artifact-reference channel + IMU**, with on-device gating or regression |
| Each demo is hard-wired to one gadget | A **device-agnostic intent protocol (NIP)** with thin adapters |
| Neuromorphic (SNN) EEG/EMG decoding is mostly done in simulation or on lab chips (Loihi) | **Delta-encoded spikes plus a LIF SNN on my DE10-Standard**, reusing my event-camera SNN core, then a path to silicon |
| Pure BCI cars crash | **Shared autonomy**: a ToF obstacle sensor blended with the BCI intent |

See **§9 Open Ends** for the full research list and how to test each one.

---

## 1. Goals, non-goals, success criteria

### Goals
1. **NeuroCore Rev A PCB.** Battery-only, 4× EEG + 2× EMG (+2 experimental) channels, IMU, ESP32-S3, an FPGA expansion header and documented noise performance.
2. **Toy RC truck under hybrid control:**
   - Steering by EMG (left and right arm).
   - Speed by SSVEP (look at the "+" or "−" LED; looking away holds the current speed).
   - Stop by clenching both arms, plus a link-loss failsafe.
3. **A model zoo** benchmarked on public data and my own recordings. Best models deployed on the ESP32-S3; an SNN deployed on the FPGA.
4. **Reuse.** The same NeuroCore drives a BLE gamepad game, smart plugs and a nano drone through NIP adapters.
5. **Write-up.** Open-source repo, characterization report and, if results hold, a short paper or preprint.

### Non-goals
- Medical use, diagnosis or any claim of clinical accuracy.
- Mains-connected acquisition of any kind.
- "Reading thoughts". Commands come only from SSVEP, EMG, EOG/blinks and (experimentally) band-power states.

### Success criteria (targets I set, to be measured)

| Metric | Target |
|---|---|
| EEG lane noise, inputs shorted, 0.5–40 Hz | ≤ 1 µV RMS |
| Eyes-closed alpha (8–12 Hz) visible at O1/O2 or behind the ear | Clear peak in ≥ 3 of 3 sessions |
| EMG clench detection | ≥ 98 % hits, ≤ 1 false trigger/min at rest |
| SSVEP 3-class (+ / − / none), 2 s window | ≥ 90 % accuracy, idle-state false positives ≤ 1/min |
| EMG to wheel latency | ≤ 300 ms |
| Link-loss to motor stop | ≤ 300 ms (tested 50× without a miss) |
| Track test (taped course) | Finish with ≤ 2 collisions; record time vs keyboard baseline |

---

## 2. System architecture

```
 ELECTRODES                     NEUROCORE (head/neck unit, battery only)                          DEVICES
 ───────────                    ──────────────────────────────────────────                        ───────
 O1/O2 or behind-ear ─┐        ┌──────────────┐   SPI   ┌───────────────────────────────┐   NIP   ┌───────────────┐
 Fp1 (focus/blink)  ──┼──────► │ ADS1299-4    │ ──────► │ ESP32-S3                      │ ──────► │ Car adapter   │
 Spare (artifact ref)─┘        │ (EEG, 250SPS)│         │  • filters, features          │ ESP-NOW │ ESP32-C3 +    │
                               └──────────────┘         │  • classical + tiny DL models │         │ TB6612 + ToF  │
 L forearm EMG ─┐              ┌──────────────┐   SPI   │  • intent FSM + safety        │         └───────────────┘
 R forearm EMG ─┼─ buffers ──► │ ADS131M04    │ ──────► │  • NIP sender                 │ ──BLE──► PC / phone game
 Shadow EEG ×2 ─┘              │ (EMG ~1-2kSPS│         │  • raw stream (LSL/USB/Wi-Fi) │ ──MQTT─► smart plugs
                               └──────────────┘         └──────────┬───────────┬────────┘ ──MSP──► drone FC
 IMU (head motion) ───────────────────────── I2C ─────────────────┘           │
                                                                    SPI header │
                                                                    ┌──────────▼─────────┐
                                                                    │ DE10-Standard FPGA │
                                                                    │ delta encoder + SNN│
                                                                    └────────────────────┘
 Laptop (Fedora): recording, training, dashboards ◄── LSL / BrainFlow ── NeuroCore raw stream
```

### Design principles
1. **Decode on the headset, send only intents.** Devices never see raw biosignals. They receive `{intent, level, confidence, flags}` at 20–50 Hz. That separation is what makes the system reusable.
2. **Three independent safety layers.**
   - Intent FSM: both-arm STOP and artifact freeze.
   - NIP link watchdog in every adapter.
   - A physical kill switch on every device.
3. **Separate ADCs for separate jobs.**
   - EEG needs µV noise at 250 SPS.
   - EMG needs ≥ 1 kSPS at mV levels.
   - Two chips avoid compromising either signal, and the second chip doubles as the cost/noise experiment.
4. **Data-first.** Every session is recorded with metadata, so models can be retrained and results reproduced.
5. **Battery-only acquisition.** No exceptions. OpenBCI's own spec for the Cyton says to power it from a 3–6 V battery only, and PiEEG's paper says everything must run from batteries.

---

## 3. Hardware track: NeuroCore Rev A

### 3.1 Channel plan and montage

| Ch | ADC | Signal | Default electrode site | Why |
|---|---|---|---|---|
| E1 | ADS1299-4 CH1 | EEG | O1 **or** behind left ear | SSVEP. Behind-ear sites avoid hair (see §9) |
| E2 | ADS1299-4 CH2 | EEG | O2 / Oz **or** behind right ear | SSVEP, alpha check |
| E3 | ADS1299-4 CH3 | EEG/EOG | Fp1 (forehead) | Focus band power, blink and eye-movement detection |
| E4 | ADS1299-4 CH4 | Artifact ref **or** FCz | Masseter/jaw or neck **or** FCz | Clench artifact regression, or ErrP experiments |
| M1 | ADS131M04 CH0 | EMG | Left forearm flexors (bipolar pair) | Steer left |
| M2 | ADS131M04 CH1 | EMG | Right forearm flexors (bipolar pair) | Steer right |
| S1 | ADS131M04 CH2 | "Shadow" EEG | Same electrode as E1 (buffered tap) | Cost/noise A/B study |
| S2 | ADS131M04 CH3 | "Shadow" EEG | Same electrode as E2 (buffered tap) | Cost/noise A/B study |
| REF | ADS1299 SRB1 | Reference | Left earlobe or mastoid | Common reference |
| BIAS | ADS1299 BIASOUT | Driven right leg | Right mastoid | Common-mode rejection |

> The **S1/S2 shadow lane** is optional (DNP = do-not-populate). If it misbehaves, depopulate it and the board still works as a 4-EEG + 2-EMG device.

### 3.2 Part choices

Prices were checked in Nov 2026. Re-check before ordering.

| Block | Part (candidate) | Reason | Price seen |
|---|---|---|---|
| EEG ADC | **TI ADS1299-4PAGR** (TQFP-64) | Purpose-built 4-ch, 24-bit biopotential ADC with PGA, lead-off detect, bias drive; daisy-chainable | ₹3,093 qty 1 at Lion Circuits (Bengaluru); ~$30–38 at LCSC |
| EMG / experimental ADC | **TI ADS131M04IPWR** (TSSOP-20) | 4-ch simultaneous 24-bit ΔΣ, up to 64 kSPS, PGA up to 128; **~10× cheaper** than ADS1299-4 | ~$3.2 at LCSC, ~$2.9 at JLCPCB |
| Fallback 2-ch AFE | TI ADS1292R | 2-ch 24-bit ECG AFE, if the ADS131M04 lane fails | ~$6.4 at LCSC; ₹1,749 at Lion Circuits |
| Input buffers (ADS131M04 lane) | OPA2333 (zero-drift) **or** OPA2376 (low noise, pA bias) | ADS131M04 input impedance is only ≥ 1 MΩ at gain ≥ 8, so buffer the electrodes | est., verify |
| MCU | **ESP32-S3-WROOM-1-N16R8** | Dual core, PSRAM for models, BLE + Wi-Fi + ESP-NOW, ESP-DL support | est., verify |
| IMU | BMI270 or LSM6DSO | Head-motion artifact reference; head tilt as an extra input | est., verify |
| Power | 1S Li-ion/LiPo → charger IC → separate **low-noise analog LDO(s)** + digital LDO | ADS1299 needs AVDD−AVSS = 5 V (unipolar 5 V or ±2.5 V; pick one in review). ADS131M04 runs AVDD 2.7–3.6 V | est. |
| Protection | 10–22 kΩ series resistor per electrode + low-leakage ESD diodes | Limits patient current, protects the ADCs | cheap |
| Connectors | 2.54 mm or JST-PH electrode headers; USB-C (charge/flash); 2×8 FPGA/SPI header; Qwiic | Snap-cable leads plug into the headers | cheap |
| Indicators | Lead-off LED per channel; status RGB; **"USB connected — check isolation" LED** | Safety and usability | cheap |

### 3.3 Reference designs to study (and their licences)

I've done IP coursework, so licensing is part of the design.

| Project | What to learn | Licence | Can I copy layout/circuits into my board? |
|---|---|---|---|
| **EEGFrontier V1**: ADS1299-4 + XIAO RP2040, KiCad | Closest match to Rev A: 4-ch ADS1299 layout and firmware | MIT | Yes, with attribution |
| **dcmini-hw** (KiCad) | Compact biosignal hardware | CERN-OHL-P | Yes (permissive) |
| **Adam-EEG** (India, quad ADS1299, EAGLE) | Bias loop, lead-off detection, references | Apache-2.0 | Yes, with notice |
| **Cerelog ESP-EEG**: ESP32 + ADS1299 | ESP32 + ADS1299 integration, active bias | Firmware MIT; schematic CC-BY-NC-SA | Learn only. **Non-commercial**; don't copy if I may sell |
| **PiEEG** (ADS1299 Raspberry Pi shield) | Software stack, battery-only design | GPL-3.0 | Copying makes my work GPL |
| **BioAmp EXG Pill** / Upside Down Labs | Discrete INA + op-amp EMG/EEG front ends | CERN-OHL (hardware) + MIT (software) | Check the exact CERN-OHL variant in the repo. The sibling EMG Pill uses CERN-OHL-**S** (strongly reciprocal), meaning derivatives must be shared alike |
| **OpenBCI Cyton** | Gold-standard 4-layer layout; Gerbers and BOM published | See OpenBCI repo | Check before copying |
| **TI ADS1299 datasheet + EVM** | The authority on bias, reference and layout | n/a | Yes (reference design) |

**My licence choice:** hardware **CERN-OHL-W** (or -P), firmware and software **Apache-2.0**. Don't import reciprocal or NC material into the main board, so the licence choice stays open.

### 3.4 Layout rules (checklist for review)
- [ ] **4-layer stack:** Sig / GND / PWR / Sig. OpenBCI's Cyton is a 4-layer board; follow that precedent.
- [ ] Analog and digital split: one ground plane, partitioned placement, **single tie point** near the ADCs. No digital traces under the analog front end.
- [ ] Electrode traces short, **differential pairs routed symmetrically**, guard ring driven by the bias or shield where useful.
- [ ] ESP32-S3 antenna at the board edge, far from the AFE. Keep-out per the module datasheet.
- [ ] ADC clock lines short; series termination on SPI.
- [ ] Local decoupling exactly as the datasheet specifies (values and placement), cited in schematic notes.
- [ ] Test points on every rail, on CLK/DRDY/SPI, on BIASOUT, and a shorted-input jumper per channel.
- [ ] Fiducials, and part orientation marks for JLC/Lion assembly.
- [ ] ERC/DRC clean in KiCad (`kicad-cli sch erc`, `kicad-cli pcb drc`) and **peer review** by a senior or faculty member before ordering.

### 3.5 Fabrication and assembly in India
- **Option A: Indian fab + assembly** (Lion Circuits Bengaluru, PCB Power, etc.). Faster, no import hassle. Lion Circuits also stocks the ADS1299-4.
- **Option B: JLCPCB/PCBWay.** Often cheapest for 4-layer boards plus assembly. Budget for shipping and customs, and stock-check the parts (ADS1299-4 and ADS131M04 are listed at LCSC/JLC).
- Order **5 bare boards** and assemble **2**. Hand-solder the TQFP-64 only if confident; otherwise use the assembly service.

### 3.6 Bring-up and characterization protocol (Phase 4)
1. Power rails only, ADCs not populated or held in reset: check voltages and ripple.
2. SPI ID registers read correctly (ADS1299 ID register; ADS131M04 ID).
3. **Internal test signal** (ADS1299 has an internal square-wave test source): verify gain and scaling.
4. **Shorted-input noise:** 60 s per channel at 250 SPS. Report µV RMS and µV p-p for 0.5–40 Hz. Repeat on the shadow lane.
5. **CMRR:** apply a 50 Hz common-mode signal from a battery-powered function generator through a resistor network, measure rejection. Repeat with a 10 kΩ imbalance on one input.
6. **Lead-off detection** works per channel.
7. **Human tests:**
   - Eyes-closed alpha (60 s eyes open vs 60 s closed).
   - Blinks on Fp1.
   - EMG clench on M1/M2.
   - SSVEP at 3 frequencies.
8. Write `docs/reports/neurocore-revA-characterization.md` with plots. **This report is itself a deliverable.**

### 3.7 Rev B ideas (after data)
- Pick one ADC family based on the A/B result. If the ADS131M04 is good enough, a ~₹1,000-class 4-ch board becomes possible.
- Active electrodes (buffer at the electrode) for dry contacts.
- Behind-ear flex-PCB electrode strip.
- Fold the car's SSVEP LED driver onto a small "stimulus" daughterboard.

---

## 4. Car conversion: the toy RC truck (prototype)

### 4.1 What's in the photo (verify on the bench)
- One cylindrical **Li-ion pack** marked IS 16046 (Indian Li-ion standard). **Measure its voltage**:
  - ~3.7–4.2 V means 1S.
  - ~7.4–8.4 V means 2S.
- Stock **receiver board** (green PCB, small decoder IC). It will be **removed**.
- **Two motors:** a drive motor in the gearbox and a steering motor (likely a 2-wire DC motor with spring centring). Identify each by briefly applying 3 V and watching what moves.

### 4.2 Conversion steps
1. Photograph and label every wire. Desolder the receiver board, keep the battery connector.
2. Fit an **ESP32-C3 SuperMini (or any ESP32)** as the car adapter.
3. Choose the motor driver:
   - **Don't use the L298N here.** It is a BJT bridge that drops roughly 1.4–3 V, which is fatal on a 1S pack and wasteful on 2S.
   - Use a **TB6612FNG** (~0.5 V drop, about ₹135) or a **DRV8833** (2.7–10.8 V, about ₹72) instead.
   - Channel A drives the motor; channel B drives steering, using bang-bang PWM with a current limit.
4. Power the logic:
   - 2S pack: buck converter to 3.3 V.
   - 1S pack: low-dropout 3.3 V regulator.
   - Add a bulk capacitor near the driver.
5. Fit a **physical kill switch** in series with the motor supply.
6. Optional: a **VL53L0X/VL53L1X ToF** sensor at the front for shared autonomy (§9 O5).
7. Optional: two **SSVEP LEDs** on the car (green/red) for the "look at the car" experiment (§9 O3).
8. Adapter firmware must have:
   - An ESP-NOW NIP receiver.
   - A **300 ms watchdog** that triggers STOP.
   - A speed cap.
   - A soft-start ramp.
   - A "wheels-off-ground" bench mode.

### 4.3 The 2WD acrylic chassis = bench rig
- Keep the **L298N here** if this chassis runs on 2× 18650 (7.4 V). The voltage drop is tolerable on a desk rig.
- It has encoder-disc slots. Add IR slot sensors for wheel speed, so latency and speed control can be measured objectively.
- Use it for indoor algorithm testing. The truck is for demos.

---

## 5. Signal pipeline and control v1 (rule-based baseline)

Build this before any ML. It sets the baseline every model must beat.

### 5.1 Preprocessing (all channels)
- 50 Hz notch plus harmonics (Indian mains).
- EEG band-pass 1–40 Hz. EMG band-pass 20–450 Hz with the lane at ≥ 1 kSPS.
- Running impedance and lead-off check. Flag a channel `BAD` instead of feeding garbage forward.

### 5.2 EMG steering
- Rectify, then take the RMS envelope over 100–150 ms windows with 25 ms hops.
- Hysteresis threshold: ON at `rest_p95 + 0.5·(clench_p50 − rest_p95)`, OFF at 60 % of the ON level. Debounce 80 ms.
- Classic features for later ML: MAV, RMS, WL, ZC, SSC.

### 5.3 SSVEP speed
- Stimuli:
  - Option A (most reliable): an LED box on the desk at 50–70 cm.
  - Option B (experiment): LEDs on the car.
  - Frequencies: start at **8.57, 10, 12 Hz** (and 15 Hz) and avoid harmonics of each other. Drive the LEDs with ESP32 LEDC hardware PWM for exact timing.
- Decoder ladder:
  1. PSD peak.
  2. **CCA / FBCCA** (training-free).
  3. **TRCA / eTRCA** (needs a few training trials).
  4. Online-adaptive CCA.
- Windows: 1–2 s, sliding by 250 ms. Require **2 consecutive agreeing windows** before acting.
- **Idle-state detection:** "not looking at any LED" must map to `HOLD`. Threshold the max correlation against a per-user baseline.

### 5.4 Focus (experimental)
- Features: theta, alpha and beta band powers and their ratios on Fp1 (and O1/O2).
- Freeze this feature whenever EMG is ON, the IMU shows motion, or the E4 artifact channel is high.

### 5.5 Intent FSM (runs on NeuroCore)
```
States: IDLE → ARMED → DRIVE ⇄ STEER_L/STEER_R → STOPPED (latched)
Rules:
  both arms ≥ 300 ms              → STOPPED (latched; clear with 2 s relax + SSVEP "−")
  double both-arm clench           → toggle REVERSE gear (only when speed = 0)
  L xor R                          → steer while held
  SSVEP "+" / "−" (2 windows)      → speed level ±1 (0..3); none → hold
  artifact flag (EMG/IMU/E4)       → freeze speed changes
  any channel BAD                  → speed capped at level 1
  NeuroCore watchdog / brownout    → emit STOP, stop emitting
```

### 5.6 Calibration routine (about 90 s at startup)
1. 20 s rest with eyes open.
2. 3 × 2 s clench per arm.
3. 3 × 4 s per SSVEP LED.
4. Optional: 2 × 20 s focus vs relax.

Store the thresholds per user and session; log everything.

---

## 6. AI/ML track

### 6.1 Model zoo to test

| Family | Model | Task | Runs where | Library |
|---|---|---|---|---|
| Classical | Band power + LDA / logistic regression | Focus, EMG | ESP32-S3 (trivial) | scikit-learn |
| Classical | EMG time-domain features (MAV/RMS/WL/ZC/SSC) + LDA | EMG gestures | ESP32-S3 | scikit-learn |
| Classical | CCA, **FBCCA**, **TRCA/eTRCA**, OACCA | SSVEP | ESP32-S3 (CCA/FBCCA); PC first | SSVEPAnalysisToolbox, MetaBCI (brainda) |
| Riemannian | MDM, tangent space + LR | Focus, motor imagery | PC; small ones on MCU | pyRiemann |
| Compact CNN | **EEGNet** | All EEG paradigms | ESP32-S3 via ESP-DL (int8) | braindecode / own PyTorch |
| CNN | ShallowFBCSPNet, Deep4Net | MI / focus baselines | PC | braindecode |
| SSVEP CNN | Compact-CNN (Waytowich et al.) | Asynchronous SSVEP | PC, then MCU | own PyTorch |
| Attention | EEG-Conformer, ATCNet | Upper-bound baselines | PC (RTX 3050 Ti) | braindecode / selfeeg |
| Foundation | **CBraMod** (ICLR 2025, code public), LaBraM, EEGPT | Fine-tune, then **distil** into EEGNet | PC only | official repos |
| SNN | LIF / adaptive-LIF networks, resonate-and-fire for SSVEP | EMG + SSVEP | DE10 FPGA (RTL), PC sim | snnTorch, SpikingJelly, Norse; Lava (Loihi), Rockpool (Xylo) for reference |

**Caution about foundation models.** Recent work, including an ICML 2025 study and EEG-FM benchmarks, questions how much large EEG foundation models beat compact baselines. Testing that on 4-channel DIY data is itself an open question (O8).

### 6.2 Datasets
- **SSVEP:**
  - Tsinghua *Benchmark* (40 targets, 64 ch, 35 subjects).
  - **BETA** (70 subjects).
  - Both are loadable via SSVEPAnalysisToolbox. Sub-select O1/O2/Oz-equivalent channels to mimic my montage.
- **EMG:** **Ninapro DB2** (40 subjects, 12 electrodes, 2 kHz) and **DB5** (double Myo, 16 ch). Use them for pre-training the EMG classifiers and SNN encoders.
- **Motor imagery** (stretch goal): MOABB / MetaBCI loaders (e.g., BNCI2014001).
- **My own data:** the `data/` schema in §11.4. Aim for ≥ 10 sessions across ≥ 5 days to measure session drift.

### 6.3 Evaluation protocol (applies to every model)
- Splits:
  - Within-session (k-fold over trials).
  - **Cross-session** (train on days 1–4, test on day 5). This is the one that matters.
  - Cross-subject, if friends volunteer.
- Metrics:
  - Accuracy, ITR (bits/min).
  - **False activations per minute at rest.**
  - Decision latency.
  - Model size, MCU inference time and energy (measure with an INA219 on the supply).
- **Artifact sanity tests**, required before any result is believed:
  1. Rerun with a relaxed jaw and fixed gaze. Does performance hold?
  2. Train with shuffled labels. Accuracy should fall to chance.
  3. Electrodes off (resistor dummy). The model should output "none".
  4. Check what the model relies on: saliency over channels and frequencies.

### 6.4 On-device deployment
- **ESP32-S3 + ESP-DL.** Export PyTorch or ONNX, quantize with **ESP-PPQ** (post-training int8/int16), and load the `.espdl` model. ESP-DL needs **ESP-IDF ≥ 5.3**.
- Keep FBCCA/CCA in plain C using ESP-DSP or hand-written matrix code. It's cheap and needs no training.
- Budget: decision every 250 ms; inference ≤ 20 ms; total RAM for models ≤ 2 MB of PSRAM.

---

## 7. SNN, FPGA and chip track

I already have an event-camera + SNN design on the DE10-Standard (`github.com/Nukebyt/FPGA-SNN-Event-Camera`). Reuse its LIF core.

### 7.1 Pipeline
```
NeuroCore (SPI master) ──► DE10 GPIO SPI slave ──► delta/sigma-delta spike encoder (per channel)
   ──► LIF / adaptive-LIF layer(s) (int weights in BRAM) ──► intent readout (rate/first-spike)
   ──► SPI back to NeuroCore (NIP intent + confidence)
```

### 7.2 Steps
1. **Software first.** Train with snnTorch (surrogate gradients):
   - EMG: delta-encode the envelope into UP/DOWN spikes, then L/R/both/none.
   - SSVEP: filter bank, delta encoding, then the class. Also try **resonate-and-fire neurons** tuned to the stimulus frequencies (O4).
2. **Quantize** weights to int8 and then int4; check the accuracy loss.
3. **RTL:**
   - SystemVerilog modules: `spi_slave`, `delta_encoder`, `lif_layer`, `readout`, `intent_watchdog`.
   - **cocotb** tests replay recorded data and must match the Python model bit-exactly.
   - **Verilator** for speed.
   - **SymbiYosys** formal proof that *"no valid input for N cycles ⇒ output = STOP"*.
4. **Deploy on the DE10.** Measure latency (cycles), resource use, and board power delta.
5. **Compare** against EEGNet-int8 on the ESP32-S3: accuracy, latency, energy per decision.

**hls4ml note:** its Intel/Altera flow now goes through the oneAPI backend (the Quartus backend is deprecated), and Intel's oneAPI FPGA add-on lists Agilex, Stratix 10, Arria 10 and Cyclone 10 GX. Cyclone V isn't among them. Plan on **hand-written RTL** for the DE10 and don't count on hls4ml.

### 7.3 Path to silicon (the reusable chip)
**"NeuroCore Intent Engine" IP** = SPI in + delta encoder + small SNN + intent FSM + formally verified watchdog. Digital only, so it fits student shuttles.

| Route | Facts (Nov 2026) | Fit |
|---|---|---|
| **Tiny Tapeout** | Runs on IHP SG13G2 (130 nm) plus SKY/GF shuttles. TTIHP25b pricing was about €50/tile. **IHP chips are loaned and shipped only to the EU/Switzerland**, so check which current shuttle ships to India before committing. The site shows open SKY and GF shuttles | Tiny digital SNN + watchdog tile |
| **C2S Programme (MeitY) via SCL Mohali** | 180 nm MPW shuttles roughly every three months through the ChipIN Centre; 254 tape-outs so far (175 at SCL). **Check whether VIT is a participating institution** | Bigger digital block, Indian fab |
| **IHP SG13G2 open PDK (long term)** | BiCMOS (has BJTs, good for low-noise analog); open AMS chip template exists | Mixed-signal AFE + SNN: a PhD-scale open end |

---

## 8. Reuse: the NeuroCore Intent Protocol (NIP) and adapters

### 8.1 NIP v0 frame (little-endian, 16 bytes)
| Field | Type | Notes |
|---|---|---|
| magic | u16 | `0x4E50` ("NP") |
| version | u8 | 0 |
| seq | u8 | wraps |
| t_ms | u32 | NeuroCore uptime |
| intent | u8 | 0 STOP, 1 HOLD, 2 FWD, 3 REV, 4 LEFT, 5 RIGHT, 6 MODE_NEXT, 7 AUX_A, 8 AUX_B |
| level | u8 | 0–3 speed or analog level |
| confidence | u8 | 0–255 |
| flags | u8 | bit0 calibrated, bit1 artifact, bit2 channel_bad, bit3 latched_stop, bit4 test_mode |
| reserved | u16 | 0 |
| crc16 | u16 | CRC-16/CCITT-FALSE over bytes 0–13 |

- Rate: **25 Hz heartbeat**, even when the intent is unchanged.
- Adapters must STOP or fail safe after **300 ms** without a valid frame (drone: hover or land).
- Transports: ESP-NOW (car, drone bridge), BLE (gamepad), Wi-Fi/MQTT (home), UART/SPI (FPGA).
- Ship `protocol/nip/` with a C header, a Python codec and **golden test vectors**.

### 8.2 Adapters (in order)
1. **Car** (§4): ESP-NOW receiver, motor control, ToF shared autonomy.
2. **BLE gamepad.** Use the **ESP32-BLE-Gamepad** library (NimBLE; ESP32/C3/S3; configurable buttons and axes). It pairs with Windows and Android as a normal controller, so any game works: intents map to axes and buttons. Good first test game: a racing game with throttle on SSVEP and steering on EMG.
3. **Appliances.** Prefer certified smart plugs (Tasmota/ESPHome or a Home Assistant integration) over DIY 230 V relays. NIP goes to MQTT, then to the plug. SSVEP is the selector ("look at the lamp LED to toggle").
4. **Drone (last)** — options in order:
   - **LiteWing** (CircuitDigest, India): ESP32-S3, open hardware, Betaflight support.
   - **ESP-Drone** (Espressif): ESP32/S2/S3, Crazyflie-derived firmware, height-hold and position-hold, limited support since Dec 2022.
   - A Betaflight/INAV whoop. Betaflight accepts `MSP_SET_RAW_RC` from a companion computer. **MSP as the sole receiver is bench-only.** For flight, use MSP override with ARM and the kill switch kept on a real RC transmitter. INAV needs `MSP_SET_RAW_RC` at ≥ 5 Hz.
   - **Rule:** NIP sends only high-level commands (take off, land, hover, yaw, creep forward). The autopilot flies. Props off until the full failsafe test passes. Stay in the nano (<250 g) class and check the current Indian drone rules before outdoor flights.

---

## 9. Open ends: where NeuroDrive can do something new

> A quick search for these found no direct prior work, which is **not** a literature review. Before claiming novelty, do a proper search (Google Scholar, IEEE Xplore, arXiv) and record it in `docs/litreview.md`.

| ID | Question | Why it's open | How to test | A "new" result would be |
|---|---|---|---|---|
| **O1** | Is a ~$3 **ADS131M04 + buffer** lane good enough for SSVEP and EMG compared with a ~$30 **ADS1299-4**? | Hobby boards use 12-bit MCU ADCs; research boards use ADS1299; the middle is unmeasured | Same electrodes, same time (shadow lane). Compare noise, CMRR and SSVEP accuracy | A cost/noise curve showing a ~10× cheaper AFE is sufficient (or exactly where it fails) |
| **O2** | Can arm-clench artifacts in EEG be removed on-device using an **artifact reference (E4 jaw/neck) + IMU**? | Hybrid EEG+EMG work fuses the signals for intent; less is known about using a spare channel to clean EEG in real time on an MCU | Record clench vs no-clench; regression or adaptive-filter cleanup; measure SSVEP/focus accuracy during clenches | Hybrid control that stays accurate *while* steering |
| **O3** | **Behind-ear, hair-free SSVEP** with stimuli **mounted on the vehicle** | Non-hair SSVEP works in the lab (12 targets, ~84 % online); colour (green-red / green-blue) stimuli strengthen behind-ear SSVEP. Moving, vehicle-mounted stimuli are less studied | Desk LEDs vs car LEDs at 0.5, 1 and 2 m; accuracy vs distance and stimulus size | "Look at the car to control the car": no screen needed |
| **O4** | **Resonate-and-fire SNN** as an SSVEP detector in RTL | SNN-for-EEG work mostly targets motor imagery or P300; resonators match SSVEP physics naturally | snnTorch model, then RTL, then DE10; compare with FBCCA on accuracy and energy | Frequency detection in a few hundred LUTs, with energy numbers |
| **O5** | **Shared autonomy** (ToF + BCI intent blending) on a ₹-budget car | Brain-controlled vehicle research notes limited accuracy and command count; blending is studied on wheelchairs, rarely on cheap platforms | Same course, BCI-only vs blended; collisions, time, false stops | Quantified safety gain from a ₹300 sensor |
| **O6** | **ErrP auto-undo:** detect "that was wrong" after a misclassified command | Needs an FCz-like channel; few low-channel DIY attempts | E4 at FCz; inject random wrong turns; classify the error response | Self-correcting BCI on 4 channels |
| **O7** | **One decoder, many devices:** does calibration on the car transfer to the game and the drone? | Most demos are single-device | Calibrate once, use on car, game and plugs; measure the drop | Evidence that an "intent layer" generalizes across devices |
| **O8** | Do **EEG foundation models** (CBraMod/LaBraM) help on 4-channel DIY data after **distillation** to EEGNet? | Benchmarks question foundation-model gains even on lab data | Fine-tune on my sessions, distil, compare with EEGNet trained from scratch | An honest answer for low-channel hardware |
| **O9** | **Online co-adaptation** (OACCA-style for SSVEP, incremental LDA for EMG) to beat day-to-day drift | Cross-session drift is the real-world killer | Days 1–5 with vs without adaptation | Fewer recalibrations per week |
| **O10** | **Open dataset:** low-cost hybrid EEG+EMG control data, with hardware specs | Few public datasets come from low-cost hybrid hardware | Release `data/` with consent forms and specs | A citable dataset |

Indian context worth knowing:
- Upside Down Labs (Delhi) builds open biopotential hardware.
- IIT Delhi (Manan Suri's group) published low-power neuromorphic EMG gesture classification on Intel's Loihi.
- These are useful people and labs to read, cite and maybe contact.

---

## 10. Phased plan

Durations assume part-time work. Each phase ends with a **human gate**: what I personally verify before the next phase starts.

| Phase | Name | Duration | Depends on |
|---|---|---|---|
| 0 | Repo, toolchain, safety doc | 2–3 days | — |
| 1 | Car adapter + NIP over keyboard | 1 week | 0 |
| 2 | Rev 0 signals with off-the-shelf boards (parallel with 3) | 2 weeks | 0 |
| 3 | NeuroCore Rev A design → order | 3–4 weeks | 0 (informed by 2) |
| 4 | Rev A bring-up + characterization | 2 weeks | 3 |
| 5 | Rule-based hybrid control → car demo | 2 weeks | 1, 4 |
| 6 | ML zoo + on-device models | 3–4 weeks | 5 |
| 7 | SNN on DE10 | 3–4 weeks | 6 |
| 8 | Adapters: gamepad → plugs → drone | 3 weeks | 5 |
| 9 | Open-end experiments, write-up, chip IP | ongoing | 6–8 |

### Phase 0: Foundations
- **Tasks:**
  - Create the repo (layout in §11.2).
  - Add `CLAUDE.md`, `docs/safety.md` and `docs/decisions/` (ADR template).
  - Python 3.12 venv; pre-commit (ruff, black, clang-format, verible for SV).
  - ESP-IDF ≥ 5.3; KiCad 8/9 with `kicad-cli`; cocotb + Verilator + SymbiYosys (already installed).
- **Done when:** `make check` runs lint and all (empty) test suites green in CI (GitHub Actions).
- **Gate:** I've read and signed off `docs/safety.md`.

### Phase 1: Car adapter (no biosignals yet)
- **Tasks:**
  - Rewire the truck (§4).
  - Car firmware: ESP-NOW NIP receiver, TB6612/DRV8833 control, 300 ms watchdog, speed cap, ramp, bench mode.
  - `host/teleop.py` sends NIP from keyboard keys over a USB-connected ESP32 bridge.
  - `protocol/nip` codec + golden vectors + unit tests in C and Python.
- **Done when:**
  - Keyboard drives the car.
  - Pulling the bridge's power stops the car within ≤ 300 ms in 50/50 trials (log them).
  - CRC-corrupted frames are ignored.
- **Gate:** I run the failsafe test myself with wheels off the ground.

### Phase 2: Rev 0 signals (de-risk while the PCB is being made)
- **Buy:** 2–3 BioAmp EXG Pills (or one NPG Lite), gel electrodes, snap cables, skin-prep gel.
- **Tasks:**
  - Chords firmware + Chords-Python to LSL.
  - `host/record.py` writes to the `data/` schema.
  - Record rest, blinks, clenches, alpha and SSVEP (desk LEDs).
  - Rule-based EMG steering of the car through NIP.
- **Done when:** ≥ 3 sessions recorded; EMG steering of the car works; SSVEP peaks are visible in offline FFT.
- **Gate:** electrode-placement notes and photos in `docs/montage.md`.

### Phase 3: NeuroCore Rev A design
- **Tasks:**
  - Requirements doc → block diagram → schematic (KiCad).
  - Datasheet-driven notes per IC (`docs/datasheets/<part>.md`, summarizing the exact sections used).
  - Layout per §3.4, then ERC/DRC.
  - BOM with LCSC/Lion part numbers and stock check.
  - Fab outputs.
- **Done when:** checklist §3.4 is all ticked, ERC/DRC are clean, and a reviewer has signed off.
- **Gate:** **I place the order.** Claude never orders.

### Phase 4: Bring-up + characterization
- **Tasks:** `firmware/neurocore` drivers (ADS1299, ADS131M04, IMU), raw streaming over USB/Wi-Fi to LSL, then the §3.6 protocol.
- **Done when:** the characterization report is complete with noise tables and the alpha/SSVEP/EMG plots.
- **Gate:** I review the plots; decide whether to keep the shadow lane (DNP or not).

### Phase 5: Hybrid control v1
- **Tasks:** §5 pipeline on the ESP32-S3; calibration UI (`host/calibrate.py`, simple Qt or web); ToF shared autonomy on the car (feature flag).
- **Done when:** the §1 success-criteria table is filled in for EMG, SSVEP, latency and the track test.
- **Gate:** demo video plus a metrics table in `docs/reports/`.

### Phase 6: ML zoo
- **Tasks:**
  - `ml/` pipelines for every model in §6.1; public-dataset loaders; my-data loaders.
  - Evaluation harness (§6.3) producing one comparison table.
  - Best-of export: ESP-PPQ to ESP-DL, then on-device A/B against the rule baseline.
- **Done when:** the comparison table is generated by one command (`make bench`) and the best on-device model beats the rules on cross-session accuracy or false activations.
- **Gate:** the artifact sanity tests (§6.3) are passed and documented.

### Phase 7: SNN on FPGA
- **Tasks:** §7.2 steps 1–5.
- **Done when:**
  - cocotb is bit-exact with Python.
  - The formal watchdog proof passes.
  - The DE10 runs live with NeuroCore over SPI.
  - Energy and latency are compared with the ESP32 model.
- **Gate:** resource and power report.

### Phase 8: Adapters
- **Tasks:** BLE gamepad, then smart plug, then drone (props off, then tethered, then open field).
- **Done when:** each adapter passes the same NIP failsafe test suite.
- **Gate:** I personally run the drone failsafe test before any flight.

### Phase 9: Research + release
- Pick 2–3 open ends (§9). Recommended first: **O1, O3, O5**: cheap, measurable and demo-friendly.
- Write the report or preprint; release hardware, firmware and data; optional Tiny Tapeout/C2S submission of the Intent Engine.

---

## 11. Guide for Claude Code

`CLAUDE.md` (separate file in the repo root) holds the standing rules. This section is the build map.

### 11.1 How to work on this repo
- Work **one phase at a time**, on a branch `phase-N-<slug>`. Start every phase by reading this roadmap section and `docs/STATUS.md`. End it by updating `docs/STATUS.md` (done, open, decisions).
- **Tests before features.** Each task card lists acceptance tests; write them first.
- **Datasheet-first.** Never invent register addresses, bit fields, timing or pinouts. If the datasheet section isn't in `docs/datasheets/`, **stop and ask** me to add the PDF. Cite the section in code comments (`// ADS1299 DS §9.6.1.3 CONFIG1`).
- **Hardware actions are human-only:** powering a new board, flashing motor firmware for the first time, putting electrodes on a person, ordering parts or PCBs. Claude prepares scripts and checklists; I execute.
- Prefer **simulation and replay**: BrainFlow's synthetic and playback boards, recorded `data/` sessions, cocotb testbenches.
- Keep the **NIP spec** as the single source of truth; regenerate the C and Python codecs from it if they diverge.

### 11.2 Repository layout
```
neurodrive/
├── CLAUDE.md                 # standing rules for Claude Code
├── ROADMAP.md                # this file
├── Makefile                  # check, test, bench, sim, formal, fw-*, docs
├── docs/
│   ├── STATUS.md             # living progress log
│   ├── safety.md             # electrical, motor and drone rules (signed off)
│   ├── montage.md            # electrode sites + photos
│   ├── litreview.md          # novelty checks for §9
│   ├── decisions/            # ADR-0001-*.md
│   ├── datasheets/           # PDFs + per-part notes (git-lfs)
│   └── reports/              # characterization, benchmarks, demos
├── hardware/
│   ├── neurocore/            # KiCad project, BOM.csv, fab/, review-checklist.md
│   └── stimulus-board/       # (Rev B) SSVEP LED board
├── protocol/nip/             # spec.md, nip.h, nip.c, nip.py, vectors.json, tests/
├── firmware/
│   ├── neurocore/            # ESP-IDF: drivers/{ads1299,ads131m04,imu}, dsp/, intent/, link/, safety/
│   ├── car/                  # ESP-IDF: espnow_rx, motor, watchdog, tof, ssvep_leds
│   ├── bridge-usb/           # ESP32 USB↔ESP-NOW bridge for teleop/testing
│   └── adapters/{gamepad,plug,drone}/
├── host/                     # Python pkg: acquire (LSL/BrainFlow), record, calibrate, teleop, dashboards
├── ml/
│   ├── data/                 # loaders: public datasets + my sessions
│   ├── features/             # band power, EMG TD features, filter banks
│   ├── models/{classical,dl,snn}/
│   ├── eval/                 # protocol §6.3, sanity tests, report generator
│   └── export/               # onnx → esp-ppq → .espdl; snn → int weights → RTL mem files
├── rtl/
│   ├── src/                  # spi_slave.sv, delta_encoder.sv, lif_layer.sv, readout.sv, intent_watchdog.sv
│   ├── tb/                   # cocotb tests (replay data/)
│   ├── formal/               # SymbiYosys .sby + properties
│   └── quartus/              # DE10-Standard project, pin assignments
└── data/                     # gitignored raw; only metadata + small samples committed
```

### 11.3 Tech stack (pinned)
- **Python 3.12** (venv). numpy, scipy, mne, brainflow, pylsl, scikit-learn, pyriemann, torch, braindecode, snntorch, onnx, esp-ppq, metabci, SSVEPAnalysisToolbox, pytest, hypothesis.
- **Firmware:** ESP-IDF ≥ 5.3 (C), ESP-DSP, ESP-DL, ESP-NOW, NimBLE. Unity tests on target where possible; host-side tests via a shim.
- **RTL:** SystemVerilog-2012 subset that Verilator and Quartus both accept. cocotb, Verilator, SymbiYosys, Quartus Prime Lite for Cyclone V.
- **EDA:** KiCad 8/9 with `kicad-cli` for ERC, DRC, BOM and Gerbers in CI.

### 11.4 Data schema (`data/<subject>/<YYYYMMDD>_<session>/`)
- `raw.fif` or `raw.csv`: all channels, µV, with timestamps.
- `events.csv`: onset_s, duration_s, label (e.g., `clench_L`, `ssvep_10Hz`, `rest`), source.
- `meta.json`: hardware rev, firmware hash, montage, sample rates, electrode type, skin prep, impedances, room notes (fan, AC nearby), consent ID.
- `calib.json`: thresholds produced by the calibration routine.

### 11.5 Task cards (copy into issues)

**T1.1 NIP codec.** Implement `protocol/nip/{nip.h,nip.c,nip.py}` per §8.1. Tests: round-trip, CRC mismatch rejected, seq wrap, vectors match across C and Python.

**T1.2 Car firmware.** ESP-NOW receive into the NIP decoder, then the intent-to-motor map. Watchdog: no valid frame for 300 ms ⇒ coast then brake. Tests: host-side unit tests for the mapping and watchdog using simulated time; HIL script for the 50-trial failsafe log.

**T2.1 Recorder.** `host/record.py`: LSL inlet to the §11.4 files, with event markers from keypresses or a stimulus script. Tests: synthetic stream round-trip, timestamps monotonic.

**T3.x Schematic support** (Claude assists, I draw):
- Generate per-IC checklists from the datasheets.
- Review netlist exports against the checklist.
- Build `BOM.csv` with LCSC/Lion part numbers.
- Script the `kicad-cli` ERC/DRC/BOM/Gerber export in CI.

**T4.1 ADS1299 driver.** Init sequence, RDATAC, DRDY interrupt, 24-bit sign extension, µV scaling by PGA, lead-off status, internal test signal. Every register write cites a datasheet section. Tests: decode known byte frames; scaling against hand-computed values.

**T4.2 ADS131M04 driver.** Frame format, CRC, gain and OSR configuration, sample-rate decimation. Same testing rules as T4.1.

**T5.1 DSP + intent.** Notch and band-pass filters (biquads), EMG envelope, FBCCA, intent FSM (§5.5) as a pure function with property-based tests:
- STOP latch can't be bypassed.
- No speed increase while the artifact flag is set.
- Watchdog always wins.

**T6.1 Bench harness.** `make bench`: run every model on every dataset split, output `docs/reports/bench.md` plus a CSV. Include the sanity tests.

**T7.1 SNN RTL.** LIF layer with parameterized width; delta encoder; cocotb bit-exact replay; SBY property `assert property (no_valid_in_N |-> out_intent == STOP)`.

**T8.1 Gamepad adapter.** NIP to ESP32-BLE-Gamepad axes and buttons; on watchdog, center the axes and release all buttons.

---

## 12. Safety rules (summary of `docs/safety.md`)
Trimmed to three rules on 2026-11-19 (one-person project):
1. **Acquisition runs on battery only.** If the laptop is connected over USB, it is **unplugged from its charger**, or the link uses a **USB isolator**.
2. Series resistors on every electrode; never omit them to "improve signal".
3. The NIP 300 ms link watchdog and STOP handling stay in every adapter.

Not a medical device; no medical claims.

---

## 13. Budget sketch (Rev A path)

Prices marked "seen" were found while researching this roadmap (Nov 2026); everything else is an estimate to verify.

| Item | Qty | Cost |
|---|---|---|
| ADS1299-4PAGR | 2 | ₹3,093 each seen (Lion Circuits) → ~₹6.2k |
| ADS131M04IPWR | 2 | ~$3 each seen (LCSC/JLC) |
| ESP32-S3-WROOM-1-N16R8, IMU, LDOs, op-amps, passives, connectors | 2 sets | est. ₹2–3k |
| 4-layer PCB ×5 + assembly ×2 | 1 order | get quotes (Lion Circuits vs JLCPCB incl. customs) |
| TB6612FNG **or** DRV8833 module | 1 | ₹135 / ₹72 seen (Robocraze) |
| ESP32-C3 SuperMini (car) + ESP32 (USB bridge) | 2 | est. ₹500–800 |
| VL53L0X/L1X ToF | 1 | est. |
| Gel electrodes, snap cables, skin-prep gel, headband | — | est. ₹1–2k |
| Rev 0: 2–3 BioAmp EXG Pills | — | check Robu.in |
| **Rough total to a working car demo** | | **~₹15–20k**, dominated by the two ADS1299-4s and the PCB order |

**Cheaper alternative:** skip Rev A and build directly on 3× BioAmp EXG Pill or an NPG Lite. That path gives a faster demo but no PCB and little novelty. The O1 study and the reusable-chip story both need Rev A.

---

## 14. Sources
- Upside Down Labs brain-controlled car (NPG Lite): https://www.crowdsupply.com/upside-down-labs/neuro-playground-lite/updates/build-a-brain-controlled-robotic-car
- NPG Lite specs (ESP32-C6, 3-ch, 12-bit): https://www.crowdsupply.com/upside-down-labs/neuro-playground-lite/updates/getting-started-tutorial
- BioAmp EXG Pill (KiCad, CERN/MIT): https://github.com/upsidedownlabs/BioAmp-EXG-Pill · https://www.electronics-lab.com/news/bioamp-exg-pill-analog-front-end-amplification-for-ecg-emg-eog-and-eeg-biosensing/
- Chords-Python (LSL streaming): https://github.com/upsidedownlabs/Chords-Python
- TI ADS1299 family: https://ti.com/document-viewer/ADS1299/datasheet
- ADS1299-4 price, Lion Circuits: https://www.lioncircuits.com/parts/ADS1299-4PAGR · LCSC: https://www.lcsc.com/product-image/C2651574.html
- TI ADS131M04: https://ti.com/product/ADS131M04 · LCSC: https://www.lcsc.com/product-image/C2862562.html · JLCPCB: https://jlcpcb.com/partdetail/TexasInstruments-ADS131M04IPWR/C2862562
- ADS1292R: https://www.lcsc.com/product-image/C528635.html · https://www.lioncircuits.com/parts/ADS1292RIPBS
- EEGFrontier (ADS1299-4, KiCad, MIT): https://hub.allspice.io/AllSpiceMirrors/EEGFrontier
- Cerelog ESP-EEG + licence notes: https://github.com/Cerelog-ESP-EEG/ESP-EEG · https://www.autodidacts.io/cerelog-esp-eeg-affordable-openbci-like-board
- PiEEG (GPL-3.0, battery-only): https://www.arxiv.org/pdf/2201.02228 · https://arxiv.org/pdf/2301.03459v1.pdf
- Adam-EEG (Apache-2.0): https://dev.to/shiva16/adam-eeg-an-open-source-32-channel-eeg-board-from-2015-quad-ads1299-dual-atmega328-2gfi
- dcmini-hw (CERN-OHL-P): https://gh.r-mj.com/dcmini-org/dcmini-hw
- OpenBCI Cyton specs (battery only, 4-layer): https://docs.openbci.com/Cyton/CytonSpecs/
- BrainFlow supported boards: https://brainflow.readthedocs.io/en/stable/SupportedBoards.html
- MetaBCI: https://pypi.org/project/metabci/ · SSVEPAnalysisToolbox: https://ssvep-analysis-toolbox.readthedocs.io/
- SSVEP datasets (Benchmark, BETA): https://www.ncbi.nlm.nih.gov/pmc/articles/PMC7324867/
- Non-hair-bearing SSVEP BCI (online): https://escholarship.org/content/qt0f34k44w/qt0f34k44w.pdf · chromatic behind-ear SSVEP: https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5855130/
- Hybrid EEG+EMG (examples): https://arxiv.org/abs/2502.10904v1 · https://journal.hep.com.cn/brainx/EN/10.1002/brx2.56
- CBraMod: https://arxiv.org/html/2412.07236v1 · EEG-FM-Bench: https://arxiv.org/pdf/2508.17742v1 · "Are Large Brainwave Foundation Models Capable Yet?": https://arxiv.org/html/2507.01196v1
- Model families (EEGNet, ShallowFBCSPNet, EEG-Conformer…): https://arxiv.org/pdf/2511.18294 · https://selfeeg.readthedocs.io/en/stable/selfeeg.models.html
- Compact-CNN for asynchronous SSVEP: https://arxiv.org/pdf/1803.04566
- Neuromorphic EMG (IIT Delhi, Loihi): https://arxiv.org/abs/2206.02061v1 · signals-to-spikes EMG: https://arxiv.org/abs/2106.11169v2
- SNN frameworks: https://arxiv.org/html/2603.03529 · https://arxiv.org/html/2404.05807v1
- ESP-DL / ESP-PPQ: https://docs.espressif.com/projects/esp-dl/en/latest/getting_started/readme.html · https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_quantize_model.html
- hls4ml oneAPI backend + Intel device list: https://fastmachinelearning.org/hls4ml/advanced/oneapi.html · https://download-software.intel.com/content/www/us/en/developer/tools/oneapi/fpga.html
- Ninapro: https://arxiv.org/pdf/2504.03221 · https://arxiv.org/pdf/2006.03645
- Motor drivers (India prices, drops): https://robocraze.com/blogs/post/drv8833-vs-l298n-vs-tb6612fng-which-motor-driver-should-you-use · https://zbotic.in/motor-driver-comparison-l298n-vs-l293d-vs-tb6612-vs-drv8825/
- ESP32-BLE-Gamepad: https://github.com/lemmingDev/ESP32-BLE-Gamepad
- ESP-Drone: https://docs.espressif.com/projects/espressif-esp-drone/ · LiteWing: https://circuitdigest.com/wiki/litewing/
- Betaflight MSP companion control: https://betaflight.com/docs/wiki/guides/current/MSP-Companion-Computer-Control · INAV Rx/MSP: https://github.com/iNavFlight/inav/blob/master/docs/Rx.md
- Tiny Tapeout runs + FAQ: https://www.tinytapeout.com/runs · https://docs.google.com/document/d/12KPutDPBst7FF0ncB1qyxErDYeATmXoQLGjRq38dfjU/mobilebasic
- C2S Programme / SCL Mohali: https://informatics.nic.in/files/websites/january-2026/government-democratizes-chip-design-access-across-indian-universities.php · https://www.communicationstoday.co.in/?p=419716
- IHP SG13G2 AMS template: https://github.com/iic-jku/ihp-sg13g2-ams-chip-template
