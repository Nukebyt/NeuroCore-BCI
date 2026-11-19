/* Simulated ADS1299-4 for host tests. Behaviour and timing rules from TI SBAS499C:
 *   - register file and reset defaults (§9.6 Table 11; CH5..8 read 0 on the -4)
 *   - wakes up in RDATAC; RREG/WREG/other commands ignored until SDATAC (§9.5.3.7)
 *   - reset only after tPOR from power-up; RESET low >= 2 tCLK; 18 tCLK before the next
 *     command (§11.1 Table 30, §9.4.2)
 *   - multi-byte commands: >= 4 tCLK between the ends of consecutive bytes (§9.5.3.1)
 *   - tSCCS >= 4 tCLK before CS high, tCSH >= 2 tCLK CS high (§7.6)
 *   - START must not rise before the internal reference has had 150 ms (§7.5)
 * Every broken rule increments `violations` and records a message.
 */
#ifndef FAKE_ADS1299_H
#define FAKE_ADS1299_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ads1299.h"

typedef struct {
    /* configuration */
    uint32_t sclk_hz;
    uint32_t tclk_ns; /* the fake's actual master clock period */
    uint8_t id;
    uint8_t stuck_mask[ADS1299_NUM_REGS]; /* bits that ignore writes (fault injection) */
    uint8_t frame[ADS1299_FRAME_BYTES];   /* bytes returned for a data read */

    /* state */
    uint64_t t_ns;
    bool cs_low;
    bool pwdn_high, reset_high, start_high;
    bool powered;
    uint64_t t_power_ns, t_reset_low_ns, t_reset_done_ns, t_refbuf_on_ns, t_cs_high_ns;
    uint64_t t_last_byte_end_ns;
    bool have_reset, refbuf_on;
    bool rdatac;
    uint8_t regs[ADS1299_NUM_REGS];

    /* per-transaction parser */
    int pos; /* byte index within the CS-low transaction */
    uint8_t op, count;
    bool data_read; /* transaction is clocking out conversion data */

    /* observations */
    int violations;
    char last_violation[160];
    int ignored_cmds;
    int resets, frames_read, wreg_count;
    int start_rises;
} fake_ads1299_t;

void fake_ads1299_init(fake_ads1299_t *f, uint32_t sclk_hz);
/* HAL bound to the fake. */
ads1299_hal_t fake_ads1299_hal(fake_ads1299_t *f);

#endif /* FAKE_ADS1299_H */
