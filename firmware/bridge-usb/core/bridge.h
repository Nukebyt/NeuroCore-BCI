/*
 * USB-serial -> ESP-NOW bridge core (ADR-0003). Pure logic; the ESP-IDF glue calls
 * bridge_rx_bytes() with bytes from USB CDC and transmits whatever `send` is given.
 *
 * The bridge never generates, repeats or buffers NIP frames: if the host goes quiet, the
 * car's 300 ms watchdog (docs/safety.md S1) fires.
 */
#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cobs.h"
#include "nip.h"

#define BRIDGE_TYPE_NIP    0x01u
#define BRIDGE_TYPE_STATUS 0x02u
#define BRIDGE_MAX_PAYLOAD 64u
#define BRIDGE_MAX_RAW     (1u + BRIDGE_MAX_PAYLOAD)
#define BRIDGE_MAX_ENCODED COBS_MAX_ENCODED(BRIDGE_MAX_RAW)

typedef void (*bridge_send_fn)(const uint8_t frame[NIP_FRAME_LEN], void *ctx);

typedef struct {
    uint8_t buf[BRIDGE_MAX_ENCODED];
    size_t len;
    bool overflow;
    uint32_t forwarded; /* valid NIP frames handed to send() */
    uint32_t rejected;  /* well-framed messages that were not valid NIP */
    uint32_t rx_errors; /* framing errors (bad COBS, oversized, empty) */
} bridge_t;

void bridge_init(bridge_t *b);

/* Feed raw serial bytes. Calls send() once per valid NIP frame, in order. */
void bridge_rx_bytes(bridge_t *b, const uint8_t *data, size_t len, bridge_send_fn send, void *ctx);

#endif /* BRIDGE_H */
