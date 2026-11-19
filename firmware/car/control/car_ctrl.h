/*
 * Car adapter core: NIP frames in, abstract motor commands out (ROADMAP §4.2 step 8, T1.2).
 * Pure logic with caller-supplied time, so it runs unchanged on the ESP32-C3 and in host tests.
 * No pins, PWM or driver specifics here: those come from the motor-driver datasheet later.
 *
 * Safety (docs/safety.md): S1 link watchdog, S2 STOP / latched stop. Also speed caps and
 * artifact freeze as defense in depth (NeuroCore's FSM enforces them first).
 * See ADR-0002 for the behaviour choices.
 */
#ifndef CAR_CTRL_H
#define CAR_CTRL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_watchdog.h"
#include "nip.h"

#define CAR_LEVELS 4u

typedef struct {
    uint16_t duty_permille[CAR_LEVELS]; /* per NIP level; [0] == 0, non-decreasing, <= 1000 */
    uint8_t max_level;                  /* hard speed cap, 0..3 */
    uint16_t ramp_permille_per_s;       /* soft start: max rise rate of |duty|, > 0 */
    uint16_t brake_after_coast_ms;      /* on link loss: coast this long, then brake */
    bool bench_mode;                    /* wheels-off-ground mode: caps level at 1 */
} car_ctrl_cfg_t;

typedef enum { CAR_DRIVE_COAST = 0, CAR_DRIVE_FWD, CAR_DRIVE_REV, CAR_DRIVE_BRAKE } car_drive_t;
typedef enum { CAR_STEER_CENTER = 0, CAR_STEER_LEFT, CAR_STEER_RIGHT } car_steer_t;
typedef enum { CAR_RX_ACCEPTED = 0, CAR_RX_INVALID, CAR_RX_STALE } car_rx_t;

typedef struct {
    car_drive_t drive;
    uint16_t duty_permille; /* 0 unless drive is FWD or REV */
    car_steer_t steer;
} car_motor_cmd_t;

typedef struct {
    bool valid; /* false if init failed: tick() then always brakes */
    car_ctrl_cfg_t cfg;
    link_watchdog_t wd;
    bool have_seq;
    uint8_t last_seq;
    nip_frame_t frame;    /* last accepted frame */
    int8_t gear;          /* +1 forward, -1 reverse, 0 neutral */
    int16_t applied_duty; /* signed permille actually commanded */
    uint8_t applied_level;
    bool ticked;
    uint32_t last_tick_ms;
    bool link_lost;
    uint32_t link_lost_since_ms;
} car_ctrl_t;

/* Conservative defaults; duty values are to be tuned on the bench. bench_mode defaults ON. */
car_ctrl_cfg_t car_ctrl_default_cfg(void);

bool car_ctrl_cfg_valid(const car_ctrl_cfg_t *cfg);

/* Returns false (and leaves the controller in always-brake state) if cfg is invalid.
 * Firmware must not enable the motor driver if this returns false. */
bool car_ctrl_init(car_ctrl_t *c, const car_ctrl_cfg_t *cfg);

/* Feed one received transport payload. Only ACCEPTED frames feed the link watchdog. */
car_rx_t car_ctrl_on_frame(car_ctrl_t *c, const uint8_t *buf, size_t len, uint32_t now_ms);

/* Call periodically (e.g. every 10 ms) with a monotonic ms clock. */
car_motor_cmd_t car_ctrl_tick(car_ctrl_t *c, uint32_t now_ms);

#endif /* CAR_CTRL_H */
