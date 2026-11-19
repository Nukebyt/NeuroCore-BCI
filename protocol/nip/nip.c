/* NeuroCore Intent Protocol v0 codec. Normative spec: protocol/nip/spec.md. */
#include "nip.h"

/* Byte offsets (spec §1). */
enum {
    OFF_MAGIC = 0,
    OFF_VERSION = 2,
    OFF_SEQ = 3,
    OFF_T_MS = 4,
    OFF_INTENT = 8,
    OFF_LEVEL = 9,
    OFF_CONFIDENCE = 10,
    OFF_FLAGS = 11,
    OFF_RESERVED = 12,
    OFF_CRC = 14,
    CRC_COVERED_LEN = 14
};

static uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static nip_status_t field_status(uint8_t intent, uint8_t level, uint8_t flags, uint16_t reserved)
{
    if (intent > (uint8_t)NIP_INTENT_MAX) {
        return NIP_ERR_INTENT;
    }
    if (level > NIP_LEVEL_MAX) {
        return NIP_ERR_LEVEL;
    }
    if ((flags & (uint8_t)~NIP_FLAGS_DEFINED) != 0u) {
        return NIP_ERR_FLAGS;
    }
    if (reserved != 0u) {
        return NIP_ERR_RESERVED;
    }
    return NIP_OK;
}

uint16_t nip_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (int b = 0; b < 8; b++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

nip_status_t nip_encode(const nip_frame_t *frame, uint8_t out[NIP_FRAME_LEN])
{
    if (frame == NULL || out == NULL) {
        return NIP_ERR_ARG;
    }
    nip_status_t st = field_status(frame->intent, frame->level, frame->flags, 0u);
    if (st != NIP_OK) {
        return st;
    }
    put_u16le(&out[OFF_MAGIC], (uint16_t)NIP_MAGIC);
    out[OFF_VERSION] = (uint8_t)NIP_VERSION;
    out[OFF_SEQ] = frame->seq;
    put_u32le(&out[OFF_T_MS], frame->t_ms);
    out[OFF_INTENT] = frame->intent;
    out[OFF_LEVEL] = frame->level;
    out[OFF_CONFIDENCE] = frame->confidence;
    out[OFF_FLAGS] = frame->flags;
    put_u16le(&out[OFF_RESERVED], 0u);
    put_u16le(&out[OFF_CRC], nip_crc16(out, CRC_COVERED_LEN));
    return NIP_OK;
}

nip_status_t nip_decode(const uint8_t *buf, size_t len, nip_frame_t *out)
{
    if (buf == NULL || out == NULL) {
        return NIP_ERR_ARG;
    }
    if (len != NIP_FRAME_LEN) {
        return NIP_ERR_LEN;
    }
    if (get_u16le(&buf[OFF_MAGIC]) != NIP_MAGIC) {
        return NIP_ERR_MAGIC;
    }
    if (buf[OFF_VERSION] != NIP_VERSION) {
        return NIP_ERR_VERSION;
    }
    if (get_u16le(&buf[OFF_CRC]) != nip_crc16(buf, CRC_COVERED_LEN)) {
        return NIP_ERR_CRC;
    }
    nip_status_t st = field_status(buf[OFF_INTENT], buf[OFF_LEVEL], buf[OFF_FLAGS],
                                   get_u16le(&buf[OFF_RESERVED]));
    if (st != NIP_OK) {
        return st;
    }
    out->seq = buf[OFF_SEQ];
    out->t_ms = get_u32le(&buf[OFF_T_MS]);
    out->intent = buf[OFF_INTENT];
    out->level = buf[OFF_LEVEL];
    out->confidence = buf[OFF_CONFIDENCE];
    out->flags = buf[OFF_FLAGS];
    return NIP_OK;
}

uint8_t nip_next_seq(uint8_t seq)
{
    return (uint8_t)(seq + 1u);
}

bool nip_seq_is_newer(uint8_t candidate, uint8_t last)
{
    uint8_t diff = (uint8_t)(candidate - last);
    return diff >= 1u && diff <= 127u;
}
