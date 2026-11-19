/* COBS encode/decode (Cheshire & Baker), no allocation. Twin of host/serial_link.py. */
#ifndef COBS_H
#define COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Worst-case encoded size (without the 0x00 delimiter). */
#define COBS_MAX_ENCODED(n) ((n) + (n) / 254u + 1u)

/* Encodes `len` bytes into `out` (no delimiter). Returns encoded length, or 0 if out_cap is
 * too small. */
size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap);

/* Decodes one frame (without delimiter). False on malformed input or if out_cap is too small. */
bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap, size_t *out_len);

#endif /* COBS_H */
