/* ADS1299-4 driver. Datasheet: TI SBAS499C; facts and citations in ads1299_regs.h. */
#include "ads1299.h"

#include <string.h>

/* --- timing helpers (worst-case tCLK, DS §7.6) ------------------------------------------ */

static uint32_t ceil_us(uint64_t ns)
{
    return (uint32_t)((ns + 999u) / 1000u);
}

static uint32_t tclk_us(uint32_t n_tclk)
{
    return ceil_us((uint64_t)n_tclk * ADS1299_TCLK_MAX_NS);
}

static void delay_us(const ads1299_t *dev, uint32_t us)
{
    if (us > 0u) {
        dev->hal->delay_us(dev->hal->ctx, us);
    }
}

/* --- pure helpers ------------------------------------------------------------------------ */

ads1299_config_t ads1299_default_config(void)
{
    /* NeuroCore Rev A montage (ADR-0007): referential to REF on SRB1, BIAS driven from the
     * positive inputs, DC lead-off on the positive inputs, gain 24, 250 SPS. */
    ads1299_config_t c;
    memset(&c, 0, sizeof c);
    c.sclk_hz = 4000000u;
    c.rate = ADS1299_DR_250;
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        c.gain[ch] = 24u;
    }
    c.srb1_reference = true;
    c.bias_enable = true;
    c.bias_sensp = 0x0Fu;
    c.bias_sensn = 0x00u;
    c.lead_off_enable = true;
    c.loff = 0x00u; /* COMP_TH 95 %, ILEAD_OFF 6 nA, DC detection (DS §9.6.1.5) */
    c.loff_sensp = 0x0Fu;
    c.loff_sensn = 0x00u;
    c.vcap1_settle_ms = 0u;
    return c;
}

int32_t ads1299_sign_extend24(uint32_t raw24)
{
    raw24 &= 0x00FFFFFFu;
    if (raw24 & 0x00800000u) {
        return (int32_t)raw24 - (int32_t)0x01000000;
    }
    return (int32_t)raw24;
}

bool ads1299_gain_code(uint8_t gain, uint8_t *code)
{
    /* DS §9.6.1.6, GAINn[2:0]. */
    static const uint8_t gains[] = {1u, 2u, 4u, 6u, 8u, 12u, 24u};
    for (uint8_t i = 0; i < (uint8_t)sizeof gains; i++) {
        if (gains[i] == gain) {
            *code = i;
            return true;
        }
    }
    return false;
}

float ads1299_code_to_uv(int32_t code, uint8_t gain)
{
    /* DS §9.5.1 Eq. 8: 1 LSB = (2 x VREF / gain) / 2^24. */
    double lsb_uv = (2.0 * ADS1299_VREF_UV / (double)gain) / 16777216.0;
    return (float)((double)code * lsb_uv);
}

ads1299_status_t ads1299_decode_frame(const uint8_t raw[ADS1299_FRAME_BYTES],
                                      const uint8_t gain[ADS1299_NUM_CH], ads1299_frame_t *out)
{
    /* DS §9.4.4: status = 1100 + LOFF_STATP + LOFF_STATN + GPIO[7:4], then 24-bit channels. */
    uint32_t status = ((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2];
    out->sync_ok = (status >> 20) == ADS1299_STATUS_SYNC;
    out->loff_statp = (uint8_t)((status >> 12) & 0xFFu);
    out->loff_statn = (uint8_t)((status >> 4) & 0xFFu);
    out->gpio = (uint8_t)(status & 0x0Fu);
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        const uint8_t *p = &raw[3u + 3u * ch];
        uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        out->code[ch] = ads1299_sign_extend24(v);
        out->uv[ch] = ads1299_code_to_uv(out->code[ch], gain[ch]);
    }
    return out->sync_ok ? ADS1299_OK : ADS1299_ERR_SYNC;
}

static uint8_t chset_for(const ads1299_config_t *cfg, unsigned ch, uint8_t mux)
{
    uint8_t code = 0;
    (void)ads1299_gain_code(cfg->gain[ch], &code);
    return (uint8_t)(((unsigned)code << ADS1299_CHSET_GAIN_SHIFT) | (mux & ADS1299_CHSET_MUX_MASK));
}

ads1299_status_t ads1299_config_registers(const ads1299_config_t *cfg,
                                          uint8_t regs[ADS1299_NUM_REGS])
{
    const uint8_t ch_mask = (uint8_t)((1u << ADS1299_NUM_CH) - 1u);
    if ((unsigned)cfg->rate > (unsigned)ADS1299_DR_250) {
        return ADS1299_ERR_ARG; /* DR = 111 is reserved (DS §9.6.1.2) */
    }
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        uint8_t code;
        if (!ads1299_gain_code(cfg->gain[ch], &code)) {
            return ADS1299_ERR_ARG;
        }
    }
    if ((cfg->bias_sensp | cfg->bias_sensn | cfg->loff_sensp | cfg->loff_sensn) & ~ch_mask) {
        return ADS1299_ERR_ARG; /* channels 5..8 do not exist on the ADS1299-4 */
    }
    if (cfg->loff & 0x10u) {
        return ADS1299_ERR_ARG; /* LOFF bit 4 is reserved (DS §9.6.1.5) */
    }
    memset(regs, 0, ADS1299_NUM_REGS);
    regs[ADS1299_REG_CONFIG1] = (uint8_t)(ADS1299_CONFIG1_RESERVED | (uint8_t)cfg->rate);
    regs[ADS1299_REG_CONFIG2] = ADS1299_CONFIG2_RESERVED;
    regs[ADS1299_REG_CONFIG3] =
        (uint8_t)(ADS1299_CONFIG3_PD_REFBUF | ADS1299_CONFIG3_RESERVED |
                  (cfg->bias_enable ? (ADS1299_CONFIG3_BIASREF_INT | ADS1299_CONFIG3_PD_BIAS)
                                    : 0u));
    regs[ADS1299_REG_LOFF] = cfg->loff;
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        regs[ADS1299_REG_CH1SET + ch] = chset_for(cfg, ch, ADS1299_MUX_NORMAL);
    }
    regs[ADS1299_REG_BIAS_SENSP] = cfg->bias_sensp;
    regs[ADS1299_REG_BIAS_SENSN] = cfg->bias_sensn;
    regs[ADS1299_REG_LOFF_SENSP] = cfg->loff_sensp;
    regs[ADS1299_REG_LOFF_SENSN] = cfg->loff_sensn;
    regs[ADS1299_REG_MISC1] = (uint8_t)(cfg->srb1_reference ? ADS1299_MISC1_SRB1 : 0u);
    regs[ADS1299_REG_CONFIG4] = (uint8_t)(cfg->lead_off_enable ? ADS1299_CONFIG4_PD_LOFF_COMP : 0u);
    return ADS1299_OK;
}

/* --- SPI transactions ------------------------------------------------------------------- */

static void cs_begin(const ads1299_t *dev)
{
    dev->hal->cs(dev->hal->ctx, true);
}

static void cs_end(const ads1299_t *dev)
{
    delay_us(dev, tclk_us(ADS1299_TSCCS_TCLK)); /* tSCCS before CS high (DS §7.6) */
    dev->hal->cs(dev->hal->ctx, false);
    delay_us(dev, tclk_us(ADS1299_TCSH_TCLK)); /* tCSH; also covers 4 tCLK after SDATAC */
}

/* One byte of a multi-byte command, spaced by tSDECODE (DS §9.5.3.1). */
static uint8_t cmd_byte(const ads1299_t *dev, uint8_t tx, bool first)
{
    uint8_t rx = 0;
    if (!first) {
        delay_us(dev, dev->byte_gap_us);
    }
    dev->hal->xfer(dev->hal->ctx, &tx, &rx, 1u);
    return rx;
}

static void raw_command(ads1299_t *dev, uint8_t cmd)
{
    cs_begin(dev);
    (void)cmd_byte(dev, cmd, true);
    cs_end(dev);
    if (cmd == ADS1299_CMD_SDATAC) {
        dev->rdatac = false;
    } else if (cmd == ADS1299_CMD_RDATAC) {
        dev->rdatac = true;
    } else if (cmd == ADS1299_CMD_RESET) {
        dev->rdatac = true;                              /* registers back to defaults */
        delay_us(dev, tclk_us(ADS1299_RESET_INIT_TCLK)); /* DS §9.4.2 */
    }
}

ads1299_status_t ads1299_command(ads1299_t *dev, uint8_t cmd)
{
    if (dev == NULL || dev->hal == NULL) {
        return ADS1299_ERR_STATE;
    }
    if (cmd == ADS1299_CMD_RDATAC && dev->rdatac) {
        return ADS1299_OK;
    }
    /* In RDATAC every command except SDATAC is ignored (DS §9.5.3.7). */
    if (dev->rdatac && cmd != ADS1299_CMD_SDATAC) {
        raw_command(dev, ADS1299_CMD_SDATAC);
    }
    raw_command(dev, cmd);
    return ADS1299_OK;
}

static ads1299_status_t check_range(const ads1299_t *dev, uint8_t addr, uint8_t n)
{
    if (dev == NULL || dev->hal == NULL) {
        return ADS1299_ERR_STATE;
    }
    if (n == 0u || n > 32u || (unsigned)addr + n > ADS1299_NUM_REGS) {
        return ADS1299_ERR_ARG;
    }
    return ADS1299_OK;
}

ads1299_status_t ads1299_read_regs(ads1299_t *dev, uint8_t addr, uint8_t *data, uint8_t n)
{
    ads1299_status_t st = check_range(dev, addr, n);
    if (st != ADS1299_OK) {
        return st;
    }
    if (dev->rdatac) {
        raw_command(dev, ADS1299_CMD_SDATAC); /* RREG is ignored in RDATAC (DS Table 10) */
    }
    cs_begin(dev);
    (void)cmd_byte(dev, (uint8_t)(ADS1299_CMD_RREG | (addr & ADS1299_REG_ADDR_MASK)), true);
    (void)cmd_byte(dev, (uint8_t)(n - 1u), false);
    for (uint8_t i = 0; i < n; i++) {
        data[i] = cmd_byte(dev, 0x00u, false);
    }
    cs_end(dev);
    return ADS1299_OK;
}

ads1299_status_t ads1299_write_regs(ads1299_t *dev, uint8_t addr, const uint8_t *data, uint8_t n)
{
    ads1299_status_t st = check_range(dev, addr, n);
    if (st != ADS1299_OK) {
        return st;
    }
    if (dev->rdatac) {
        raw_command(dev, ADS1299_CMD_SDATAC);
    }
    cs_begin(dev);
    (void)cmd_byte(dev, (uint8_t)(ADS1299_CMD_WREG | (addr & ADS1299_REG_ADDR_MASK)), true);
    (void)cmd_byte(dev, (uint8_t)(n - 1u), false);
    for (uint8_t i = 0; i < n; i++) {
        (void)cmd_byte(dev, data[i], false);
    }
    cs_end(dev);
    return ADS1299_OK;
}

/* Bits that read back differently from what was written. */
static uint8_t verify_mask(uint8_t addr)
{
    if (addr == ADS1299_REG_CONFIG3) {
        return (uint8_t)~ADS1299_CONFIG3_BIAS_STAT; /* BIAS_STAT is read-only */
    }
    return 0xFFu;
}

static ads1299_status_t write_verify(ads1299_t *dev, uint8_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t back[ADS1299_NUM_REGS];
    ads1299_status_t st = ads1299_write_regs(dev, addr, data, n);
    if (st == ADS1299_OK) {
        st = ads1299_read_regs(dev, addr, back, n);
    }
    if (st != ADS1299_OK) {
        return st;
    }
    for (uint8_t i = 0; i < n; i++) {
        uint8_t m = verify_mask((uint8_t)(addr + i));
        if ((back[i] & m) != (data[i] & m)) {
            return ADS1299_ERR_VERIFY;
        }
    }
    return ADS1299_OK;
}

/* Register blocks written at init: CONFIG1..CH4SET, BIAS_SENSP..LOFF_FLIP, MISC1..CONFIG4.
 * CH5SET..CH8SET (absent on the -4), the read-only LOFF_STAT pair and GPIO are skipped. */
static const uint8_t k_blocks[][2] = {
    {ADS1299_REG_CONFIG1, ADS1299_REG_CH1SET + ADS1299_NUM_CH - ADS1299_REG_CONFIG1},
    {ADS1299_REG_BIAS_SENSP, ADS1299_REG_LOFF_FLIP - ADS1299_REG_BIAS_SENSP + 1u},
    {ADS1299_REG_MISC1, ADS1299_REG_CONFIG4 - ADS1299_REG_MISC1 + 1u},
};

ads1299_status_t ads1299_init(ads1299_t *dev, const ads1299_hal_t *hal, const ads1299_config_t *cfg)
{
    uint8_t regs[ADS1299_NUM_REGS];
    if (dev == NULL || hal == NULL || cfg == NULL) {
        return ADS1299_ERR_ARG;
    }
    memset(dev, 0, sizeof *dev);
    if (cfg->sclk_hz == 0u || cfg->sclk_hz > ADS1299_SCLK_MAX_HZ) {
        return ADS1299_ERR_ARG;
    }
    ads1299_status_t st = ads1299_config_registers(cfg, regs);
    if (st != ADS1299_OK) {
        return st;
    }
    dev->hal = hal;
    dev->cfg = *cfg;
    dev->rdatac = true; /* power-up default (DS §9.5.3.7) */

    uint64_t byte_ns = (8000000000ull + cfg->sclk_hz - 1u) / cfg->sclk_hz;
    uint64_t decode_ns = (uint64_t)ADS1299_TSDECODE_TCLK * ADS1299_TCLK_MAX_NS;
    dev->byte_gap_us = byte_ns >= decode_ns ? 0u : ceil_us(decode_ns - byte_ns);

    /* Power-up, DS §11.1 and Figure 67. Supplies are stable when this is called. */
    hal->pin(hal->ctx, ADS1299_PIN_START, false);
    hal->pin(hal->ctx, ADS1299_PIN_RESET, true);
    hal->pin(hal->ctx, ADS1299_PIN_PWDN, true);
    uint32_t wait_us = tclk_us(ADS1299_TPOR_TCLK);
    if ((uint64_t)cfg->vcap1_settle_ms * 1000u > wait_us) {
        wait_us = cfg->vcap1_settle_ms * 1000u;
    }
    delay_us(dev, wait_us);
    hal->pin(hal->ctx, ADS1299_PIN_RESET, false);
    delay_us(dev, tclk_us(ADS1299_TRST_TCLK));
    hal->pin(hal->ctx, ADS1299_PIN_RESET, true);
    delay_us(dev, tclk_us(ADS1299_RESET_INIT_TCLK));

    raw_command(dev, ADS1299_CMD_SDATAC); /* device wakes up in RDATAC */

    st = ads1299_read_regs(dev, ADS1299_REG_ID, &dev->id, 1u);
    if (st != ADS1299_OK) {
        return st;
    }
    if ((dev->id & ADS1299_ID_MASK) != ADS1299_ID_ADS1299_4) {
        return ADS1299_ERR_ID;
    }

    /* Internal reference first, then wait for it (DS Figure 67; start-up 150 ms, §7.5). */
    st = write_verify(dev, ADS1299_REG_CONFIG3, &regs[ADS1299_REG_CONFIG3], 1u);
    if (st != ADS1299_OK) {
        return st;
    }
    delay_us(dev, ADS1299_REF_STARTUP_MS * 1000u);

    for (size_t b = 0; b < sizeof k_blocks / sizeof k_blocks[0]; b++) {
        st = write_verify(dev, k_blocks[b][0], &regs[k_blocks[b][0]], k_blocks[b][1]);
        if (st != ADS1299_OK) {
            return st;
        }
    }

    ads1299_start(dev);
    return ads1299_command(dev, ADS1299_CMD_RDATAC);
}

ads1299_status_t ads1299_read_frame(ads1299_t *dev, ads1299_frame_t *out)
{
    static const uint8_t zeros[ADS1299_FRAME_BYTES] = {0};
    uint8_t raw[ADS1299_FRAME_BYTES];
    if (dev == NULL || dev->hal == NULL || !dev->rdatac) {
        return ADS1299_ERR_STATE;
    }
    cs_begin(dev);
    dev->hal->xfer(dev->hal->ctx, zeros, raw, ADS1299_FRAME_BYTES); /* DIN low (§9.4.4) */
    cs_end(dev);
    return ads1299_decode_frame(raw, dev->cfg.gain, out);
}

ads1299_status_t ads1299_set_test_signal(ads1299_t *dev, bool on)
{
    if (dev == NULL || dev->hal == NULL) {
        return ADS1299_ERR_STATE;
    }
    /* Figure 67: CONFIG2 = D0h (internal 1x test signal, pulsed fCLK / 2^21) and
     * CHnSET mux = 101; off restores the configured values. */
    uint8_t blk[1u + ADS1299_NUM_CH];
    blk[0] = (uint8_t)(on ? (ADS1299_CONFIG2_RESERVED | ADS1299_CONFIG2_INT_CAL)
                          : ADS1299_CONFIG2_RESERVED);
    ads1299_status_t st = write_verify(dev, ADS1299_REG_CONFIG2, blk, 1u);
    if (st != ADS1299_OK) {
        return st;
    }
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        blk[ch] = chset_for(&dev->cfg, ch, on ? ADS1299_MUX_TEST : ADS1299_MUX_NORMAL);
    }
    st = write_verify(dev, ADS1299_REG_CH1SET, blk, ADS1299_NUM_CH);
    if (st != ADS1299_OK) {
        return st;
    }
    return ads1299_command(dev, ADS1299_CMD_RDATAC);
}

void ads1299_stop(ads1299_t *dev)
{
    dev->hal->pin(dev->hal->ctx, ADS1299_PIN_START, false);
    dev->running = false;
}

void ads1299_start(ads1299_t *dev)
{
    dev->hal->pin(dev->hal->ctx, ADS1299_PIN_START, true);
    dev->running = true;
}
