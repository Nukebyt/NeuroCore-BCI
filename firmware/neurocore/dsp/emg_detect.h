/*
 * EMG envelope + hysteresis detector (ROADMAP §5.2), C port of ml/features/emg.py.
 * Envelope: RMS over EMG_WIN samples, one value every EMG_HOP samples (125 ms / 25 ms at
 * 1 kHz). Detector: ON when envelope >= on for debounce_ms, OFF when < off for debounce_ms.
 */
#ifndef EMG_DETECT_H
#define EMG_DETECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMG_WIN 125u
#define EMG_HOP 25u

typedef struct {
    float buf[EMG_WIN];
    size_t head;
    size_t filled;
    size_t since_hop;
} emg_envelope_t;

typedef struct {
    float on_uv;
    float off_uv;
    uint32_t debounce_ms;
    bool active;
    bool pending;
    uint32_t pending_since_ms;
} emg_detector_t;

void emg_envelope_init(emg_envelope_t *e);
/* Feed one filtered sample. Returns true and sets *env_uv when a hop completes. */
bool emg_envelope_push(emg_envelope_t *e, float x_uv, float *env_uv);

void emg_detector_init(emg_detector_t *d, float on_uv, float off_uv, uint32_t debounce_ms);
bool emg_detector_update(emg_detector_t *d, float env_uv, uint32_t t_ms);

#endif /* EMG_DETECT_H */
