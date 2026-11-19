/*
 * T1.2 host tests for the car adapter core, with simulated time.
 * Safety rules under test (docs/safety.md): S1 link watchdog, S2 STOP/latched stop.
 * Also covers the design choices in ADR-0002: speed caps, artifact freeze, ramp, gear.
 */
#include <string.h>

#include "car_ctrl.h"
#include "link_watchdog.h"
#include "minitest.h"
#include "nip.h"

#define TICK_MS 10u

static car_ctrl_cfg_t cfg_drive(void)
{
    car_ctrl_cfg_t cfg = car_ctrl_default_cfg();
    cfg.bench_mode = false;
    cfg.max_level = 3;
    return cfg;
}

static void make_frame(uint8_t out[NIP_FRAME_LEN], uint8_t seq, uint8_t intent, uint8_t level,
                       uint8_t flags)
{
    nip_frame_t f = {
        .seq = seq, .t_ms = 0, .intent = intent, .level = level, .confidence = 200, .flags = flags};
    (void)nip_encode(&f, out);
}

static car_rx_t send(car_ctrl_t *c, uint32_t now, uint8_t seq, uint8_t intent, uint8_t level,
                     uint8_t flags)
{
    uint8_t buf[NIP_FRAME_LEN];
    make_frame(buf, seq, intent, level, flags);
    return car_ctrl_on_frame(c, buf, NIP_FRAME_LEN, now);
}

/* Tick from `from` to `to` inclusive, sending a heartbeat of `intent` every 40 ms. */
static car_motor_cmd_t drive_for(car_ctrl_t *c, uint32_t from, uint32_t to, uint8_t *seq,
                                 uint8_t intent, uint8_t level, uint8_t flags)
{
    car_motor_cmd_t cmd = {0};
    for (uint32_t t = from; t != to + TICK_MS; t += TICK_MS) {
        if ((t - from) % 40u == 0u) {
            *seq = nip_next_seq(*seq);
            (void)send(c, t, *seq, intent, level, flags);
        }
        cmd = car_ctrl_tick(c, t);
    }
    return cmd;
}

/* ------------------------------------------------------------------ watchdog unit */

static void test_watchdog_starts_expired_and_times_out_at_300ms(void)
{
    link_watchdog_t wd;
    link_watchdog_init(&wd);
    MT_CHECK(link_watchdog_expired(&wd, 0));
    link_watchdog_feed(&wd, 1000);
    MT_CHECK(!link_watchdog_expired(&wd, 1000));
    MT_CHECK(!link_watchdog_expired(&wd, 1299));
    MT_CHECK(link_watchdog_expired(&wd, 1300));
    /* Expiry latches: even if time appears to go back, it stays expired until fed. */
    MT_CHECK(link_watchdog_expired(&wd, 1100));
}

static void test_watchdog_handles_time_wrap(void)
{
    link_watchdog_t wd;
    link_watchdog_init(&wd);
    uint32_t t0 = UINT32_MAX - 100u;
    link_watchdog_feed(&wd, t0);
    MT_CHECK(!link_watchdog_expired(&wd, t0 + 299u)); /* wraps past zero */
    MT_CHECK(link_watchdog_expired(&wd, t0 + 300u));
}

/* ------------------------------------------------------------------ config */

static void test_config_validation(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = car_ctrl_default_cfg();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    MT_CHECK(cfg.bench_mode); /* safe default: wheels off the ground */

    car_ctrl_cfg_t bad = cfg;
    bad.duty_permille[0] = 10;
    MT_CHECK(!car_ctrl_init(&c, &bad));
    bad = cfg;
    bad.duty_permille[2] = (uint16_t)(bad.duty_permille[1] - 1u);
    MT_CHECK(!car_ctrl_init(&c, &bad));
    bad = cfg;
    bad.duty_permille[3] = 1001;
    MT_CHECK(!car_ctrl_init(&c, &bad));
    bad = cfg;
    bad.max_level = 4;
    MT_CHECK(!car_ctrl_init(&c, &bad));
    bad = cfg;
    bad.ramp_permille_per_s = 0;
    MT_CHECK(!car_ctrl_init(&c, &bad));
    MT_CHECK(!car_ctrl_init(&c, NULL));
}

static void test_uninitialised_controller_brakes(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t bad = car_ctrl_default_cfg();
    bad.max_level = 9;
    MT_CHECK(!car_ctrl_init(&c, &bad));
    car_motor_cmd_t cmd = car_ctrl_tick(&c, 0);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_BRAKE);
    MT_CHECK_EQ(cmd.duty_permille, 0);
}

/* ------------------------------------------------------------------ S1 link watchdog */

static void test_no_frames_coast_then_brake(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    car_motor_cmd_t cmd = car_ctrl_tick(&c, 0);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_COAST);
    MT_CHECK_EQ(cmd.duty_permille, 0);
    cmd = car_ctrl_tick(&c, cfg.brake_after_coast_ms - 1u);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_COAST);
    cmd = car_ctrl_tick(&c, cfg.brake_after_coast_ms);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_BRAKE);
    MT_CHECK_EQ(cmd.duty_permille, 0);
}

static void test_link_loss_stops_within_300ms(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint8_t seq = 0;
    car_motor_cmd_t cmd = drive_for(&c, 1000, 2000, &seq, NIP_INTENT_FWD, 2, 0);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_FWD);
    MT_CHECK_EQ(cmd.duty_permille, cfg.duty_permille[2]);

    /* Last frame was at t=2000 (2000-1000 is a multiple of 40). Then silence. */
    for (uint32_t t = 2010; t < 2300; t += TICK_MS) {
        cmd = car_ctrl_tick(&c, t);
        MT_CHECK_EQ(cmd.drive, CAR_DRIVE_FWD);
    }
    cmd = car_ctrl_tick(&c, 2300);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_COAST);
    MT_CHECK_EQ(cmd.duty_permille, 0);
    MT_CHECK_EQ(cmd.steer, CAR_STEER_CENTER);
    cmd = car_ctrl_tick(&c, 2300u + cfg.brake_after_coast_ms);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_BRAKE);
}

static void test_corrupted_frames_do_not_feed_watchdog(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    MT_CHECK_EQ(send(&c, 0, 1, NIP_INTENT_FWD, 1, 0), CAR_RX_ACCEPTED);
    uint8_t seq = 1;
    car_motor_cmd_t cmd = {0};
    for (uint32_t t = 0; t <= 300; t += TICK_MS) {
        if (t % 40u == 20u || t == 300u) {
            uint8_t buf[NIP_FRAME_LEN];
            seq = nip_next_seq(seq);
            make_frame(buf, seq, NIP_INTENT_FWD, 1, 0);
            buf[NIP_FRAME_LEN - 1u] ^= 0x5Au; /* break the CRC */
            MT_CHECK_EQ(car_ctrl_on_frame(&c, buf, NIP_FRAME_LEN, t), CAR_RX_INVALID);
            MT_CHECK_EQ(car_ctrl_on_frame(&c, buf, 7, t), CAR_RX_INVALID);
        }
        cmd = car_ctrl_tick(&c, t);
        if (t == 290u) {
            MT_CHECK_EQ(cmd.drive, CAR_DRIVE_FWD);
            MT_CHECK(cmd.duty_permille > 0u);
        }
    }
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_COAST);
    MT_CHECK_EQ(cmd.duty_permille, 0);
}

static void test_duplicate_seq_ignored_and_resync_after_expiry(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    MT_CHECK_EQ(send(&c, 0, 10, NIP_INTENT_FWD, 1, 0), CAR_RX_ACCEPTED);
    for (uint32_t t = 40; t < 300; t += 40) {
        MT_CHECK_EQ(send(&c, t, 10, NIP_INTENT_FWD, 1, 0), CAR_RX_STALE); /* replayed */
        MT_CHECK_EQ(send(&c, t, 9, NIP_INTENT_FWD, 1, 0), CAR_RX_STALE);  /* older */
    }
    MT_CHECK_EQ(car_ctrl_tick(&c, 300).duty_permille, 0); /* stale frames didn't feed it */
    /* Sender rebooted: seq restarts low. Accepted because the link already expired. */
    MT_CHECK_EQ(send(&c, 310, 0, NIP_INTENT_FWD, 1, 0), CAR_RX_ACCEPTED);
    MT_CHECK_EQ(send(&c, 350, 1, NIP_INTENT_FWD, 1, 0), CAR_RX_ACCEPTED);
}

/* ------------------------------------------------------------------ S2 STOP */

static void test_stop_intent_brakes_and_returns_to_neutral(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint8_t seq = 0;
    (void)drive_for(&c, 0, 1000, &seq, NIP_INTENT_FWD, 3, 0);
    seq = nip_next_seq(seq);
    MT_CHECK_EQ(send(&c, 1010, seq, NIP_INTENT_STOP, 3, 0), CAR_RX_ACCEPTED);
    car_motor_cmd_t cmd = car_ctrl_tick(&c, 1010);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_BRAKE);
    MT_CHECK_EQ(cmd.duty_permille, 0);
    /* HOLD after STOP must not restart motion: gear is neutral until FWD/REV. */
    cmd = drive_for(&c, 1020, 2000, &seq, NIP_INTENT_HOLD, 2, 0);
    MT_CHECK_EQ(cmd.duty_permille, 0);
    MT_CHECK(cmd.drive == CAR_DRIVE_COAST || cmd.drive == CAR_DRIVE_BRAKE);
}

static void test_latched_stop_flag_overrides_intent(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint8_t seq = 0;
    car_motor_cmd_t cmd = drive_for(&c, 0, 1000, &seq, NIP_INTENT_FWD, 3, NIP_FLAG_LATCHED_STOP);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_BRAKE);
    MT_CHECK_EQ(cmd.duty_permille, 0);
}

/* ------------------------------------------------------------------ speed caps, artifact freeze */

static void test_speed_caps(void)
{
    car_ctrl_t c;
    uint8_t seq = 0;
    car_ctrl_cfg_t cfg = cfg_drive();
    cfg.max_level = 2;
    MT_CHECK(car_ctrl_init(&c, &cfg));
    MT_CHECK_EQ(drive_for(&c, 0, 2000, &seq, NIP_INTENT_FWD, 3, 0).duty_permille,
                cfg.duty_permille[2]);

    cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    MT_CHECK_EQ(drive_for(&c, 0, 2000, &seq, NIP_INTENT_FWD, 3, NIP_FLAG_CHANNEL_BAD).duty_permille,
                cfg.duty_permille[1]);

    cfg = cfg_drive();
    cfg.bench_mode = true;
    MT_CHECK(car_ctrl_init(&c, &cfg));
    MT_CHECK_EQ(drive_for(&c, 0, 2000, &seq, NIP_INTENT_FWD, 3, 0).duty_permille,
                cfg.duty_permille[1]);
}

static void test_artifact_flag_blocks_speed_increase_only(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint8_t seq = 0;
    MT_CHECK_EQ(drive_for(&c, 0, 1000, &seq, NIP_INTENT_FWD, 1, 0).duty_permille,
                cfg.duty_permille[1]);
    MT_CHECK_EQ(drive_for(&c, 1010, 2000, &seq, NIP_INTENT_FWD, 3, NIP_FLAG_ARTIFACT).duty_permille,
                cfg.duty_permille[1]);
    MT_CHECK_EQ(drive_for(&c, 2010, 2100, &seq, NIP_INTENT_FWD, 0, NIP_FLAG_ARTIFACT).duty_permille,
                0);
    /* Artifact cleared: increase allowed again. */
    MT_CHECK_EQ(drive_for(&c, 2110, 3000, &seq, NIP_INTENT_FWD, 3, 0).duty_permille,
                cfg.duty_permille[3]);
}

/* ------------------------------------------------------------------ ramp, gear, steering */

static void test_ramp_up_is_limited_and_slow_down_is_instant(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint32_t step = (uint32_t)cfg.ramp_permille_per_s * TICK_MS / 1000u;
    MT_CHECK(step > 0u);
    MT_CHECK_EQ(send(&c, 0, 1, NIP_INTENT_FWD, 3, 0), CAR_RX_ACCEPTED);
    uint16_t prev = car_ctrl_tick(&c, 0).duty_permille;
    MT_CHECK_EQ(prev, 0);
    for (uint32_t t = TICK_MS; t <= 200; t += TICK_MS) {
        uint16_t d = car_ctrl_tick(&c, t).duty_permille;
        MT_CHECK(d >= prev);
        MT_CHECK((uint32_t)(d - prev) <= step);
        prev = d;
    }
    uint8_t seq = 1;
    MT_CHECK_EQ(drive_for(&c, 210, 1500, &seq, NIP_INTENT_FWD, 3, 0).duty_permille,
                cfg.duty_permille[3]);
    seq = nip_next_seq(seq);
    MT_CHECK_EQ(send(&c, 1510, seq, NIP_INTENT_FWD, 1, 0), CAR_RX_ACCEPTED);
    MT_CHECK_EQ(car_ctrl_tick(&c, 1510).duty_permille, cfg.duty_permille[1]);
}

static void test_reversal_passes_through_zero(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint32_t step = (uint32_t)cfg.ramp_permille_per_s * TICK_MS / 1000u;
    uint8_t seq = 0;
    MT_CHECK_EQ(drive_for(&c, 0, 1000, &seq, NIP_INTENT_FWD, 2, 0).drive, CAR_DRIVE_FWD);
    seq = nip_next_seq(seq);
    MT_CHECK_EQ(send(&c, 1010, seq, NIP_INTENT_REV, 2, 0), CAR_RX_ACCEPTED);
    car_motor_cmd_t cmd = car_ctrl_tick(&c, 1010);
    MT_CHECK(cmd.drive != CAR_DRIVE_FWD);
    MT_CHECK(cmd.duty_permille <= step);
    cmd = drive_for(&c, 1020, 2000, &seq, NIP_INTENT_REV, 2, 0);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_REV);
    MT_CHECK_EQ(cmd.duty_permille, cfg.duty_permille[2]);
}

static void test_steering_keeps_gear_and_centers_otherwise(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint8_t seq = 0;
    (void)drive_for(&c, 0, 1000, &seq, NIP_INTENT_FWD, 1, 0);
    car_motor_cmd_t cmd = drive_for(&c, 1010, 1200, &seq, NIP_INTENT_LEFT, 1, 0);
    MT_CHECK_EQ(cmd.steer, CAR_STEER_LEFT);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_FWD);
    MT_CHECK_EQ(cmd.duty_permille, cfg.duty_permille[1]);
    cmd = drive_for(&c, 1210, 1400, &seq, NIP_INTENT_RIGHT, 1, 0);
    MT_CHECK_EQ(cmd.steer, CAR_STEER_RIGHT);
    cmd = drive_for(&c, 1410, 1600, &seq, NIP_INTENT_HOLD, 1, 0);
    MT_CHECK_EQ(cmd.steer, CAR_STEER_CENTER);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_FWD);
    cmd = drive_for(&c, 1610, 1800, &seq, NIP_INTENT_AUX_A, 1, 0);
    MT_CHECK_EQ(cmd.steer, CAR_STEER_CENTER);
}

static void test_works_across_uptime_wrap(void)
{
    car_ctrl_t c;
    car_ctrl_cfg_t cfg = cfg_drive();
    MT_CHECK(car_ctrl_init(&c, &cfg));
    uint8_t seq = 0;
    uint32_t t0 = UINT32_MAX - 495u; /* multiple of TICK_MS away from the wrap */
    car_motor_cmd_t cmd = drive_for(&c, t0, t0 + 1000u, &seq, NIP_INTENT_FWD, 1, 0);
    MT_CHECK_EQ(cmd.drive, CAR_DRIVE_FWD);
    MT_CHECK_EQ(cmd.duty_permille, cfg.duty_permille[1]);
    cmd = car_ctrl_tick(&c, t0 + 1000u + 300u);
    MT_CHECK_EQ(cmd.duty_permille, 0);
}

/* ------------------------------------------------------------------ randomized property */

static uint32_t rng_state = 0x2545F491u;
static uint32_t rnd(uint32_t n)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state % n;
}

/* Whatever arrives, S1, S2 and the speed caps hold on every tick. */
static void test_random_traffic_never_breaks_safety(void)
{
    for (int run = 0; run < 2; run++) {
        car_ctrl_t c;
        car_ctrl_cfg_t cfg = car_ctrl_default_cfg();
        cfg.bench_mode = (run == 1);
        cfg.max_level = 2;
        MT_CHECK(car_ctrl_init(&c, &cfg));
        uint8_t cap = cfg.bench_mode ? (uint8_t)1u : cfg.max_level;

        uint32_t now = 5000;
        uint8_t seq = 0;
        bool have_accept = false;
        uint32_t last_accept = 0;
        nip_frame_t last = {0};
        int violations = 0;

        for (int i = 0; i < 200000; i++) {
            now += 1u + rnd(60);
            uint32_t ev = rnd(10);
            if (ev < 6) {
                uint8_t buf[NIP_FRAME_LEN];
                if (ev != 0u) {
                    seq = nip_next_seq(seq); /* ev == 0 replays the previous seq */
                }
                nip_frame_t f = {.seq = seq,
                                 .t_ms = now,
                                 .intent = (uint8_t)rnd(NIP_INTENT_MAX + 1u),
                                 .level = (uint8_t)rnd(NIP_LEVEL_MAX + 1u),
                                 .confidence = (uint8_t)rnd(256),
                                 .flags = (uint8_t)(rnd(32) & rnd(32))};
                (void)nip_encode(&f, buf);
                bool corrupt = (ev == 1);
                if (corrupt) {
                    buf[rnd(NIP_FRAME_LEN)] ^= (uint8_t)(1u << rnd(8));
                }
                car_rx_t rx = car_ctrl_on_frame(&c, buf, NIP_FRAME_LEN, now);
                if (corrupt && rx != CAR_RX_INVALID) {
                    violations++;
                }
                if (rx == CAR_RX_ACCEPTED) {
                    have_accept = true;
                    last_accept = now;
                    last = f;
                }
            }
            car_motor_cmd_t cmd = car_ctrl_tick(&c, now);
            bool link_ok = have_accept && (now - last_accept) < NIP_LINK_TIMEOUT_MS;
            bool moving = cmd.duty_permille > 0u;
            if (!link_ok && moving) {
                violations++; /* S1 */
            }
            if (link_ok &&
                (last.intent == NIP_INTENT_STOP || (last.flags & NIP_FLAG_LATCHED_STOP) != 0u)) {
                if (cmd.drive != CAR_DRIVE_BRAKE || moving) {
                    violations++; /* S2 */
                }
            }
            if (cmd.duty_permille > cfg.duty_permille[cap]) {
                violations++; /* speed cap */
            }
            if (link_ok && (last.flags & NIP_FLAG_CHANNEL_BAD) != 0u &&
                cmd.duty_permille > cfg.duty_permille[1]) {
                violations++; /* channel_bad cap */
            }
            if ((cmd.drive == CAR_DRIVE_COAST || cmd.drive == CAR_DRIVE_BRAKE) && moving) {
                violations++; /* consistency */
            }
        }
        MT_CHECK_EQ(violations, 0);
    }
}

int main(void)
{
    MT_RUN(test_watchdog_starts_expired_and_times_out_at_300ms);
    MT_RUN(test_watchdog_handles_time_wrap);
    MT_RUN(test_config_validation);
    MT_RUN(test_uninitialised_controller_brakes);
    MT_RUN(test_no_frames_coast_then_brake);
    MT_RUN(test_link_loss_stops_within_300ms);
    MT_RUN(test_corrupted_frames_do_not_feed_watchdog);
    MT_RUN(test_duplicate_seq_ignored_and_resync_after_expiry);
    MT_RUN(test_stop_intent_brakes_and_returns_to_neutral);
    MT_RUN(test_latched_stop_flag_overrides_intent);
    MT_RUN(test_speed_caps);
    MT_RUN(test_artifact_flag_blocks_speed_increase_only);
    MT_RUN(test_ramp_up_is_limited_and_slow_down_is_instant);
    MT_RUN(test_reversal_passes_through_zero);
    MT_RUN(test_steering_keeps_gear_and_centers_otherwise);
    MT_RUN(test_works_across_uptime_wrap);
    MT_RUN(test_random_traffic_never_breaks_safety);
    return MT_REPORT();
}
