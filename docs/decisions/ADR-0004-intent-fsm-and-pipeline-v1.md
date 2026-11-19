# ADR-0004: Intent FSM and rule-based pipeline v1

- Status: accepted
- Date: 2026-11-19
- Phase: 5 (software built early, before hardware)
- Safety-relevant: yes (S2)

## Context
ROADMAP §5.5 gives the FSM rules in outline. To implement and test them (T5.1), several details had to be fixed. The pipeline (§5.1–5.3) also needed concrete parameters. Everything is a Python reference (`ml/intent/`, `ml/features/`, `ml/models/classical/ssvep.py`) to be ported to `firmware/neurocore/` in C, checked against the same scenarios.

## Decision

**FSM** (`ml/intent/fsm.py`, one step per 25 Hz NIP frame):
1. Order of precedence: fault > STOP latch > both-arm hold > not calibrated > everything else.
2. **Fault** (NeuroCore watchdog/brownout): emit STOP once with `latched_stop`, then emit nothing, so adapters fall to their 300 ms watchdog.
3. **STOP latch:** both arms ≥ 300 ms. Cleared only by ≥ 2000 ms with no EMG on either arm followed by SSVEP "−", while calibrated. The clearing "−" is consumed.
4. **Not calibrated:** IDLE, output STOP (not latched). Calibration returning → ARMED.
5. **Double clench:** two both-arm pulses each < 300 ms, second starting ≤ 600 ms after the first ended. Toggles reverse only at level 0; ignored otherwise.
6. **Speed (rev 2, edge-triggered):** SSVEP "+"/"−" change the level by 1 (0..3), **once per look**. The next step needs the decoder to report NONE for ≥ 500 ms (`ssvep_rearm_ms`), plus the 1000 ms refractory. Artifact flag blocks increases only. `channel_bad` clamps the stored level to 1 (it does not jump back up when the channel recovers). The pipeline holds the decoder's latest decision between its 250 ms steps.
7. **Gear assert:** after arming, a gear toggle or leaving level 0, send 3 FWD/REV frames before any LEFT/RIGHT, because the car sets its gear only from FWD/REV (ADR-0002).
8. Steering: left XOR right. Both arms on (shorter than 300 ms) is a pulse in progress, not steering.

**Pipeline** (`ml/intent/pipeline.py`):
- EMG at 1 kHz: notch + 20–450 Hz, RMS envelope 125 ms / 25 ms hop, hysteresis per §5.2 formula, 80 ms debounce.
- EEG at 250 Hz: 50/100 Hz notch only before FBCCA (FBCCA applies its own sub-band filters; Chen et al. 2015 weights).
- SSVEP "+" = 10 Hz, "−" = 12 Hz (from the §5.3 candidates). 2 s window, 250 ms step, 2 agreeing windows. Idle threshold = rest mean + 3 SD of the best FBCCA score.

## Evidence
- Property tests (hypothesis, 400 generated sessions) for the latch, both-arm hold, fault, artifact, channel_bad, level bounds, reverse-at-zero and calibration rules. A mutation run (7 deliberate bugs) was caught every time.
- Synthetic end-to-end demo (`python -m host.simulate --print`) drives, steers, latches STOP and clears.

## Known limitations (to measure on real data)
1. ~~**SSVEP lag overshoot.**~~ Fixed in rev 2 (edge-triggered speed, item 6). Two causes were found: the pipeline fed NONE between decoder steps (re-arming every 40 ms), and single-step decoder dropouts inside one look. Demo now: one 4 s look → exactly one step. Trade-off: going from 0 to 3 needs three separate looks.
2. **Asymmetric clench onset.** A both-arm clench can emit one LEFT/RIGHT frame (40 ms) before the second arm crosses threshold.
3. **Idle false positives.** One false "−" in ~20 s of synthetic idle in the demo (harmless direction). Target ≤ 1/min (§1).
4. **FBCCA vs CCA.** On featureless synthetic noise, FBCCA is worse than plain CCA below ~1.5 µV. Compare on real data in Phase 6.
5. SSVEP sweep on synthetic data: 2 µV 52 %, 3 µV 85 %, 4 µV 100 % (all errors were misses, none wrong-LED).
