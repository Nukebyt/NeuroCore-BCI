/* ADS1299-4 driver (task card T4.1). Platform-free: all I/O goes through ads1299_hal_t,
 * so the whole driver runs in host tests against a simulated device.
 *
 * Datasheet: TI SBAS499C. Register/timing facts live in ads1299_regs.h with citations.
 * Not thread-safe; call from one task. No dynamic allocation.
 */
#ifndef ADS1299_H
#define ADS1299_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ads1299_regs.h"

typedef enum {
    ADS1299_OK = 0,
    ADS1299_ERR_ARG,    /* bad argument or config */
    ADS1299_ERR_ID,     /* ID register is not an ADS1299-4 */
    ADS1299_ERR_VERIFY, /* register read-back differs from what was written */
    ADS1299_ERR_SYNC,   /* status word does not start with 1100b */
    ADS1299_ERR_STATE,  /* call not valid in the current mode */
} ads1299_status_t;

typedef enum { ADS1299_PIN_PWDN, ADS1299_PIN_RESET, ADS1299_PIN_START } ads1299_pin_t;

typedef struct {
    void *ctx;
    /* Drive CS: asserted = true pulls CS low. */
    void (*cs)(void *ctx, bool asserted);
    /* Full-duplex transfer of n bytes while CS is asserted; rx may be NULL. */
    void (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n);
    /* Drive PWDN / RESET / START (true = high). */
    void (*pin)(void *ctx, ads1299_pin_t pin, bool high);
    void (*delay_us)(void *ctx, uint32_t us);
} ads1299_hal_t;

/* Output data rate, CONFIG1 DR[2:0] (DS §9.6.1.2) with fCLK = 2.048 MHz. */
typedef enum {
    ADS1299_DR_16K = 0,
    ADS1299_DR_8K = 1,
    ADS1299_DR_4K = 2,
    ADS1299_DR_2K = 3,
    ADS1299_DR_1K = 4,
    ADS1299_DR_500 = 5,
    ADS1299_DR_250 = 6,
} ads1299_rate_t;

typedef struct {
    uint32_t sclk_hz;             /* SPI clock, <= ADS1299_SCLK_MAX_HZ */
    ads1299_rate_t rate;          /* output data rate */
    uint8_t gain[ADS1299_NUM_CH]; /* PGA gain per channel: 1, 2, 4, 6, 8, 12 or 24 */
    bool srb1_reference;          /* MISC1.SRB1: all inverting inputs to SRB1 (REF electrode) */
    bool bias_enable;             /* CONFIG3: BIAS buffer on, BIASREF internal */
    uint8_t bias_sensp;           /* BIAS_SENSP channel mask (bit0 = ch1) */
    uint8_t bias_sensn;           /* BIAS_SENSN channel mask */
    bool lead_off_enable;         /* CONFIG4.PD_LOFF_COMP */
    uint8_t loff;                 /* raw LOFF register: COMP_TH, ILEAD_OFF, FLEAD_OFF (§9.6.1.5) */
    uint8_t loff_sensp;           /* LOFF_SENSP channel mask */
    uint8_t loff_sensn;           /* LOFF_SENSN channel mask */
    /* Extra wait before reset for VCAP1 to exceed 1.1 V (DS §11.1, Figure 25). Depends on
     * the VCAP1 capacitor; the driver always waits at least tPOR on top of this. */
    uint32_t vcap1_settle_ms;
} ads1299_config_t;

typedef struct {
    bool sync_ok;       /* status starts with 1100b */
    uint8_t loff_statp; /* lead-off, positive inputs (bit0 = ch1) */
    uint8_t loff_statn; /* lead-off, negative inputs */
    uint8_t gpio;       /* GPIO[7:4] */
    int32_t code[ADS1299_NUM_CH];
    float uv[ADS1299_NUM_CH];
} ads1299_frame_t;

typedef struct {
    const ads1299_hal_t *hal;
    ads1299_config_t cfg;
    bool rdatac;  /* device is in read-data-continuous mode */
    bool running; /* START pin high */
    uint8_t id;
    uint32_t byte_gap_us; /* inserted between bytes of multi-byte commands */
} ads1299_t;

/* NeuroCore Rev A montage defaults (ADR-0007). */
ads1299_config_t ads1299_default_config(void);

/* --- pure helpers (no I/O) -------------------------------------------------------------- */
int32_t ads1299_sign_extend24(uint32_t raw24);
/* GAINn[2:0] code for a PGA gain; returns false for unsupported gains. */
bool ads1299_gain_code(uint8_t gain, uint8_t *code);
/* Input-referred microvolts for a code: 1 LSB = (2 x VREF / gain) / 2^24 (DS §9.5.1 Eq. 8). */
float ads1299_code_to_uv(int32_t code, uint8_t gain);
/* Decode one RDATAC/RDATA frame (status + 4 channels). Returns ADS1299_ERR_SYNC if the
 * status header is not 1100b (out still filled). */
ads1299_status_t ads1299_decode_frame(const uint8_t raw[ADS1299_FRAME_BYTES],
                                      const uint8_t gain[ADS1299_NUM_CH], ads1299_frame_t *out);
/* Register image that ads1299_init writes for a config (index = register address). */
ads1299_status_t ads1299_config_registers(const ads1299_config_t *cfg,
                                          uint8_t regs[ADS1299_NUM_REGS]);

/* --- device operations ------------------------------------------------------------------ */
/* Power-up per DS §11.1 / Figure 67: PWDN+RESET high, wait max(tPOR, VCAP1), reset pulse,
 * SDATAC, check ID, enable internal reference and wait for it, write and verify all
 * registers, START high, RDATAC. Supplies must already be stable. */
ads1299_status_t ads1299_init(ads1299_t *dev, const ads1299_hal_t *hal,
                              const ads1299_config_t *cfg);
ads1299_status_t ads1299_command(ads1299_t *dev, uint8_t cmd);
ads1299_status_t ads1299_read_regs(ads1299_t *dev, uint8_t addr, uint8_t *data, uint8_t n);
ads1299_status_t ads1299_write_regs(ads1299_t *dev, uint8_t addr, const uint8_t *data, uint8_t n);
/* Read one frame after DRDY falls (RDATAC mode). */
ads1299_status_t ads1299_read_frame(ads1299_t *dev, ads1299_frame_t *out);
/* Switch all channels to the internal test signal (DS §9.6.1.3, Figure 67) or back. */
ads1299_status_t ads1299_set_test_signal(ads1299_t *dev, bool on);
/* START pin low/high (conversions stop/start). */
void ads1299_stop(ads1299_t *dev);
void ads1299_start(ads1299_t *dev);

#endif /* ADS1299_H */
