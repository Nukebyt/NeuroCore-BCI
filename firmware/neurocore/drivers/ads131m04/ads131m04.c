/* ADS131M04 driver. Datasheet: TI SBAS890D; facts and citations in ads131m04_regs.h. */
#include "ads131m04.h"

#include <string.h>

#define MAX_FRAME_WORDS    8u
#define REG_WRITABLE_FIRST ADS131M04_REG_MODE /* register-map CRC space 02h..1Ch, §8.3.13 */
#define REG_WRITABLE_LAST  0x1Cu

/* --- pure helpers ------------------------------------------------------------------------ */

ads131m04_config_t ads131m04_default_config(void)
{
    /* NeuroCore Rev A (ADR-0008): CLKIN 8 MHz from the ESP32, OSR 4096 -> 976.6 SPS,
     * gain 4 (+-300 mV) on every channel, all four channels on, DC block off. */
    ads131m04_config_t c;
    memset(&c, 0, sizeof c);
    c.sclk_hz = 8000000u;
    c.clkin_hz = 8000000u;
    c.osr = ADS131M04_OSR_4096;
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        c.gain[ch] = 4u;
    }
    c.ch_enable = 0x0Fu;
    c.dcblock = 0u;
    return c;
}

uint16_t ads131m04_crc16(const uint8_t *data, size_t n)
{
    /* §8.3.12: CCITT polynomial, seed FFFFh, data MSB first. */
    uint16_t crc = ADS131M04_CRC_SEED;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((unsigned)data[i] << 8);
        for (unsigned b = 0; b < 8u; b++) {
            unsigned shifted = (unsigned)crc << 1;
            crc = (uint16_t)((crc & 0x8000u) ? (shifted ^ ADS131M04_CRC_POLY) : shifted);
        }
    }
    return crc;
}

int32_t ads131m04_sign_extend24(uint32_t raw24)
{
    raw24 &= 0x00FFFFFFu;
    if (raw24 & 0x00800000u) {
        return (int32_t)raw24 - (int32_t)0x01000000;
    }
    return (int32_t)raw24;
}

bool ads131m04_gain_code(uint8_t gain, uint8_t *code)
{
    /* §8.6.5: PGAGAINn 000b..111b = 1, 2, 4, ..., 128. */
    for (uint8_t i = 0; i < 8u; i++) {
        if (gain == (uint8_t)(1u << i)) {
            *code = i;
            return true;
        }
    }
    return false;
}

float ads131m04_code_to_uv(int32_t code, uint8_t gain)
{
    /* §8.5.1.9 Eq. 10: 1 LSB = (2.4 V / gain) / 2^24. */
    double lsb_uv = (2.0 * ADS131M04_VREF_UV / (double)gain) / 16777216.0;
    return (float)((double)code * lsb_uv);
}

double ads131m04_data_rate_hz(uint32_t clkin_hz, ads131m04_osr_t osr)
{
    /* fMOD = fCLKIN / 2 (§8.3.3); OSR code n -> 128 << n (§8.6.4, Table 8-2). */
    return (double)clkin_hz / 2.0 / (double)(128u << (unsigned)osr);
}

static uint32_t get24(const uint8_t *buf, size_t word)
{
    const uint8_t *p = &buf[word * ADS131M04_WORD_BYTES];
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

ads131m04_status_t ads131m04_decode_frame(const uint8_t raw[ADS131M04_FRAME_BYTES],
                                          const uint8_t gain[ADS131M04_NUM_CH],
                                          ads131m04_frame_t *out)
{
    /* §8.5.1.7: response word, four channel words, CRC word over everything before it. */
    out->status = (uint16_t)(get24(raw, 0) >> 8);
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        out->code[ch] = ads131m04_sign_extend24(get24(raw, 1u + ch));
        out->uv[ch] = ads131m04_code_to_uv(out->code[ch], gain[ch]);
    }
    uint16_t want = ads131m04_crc16(raw, (ADS131M04_FRAME_WORDS - 1u) * ADS131M04_WORD_BYTES);
    uint16_t got = (uint16_t)(get24(raw, ADS131M04_FRAME_WORDS - 1u) >> 8);
    out->crc_ok = want == got;
    return out->crc_ok ? ADS131M04_OK : ADS131M04_ERR_CRC;
}

ads131m04_status_t ads131m04_config_registers(const ads131m04_config_t *cfg, uint16_t *mode,
                                              uint16_t *clock, uint16_t *gain1,
                                              uint16_t *thrshld_lsb)
{
    if ((unsigned)cfg->osr > (unsigned)ADS131M04_OSR_8192 || cfg->ch_enable > 0x0Fu ||
        cfg->dcblock > ADS131M04_DCBLOCK_MASK) {
        return ADS131M04_ERR_ARG;
    }
    uint16_t g = 0;
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        uint8_t code;
        if (!ads131m04_gain_code(cfg->gain[ch], &code)) {
            return ADS131M04_ERR_ARG;
        }
        g = (uint16_t)(g | ((unsigned)code << ADS131M04_GAIN_SHIFT(ch)));
    }
    /* MODE: 24-bit words, SPI timeout on, RESET flag cleared, CCITT CRC, input CRC off,
     * DRDY = most lagging channel, active-low level (§8.6.3). */
    *mode = (uint16_t)(ADS131M04_MODE_WLENGTH_24 | ADS131M04_MODE_TIMEOUT);
    *clock = (uint16_t)(((unsigned)cfg->ch_enable << ADS131M04_CLOCK_CH_EN_SHIFT) |
                        ((unsigned)cfg->osr << ADS131M04_CLOCK_OSR_SHIFT) | ADS131M04_CLOCK_PWR_HR);
    *gain1 = g;
    *thrshld_lsb = cfg->dcblock;
    return ADS131M04_OK;
}

/* --- frames ------------------------------------------------------------------------------ */

static uint32_t ceil_div_u32(uint64_t a, uint64_t b)
{
    return (uint32_t)((a + b - 1u) / b);
}

static void delay_us(const ads131m04_t *dev, uint32_t us)
{
    if (us > 0u) {
        dev->hal->delay_us(dev->hal->ctx, us);
    }
}

/* One SPI frame: DIN carries `words` (16-bit, MSB-aligned), padded with zero words to at
 * least a full frame. DOUT is returned in rx (>= ADS131M04_FRAME_BYTES). */
static void frame(const ads131m04_t *dev, const uint16_t *words, size_t n, uint8_t *rx)
{
    uint8_t tx[MAX_FRAME_WORDS * ADS131M04_WORD_BYTES];
    size_t n_words = n > ADS131M04_FRAME_WORDS ? n : ADS131M04_FRAME_WORDS;
    memset(tx, 0, sizeof tx);
    for (size_t i = 0; i < n; i++) {
        tx[i * ADS131M04_WORD_BYTES] = (uint8_t)(words[i] >> 8);
        tx[i * ADS131M04_WORD_BYTES + 1u] = (uint8_t)words[i];
    }
    dev->hal->cs(dev->hal->ctx, true);
    dev->hal->xfer(dev->hal->ctx, tx, rx, n_words * ADS131M04_WORD_BYTES);
    dev->hal->cs(dev->hal->ctx, false);
    delay_us(dev, 1u); /* td(SCCS) 10 ns and tw(CSH) 15 ns (§6.6), rounded up */
}

/* Send `cmd` then a NULL frame; return the command's response (it arrives one frame later,
 * §8.5.1.7) after checking the output CRC of that second frame. */
static ads131m04_status_t transact(ads131m04_t *dev, const uint16_t *words, size_t n,
                                   uint16_t *response)
{
    uint8_t rx[MAX_FRAME_WORDS * ADS131M04_WORD_BYTES];
    const uint16_t null_cmd = ADS131M04_CMD_NULL;
    frame(dev, words, n, rx);
    frame(dev, &null_cmd, 1u, rx);
    uint16_t want = ads131m04_crc16(rx, (ADS131M04_FRAME_WORDS - 1u) * ADS131M04_WORD_BYTES);
    if ((uint16_t)(get24(rx, ADS131M04_FRAME_WORDS - 1u) >> 8) != want) {
        dev->crc_errors++;
        return ADS131M04_ERR_CRC;
    }
    *response = (uint16_t)(get24(rx, 0) >> 8);
    return ADS131M04_OK;
}

ads131m04_status_t ads131m04_read_reg(ads131m04_t *dev, uint8_t addr, uint16_t *value)
{
    if (dev == NULL || dev->hal == NULL) {
        return ADS131M04_ERR_STATE;
    }
    if (addr > ADS131M04_ADDR_MASK) {
        return ADS131M04_ERR_ARG;
    }
    const uint16_t cmd = (uint16_t)(ADS131M04_CMD_RREG | ((unsigned)addr << ADS131M04_ADDR_SHIFT));
    return transact(dev, &cmd, 1u, value);
}

static ads131m04_status_t write_reg_raw(ads131m04_t *dev, uint8_t addr, uint16_t value)
{
    const uint16_t words[2] = {
        (uint16_t)(ADS131M04_CMD_WREG | ((unsigned)addr << ADS131M04_ADDR_SHIFT)), value};
    uint16_t resp = 0;
    ads131m04_status_t st = transact(dev, words, 2u, &resp);
    if (st != ADS131M04_OK) {
        return st;
    }
    /* Acknowledgement 010a aaaa ammm mmmm with mmm mmmm = registers written - 1 = 0. */
    const uint16_t ack = (uint16_t)(ADS131M04_RESP_WREG | ((unsigned)addr << ADS131M04_ADDR_SHIFT));
    return resp == ack ? ADS131M04_OK : ADS131M04_ERR_ACK;
}

ads131m04_status_t ads131m04_write_reg(ads131m04_t *dev, uint8_t addr, uint16_t value)
{
    if (dev == NULL || dev->hal == NULL) {
        return ADS131M04_ERR_STATE;
    }
    if (addr < REG_WRITABLE_FIRST || addr > REG_WRITABLE_LAST || addr == 0x05u) {
        return ADS131M04_ERR_ARG; /* 05h is RESERVED (§8.6.6) */
    }
    ads131m04_status_t st = write_reg_raw(dev, addr, value);
    if (st != ADS131M04_OK) {
        return st;
    }
    uint16_t back = 0;
    st = ads131m04_read_reg(dev, addr, &back);
    if (st != ADS131M04_OK) {
        return st;
    }
    return back == value ? ADS131M04_OK : ADS131M04_ERR_VERIFY;
}

static uint32_t tclkin_us(const ads131m04_t *dev, uint32_t n)
{
    return ceil_div_u32((uint64_t)n * 1000000u, dev->cfg.clkin_hz);
}

void ads131m04_sync(ads131m04_t *dev)
{
    /* Low for 2 tCLKIN: inside the 1..2047 tCLKIN synchronization window (§6.6, §8.5.2). */
    dev->hal->sync_reset(dev->hal->ctx, false);
    delay_us(dev, tclkin_us(dev, 2u));
    dev->hal->sync_reset(dev->hal->ctx, true);
}

static uint16_t ch_cfg_addr(unsigned ch)
{
    return (uint16_t)(ADS131M04_REG_CH0_CFG + ADS131M04_CH_REG_STRIDE * ch);
}

ads131m04_status_t ads131m04_init(ads131m04_t *dev, const ads131m04_hal_t *hal,
                                  const ads131m04_config_t *cfg)
{
    uint16_t mode, clock, gain1, thr;
    if (dev == NULL || hal == NULL || cfg == NULL) {
        return ADS131M04_ERR_ARG;
    }
    memset(dev, 0, sizeof *dev);
    if (cfg->sclk_hz == 0u || cfg->sclk_hz > ADS131M04_SCLK_MAX_HZ ||
        cfg->clkin_hz < ADS131M04_CLKIN_MIN_HZ || cfg->clkin_hz > ADS131M04_CLKIN_MAX_HZ) {
        return ADS131M04_ERR_ARG;
    }
    ads131m04_status_t st = ads131m04_config_registers(cfg, &mode, &clock, &gain1, &thr);
    if (st != ADS131M04_OK) {
        return st;
    }
    dev->hal = hal;
    dev->cfg = *cfg;

    /* Power-on reset completes tPOR after the supplies (§8.4.1.1); then a hardware reset:
     * SYNC/RESET low >= 2048 tCLKIN, then tREGACQ before talking (§8.4.1.2). */
    hal->sync_reset(hal->ctx, true);
    delay_us(dev, ADS131M04_TPOR_US);
    hal->sync_reset(hal->ctx, false);
    delay_us(dev, tclkin_us(dev, ADS131M04_TW_RSL_TCLKIN) + 1u);
    hal->sync_reset(hal->ctx, true);
    delay_us(dev, ADS131M04_TREGACQ_US + 1u);

    st = ads131m04_read_reg(dev, ADS131M04_REG_ID, &dev->id);
    if (st != ADS131M04_OK) {
        return st;
    }
    if ((dev->id & ADS131M04_ID_MASK) != ADS131M04_ID_VALUE) {
        return ADS131M04_ERR_ID;
    }

    const struct {
        uint8_t addr;
        uint16_t value;
    } writes[] = {
        {ADS131M04_REG_MODE, mode},
        {ADS131M04_REG_CLOCK, clock},
        {ADS131M04_REG_GAIN1, gain1},
        {ADS131M04_REG_THRSHLD_LSB, thr},
    };
    for (size_t i = 0; i < sizeof writes / sizeof writes[0]; i++) {
        st = ads131m04_write_reg(dev, writes[i].addr, writes[i].value);
        if (st != ADS131M04_OK) {
            return st;
        }
    }
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        st = ads131m04_write_reg(dev, (uint8_t)ch_cfg_addr(ch), ADS131M04_MUX_NORMAL);
        if (st != ADS131M04_OK) {
            return st;
        }
    }

    /* MODE.RESET = 0 must have cleared STATUS.RESET (§8.6.3). */
    uint16_t status = 0;
    st = ads131m04_read_reg(dev, ADS131M04_REG_STATUS, &status);
    if (st != ADS131M04_OK) {
        return st;
    }
    if (status & ADS131M04_STATUS_RESET) {
        return ADS131M04_ERR_VERIFY;
    }

    ads131m04_sync(dev); /* empty the FIFOs before streaming (§8.5.1.9.1, Figure 8-20) */
    dev->ready = true;
    return ADS131M04_OK;
}

ads131m04_status_t ads131m04_read_frame(ads131m04_t *dev, ads131m04_frame_t *out)
{
    uint8_t rx[MAX_FRAME_WORDS * ADS131M04_WORD_BYTES];
    const uint16_t null_cmd = ADS131M04_CMD_NULL;
    if (dev == NULL || dev->hal == NULL || !dev->ready) {
        return ADS131M04_ERR_STATE;
    }
    frame(dev, &null_cmd, 1u, rx);
    ads131m04_status_t st = ads131m04_decode_frame(rx, dev->cfg.gain, out);
    if (st == ADS131M04_ERR_CRC) {
        dev->crc_errors++;
    }
    return st;
}

ads131m04_status_t ads131m04_set_test_signal(ads131m04_t *dev, bool on)
{
    if (dev == NULL || dev->hal == NULL || !dev->ready) {
        return ADS131M04_ERR_STATE;
    }
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        ads131m04_status_t st = ads131m04_write_reg(
            dev, (uint8_t)ch_cfg_addr(ch), on ? ADS131M04_MUX_POS_TEST : ADS131M04_MUX_NORMAL);
        if (st != ADS131M04_OK) {
            return st;
        }
    }
    return ADS131M04_OK;
}
