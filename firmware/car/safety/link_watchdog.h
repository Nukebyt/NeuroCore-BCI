/*
 * NIP link watchdog (docs/safety.md S1). Keep tests proving S1 when changing this.
 *
 * Expired when no valid frame has been fed for NIP_LINK_TIMEOUT_MS (300 ms), or never fed.
 * The timeout is the protocol constant, not a parameter, so configuration cannot weaken it.
 * Expiry latches until the next feed. Callers must check at least every few seconds so a
 * u32 time wrap (~49.7 days) can never make an old feed look fresh.
 */
#ifndef LINK_WATCHDOG_H
#define LINK_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool fed;
    uint32_t last_valid_ms;
} link_watchdog_t;

/* Starts expired: no motion until the first valid frame. */
void link_watchdog_init(link_watchdog_t *wd);

/* Call only for frames that decoded NIP_OK and passed the adapter's sequence check. */
void link_watchdog_feed(link_watchdog_t *wd, uint32_t now_ms);

/* True if expired at now_ms. Latches expiry. */
bool link_watchdog_expired(link_watchdog_t *wd, uint32_t now_ms);

#endif /* LINK_WATCHDOG_H */
