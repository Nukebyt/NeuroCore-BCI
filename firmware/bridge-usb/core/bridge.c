#include "bridge.h"

#include <string.h>

void bridge_init(bridge_t *b)
{
    memset(b, 0, sizeof *b);
}

static void handle_frame(bridge_t *b, bridge_send_fn send, void *ctx)
{
    uint8_t raw[BRIDGE_MAX_RAW];
    size_t raw_len = 0;
    if (b->overflow || !cobs_decode(b->buf, b->len, raw, sizeof raw, &raw_len) || raw_len == 0u) {
        b->rx_errors++;
        return;
    }
    nip_frame_t frame;
    if (raw[0] != BRIDGE_TYPE_NIP || raw_len - 1u != NIP_FRAME_LEN ||
        nip_decode(&raw[1], NIP_FRAME_LEN, &frame) != NIP_OK) {
        b->rejected++;
        return;
    }
    b->forwarded++;
    send(&raw[1], ctx);
}

void bridge_rx_bytes(bridge_t *b, const uint8_t *data, size_t len, bridge_send_fn send, void *ctx)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        if (byte != 0u) {
            if (b->len < sizeof b->buf) {
                b->buf[b->len++] = byte;
            } else {
                b->overflow = true;
            }
            continue;
        }
        if (b->len > 0u || b->overflow) {
            handle_frame(b, send, ctx);
        }
        b->len = 0;
        b->overflow = false;
    }
}
