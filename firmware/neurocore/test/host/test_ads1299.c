/* ADS1299-4 driver tests (task card T4.1) against a simulated device (fake_ads1299.c).
 * Expected numbers are hand-computed from TI SBAS499C; see comments. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ads1299.h"
#include "fake_ads1299.h"
#include "minitest.h"

static bool near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

/* --- pure helpers ------------------------------------------------------------------------ */

static void test_sign_extend(void)
{
    /* DS §9.5.1 Table 9: 7FFFFFh = +FS, 000001h = +1, FFFFFFh = -1, 800000h = -FS. */
    MT_CHECK_EQ(ads1299_sign_extend24(0x7FFFFFu), 8388607);
    MT_CHECK_EQ(ads1299_sign_extend24(0x000001u), 1);
    MT_CHECK_EQ(ads1299_sign_extend24(0x000000u), 0);
    MT_CHECK_EQ(ads1299_sign_extend24(0xFFFFFFu), -1);
    MT_CHECK_EQ(ads1299_sign_extend24(0x800000u), -8388608);
    MT_CHECK_EQ(ads1299_sign_extend24(0xFF800000u), -8388608); /* upper byte ignored */
}

static void test_gain_codes(void)
{
    /* DS §9.6.1.6: 000=1 001=2 010=4 011=6 100=8 101=12 110=24, 111 do not use. */
    const uint8_t gains[] = {1, 2, 4, 6, 8, 12, 24};
    for (uint8_t i = 0; i < 7; i++) {
        uint8_t code = 0xFF;
        MT_CHECK(ads1299_gain_code(gains[i], &code));
        MT_CHECK_EQ(code, i);
    }
    uint8_t code = 0;
    MT_CHECK(!ads1299_gain_code(3, &code));
    MT_CHECK(!ads1299_gain_code(0, &code));
    MT_CHECK(!ads1299_gain_code(48, &code));
}

static void test_scaling(void)
{
    /* Eq. 8: 1 LSB = 2 * 4.5 V / gain / 2^24.
     * gain 24: 9e6 uV / 24 / 16777216 = 0.0223517418 uV
     * gain 1:  9e6 uV / 16777216      = 0.5364418030 uV */
    MT_CHECK(near(ads1299_code_to_uv(1, 24), 0.0223517418, 1e-8));
    MT_CHECK(near(ads1299_code_to_uv(1, 1), 0.5364418030, 1e-7));
    /* +FS at gain 24: 8388607 LSB = 187499.98 uV (FS = VREF / gain = 187.5 mV). */
    MT_CHECK(near(ads1299_code_to_uv(8388607, 24), 187499.98, 0.05));
    MT_CHECK(near(ads1299_code_to_uv(-8388608, 24), -187500.0, 0.05));
    /* 100 uV at gain 24 is 4474 codes (100 / 0.0223517 = 4473.9). */
    MT_CHECK(near(ads1299_code_to_uv(4474, 24), 100.0, 0.03));
}

static void test_decode_frame(void)
{
    /* Status: 1100 | LOFF_STATP=0x05 | LOFF_STATN=0x02 | GPIO=0xA (DS §9.4.4)
     *   bits: 1100 0000 0101 0000 0010 1010 -> C0 50 2A */
    const uint8_t raw[ADS1299_FRAME_BYTES] = {
        0xC0, 0x50, 0x2A, /* status */
        0x00, 0x11, 0x7A, /* ch1 = 4474 */
        0xFF, 0xFF, 0xFF, /* ch2 = -1 */
        0x7F, 0xFF, 0xFF, /* ch3 = +FS */
        0x80, 0x00, 0x00, /* ch4 = -FS */
    };
    const uint8_t gain[ADS1299_NUM_CH] = {24, 24, 1, 24};
    ads1299_frame_t f;
    MT_CHECK_EQ(ads1299_decode_frame(raw, gain, &f), ADS1299_OK);
    MT_CHECK(f.sync_ok);
    MT_CHECK_EQ(f.loff_statp, 0x05);
    MT_CHECK_EQ(f.loff_statn, 0x02);
    MT_CHECK_EQ(f.gpio, 0x0A);
    MT_CHECK_EQ(f.code[0], 4474);
    MT_CHECK_EQ(f.code[1], -1);
    MT_CHECK_EQ(f.code[2], 8388607);
    MT_CHECK_EQ(f.code[3], -8388608);
    MT_CHECK(near(f.uv[0], 100.0, 0.03));
    MT_CHECK(near(f.uv[2], 4499999.5, 1.0)); /* gain 1 full scale = 4.5 V */

    uint8_t bad[ADS1299_FRAME_BYTES];
    memcpy(bad, raw, sizeof bad);
    bad[0] = 0x40; /* header 0100: not a status word */
    MT_CHECK_EQ(ads1299_decode_frame(bad, gain, &f), ADS1299_ERR_SYNC);
    MT_CHECK(!f.sync_ok);
}

static void test_config_registers(void)
{
    ads1299_config_t c = ads1299_default_config();
    uint8_t r[ADS1299_NUM_REGS];
    MT_CHECK_EQ(ads1299_config_registers(&c, r), ADS1299_OK);
    /* 250 SPS: CONFIG1 = reserved 1001 0xxx + DR 110 = 0x96 (DS §9.6.1.2, Figure 67). */
    MT_CHECK_EQ(r[ADS1299_REG_CONFIG1], 0x96);
    MT_CHECK_EQ(r[ADS1299_REG_CONFIG2], 0xC0);
    /* Reference buffer + reserved + BIASREF_INT + PD_BIAS = 1110 1100 (DS §9.6.1.4). */
    MT_CHECK_EQ(r[ADS1299_REG_CONFIG3], 0xEC);
    /* Gain 24, normal input, SRB2 open: 0110 0000. */
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        MT_CHECK_EQ(r[ADS1299_REG_CH1SET + ch], 0x60);
    }
    MT_CHECK_EQ(r[ADS1299_REG_MISC1], 0x20);
    MT_CHECK_EQ(r[ADS1299_REG_BIAS_SENSP], 0x0F);
    MT_CHECK_EQ(r[ADS1299_REG_LOFF_SENSP], 0x0F);
    MT_CHECK_EQ(r[ADS1299_REG_CONFIG4], 0x02);

    c.gain[2] = 3; /* unsupported */
    MT_CHECK_EQ(ads1299_config_registers(&c, r), ADS1299_ERR_ARG);
    c = ads1299_default_config();
    c.rate = (ads1299_rate_t)7; /* reserved DR code */
    MT_CHECK_EQ(ads1299_config_registers(&c, r), ADS1299_ERR_ARG);
    c = ads1299_default_config();
    c.bias_sensp = 0x10; /* channel 5 does not exist on the -4 */
    MT_CHECK_EQ(ads1299_config_registers(&c, r), ADS1299_ERR_ARG);
}

/* --- device sequences ---------------------------------------------------------------- */

static void run_init(uint32_t sclk_hz, fake_ads1299_t *f, ads1299_t *dev, ads1299_hal_t *hal,
                     ads1299_status_t expect)
{
    fake_ads1299_init(f, sclk_hz);
    *hal = fake_ads1299_hal(f);
    ads1299_config_t c = ads1299_default_config();
    c.sclk_hz = sclk_hz;
    ads1299_status_t st = ads1299_init(dev, hal, &c);
    MT_CHECK_EQ(st, expect);
}

static void check_registers_match_config(const fake_ads1299_t *f)
{
    ads1299_config_t c = ads1299_default_config();
    uint8_t want[ADS1299_NUM_REGS];
    ads1299_config_registers(&c, want);
    for (uint8_t a = ADS1299_REG_CONFIG1; a <= ADS1299_REG_CONFIG4; a++) {
        if (a == ADS1299_REG_LOFF_STATP || a == ADS1299_REG_LOFF_STATN || a == ADS1299_REG_GPIO ||
            (a >= 0x09 && a <= 0x0C)) {
            continue;
        }
        if (f->regs[a] != want[a]) {
            fprintf(stderr, "reg 0x%02X = 0x%02X, want 0x%02X\n", a, f->regs[a], want[a]);
        }
        MT_CHECK_EQ(f->regs[a], want[a]);
    }
}

static void test_init_4mhz(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(4000000u, &f, &dev, &hal, ADS1299_OK);
    if (f.violations) {
        fprintf(stderr, "violation: %s\n", f.last_violation);
    }
    MT_CHECK_EQ(f.violations, 0);
    MT_CHECK_EQ(f.ignored_cmds, 0); /* SDATAC was sent before any register access */
    MT_CHECK_EQ(f.resets, 1);
    MT_CHECK(f.pwdn_high && f.reset_high && f.start_high);
    MT_CHECK(f.rdatac);
    MT_CHECK(dev.rdatac && dev.running);
    MT_CHECK_EQ(dev.id, 0x3C);
    check_registers_match_config(&f);
    /* Power-up waits at least tPOR (2^18 x 488 ns = 128 ms) and the 150 ms reference
     * start-up, so the whole sequence takes more than 278 ms of simulated time. */
    MT_CHECK(f.t_ns > 278000000ull);
}

static void test_init_16mhz_inserts_decode_gaps(void)
{
    /* At 16 MHz a byte takes 500 ns < 4 tCLK, so the driver must space command bytes. */
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(16000000u, &f, &dev, &hal, ADS1299_OK);
    if (f.violations) {
        fprintf(stderr, "violation: %s\n", f.last_violation);
    }
    MT_CHECK_EQ(f.violations, 0);
    MT_CHECK(dev.byte_gap_us >= 3u); /* 4 x 666 ns - 500 ns = 2164 ns -> 3 us */
    check_registers_match_config(&f);
}

static void test_init_rejects_wrong_id(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    fake_ads1299_init(&f, 4000000u);
    f.id = 0x3Eu; /* NU_CH = 10: 8-channel ADS1299 */
    ads1299_hal_t hal = fake_ads1299_hal(&f);
    ads1299_config_t c = ads1299_default_config();
    MT_CHECK_EQ(ads1299_init(&dev, &hal, &c), ADS1299_ERR_ID);
    MT_CHECK(!f.start_high);
    MT_CHECK_EQ(f.start_rises, 0);
    MT_CHECK(!dev.running);
}

static void test_init_detects_stuck_bit(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    fake_ads1299_init(&f, 4000000u);
    f.stuck_mask[ADS1299_REG_MISC1] = ADS1299_MISC1_SRB1; /* SRB1 never sets */
    ads1299_hal_t hal = fake_ads1299_hal(&f);
    ads1299_config_t c = ads1299_default_config();
    MT_CHECK_EQ(ads1299_init(&dev, &hal, &c), ADS1299_ERR_VERIFY);
    MT_CHECK(!f.start_high);
    MT_CHECK(!dev.running);
}

static void test_init_rejects_bad_config(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    fake_ads1299_init(&f, 4000000u);
    ads1299_hal_t hal = fake_ads1299_hal(&f);
    ads1299_config_t c = ads1299_default_config();
    c.sclk_hz = 25000000u; /* above 20 MHz */
    MT_CHECK_EQ(ads1299_init(&dev, &hal, &c), ADS1299_ERR_ARG);
    c = ads1299_default_config();
    c.gain[0] = 5;
    MT_CHECK_EQ(ads1299_init(&dev, &hal, &c), ADS1299_ERR_ARG);
    MT_CHECK_EQ(f.resets, 0); /* nothing touched the device */
    MT_CHECK(!f.pwdn_high);
}

static void test_vcap1_wait_is_honoured(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    fake_ads1299_init(&f, 4000000u);
    ads1299_hal_t hal = fake_ads1299_hal(&f);
    ads1299_config_t c = ads1299_default_config();
    c.vcap1_settle_ms = 900u;
    MT_CHECK_EQ(ads1299_init(&dev, &hal, &c), ADS1299_OK);
    MT_CHECK(f.t_reset_low_ns - f.t_power_ns >= 900000000ull);
    MT_CHECK_EQ(f.violations, 0);
}

static void test_read_frame(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(4000000u, &f, &dev, &hal, ADS1299_OK);
    const uint8_t frame[ADS1299_FRAME_BYTES] = {0xC0, 0x00, 0x10, 0x00, 0x11, 0x7A, 0xFF, 0xEE,
                                                0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(f.frame, frame, sizeof frame);
    ads1299_frame_t out;
    MT_CHECK_EQ(ads1299_read_frame(&dev, &out), ADS1299_OK);
    MT_CHECK_EQ(f.frames_read, 1);
    MT_CHECK_EQ(out.code[0], 4474);
    MT_CHECK_EQ(out.code[1], -4474); /* FFEE86h */
    MT_CHECK_EQ(out.code[3], 1);
    MT_CHECK(near(out.uv[0], 100.0, 0.03));
    MT_CHECK(near(out.uv[1], -100.0, 0.03));
    MT_CHECK_EQ(out.gpio, 0x0);
    MT_CHECK_EQ(out.loff_statn, 0x01);
    MT_CHECK_EQ(f.violations, 0);

    f.frame[0] = 0x00; /* lost sync */
    MT_CHECK_EQ(ads1299_read_frame(&dev, &out), ADS1299_ERR_SYNC);
}

static void test_read_frame_requires_rdatac(void)
{
    ads1299_t dev;
    memset(&dev, 0, sizeof dev);
    ads1299_frame_t out;
    MT_CHECK_EQ(ads1299_read_frame(&dev, &out), ADS1299_ERR_STATE);
}

static void test_test_signal_roundtrip(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(4000000u, &f, &dev, &hal, ADS1299_OK);
    MT_CHECK_EQ(ads1299_set_test_signal(&dev, true), ADS1299_OK);
    /* Figure 67: CONFIG2 = D0h, CHnSET mux = 101 (test signal), gain kept. */
    MT_CHECK_EQ(f.regs[ADS1299_REG_CONFIG2], 0xD0);
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        MT_CHECK_EQ(f.regs[ADS1299_REG_CH1SET + ch], 0x65);
    }
    MT_CHECK(f.rdatac && dev.rdatac);
    MT_CHECK_EQ(ads1299_set_test_signal(&dev, false), ADS1299_OK);
    check_registers_match_config(&f);
    MT_CHECK_EQ(f.ignored_cmds, 0);
    MT_CHECK_EQ(f.violations, 0);
}

static void test_register_access_leaves_rdatac_off_until_restored(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(4000000u, &f, &dev, &hal, ADS1299_OK);
    uint8_t v = 0;
    /* Reading a register while streaming: the driver drops to SDATAC itself. */
    MT_CHECK_EQ(ads1299_read_regs(&dev, ADS1299_REG_ID, &v, 1), ADS1299_OK);
    MT_CHECK_EQ(v, 0x3C);
    MT_CHECK(!f.rdatac && !dev.rdatac);
    MT_CHECK_EQ(f.ignored_cmds, 0);
    MT_CHECK_EQ(ads1299_command(&dev, ADS1299_CMD_RDATAC), ADS1299_OK);
    MT_CHECK(f.rdatac && dev.rdatac);
    MT_CHECK_EQ(ads1299_read_regs(&dev, 0x17, &v, 2), ADS1299_ERR_ARG); /* past last reg */
    MT_CHECK_EQ(f.violations, 0);
}

static void test_command_while_streaming_sends_sdatac_first(void)
{
    /* In RDATAC the device ignores every command except SDATAC (DS §9.5.3.7). */
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(4000000u, &f, &dev, &hal, ADS1299_OK);
    MT_CHECK(f.rdatac);
    MT_CHECK_EQ(ads1299_command(&dev, ADS1299_CMD_RESET), ADS1299_OK);
    MT_CHECK_EQ(f.ignored_cmds, 0);
    MT_CHECK_EQ(f.resets, 2); /* pin reset at init + this command */
    MT_CHECK(dev.rdatac);     /* RESET returns the device to RDATAC */
    MT_CHECK_EQ(f.violations, 0);
}

static void test_stop_start(void)
{
    fake_ads1299_t f;
    ads1299_t dev;
    ads1299_hal_t hal;
    run_init(4000000u, &f, &dev, &hal, ADS1299_OK);
    ads1299_stop(&dev);
    MT_CHECK(!f.start_high && !dev.running);
    ads1299_start(&dev);
    MT_CHECK(f.start_high && dev.running);
    MT_CHECK_EQ(f.violations, 0);
}

int main(void)
{
    MT_RUN(test_sign_extend);
    MT_RUN(test_gain_codes);
    MT_RUN(test_scaling);
    MT_RUN(test_decode_frame);
    MT_RUN(test_config_registers);
    MT_RUN(test_init_4mhz);
    MT_RUN(test_init_16mhz_inserts_decode_gaps);
    MT_RUN(test_init_rejects_wrong_id);
    MT_RUN(test_init_detects_stuck_bit);
    MT_RUN(test_init_rejects_bad_config);
    MT_RUN(test_vcap1_wait_is_honoured);
    MT_RUN(test_read_frame);
    MT_RUN(test_read_frame_requires_rdatac);
    MT_RUN(test_test_signal_roundtrip);
    MT_RUN(test_register_access_leaves_rdatac_off_until_restored);
    MT_RUN(test_command_while_streaming_sends_sdatac_first);
    MT_RUN(test_stop_start);
    return MT_REPORT();
}
