/*
 * C intent FSM vs the Python reference: same xorshift32 sessions, same FNV-1a output hashes
 * (spec in ml/intent/gen_fsm_vectors.py), plus direct STOP-latch tests (docs/safety.md S2).
 */
#include <stdio.h>

#include "fsm_vectors.h"
#include "intent_fsm.h"
#include "minitest.h"
#include "nip.h"

#define TICK_MS 40u

typedef struct {
    uint32_t x;
} rng_t;

static uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->x;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->x = x;
    return x;
}

static uint32_t fnv(uint32_t h, uint8_t b)
{
    return (h ^ b) * 0x01000193u;
}

/* Runs one generated session; returns the output hash and the step count. */
static uint32_t run_session(uint32_t seed, uint32_t *n_steps)
{
    static const bool ARMS[7][2] = {{false, false}, {false, false}, {false, false}, {false, false},
                                    {true, false},  {false, true},  {true, true}};
    static const ssvep_t SSVEPS[5] = {SSVEP_NONE, SSVEP_NONE, SSVEP_NONE, SSVEP_PLUS, SSVEP_MINUS};
    rng_t rng = {seed};
    fsm_state_t s;
    fsm_init(&s);
    const fsm_cfg_t cfg = fsm_default_cfg();
    uint32_t h = 0x811C9DC5u;
    uint32_t t = 0;
    *n_steps = 0;

    uint32_t n_segments = 1u + rng_next(&rng) % 30u;
    for (uint32_t seg = 0; seg < n_segments; seg++) {
        uint32_t c = rng_next(&rng) % 2u;
        uint32_t n = c ? 1u + rng_next(&rng) % 8u : 1u + rng_next(&rng) % 60u;
        uint32_t arms = rng_next(&rng) % 7u;
        ssvep_t ssvep = SSVEPS[rng_next(&rng) % 5u];
        bool artifact = rng_next(&rng) % 8u == 0u;
        bool bad = rng_next(&rng) % 9u == 0u;
        bool cal = rng_next(&rng) % 13u != 0u;
        bool fault = rng_next(&rng) % 41u == 0u;
        for (uint32_t k = 0; k < n; k++) {
            fsm_inputs_t in = {.t_ms = t,
                               .emg_left = ARMS[arms][0],
                               .emg_right = ARMS[arms][1],
                               .ssvep = ssvep,
                               .artifact = artifact,
                               .channel_bad = bad,
                               .calibrated = cal,
                               .fault = fault && k == 0u};
            fsm_output_t o = fsm_step(&s, &in, &cfg);
            h = fnv(h, o.emit ? o.intent : 0xFFu);
            h = fnv(h, o.level);
            h = fnv(h, o.flags);
            t += TICK_MS;
            (*n_steps)++;
        }
    }
    return h;
}

static void test_matches_python_reference(void)
{
    MT_CHECK_EQ(FSM_N_VECTORS, 300);
    for (size_t i = 0; i < FSM_N_VECTORS; i++) {
        uint32_t n = 0;
        uint32_t h = run_session(FSM_VECTORS[i].seed, &n);
        if (h != FSM_VECTORS[i].hash || n != FSM_VECTORS[i].n_steps) {
            fprintf(stderr, "seed %u: steps %u/%u hash %08X/%08X\n", FSM_VECTORS[i].seed, n,
                    FSM_VECTORS[i].n_steps, h, FSM_VECTORS[i].hash);
            MT_CHECK(0);
        }
    }
}

/* ---------------------------------------------------------------- explicit S2 tests */

static fsm_output_t tick(fsm_state_t *s, uint32_t t, bool l, bool r, ssvep_t ssvep)
{
    const fsm_cfg_t cfg = fsm_default_cfg();
    fsm_inputs_t in = {
        .t_ms = t, .emg_left = l, .emg_right = r, .ssvep = ssvep, .calibrated = true};
    return fsm_step(s, &in, &cfg);
}

static void test_both_arms_300ms_latches_stop(void)
{
    fsm_state_t s;
    fsm_init(&s);
    uint32_t t = 0;
    for (; t < 400; t += TICK_MS) {
        (void)tick(&s, t, false, false, SSVEP_NONE);
    }
    (void)tick(&s, t, false, false, SSVEP_PLUS);
    t += TICK_MS;
    for (uint32_t k = 0; k < 7; k++, t += TICK_MS) { /* 0..240 ms held */
        fsm_output_t o = tick(&s, t, true, true, SSVEP_NONE);
        MT_CHECK(!(o.flags & NIP_FLAG_LATCHED_STOP));
    }
    fsm_output_t o = tick(&s, t, true, true, SSVEP_NONE); /* 280 ms */
    MT_CHECK(!(o.flags & NIP_FLAG_LATCHED_STOP));
    t += TICK_MS;
    o = tick(&s, t, true, true, SSVEP_NONE); /* 320 ms >= 300 */
    MT_CHECK_EQ(o.intent, NIP_INTENT_STOP);
    MT_CHECK(o.flags & NIP_FLAG_LATCHED_STOP);
    MT_CHECK_EQ(s.mode, FSM_STOPPED);
}

static void test_latch_needs_2s_relax_then_minus(void)
{
    fsm_state_t s;
    fsm_init(&s);
    uint32_t t = 0;
    for (; t <= 400; t += TICK_MS) {
        (void)tick(&s, t, true, true, SSVEP_NONE);
    }
    MT_CHECK_EQ(s.mode, FSM_STOPPED);
    uint32_t relax_start = t;
    /* MINUS too early, PLUS any time, EMG resets the relax timer. */
    for (; t < relax_start + 1960u; t += TICK_MS) {
        MT_CHECK_EQ(tick(&s, t, false, false, SSVEP_MINUS).intent, NIP_INTENT_STOP);
    }
    MT_CHECK_EQ(tick(&s, t, true, false, SSVEP_NONE).intent, NIP_INTENT_STOP);
    t += TICK_MS;
    relax_start = t;
    for (; t < relax_start + 2000u; t += TICK_MS) {
        MT_CHECK_EQ(tick(&s, t, false, false, SSVEP_PLUS).intent, NIP_INTENT_STOP);
    }
    fsm_output_t o = tick(&s, t, false, false, SSVEP_MINUS);
    MT_CHECK(o.intent != NIP_INTENT_STOP);
    MT_CHECK(!(o.flags & NIP_FLAG_LATCHED_STOP));
    MT_CHECK_EQ(o.level, 0);
    MT_CHECK_EQ(s.mode, FSM_ARMED);
}

static void test_fault_stop_once_then_silent(void)
{
    fsm_state_t s;
    fsm_init(&s);
    const fsm_cfg_t cfg = fsm_default_cfg();
    fsm_inputs_t in = {.t_ms = 0, .calibrated = true, .fault = true};
    fsm_output_t o = fsm_step(&s, &in, &cfg);
    MT_CHECK(o.emit);
    MT_CHECK_EQ(o.intent, NIP_INTENT_STOP);
    in.fault = false;
    for (uint32_t t = 40; t < 2000; t += TICK_MS) {
        in.t_ms = t;
        in.ssvep = SSVEP_MINUS;
        MT_CHECK(!fsm_step(&s, &in, &cfg).emit);
    }
}

/* Two 120 ms both-arm pulses; second starts `gap` ms after the first is released. */
static bool double_clench_toggles(uint32_t gap)
{
    fsm_state_t s;
    fsm_init(&s);
    uint32_t t = 0;
    for (; t < 200; t += TICK_MS) {
        (void)tick(&s, t, false, false, SSVEP_NONE);
    }
    for (int k = 0; k < 3; k++, t += TICK_MS) {
        (void)tick(&s, t, true, true, SSVEP_NONE);
    }
    uint32_t released = t;
    for (; t < released + gap; t += TICK_MS) {
        (void)tick(&s, t, false, false, SSVEP_NONE);
    }
    for (int k = 0; k < 3; k++, t += TICK_MS) {
        (void)tick(&s, t, true, true, SSVEP_NONE);
    }
    (void)tick(&s, t, false, false, SSVEP_NONE);
    return s.reverse;
}

static void test_double_clench_gap_boundary(void)
{
    MT_CHECK(double_clench_toggles(600));
    MT_CHECK(!double_clench_toggles(640));
}

int main(void)
{
    MT_RUN(test_matches_python_reference);
    MT_RUN(test_double_clench_gap_boundary);
    MT_RUN(test_both_arms_300ms_latches_stop);
    MT_RUN(test_latch_needs_2s_relax_then_minus);
    MT_RUN(test_fault_stop_once_then_silent);
    return MT_REPORT();
}
