/*
 * NeuroCore Intent Protocol v0 codec (C11, no allocation, no I/O).
 * Normative spec: protocol/nip/spec.md. Python twin: nip.py. Both pass vectors.json.
 */
#ifndef NIP_H
#define NIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NIP_FRAME_LEN       16u
#define NIP_MAGIC           0x4E50u
#define NIP_VERSION         0u
#define NIP_LEVEL_MAX       3u
#define NIP_LINK_TIMEOUT_MS 300u /* spec §7, docs/safety.md S1. Do not raise. */
#define NIP_HEARTBEAT_HZ    25u

typedef enum {
    NIP_INTENT_STOP = 0,
    NIP_INTENT_HOLD = 1,
    NIP_INTENT_FWD = 2,
    NIP_INTENT_REV = 3,
    NIP_INTENT_LEFT = 4,
    NIP_INTENT_RIGHT = 5,
    NIP_INTENT_MODE_NEXT = 6,
    NIP_INTENT_AUX_A = 7,
    NIP_INTENT_AUX_B = 8,
    NIP_INTENT_MAX = NIP_INTENT_AUX_B
} nip_intent_t;

#define NIP_FLAG_CALIBRATED   (1u << 0)
#define NIP_FLAG_ARTIFACT     (1u << 1)
#define NIP_FLAG_CHANNEL_BAD  (1u << 2)
#define NIP_FLAG_LATCHED_STOP (1u << 3)
#define NIP_FLAG_TEST_MODE    (1u << 4)
#define NIP_FLAGS_DEFINED     0x1Fu

/* Codes 0..8 match spec §5 and nip.py Status. NIP_ERR_ARG is C-only. */
typedef enum {
    NIP_OK = 0,
    NIP_ERR_LEN = 1,
    NIP_ERR_MAGIC = 2,
    NIP_ERR_VERSION = 3,
    NIP_ERR_CRC = 4,
    NIP_ERR_INTENT = 5,
    NIP_ERR_LEVEL = 6,
    NIP_ERR_FLAGS = 7,
    NIP_ERR_RESERVED = 8,
    NIP_ERR_ARG = 9
} nip_status_t;

typedef struct {
    uint8_t seq;
    uint32_t t_ms;
    uint8_t intent; /* nip_intent_t */
    uint8_t level;
    uint8_t confidence;
    uint8_t flags; /* NIP_FLAG_* */
} nip_frame_t;

/* CRC-16/CCITT-FALSE (spec §4). */
uint16_t nip_crc16(const uint8_t *data, size_t len);

/* Refuses frames that would fail decode checks 5-8. Writes exactly NIP_FRAME_LEN bytes. */
nip_status_t nip_encode(const nip_frame_t *frame, uint8_t out[NIP_FRAME_LEN]);

/* Checks in spec §5 order and returns the first failure. *out is written only on NIP_OK. */
nip_status_t nip_decode(const uint8_t *buf, size_t len, nip_frame_t *out);

uint8_t nip_next_seq(uint8_t seq);

/* True if candidate is 1..127 steps after last, modulo 256 (spec §6). */
bool nip_seq_is_newer(uint8_t candidate, uint8_t last);

#ifdef __cplusplus
}
#endif

#endif /* NIP_H */
