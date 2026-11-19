#include "cobs.h"

size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap)
{
    if (out_cap < COBS_MAX_ENCODED(len)) {
        return 0;
    }
    size_t o = 1;        /* next write position */
    size_t code_idx = 0; /* position of the current block's code byte */
    bool block_open = true;
    uint8_t code = 1;

    for (size_t i = 0; i < len; i++) {
        if (in[i] == 0u) {
            out[code_idx] = code;
            code_idx = o++;
            code = 1;
            continue;
        }
        out[o++] = in[i];
        code++;
        if (code == 0xFFu) {
            out[code_idx] = code;
            code = 1;
            if (i + 1u < len) {
                code_idx = o++;
            } else {
                block_open = false;
            }
        }
    }
    if (block_open) {
        out[code_idx] = code;
    }
    return o;
}

bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (len == 0u) {
        return false;
    }
    size_t i = 0;
    size_t o = 0;
    while (i < len) {
        uint8_t code = in[i++];
        if (code == 0u) {
            return false;
        }
        size_t end = i + (size_t)code - 1u;
        if (end > len) {
            return false;
        }
        for (; i < end; i++) {
            if (in[i] == 0u || o >= out_cap) {
                return false;
            }
            out[o++] = in[i];
        }
        if (code != 0xFFu && i < len) {
            if (o >= out_cap) {
                return false;
            }
            out[o++] = 0u;
        }
    }
    *out_len = o;
    return true;
}
