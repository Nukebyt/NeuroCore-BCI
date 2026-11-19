/* Intent FSM, C port of ml/intent/fsm.py. Section numbers match the Python comments. */
#include "intent_fsm.h"

#include <string.h>

#include "nip.h"

#define ELAPSED(now, since) ((uint32_t)((now) - (since))) /* wrap-safe */

fsm_cfg_t fsm_default_cfg(void)
{
    fsm_cfg_t cfg = {
        .stop_hold_ms = 300u,
        .relax_clear_ms = 2000u,
        .double_clench_gap_ms = 600u,
        .level_refractory_ms = 1000u,
        .ssvep_rearm_ms = 500u,
        .gear_assert_frames = 3u,
    };
    return cfg;
}

void fsm_init(fsm_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->mode = FSM_IDLE;
    s->ssvep_rearmed = true;
}

static fsm_output_t out(uint8_t intent, uint8_t level, uint8_t flags)
{
    fsm_output_t o = {.emit = true, .intent = intent, .level = level, .flags = flags};
    return o;
}

fsm_output_t fsm_step(fsm_state_t *s, const fsm_inputs_t *in, const fsm_cfg_t *cfg)
{
    const uint32_t t = in->t_ms;
    uint8_t flags = 0u;
    if (in->calibrated) {
        flags |= NIP_FLAG_CALIBRATED;
    }
    if (in->artifact) {
        flags |= NIP_FLAG_ARTIFACT;
    }
    if (in->channel_bad) {
        flags |= NIP_FLAG_CHANNEL_BAD;
    }
    const uint8_t latched = (uint8_t)(flags | NIP_FLAG_LATCHED_STOP);

    /* 1. Fault always wins: one STOP, then stop emitting. */
    if (in->fault || s->mode == FSM_FAULT) {
        if (s->fault_reported) {
            fsm_output_t silent = {.emit = false, .intent = 0u, .level = 0u, .flags = flags};
            return silent;
        }
        s->mode = FSM_FAULT;
        s->level = 0u;
        s->fault_reported = true;
        return out(NIP_INTENT_STOP, 0u, latched);
    }

    const bool any_emg = in->emg_left || in->emg_right;
    const bool both = in->emg_left && in->emg_right;
    if (any_emg) {
        s->has_relax_since = false;
    } else if (!s->has_relax_since) {
        s->has_relax_since = true;
        s->relax_since_ms = t;
    }

    bool short_pulse = false;
    uint32_t short_pulse_start = 0u;
    if (both) {
        if (!s->has_both_since) {
            s->has_both_since = true;
            s->both_since_ms = t;
        }
    } else if (s->has_both_since) {
        if (ELAPSED(t, s->both_since_ms) < cfg->stop_hold_ms) {
            short_pulse = true;
            short_pulse_start = s->both_since_ms;
        }
        s->has_both_since = false;
    }
    ssvep_t ssvep = in->ssvep;

    /* 2. STOP latch (S2). */
    bool cleared_now = false;
    if (s->mode == FSM_STOPPED) {
        bool relaxed = s->has_relax_since && ELAPSED(t, s->relax_since_ms) >= cfg->relax_clear_ms;
        if (!(relaxed && ssvep == SSVEP_MINUS && in->calibrated)) {
            s->level = 0u;
            return out(NIP_INTENT_STOP, 0u, latched);
        }
        s->mode = FSM_ARMED;
        s->level = 0u;
        s->reverse = false;
        s->has_last_pulse_end = false;
        s->has_last_change = true;
        s->last_level_change_ms = t;
        s->gear_frames_left = cfg->gear_assert_frames;
        s->ssvep_rearmed = false;
        ssvep = SSVEP_NONE;
        cleared_now = true;
    }

    if (s->has_both_since && ELAPSED(t, s->both_since_ms) >= cfg->stop_hold_ms) {
        s->mode = FSM_STOPPED;
        s->level = 0u;
        s->gear_frames_left = 0u;
        s->has_last_pulse_end = false;
        return out(NIP_INTENT_STOP, 0u, latched);
    }

    /* 3. Not calibrated: no motion. */
    if (!in->calibrated) {
        s->mode = FSM_IDLE;
        s->level = 0u;
        s->reverse = false;
        s->gear_frames_left = 0u;
        return out(NIP_INTENT_STOP, 0u, flags);
    }
    if (s->mode == FSM_IDLE) {
        s->mode = FSM_ARMED;
        s->level = 0u;
        s->gear_frames_left = cfg->gear_assert_frames;
    }

    /* 4. Double both-arm clench toggles reverse, only at level 0. */
    if (short_pulse) {
        if (s->has_last_pulse_end &&
            ELAPSED(short_pulse_start, s->last_pulse_end_ms) <= cfg->double_clench_gap_ms) {
            if (s->level == 0u) {
                s->reverse = !s->reverse;
                s->gear_frames_left = cfg->gear_assert_frames;
            }
            s->has_last_pulse_end = false;
        } else {
            s->has_last_pulse_end = true;
            s->last_pulse_end_ms = t;
        }
    }

    /* 5. SSVEP speed changes, edge-triggered; artifact blocks increases. */
    uint8_t level = s->level;
    if (ssvep == SSVEP_NONE && !cleared_now) {
        if (!s->has_none_since) {
            s->has_none_since = true;
            s->none_since_ms = t;
        }
        if (ELAPSED(t, s->none_since_ms) >= cfg->ssvep_rearm_ms) {
            s->ssvep_rearmed = true;
        }
    } else if (ssvep != SSVEP_NONE) {
        s->has_none_since = false;
    }
    if (cleared_now) {
        s->has_none_since = false;
    }
    if (ssvep != SSVEP_NONE && s->ssvep_rearmed &&
        (!s->has_last_change || ELAPSED(t, s->last_level_change_ms) >= cfg->level_refractory_ms)) {
        uint8_t next = level;
        if (ssvep == SSVEP_PLUS && !in->artifact) {
            next = (level < NIP_LEVEL_MAX) ? (uint8_t)(level + 1u) : level;
        } else if (ssvep == SSVEP_MINUS) {
            next = (level > 0u) ? (uint8_t)(level - 1u) : 0u;
        }
        if (next != level) {
            s->has_last_change = true;
            s->last_level_change_ms = t;
            s->ssvep_rearmed = false;
            if (level == 0u) {
                s->gear_frames_left = cfg->gear_assert_frames;
            }
            level = next;
        }
    }
    if (in->channel_bad && level > 1u) {
        level = 1u; /* channel_bad cap */
    }

    /* 6. Mode and output. */
    const bool steer_l = in->emg_left && !in->emg_right;
    const bool steer_r = in->emg_right && !in->emg_left;
    if (steer_l) {
        s->mode = FSM_STEER_L;
    } else if (steer_r) {
        s->mode = FSM_STEER_R;
    } else {
        s->mode = (level > 0u) ? FSM_DRIVE : FSM_ARMED;
    }
    const uint8_t gear_intent = s->reverse ? NIP_INTENT_REV : NIP_INTENT_FWD;
    uint8_t intent;
    if (s->gear_frames_left > 0u) {
        intent = gear_intent;
        s->gear_frames_left--;
    } else if (steer_l) {
        intent = NIP_INTENT_LEFT;
    } else if (steer_r) {
        intent = NIP_INTENT_RIGHT;
    } else if (level > 0u) {
        intent = gear_intent;
    } else {
        intent = NIP_INTENT_HOLD;
    }
    s->level = level;
    return out(intent, level, flags);
}
