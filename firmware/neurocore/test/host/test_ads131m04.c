/* ADS131M04 driver tests (task card T4.2) against a simulated device (fake_ads131m04.c).
 * Expected numbers are hand-computed from TI SBAS890D; see comments. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ads131m04.h"
#include "fake_ads131m04.h"
#include "minitest.h"

static bool near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

static void report(const fake_ads131m04_t *f)
{
    if (f->violations) {
        fprintf(stderr, "violation: %s\n", f->last_violation);
    }
}

/* --- pure helpers ------------------------------------------------------------------------ */

static void test_crc16(void)
{
    /* CCITT polynomial 1021h, seed FFFFh (§8.3.12): standard check value for "123456789"
     * with this polynomial and seed is 29B1h. Empty input returns the seed. */
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    MT_CHECK_EQ(ads131m04_crc16(check, sizeof check), 0x29B1);
    MT_CHECK_EQ(ads131m04_crc16(check, 0), 0xFFFF);
}

static void test_sign_extend(void)
{
    /* Table 8-10. */
    MT_CHECK_EQ(ads131m04_sign_extend24(0x7FFFFFu), 8388607);
    MT_CHECK_EQ(ads131m04_sign_extend24(0x000001u), 1);
    MT_CHECK_EQ(ads131m04_sign_extend24(0xFFFFFFu), -1);
    MT_CHECK_EQ(ads131m04_sign_extend24(0x800000u), -8388608);
}

static void test_gain_codes(void)
{
    /* §8.6.5: 000b = 1 ... 111b = 128. */
    const uint8_t gains[] = {1, 2, 4, 8, 16, 32, 64, 128};
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t code = 0xFF;
        MT_CHECK(ads131m04_gain_code(gains[i], &code));
        MT_CHECK_EQ(code, i);
    }
    uint8_t code;
    MT_CHECK(!ads131m04_gain_code(3, &code));
    MT_CHECK(!ads131m04_gain_code(0, &code));
    MT_CHECK(!ads131m04_gain_code(255, &code));
}

static void test_scaling(void)
{
    /* Eq. 10: 1 LSB = 2.4 V / gain / 2^24.
     * gain 1: 2.4e6 uV / 16777216 = 0.1430511475 uV; gain 4: 0.0357627869 uV. */
    MT_CHECK(near(ads131m04_code_to_uv(1, 1), 0.1430511475, 1e-8));
    MT_CHECK(near(ads131m04_code_to_uv(1, 4), 0.0357627869, 1e-8));
    /* +FS at gain 1: 8388607 LSB = 1199999.86 uV (FSR = 1.2 V). */
    MT_CHECK(near(ads131m04_code_to_uv(8388607, 1), 1199999.86, 0.2));
    MT_CHECK(near(ads131m04_code_to_uv(-8388608, 4), -300000.0, 0.05));
}

static void test_data_rate(void)
{
    /* Table 8-2 (HR, fCLKIN 8.192 MHz): OSR 4096 -> 1 kSPS, OSR 1024 -> 4 kSPS.
     * NeuroCore drives CLKIN at 8 MHz: 8e6 / 2 / 4096 = 976.5625 SPS. */
    MT_CHECK(near(ads131m04_data_rate_hz(8192000u, ADS131M04_OSR_4096), 1000.0, 1e-9));
    MT_CHECK(near(ads131m04_data_rate_hz(8192000u, ADS131M04_OSR_1024), 4000.0, 1e-9));
    MT_CHECK(near(ads131m04_data_rate_hz(8000000u, ADS131M04_OSR_4096), 976.5625, 1e-9));
}

static void test_config_registers(void)
{
    ads131m04_config_t c = ads131m04_default_config();
    uint16_t mode, clock, gain1, thr;
    MT_CHECK_EQ(ads131m04_config_registers(&c, &mode, &clock, &gain1, &thr), ADS131M04_OK);
    /* MODE: WLENGTH 01b (24 bit), TIMEOUT 1, RESET 0 (clears STATUS.RESET), CCITT CRC,
     * input CRC off, DRDY on most lagging channel, active-low level: 0000 0001 0001 0000. */
    MT_CHECK_EQ(mode, 0x0110);
    /* CLOCK: all four channels on (0F00h), OSR 4096 = 101b << 2 (14h), HR mode (02h). */
    MT_CHECK_EQ(clock, 0x0F16);
    /* GAIN1: gain 4 = 010b on every channel. */
    MT_CHECK_EQ(gain1, 0x2222);
    MT_CHECK_EQ(thr, 0x0000);

    c.gain[1] = 3;
    MT_CHECK_EQ(ads131m04_config_registers(&c, &mode, &clock, &gain1, &thr), ADS131M04_ERR_ARG);
    c = ads131m04_default_config();
    c.osr = (ads131m04_osr_t)7;
    MT_CHECK_EQ(ads131m04_config_registers(&c, &mode, &clock, &gain1, &thr), ADS131M04_ERR_ARG);
    c = ads131m04_default_config();
    c.ch_enable = 0x10;
    MT_CHECK_EQ(ads131m04_config_registers(&c, &mode, &clock, &gain1, &thr), ADS131M04_ERR_ARG);
    c = ads131m04_default_config();
    c.dcblock = 16;
    MT_CHECK_EQ(ads131m04_config_registers(&c, &mode, &clock, &gain1, &thr), ADS131M04_ERR_ARG);
}

static void build_frame(uint8_t raw[ADS131M04_FRAME_BYTES], uint16_t status, const int32_t c[4])
{
    memset(raw, 0, ADS131M04_FRAME_BYTES);
    raw[0] = (uint8_t)(status >> 8);
    raw[1] = (uint8_t)status;
    for (unsigned ch = 0; ch < 4; ch++) {
        uint32_t v = (uint32_t)c[ch] & 0xFFFFFFu;
        raw[3 + 3 * ch] = (uint8_t)(v >> 16);
        raw[4 + 3 * ch] = (uint8_t)(v >> 8);
        raw[5 + 3 * ch] = (uint8_t)v;
    }
    uint16_t crc = ads131m04_crc16(raw, 15);
    raw[15] = (uint8_t)(crc >> 8);
    raw[16] = (uint8_t)crc;
}

static void test_decode_frame(void)
{
    /* 0.1 mV at gain 4 = 100 / 0.0357627869 = 2796.2 -> 2796 codes. */
    const int32_t codes[4] = {2796, -2796, 8388607, -1};
    const uint8_t gain[4] = {4, 4, 1, 4};
    uint8_t raw[ADS131M04_FRAME_BYTES];
    build_frame(raw, 0x010F, codes);
    ads131m04_frame_t f;
    MT_CHECK_EQ(ads131m04_decode_frame(raw, gain, &f), ADS131M04_OK);
    MT_CHECK(f.crc_ok);
    MT_CHECK_EQ(f.status, 0x010F);
    MT_CHECK_EQ(f.code[0], 2796);
    MT_CHECK_EQ(f.code[1], -2796);
    MT_CHECK_EQ(f.code[2], 8388607);
    MT_CHECK_EQ(f.code[3], -1);
    MT_CHECK(near(f.uv[0], 99.99, 0.01));
    MT_CHECK(near(f.uv[2], 1199999.86, 0.2));

    raw[7] ^= 0x01; /* one data bit flipped */
    MT_CHECK_EQ(ads131m04_decode_frame(raw, gain, &f), ADS131M04_ERR_CRC);
    MT_CHECK(!f.crc_ok);
}

/* --- device sequences ---------------------------------------------------------------- */

static ads131m04_status_t init_with(fake_ads131m04_t *f, ads131m04_t *dev, ads131m04_hal_t *hal,
                                    uint32_t sclk_hz, uint32_t clkin_hz)
{
    fake_ads131m04_init(f, sclk_hz, clkin_hz);
    *hal = fake_ads131m04_hal(f);
    ads131m04_config_t c = ads131m04_default_config();
    c.sclk_hz = sclk_hz;
    c.clkin_hz = clkin_hz;
    return ads131m04_init(dev, hal, &c);
}

static void check_regs(const fake_ads131m04_t *f)
{
    MT_CHECK_EQ(f->regs[ADS131M04_REG_MODE], 0x0110);
    MT_CHECK_EQ(f->regs[ADS131M04_REG_CLOCK], 0x0F16);
    MT_CHECK_EQ(f->regs[ADS131M04_REG_GAIN1], 0x2222);
    MT_CHECK_EQ(f->regs[ADS131M04_REG_THRSHLD_LSB], 0x0000);
    for (unsigned ch = 0; ch < 4; ch++) {
        MT_CHECK_EQ(f->regs[ADS131M04_REG_CH0_CFG + 5 * ch] & ADS131M04_MUX_MASK,
                    ADS131M04_MUX_NORMAL);
    }
}

static void test_init(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 8000000u, 8000000u), ADS131M04_OK);
    report(&f);
    MT_CHECK_EQ(f.violations, 0);
    MT_CHECK_EQ(f.resets, 1); /* SYNC/RESET held low >= 2048 tCLKIN */
    MT_CHECK_EQ(f.syncs, 1);  /* FIFO clear at the end (§8.5.1.9.1) */
    MT_CHECK_EQ(f.short_frames, 0);
    MT_CHECK(dev.ready);
    MT_CHECK_EQ(dev.id & 0xFF00, 0x2400);
    check_regs(&f);
}

static void test_init_slow_clock_holds_reset_longer(void)
{
    /* 2048 tCLKIN at 1 MHz = 2.048 ms; a shorter pulse would only synchronize. */
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 4000000u, 1000000u), ADS131M04_OK);
    report(&f);
    MT_CHECK_EQ(f.violations, 0);
    MT_CHECK_EQ(f.resets, 1);
    MT_CHECK_EQ(f.syncs, 1);
    check_regs(&f);
}

static void test_init_rejects_wrong_id(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    fake_ads131m04_init(&f, 8000000u, 8000000u);
    f.id = 0x2810u; /* 8 channels: an ADS131M08 */
    ads131m04_hal_t hal = fake_ads131m04_hal(&f);
    ads131m04_config_t c = ads131m04_default_config();
    MT_CHECK_EQ(ads131m04_init(&dev, &hal, &c), ADS131M04_ERR_ID);
    MT_CHECK(!dev.ready);
}

static void test_init_detects_stuck_bit(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    fake_ads131m04_init(&f, 8000000u, 8000000u);
    f.stuck_mask[ADS131M04_REG_GAIN1] = 0x0020u; /* PGAGAIN1 bit 1 never sets */
    ads131m04_hal_t hal = fake_ads131m04_hal(&f);
    ads131m04_config_t c = ads131m04_default_config();
    MT_CHECK_EQ(ads131m04_init(&dev, &hal, &c), ADS131M04_ERR_VERIFY);
    MT_CHECK(!dev.ready);
}

static void test_init_rejects_bad_config(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    fake_ads131m04_init(&f, 8000000u, 8000000u);
    ads131m04_hal_t hal = fake_ads131m04_hal(&f);
    ads131m04_config_t c = ads131m04_default_config();
    c.sclk_hz = 30000000u; /* tc(SC) >= 40 ns -> 25 MHz max */
    MT_CHECK_EQ(ads131m04_init(&dev, &hal, &c), ADS131M04_ERR_ARG);
    c = ads131m04_default_config();
    c.clkin_hz = 9000000u; /* above 8.4 MHz */
    MT_CHECK_EQ(ads131m04_init(&dev, &hal, &c), ADS131M04_ERR_ARG);
    c = ads131m04_default_config();
    c.clkin_hz = 200000u; /* below 0.3 MHz */
    MT_CHECK_EQ(ads131m04_init(&dev, &hal, &c), ADS131M04_ERR_ARG);
    MT_CHECK_EQ(f.resets, 0);
    MT_CHECK_EQ(f.frames, 0);
}

static void test_read_frame(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 8000000u, 8000000u), ADS131M04_OK);
    f.ch_code[0] = 2796;
    f.ch_code[1] = -2796;
    f.ch_code[2] = 0;
    f.ch_code[3] = 8388607;
    ads131m04_frame_t out;
    MT_CHECK_EQ(ads131m04_read_frame(&dev, &out), ADS131M04_OK);
    MT_CHECK(out.crc_ok);
    MT_CHECK_EQ(out.code[0], 2796);
    MT_CHECK_EQ(out.code[1], -2796);
    MT_CHECK_EQ(out.code[3], 8388607);
    MT_CHECK(near(out.uv[0], 99.99, 0.01));
    MT_CHECK(near(out.uv[3], 299999.96, 0.05)); /* +FS at gain 4 */
    /* STATUS: RESET flag cleared by init, 24-bit words, all channels ready. */
    MT_CHECK_EQ(out.status & ADS131M04_STATUS_RESET, 0);
    MT_CHECK_EQ(out.status & 0x0300, 0x0100);
    MT_CHECK_EQ(out.status & ADS131M04_STATUS_DRDY_MASK, 0xF);

    f.corrupt_crc = true;
    MT_CHECK_EQ(ads131m04_read_frame(&dev, &out), ADS131M04_ERR_CRC);
    MT_CHECK_EQ(dev.crc_errors, 1);
    report(&f);
    MT_CHECK_EQ(f.violations, 0);
}

static void test_read_frame_requires_init(void)
{
    ads131m04_t dev;
    memset(&dev, 0, sizeof dev);
    ads131m04_frame_t out;
    MT_CHECK_EQ(ads131m04_read_frame(&dev, &out), ADS131M04_ERR_STATE);
}

static void test_register_roundtrip(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 8000000u, 8000000u), ADS131M04_OK);
    uint16_t v = 0;
    MT_CHECK_EQ(ads131m04_write_reg(&dev, ADS131M04_REG_THRSHLD_LSB, 0x0003), ADS131M04_OK);
    MT_CHECK_EQ(f.regs[ADS131M04_REG_THRSHLD_LSB], 0x0003);
    MT_CHECK_EQ(ads131m04_read_reg(&dev, ADS131M04_REG_THRSHLD_LSB, &v), ADS131M04_OK);
    MT_CHECK_EQ(v, 0x0003);
    MT_CHECK_EQ(ads131m04_read_reg(&dev, ADS131M04_REG_ID, &v), ADS131M04_OK);
    MT_CHECK_EQ(v, 0x2410);
    MT_CHECK_EQ(ads131m04_write_reg(&dev, 0x40, 0), ADS131M04_ERR_ARG);
    /* Writable space is 02h..1Ch (§8.3.13); ID and STATUS are read-only. */
    MT_CHECK_EQ(ads131m04_write_reg(&dev, ADS131M04_REG_ID, 0x1234), ADS131M04_ERR_ARG);
    MT_CHECK_EQ(ads131m04_write_reg(&dev, ADS131M04_REG_STATUS, 0), ADS131M04_ERR_ARG);
    MT_CHECK_EQ(f.regs[ADS131M04_REG_ID], 0x2410);
    report(&f);
    MT_CHECK_EQ(f.violations, 0);
}

static void test_bad_wreg_ack_is_reported(void)
{
    /* WREG acknowledgement 010a aaaa ammm mmmm must report 1 register written (§8.5.1.10.8). */
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 8000000u, 8000000u), ADS131M04_OK);
    f.bad_wreg_ack = true;
    MT_CHECK_EQ(ads131m04_write_reg(&dev, ADS131M04_REG_THRSHLD_LSB, 0x0001), ADS131M04_ERR_ACK);
}

static void test_test_signal(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 8000000u, 8000000u), ADS131M04_OK);
    MT_CHECK_EQ(ads131m04_set_test_signal(&dev, true), ADS131M04_OK);
    for (unsigned ch = 0; ch < 4; ch++) {
        MT_CHECK_EQ(f.regs[ADS131M04_REG_CH0_CFG + 5 * ch] & ADS131M04_MUX_MASK,
                    ADS131M04_MUX_POS_TEST);
    }
    MT_CHECK_EQ(ads131m04_set_test_signal(&dev, false), ADS131M04_OK);
    check_regs(&f);
    MT_CHECK_EQ(f.violations, 0);
}

static void test_sync_sets_resync_flag(void)
{
    fake_ads131m04_t f;
    ads131m04_t dev;
    ads131m04_hal_t hal;
    MT_CHECK_EQ(init_with(&f, &dev, &hal, 8000000u, 8000000u), ADS131M04_OK);
    ads131m04_frame_t out;
    MT_CHECK_EQ(ads131m04_read_frame(&dev, &out), ADS131M04_OK);
    ads131m04_sync(&dev);
    MT_CHECK_EQ(f.syncs, 2);
    MT_CHECK_EQ(f.resets, 1); /* a sync pulse must not reset the device */
    MT_CHECK_EQ(ads131m04_read_frame(&dev, &out), ADS131M04_OK);
    MT_CHECK(out.status & ADS131M04_STATUS_F_RESYNC);
    MT_CHECK_EQ(ads131m04_read_frame(&dev, &out), ADS131M04_OK);
    MT_CHECK(!(out.status & ADS131M04_STATUS_F_RESYNC)); /* cleared once reported */
    MT_CHECK_EQ(f.violations, 0);
}

int main(void)
{
    MT_RUN(test_crc16);
    MT_RUN(test_sign_extend);
    MT_RUN(test_gain_codes);
    MT_RUN(test_scaling);
    MT_RUN(test_data_rate);
    MT_RUN(test_config_registers);
    MT_RUN(test_decode_frame);
    MT_RUN(test_init);
    MT_RUN(test_init_slow_clock_holds_reset_longer);
    MT_RUN(test_init_rejects_wrong_id);
    MT_RUN(test_init_detects_stuck_bit);
    MT_RUN(test_init_rejects_bad_config);
    MT_RUN(test_read_frame);
    MT_RUN(test_read_frame_requires_init);
    MT_RUN(test_register_roundtrip);
    MT_RUN(test_bad_wreg_ack_is_reported);
    MT_RUN(test_test_signal);
    MT_RUN(test_sync_sets_resync_flag);
    return MT_REPORT();
}
