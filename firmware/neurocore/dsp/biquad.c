#include "biquad.h"

#include <string.h>

int biquad_init(biquad_cascade_t *f, const float (*sos)[6], size_t n_sections)
{
    if (n_sections > BIQUAD_MAX_SECTIONS) {
        return -1;
    }
    f->sos = sos;
    f->n_sections = n_sections;
    memset(f->z, 0, sizeof f->z);
    return 0;
}

float biquad_process(biquad_cascade_t *f, float x)
{
    for (size_t i = 0; i < f->n_sections; i++) {
        const float *c = f->sos[i];
        float y = c[0] * x + f->z[i][0];
        f->z[i][0] = c[1] * x - c[4] * y + f->z[i][1];
        f->z[i][1] = c[2] * x - c[5] * y;
        x = y;
    }
    return x;
}
