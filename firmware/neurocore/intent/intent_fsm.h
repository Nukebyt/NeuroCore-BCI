/*
 * Intent FSM (ROADMAP §5.5), C port of ml/intent/fsm.py. Behaviour: ADR-0004.
 * Pure function, no allocation; one fsm_step() per 25 Hz NIP frame.
 * Keep docs/safety.md S2 (STOP latch) tests passing when changing this.
 * Equivalence with the Python reference is checked by tests/test_intent_fsm.c.
 */
#ifndef INTENT_FSM_H
#define INTENT_FSM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FSM_IDLE = 0,
    FSM_ARMED,
    FSM_DRIVE,
    FSM_STEER_L,
    FSM_STEER_R,
    FSM_STOPPED,
    FSM_FAULT
} fsm_mode_t;

typedef enum { SSVEP_NONE = 0, SSVEP_PLUS = 1, SSVEP_MINUS = 2 } ssvep_t;

typedef struct {
    uint32_t stop_hold_ms;
    uint32_t relax_clear_ms;
    uint32_t double_clench_gap_ms;
    uint32_t level_refractory_ms;
    uint32_t ssvep_rearm_ms;
    uint8_t gear_assert_frames;
} fsm_cfg_t;

typedef struct {
    uint32_t t_ms;
    bool emg_left;
    bool emg_right;
    ssvep_t ssvep;
    bool artifact;
    bool channel_bad;
    bool calibrated;
    bool fault;
} fsm_inputs_t;

typedef struct {
    bool emit;      /* false: stop emitting NIP frames (after a fault) */
    uint8_t intent; /* nip_intent_t */
    uint8_t level;
    uint8_t flags; /* NIP_FLAG_* */
} fsm_output_t;

/* Optional timestamps are (has_x, x) pairs, mirroring Python's `int | None`. */
typedef struct {
    fsm_mode_t mode;
    uint8_t level;
    bool reverse;
    bool has_both_since;
    uint32_t both_since_ms;
    bool has_last_pulse_end;
    uint32_t last_pulse_end_ms;
    bool has_relax_since;
    uint32_t relax_since_ms;
    bool has_last_change;
    uint32_t last_level_change_ms;
    uint8_t gear_frames_left;
    bool fault_reported;
    bool ssvep_rearmed;
    bool has_none_since;
    uint32_t none_since_ms;
} fsm_state_t;

fsm_cfg_t fsm_default_cfg(void);
void fsm_init(fsm_state_t *s);
fsm_output_t fsm_step(fsm_state_t *s, const fsm_inputs_t *in, const fsm_cfg_t *cfg);

#endif /* INTENT_FSM_H */
