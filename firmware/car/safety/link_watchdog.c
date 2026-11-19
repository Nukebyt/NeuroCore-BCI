/* NIP link watchdog (docs/safety.md S1). SAFETY-CRITICAL. */
#include "link_watchdog.h"

#include "nip.h"

void link_watchdog_init(link_watchdog_t *wd)
{
    wd->fed = false;
    wd->last_valid_ms = 0;
}

void link_watchdog_feed(link_watchdog_t *wd, uint32_t now_ms)
{
    wd->fed = true;
    wd->last_valid_ms = now_ms;
}

bool link_watchdog_expired(link_watchdog_t *wd, uint32_t now_ms)
{
    /* Unsigned subtraction is wrap-safe. If time appears to run backwards the result is
     * huge, which also counts as expired (fail safe). */
    if (wd->fed && (uint32_t)(now_ms - wd->last_valid_ms) >= NIP_LINK_TIMEOUT_MS) {
        wd->fed = false;
    }
    return !wd->fed;
}
