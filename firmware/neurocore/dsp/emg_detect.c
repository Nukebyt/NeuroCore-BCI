#include "emg_detect.h"

#include <math.h>
#include <string.h>

void emg_envelope_init(emg_envelope_t *e)
{
    memset(e, 0, sizeof *e);
}

bool emg_envelope_push(emg_envelope_t *e, float x_uv, float *env_uv)
{
    e->buf[e->head] = x_uv;
    e->head = (e->head + 1u) % EMG_WIN;
    if (e->filled < EMG_WIN) {
        e->filled++;
    }
    e->since_hop++;
    if (e->since_hop < EMG_HOP || e->filled < EMG_WIN) {
        return false;
    }
    e->since_hop = 0;
    /* Exact sum each hop (125 MACs per 25 ms) instead of a drifting running sum. */
    float acc = 0.0f;
    for (size_t i = 0; i < EMG_WIN; i++) {
        acc += e->buf[i] * e->buf[i];
    }
    *env_uv = sqrtf(acc / (float)EMG_WIN);
    return true;
}

void emg_detector_init(emg_detector_t *d, float on_uv, float off_uv, uint32_t debounce_ms)
{
    memset(d, 0, sizeof *d);
    d->on_uv = on_uv;
    d->off_uv = off_uv;
    d->debounce_ms = debounce_ms;
}

bool emg_detector_update(emg_detector_t *d, float env_uv, uint32_t t_ms)
{
    bool wants = d->active ? (env_uv >= d->off_uv) : (env_uv >= d->on_uv);
    if (wants == d->active) {
        d->pending = false;
    } else if (!d->pending) {
        d->pending = true;
        d->pending_since_ms = t_ms;
    }
    if (d->pending && (uint32_t)(t_ms - d->pending_since_ms) >= d->debounce_ms) {
        d->active = wants;
        d->pending = false;
    }
    return d->active;
}
