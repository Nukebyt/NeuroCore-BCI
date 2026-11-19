/* C EMG front end vs the Python reference (vectors from `python -m ml.export.gen_dsp`). */
#include <math.h>
#include <stdio.h>

#include "biquad.h"
#include "dsp_coeffs.h"
#include "dsp_vectors.h"
#include "emg_detect.h"
#include "minitest.h"

static uint32_t rng_state;
static uint32_t rng_next(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static float input_sample(uint32_t n)
{
    double u = (double)(rng_next() >> 8) / (double)(1u << 24) * 2.0 - 1.0;
    bool burst = (n >= 500u && n < 1200u) || (n >= 2000u && n < 2600u);
    return (float)(u * (burst ? 80.0 : 5.0));
}

static void test_emg_chain_matches_python(void)
{
    biquad_cascade_t f;
    MT_CHECK_EQ(biquad_init(&f, EMG_SOS_1KHZ, EMG_SOS_1KHZ_N_SECTIONS), 0);
    emg_envelope_t env;
    emg_envelope_init(&env);
    emg_detector_t det;
    emg_detector_init(&det, DSP_TEST_ON_UV, DSP_TEST_OFF_UV, 80u);
    rng_state = 12345u;

    float max_filter_err = 0.0f;
    float max_env_err = 0.0f;
    size_t n_env = 0;
    size_t n_trans = 0;
    bool state = false;
    for (uint32_t n = 0; n < DSP_N_SAMPLES; n++) {
        float y = biquad_process(&f, input_sample(n));
        if (n % DSP_CHECK_EVERY == 0u) {
            float err = fabsf(y - DSP_FILTER_CHECKS[n / DSP_CHECK_EVERY]);
            max_filter_err = err > max_filter_err ? err : max_filter_err;
        }
        float e;
        if (emg_envelope_push(&env, y, &e)) {
            MT_CHECK(n_env < DSP_N_ENVELOPE);
            if (n_env < DSP_N_ENVELOPE) {
                float err = fabsf(e - DSP_ENVELOPE[n_env]);
                max_env_err = err > max_env_err ? err : max_env_err;
            }
            n_env++;
            if (emg_detector_update(&det, e, n) != state) {
                state = det.active;
                MT_CHECK(n_trans < DSP_N_TRANSITIONS);
                if (n_trans < DSP_N_TRANSITIONS) {
                    MT_CHECK_EQ(n, DSP_TRANSITIONS[n_trans][0]);
                    MT_CHECK_EQ(state, DSP_TRANSITIONS[n_trans][1]);
                }
                n_trans++;
            }
        }
    }
    printf("  max |filter err| %.2e uV, max |envelope err| %.2e uV\n", (double)max_filter_err,
           (double)max_env_err);
    MT_CHECK_EQ(n_env, DSP_N_ENVELOPE);
    MT_CHECK_EQ(n_trans, DSP_N_TRANSITIONS);
    MT_CHECK(max_filter_err < 1e-2f); /* float32 vs float64 over 13 sections */
    MT_CHECK(max_env_err < 1e-2f);
}

static void test_biquad_rejects_too_many_sections(void)
{
    biquad_cascade_t f;
    MT_CHECK_EQ(biquad_init(&f, EMG_SOS_1KHZ, BIQUAD_MAX_SECTIONS + 1u), -1);
}

int main(void)
{
    MT_RUN(test_emg_chain_matches_python);
    MT_RUN(test_biquad_rejects_too_many_sections);
    return MT_REPORT();
}
