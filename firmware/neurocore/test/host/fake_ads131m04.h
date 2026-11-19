/* Simulated ADS131M04 for host tests. Behaviour and rules from TI SBAS890D:
 *   - frames of 24-bit words; command in word 0 of DIN; the response to a command appears in
 *     word 0 of the NEXT frame (§8.5.1.7); DOUT = response, 4 channels, CRC (CCITT, FFFFh)
 *   - RREG of one register answers with the value; WREG answers 010a aaaa ammm mmmm;
 *     RESET answers FF24h and needs a full frame (§8.5.1.10, §8.5.1.11)
 *   - with ADCs enabled every frame must be full length (§8.5.1.11)
 *   - SPI ignored before tPOR and within tREGACQ of a reset (§8.4.1)
 *   - SYNC/RESET low >= 2048 tCLKIN resets; 1..2047 tCLKIN synchronizes (§6.6, §8.5.2)
 *   - whole frame within 32768 tCLKIN while MODE.TIMEOUT = 1 (§8.5.1.7)
 * Every broken rule increments `violations`.
 */
#ifndef FAKE_ADS131M04_H
#define FAKE_ADS131M04_H

#include <stdbool.h>
#include <stdint.h>

#include "ads131m04.h"

#define FAKE_M04_MAX_FRAME_BYTES 64u

typedef struct {
    /* configuration */
    uint32_t sclk_hz, clkin_hz;
    uint16_t id;
    uint16_t stuck_mask[ADS131M04_NUM_REGS];
    int32_t ch_code[ADS131M04_NUM_CH];
    bool corrupt_crc;
    bool bad_wreg_ack; /* answer WREG with a wrong register count */

    /* state */
    uint64_t t_ns, t_ready_ns, t_sync_low_ns, t_cs_low_ns;
    bool cs_low, sync_high;
    uint16_t regs[ADS131M04_NUM_REGS];
    uint16_t next_response;
    bool have_response;                       /* else the next frame answers with STATUS */
    uint8_t rx_buf[FAKE_M04_MAX_FRAME_BYTES]; /* DIN of the current frame */
    uint8_t tx_buf[FAKE_M04_MAX_FRAME_BYTES]; /* DOUT of the current frame */
    size_t pos;

    /* observations */
    int violations;
    char last_violation[160];
    int resets, syncs, frames, short_frames;
} fake_ads131m04_t;

void fake_ads131m04_init(fake_ads131m04_t *f, uint32_t sclk_hz, uint32_t clkin_hz);
ads131m04_hal_t fake_ads131m04_hal(fake_ads131m04_t *f);

#endif /* FAKE_ADS131M04_H */
