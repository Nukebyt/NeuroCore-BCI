/* ADS131M04 driver (task card T4.2). Platform-free: all I/O goes through ads131m04_hal_t.
 *
 * Datasheet: TI SBAS890D; facts and citations in ads131m04_regs.h.
 * The device answers each command in the *next* SPI frame (§8.5.1.7); the driver hides that
 * pipeline. 24-bit words, input CRC off, output CRC always checked. No dynamic allocation.
 * CLKIN (ESP32 LEDC on NeuroCore) must be running before ads131m04_init().
 */
#ifndef ADS131M04_H
#define ADS131M04_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ads131m04_regs.h"

typedef enum {
    ADS131M04_OK = 0,
    ADS131M04_ERR_ARG,    /* bad argument or config */
    ADS131M04_ERR_ID,     /* ID register is not an ADS131M04 */
    ADS131M04_ERR_VERIFY, /* register read-back differs from what was written */
    ADS131M04_ERR_ACK,    /* unexpected command response (WREG / RESET acknowledgement) */
    ADS131M04_ERR_CRC,    /* output CRC mismatch */
    ADS131M04_ERR_STATE,  /* driver not initialised */
} ads131m04_status_t;

typedef struct {
    void *ctx;
    void (*cs)(void *ctx, bool asserted); /* asserted = CS low */
    void (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n);
    void (*sync_reset)(void *ctx, bool high); /* SYNC/RESET pin */
    void (*delay_us)(void *ctx, uint32_t us);
} ads131m04_hal_t;

/* CLOCK.OSR[2:0] codes (§8.6.4). Code 111b is excluded: the register table lists 16256 while
 * Table 8-2 lists 16384, so its data rate is ambiguous. */
typedef enum {
    ADS131M04_OSR_128 = 0,
    ADS131M04_OSR_256 = 1,
    ADS131M04_OSR_512 = 2,
    ADS131M04_OSR_1024 = 3,
    ADS131M04_OSR_2048 = 4,
    ADS131M04_OSR_4096 = 5,
    ADS131M04_OSR_8192 = 6,
} ads131m04_osr_t;

typedef struct {
    uint32_t sclk_hz;  /* <= ADS131M04_SCLK_MAX_HZ */
    uint32_t clkin_hz; /* CLKIN actually supplied, 0.3..8.4 MHz */
    ads131m04_osr_t osr;
    uint8_t gain[ADS131M04_NUM_CH]; /* 1, 2, 4, 8, 16, 32, 64 or 128 */
    uint8_t ch_enable;              /* bit n = channel n */
    uint8_t dcblock;                /* THRSHLD_LSB.DCBLOCK[3:0], 0 = off */
} ads131m04_config_t;

typedef struct {
    uint16_t status; /* STATUS register (NULL-command response) */
    bool crc_ok;
    int32_t code[ADS131M04_NUM_CH];
    float uv[ADS131M04_NUM_CH];
} ads131m04_frame_t;

typedef struct {
    const ads131m04_hal_t *hal;
    ads131m04_config_t cfg;
    bool ready;
    uint16_t id;
    uint32_t crc_errors;
} ads131m04_t;

/* NeuroCore Rev A defaults (ADR-0008): ch0/ch1 EMG, ch2/ch3 shadow EEG. */
ads131m04_config_t ads131m04_default_config(void);

/* --- pure helpers ------------------------------------------------------------------------ */
uint16_t ads131m04_crc16(const uint8_t *data, size_t n);
int32_t ads131m04_sign_extend24(uint32_t raw24);
bool ads131m04_gain_code(uint8_t gain, uint8_t *code);
/* 1 LSB = (2.4 V / gain) / 2^24 (§8.5.1.9 Eq. 10). */
float ads131m04_code_to_uv(int32_t code, uint8_t gain);
/* Output data rate = fMOD / OSR with fMOD = fCLKIN / 2 (§8.3.3, Table 8-2). */
double ads131m04_data_rate_hz(uint32_t clkin_hz, ads131m04_osr_t osr);
/* Decode an 18-byte NULL frame. Returns ADS131M04_ERR_CRC on CRC mismatch (out still filled). */
ads131m04_status_t ads131m04_decode_frame(const uint8_t raw[ADS131M04_FRAME_BYTES],
                                          const uint8_t gain[ADS131M04_NUM_CH],
                                          ads131m04_frame_t *out);
/* Register values written at init: MODE, CLOCK, GAIN1, THRSHLD_LSB, CHn_CFG. */
ads131m04_status_t ads131m04_config_registers(const ads131m04_config_t *cfg, uint16_t *mode,
                                              uint16_t *clock, uint16_t *gain1,
                                              uint16_t *thrshld_lsb);

/* --- device operations ------------------------------------------------------------------ */
/* Wait tPOR, hardware reset via SYNC/RESET, check ID, write + verify registers, clear the
 * RESET flag, then a SYNC pulse to empty the FIFOs (§8.5.1.9.1, Figure 8-20). */
ads131m04_status_t ads131m04_init(ads131m04_t *dev, const ads131m04_hal_t *hal,
                                  const ads131m04_config_t *cfg);
ads131m04_status_t ads131m04_read_reg(ads131m04_t *dev, uint8_t addr, uint16_t *value);
ads131m04_status_t ads131m04_write_reg(ads131m04_t *dev, uint8_t addr, uint16_t value);
/* Read one sample set after DRDY falls. */
ads131m04_status_t ads131m04_read_frame(ads131m04_t *dev, ads131m04_frame_t *out);
/* SYNC pulse: realigns conversions and clears the FIFOs (§8.5.2). */
void ads131m04_sync(ads131m04_t *dev);
/* Route all channels to the positive DC test signal (§8.3.9) or back to the inputs. */
ads131m04_status_t ads131m04_set_test_signal(ads131m04_t *dev, bool on);

#endif /* ADS131M04_H */
