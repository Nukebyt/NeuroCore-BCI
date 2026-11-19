/* Biquad cascade (direct form II transposed, float32), matching scipy.signal.sosfilt. */
#ifndef BIQUAD_H
#define BIQUAD_H

#include <stddef.h>

#define BIQUAD_MAX_SECTIONS 16u

typedef struct {
    const float (*sos)[6]; /* rows b0 b1 b2 a0 a1 a2, a0 == 1 (see dsp_coeffs.h) */
    size_t n_sections;
    float z[BIQUAD_MAX_SECTIONS][2];
} biquad_cascade_t;

/* Returns 0 on success, -1 if n_sections exceeds BIQUAD_MAX_SECTIONS. */
int biquad_init(biquad_cascade_t *f, const float (*sos)[6], size_t n_sections);
float biquad_process(biquad_cascade_t *f, float x);

#endif /* BIQUAD_H */
