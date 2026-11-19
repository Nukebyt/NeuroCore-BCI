/* Car adapter core. See car_ctrl.h and ADR-0002. */
#include "car_ctrl.h"

#include <string.h>

#define MAX_RAMP_DT_MS 1000u

static const car_motor_cmd_t CMD_BRAKE = {CAR_DRIVE_BRAKE, 0u, CAR_STEER_CENTER};
static const car_motor_cmd_t CMD_COAST = {CAR_DRIVE_COAST, 0u, CAR_STEER_CENTER};

car_ctrl_cfg_t car_ctrl_default_cfg(void)
{
    car_ctrl_cfg_t cfg = {
        .duty_permille = {0u, 350u, 600u, 850u},
        .max_level = 2u,
        .ramp_permille_per_s = 2000u,
        .brake_after_coast_ms = 200u,
        .bench_mode = true,
    };
    return cfg;
}

bool car_ctrl_cfg_valid(const car_ctrl_cfg_t *cfg)
{
    if (cfg == NULL || cfg->duty_permille[0] != 0u || cfg->max_level > NIP_LEVEL_MAX ||
        cfg->ramp_permille_per_s == 0u) {
        return false;
    }
    for (unsigned i = 1; i < CAR_LEVELS; i++) {
        if (cfg->duty_permille[i] < cfg->duty_permille[i - 1u] || cfg->duty_permille[i] > 1000u) {
            return false;
        }
    }
    return true;
}

bool car_ctrl_init(car_ctrl_t *c, const car_ctrl_cfg_t *cfg)
{
    memset(c, 0, sizeof *c);
    link_watchdog_init(&c->wd);
    if (!car_ctrl_cfg_valid(cfg)) {
        c->valid = false;
        return false;
    }
    c->cfg = *cfg;
    c->valid = true;
    return true;
}

car_rx_t car_ctrl_on_frame(car_ctrl_t *c, const uint8_t *buf, size_t len, uint32_t now_ms)
{
    nip_frame_t f;
    if (!c->valid || nip_decode(buf, len, &f) != NIP_OK) {
        return CAR_RX_INVALID;
    }
    /* After the link has expired, accept any valid frame so a rebooted sender can resync. */
    bool expired = link_watchdog_expired(&c->wd, now_ms);
    if (!expired && c->have_seq && !nip_seq_is_newer(f.seq, c->last_seq)) {
        return CAR_RX_STALE;
    }
    c->have_seq = true;
    c->last_seq = f.seq;
    c->frame = f;
    link_watchdog_feed(&c->wd, now_ms);
    return CAR_RX_ACCEPTED;
}

static uint8_t min_u8(uint8_t a, uint8_t b)
{
    return a < b ? a : b;
}

static uint8_t effective_level(const car_ctrl_t *c)
{
    const nip_frame_t *f = &c->frame;
    uint8_t cap = c->cfg.max_level;
    if (c->cfg.bench_mode || (f->flags & NIP_FLAG_CHANNEL_BAD) != 0u) {
        cap = min_u8(cap, 1u);
    }
    uint8_t level = min_u8(f->level, cap);
    if ((f->flags & NIP_FLAG_ARTIFACT) != 0u) {
        level = min_u8(level, c->applied_level); /* artifact freeze: no increase while flagged */
    }
    return level;
}

static int16_t ramp_towards(int16_t applied, int16_t target, uint32_t dt_ms, uint16_t rate)
{
    /* Direction change: drop to zero first (instant), then rise in the new direction. */
    if ((applied > 0 && target <= 0) || (applied < 0 && target >= 0)) {
        applied = 0;
    }
    int32_t mag_applied = applied < 0 ? -(int32_t)applied : applied;
    int32_t mag_target = target < 0 ? -(int32_t)target : target;
    if (mag_target <= mag_applied) {
        return target; /* slowing down is never ramped */
    }
    int32_t step = (int32_t)(((uint32_t)rate * dt_ms) / 1000u);
    int32_t mag = mag_applied + step;
    if (mag > mag_target) {
        mag = mag_target;
    }
    return (int16_t)(target < 0 ? -mag : mag);
}

car_motor_cmd_t car_ctrl_tick(car_ctrl_t *c, uint32_t now_ms)
{
    if (!c->valid) {
        return CMD_BRAKE;
    }
    uint32_t dt_ms = c->ticked ? (uint32_t)(now_ms - c->last_tick_ms) : 0u;
    if (dt_ms > MAX_RAMP_DT_MS) {
        dt_ms = MAX_RAMP_DT_MS;
    }
    c->ticked = true;
    c->last_tick_ms = now_ms;

    /* S1: link watchdog first. Nothing below can override it. */
    if (link_watchdog_expired(&c->wd, now_ms)) {
        if (!c->link_lost) {
            c->link_lost = true;
            c->link_lost_since_ms = now_ms;
        }
        c->applied_duty = 0;
        c->applied_level = 0;
        c->gear = 0;
        if ((uint32_t)(now_ms - c->link_lost_since_ms) >= c->cfg.brake_after_coast_ms) {
            return CMD_BRAKE;
        }
        return CMD_COAST;
    }
    c->link_lost = false;

    /* S2: STOP intent or latched stop. */
    const nip_frame_t *f = &c->frame;
    if (f->intent == NIP_INTENT_STOP || (f->flags & NIP_FLAG_LATCHED_STOP) != 0u) {
        c->applied_duty = 0;
        c->applied_level = 0;
        c->gear = 0;
        return CMD_BRAKE;
    }

    if (f->intent == NIP_INTENT_FWD) {
        c->gear = 1;
    } else if (f->intent == NIP_INTENT_REV) {
        c->gear = -1;
    }

    uint8_t level = effective_level(c); /* speed caps, artifact freeze (ADR-0002) */
    int16_t target = (int16_t)(c->gear * (int16_t)c->cfg.duty_permille[level]);
    c->applied_duty = ramp_towards(c->applied_duty, target, dt_ms, c->cfg.ramp_permille_per_s);
    c->applied_level = level;

    car_motor_cmd_t cmd = CMD_COAST;
    if (c->applied_duty > 0) {
        cmd.drive = CAR_DRIVE_FWD;
        cmd.duty_permille = (uint16_t)c->applied_duty;
    } else if (c->applied_duty < 0) {
        cmd.drive = CAR_DRIVE_REV;
        cmd.duty_permille = (uint16_t)(-c->applied_duty);
    }
    if (f->intent == NIP_INTENT_LEFT) {
        cmd.steer = CAR_STEER_LEFT;
    } else if (f->intent == NIP_INTENT_RIGHT) {
        cmd.steer = CAR_STEER_RIGHT;
    }
    return cmd;
}
