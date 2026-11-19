#include "fake_ads131m04.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void violate(fake_ads131m04_t *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->last_violation, sizeof f->last_violation, fmt, ap);
    va_end(ap);
    f->violations++;
}

static uint64_t tclkin_ns(const fake_ads131m04_t *f, uint32_t n)
{
    return (uint64_t)n * 1000000000ull / f->clkin_hz;
}

static void reset_regs(fake_ads131m04_t *f)
{
    /* Reset values: §8.6, Table 8-12. */
    memset(f->regs, 0, sizeof f->regs);
    f->regs[ADS131M04_REG_ID] = f->id;
    f->regs[ADS131M04_REG_STATUS] = 0x0500u;
    f->regs[ADS131M04_REG_MODE] = 0x0510u;
    f->regs[ADS131M04_REG_CLOCK] = 0x0F0Eu;
    f->regs[ADS131M04_REG_CFG] = 0x0600u;
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        f->regs[ADS131M04_REG_CH0_CFG + ADS131M04_CH_REG_STRIDE * ch + 3u] = 0x8000u; /* GCAL */
    }
    f->have_response = false; /* next response: STATUS */
}

void fake_ads131m04_init(fake_ads131m04_t *f, uint32_t sclk_hz, uint32_t clkin_hz)
{
    memset(f, 0, sizeof *f);
    f->sclk_hz = sclk_hz;
    f->clkin_hz = clkin_hz;
    f->id = 0x2410u; /* 0010 0100 xxxx xxxx */
    f->sync_high = true;
    f->t_ready_ns = (uint64_t)ADS131M04_TPOR_US * 1000u; /* power-on reset (§6.7) */
    reset_regs(f);
}

static bool writable(uint8_t addr)
{
    return addr >= ADS131M04_REG_MODE && addr != 0x05u && addr <= 0x1Cu;
}

static uint16_t status_word(const fake_ads131m04_t *f)
{
    uint16_t mode = f->regs[ADS131M04_REG_MODE];
    uint16_t s = (uint16_t)(f->regs[ADS131M04_REG_STATUS] & 0xF000u); /* sticky flags */
    s |= (uint16_t)(mode & (ADS131M04_MODE_RESET | 0x0300u | ADS131M04_MODE_CRC_TYPE_ANSI));
    s |= (uint16_t)((f->regs[ADS131M04_REG_CLOCK] >> 8) & 0x000Fu); /* enabled channels ready */
    return s;
}

static uint16_t word_at(const uint8_t *buf, size_t w)
{
    return (uint16_t)(((unsigned)buf[3u * w] << 8) | buf[3u * w + 1u]);
}

static void put24(uint8_t *buf, size_t w, uint32_t v)
{
    buf[3u * w] = (uint8_t)(v >> 16);
    buf[3u * w + 1u] = (uint8_t)(v >> 8);
    buf[3u * w + 2u] = (uint8_t)v;
}

/* Prepare DOUT for the frame that is starting. */
static void build_dout(fake_ads131m04_t *f)
{
    memset(f->tx_buf, 0, sizeof f->tx_buf);
    uint16_t resp = f->have_response ? f->next_response : status_word(f);
    if (!f->have_response) {
        f->regs[ADS131M04_REG_STATUS] &= (uint16_t)~ADS131M04_STATUS_F_RESYNC; /* read clears */
    }
    f->have_response = false;
    put24(f->tx_buf, 0, (uint32_t)resp << 8);
    for (unsigned ch = 0; ch < ADS131M04_NUM_CH; ch++) {
        put24(f->tx_buf, 1u + ch, (uint32_t)f->ch_code[ch] & 0x00FFFFFFu);
    }
    uint16_t crc = ads131m04_crc16(f->tx_buf, 15u);
    if (f->corrupt_crc) {
        crc ^= 0x0100u;
    }
    put24(f->tx_buf, 5u, (uint32_t)crc << 8);
}

static void end_frame(fake_ads131m04_t *f)
{
    size_t words = f->pos / 3u;
    bool adcs_on = (f->regs[ADS131M04_REG_CLOCK] & 0x0F00u) != 0;
    if (f->pos == 0) {
        return;
    }
    f->frames++;
    if (f->pos % 3u != 0) {
        violate(f, "frame of %zu bytes is not whole 24-bit words", f->pos);
    }
    if (words < ADS131M04_FRAME_WORDS && adcs_on) {
        f->short_frames++;
        violate(f, "short frame (%zu words) while ADCs are enabled", words);
    }
    uint16_t cmd = word_at(f->rx_buf, 0);
    uint8_t addr = (uint8_t)((cmd >> ADS131M04_ADDR_SHIFT) & ADS131M04_ADDR_MASK);
    uint8_t n = (uint8_t)((cmd & ADS131M04_COUNT_MASK) + 1u);
    if (cmd == ADS131M04_CMD_RESET) {
        if (words < ADS131M04_FRAME_WORDS) {
            f->next_response = 0x0011u; /* frame incomplete: not reset (§8.5.1.10.2) */
            f->have_response = true;
            return;
        }
        reset_regs(f);
        f->resets++;
        f->next_response = ADS131M04_RESP_RESET;
        f->have_response = true;
        f->t_ready_ns = f->t_ns + (uint64_t)ADS131M04_TREGACQ_US * 1000u;
    } else if ((cmd & 0xE000u) == ADS131M04_CMD_RREG) {
        if (n != 1u) {
            violate(f, "multi-register RREG is not modelled");
        }
        f->next_response =
            (uint16_t)(addr < ADS131M04_NUM_REGS
                           ? (addr == ADS131M04_REG_STATUS ? status_word(f) : f->regs[addr])
                           : 0u);
        f->have_response = true;
    } else if ((cmd & 0xE000u) == ADS131M04_CMD_WREG) {
        unsigned written = 0;
        if (words < 1u + n) {
            violate(f, "WREG frame too short for %u registers", n);
        }
        for (unsigned i = 0; i < n && 1u + i < words; i++) {
            uint8_t a = (uint8_t)(addr + i);
            if (!writable(a)) {
                continue;
            }
            uint16_t keep = f->stuck_mask[a];
            f->regs[a] = (uint16_t)((f->regs[a] & keep) | (word_at(f->rx_buf, 1u + i) & ~keep));
            written++;
        }
        f->next_response =
            (uint16_t)(ADS131M04_RESP_WREG | ((unsigned)addr << ADS131M04_ADDR_SHIFT) |
                       ((written ? written - 1u : 0u) & ADS131M04_COUNT_MASK));
        if (f->bad_wreg_ack) {
            f->next_response ^= 0x0001u;
        }
        f->have_response = true;
    }
}

static void hal_cs(void *ctx, bool asserted)
{
    fake_ads131m04_t *f = ctx;
    if (asserted) {
        if (f->cs_low) {
            violate(f, "CS asserted twice");
        }
        if (f->t_ns < f->t_ready_ns) {
            violate(f, "SPI %llu ns before the device is ready",
                    (unsigned long long)(f->t_ready_ns - f->t_ns));
        }
        if (!f->sync_high) {
            violate(f, "SPI while SYNC/RESET is low");
        }
        f->cs_low = true;
        f->pos = 0;
        f->t_cs_low_ns = f->t_ns;
        build_dout(f);
    } else {
        if (!f->cs_low) {
            violate(f, "CS released twice");
        }
        if ((f->regs[ADS131M04_REG_MODE] & ADS131M04_MODE_TIMEOUT) &&
            f->t_ns - f->t_cs_low_ns > tclkin_ns(f, ADS131M04_SPI_TIMEOUT_TCLKIN)) {
            violate(f, "frame longer than the SPI timeout");
        }
        f->cs_low = false;
        end_frame(f);
    }
}

static void hal_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    fake_ads131m04_t *f = ctx;
    if (!f->cs_low) {
        violate(f, "transfer with CS high");
    }
    f->t_ns += (uint64_t)n * 8000000000ull / f->sclk_hz;
    for (size_t i = 0; i < n; i++) {
        uint8_t in = tx ? tx[i] : 0u;
        uint8_t out = 0;
        if (f->pos < FAKE_M04_MAX_FRAME_BYTES) {
            f->rx_buf[f->pos] = in;
            out = f->tx_buf[f->pos];
        }
        f->pos++;
        if (rx) {
            rx[i] = out;
        }
    }
}

static void hal_sync_reset(void *ctx, bool high)
{
    fake_ads131m04_t *f = ctx;
    if (!high && f->sync_high) {
        f->t_sync_low_ns = f->t_ns;
    } else if (high && !f->sync_high) {
        uint64_t low = f->t_ns - f->t_sync_low_ns;
        if (low >= tclkin_ns(f, ADS131M04_TW_RSL_TCLKIN)) {
            reset_regs(f);
            f->resets++;
            f->t_ready_ns = f->t_ns + (uint64_t)ADS131M04_TREGACQ_US * 1000u;
        } else if (low > tclkin_ns(f, ADS131M04_TW_SYL_MAX_TCLKIN) || low < tclkin_ns(f, 1u)) {
            violate(f, "SYNC/RESET low %llu ns: neither a valid sync nor a reset",
                    (unsigned long long)low);
        } else {
            f->syncs++;
            f->regs[ADS131M04_REG_STATUS] |= ADS131M04_STATUS_F_RESYNC;
        }
    }
    f->sync_high = high;
}

static void hal_delay_us(void *ctx, uint32_t us)
{
    fake_ads131m04_t *f = ctx;
    f->t_ns += (uint64_t)us * 1000u;
}

ads131m04_hal_t fake_ads131m04_hal(fake_ads131m04_t *f)
{
    ads131m04_hal_t h = {f, hal_cs, hal_xfer, hal_sync_reset, hal_delay_us};
    return h;
}
