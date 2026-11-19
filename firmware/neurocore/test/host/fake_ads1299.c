#include "fake_ads1299.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void violate(fake_ads1299_t *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->last_violation, sizeof f->last_violation, fmt, ap);
    va_end(ap);
    f->violations++;
}

static void reset_regs(fake_ads1299_t *f)
{
    /* Defaults: DS §9.6 Table 11. CH5SET..CH8SET read 0 on the ADS1299-4 (footnotes 1, 2). */
    memset(f->regs, 0, sizeof f->regs);
    f->regs[ADS1299_REG_ID] = f->id;
    f->regs[ADS1299_REG_CONFIG1] = 0x96u;
    f->regs[ADS1299_REG_CONFIG2] = 0xC0u;
    f->regs[ADS1299_REG_CONFIG3] = 0x60u;
    for (unsigned ch = 0; ch < ADS1299_NUM_CH; ch++) {
        f->regs[ADS1299_REG_CH1SET + ch] = 0x61u;
    }
    f->regs[ADS1299_REG_GPIO] = 0x0Fu;
    f->rdatac = true; /* the device wakes up in RDATAC (§9.5.3.7) */
    f->refbuf_on = false;
}

void fake_ads1299_init(fake_ads1299_t *f, uint32_t sclk_hz)
{
    memset(f, 0, sizeof *f);
    f->sclk_hz = sclk_hz;
    f->tclk_ns = ADS1299_TCLK_NOM_NS;
    f->id = 0x3Cu; /* REV_ID 001, bit4 1, DEV_ID 11, NU_CH 00 */
    reset_regs(f);
}

static bool reg_writable(uint8_t addr)
{
    return addr != ADS1299_REG_ID && addr != ADS1299_REG_LOFF_STATP &&
           addr != ADS1299_REG_LOFF_STATN && addr < ADS1299_NUM_REGS;
}

static void write_reg(fake_ads1299_t *f, uint8_t addr, uint8_t v)
{
    if (!reg_writable(addr)) {
        return;
    }
    if (addr >= ADS1299_REG_CH1SET + ADS1299_NUM_CH && addr <= 0x0Cu) {
        return; /* CH5SET..CH8SET do not exist on the -4 */
    }
    uint8_t keep = f->stuck_mask[addr];
    if (addr == ADS1299_REG_CONFIG3) {
        keep |= ADS1299_CONFIG3_BIAS_STAT; /* read-only bit */
    }
    f->regs[addr] = (uint8_t)((f->regs[addr] & keep) | (v & (uint8_t)~keep));
    f->wreg_count++;
    if (addr == ADS1299_REG_CONFIG3) {
        bool on = (f->regs[addr] & ADS1299_CONFIG3_PD_REFBUF) != 0;
        if (on && !f->refbuf_on) {
            f->t_refbuf_on_ns = f->t_ns;
        }
        f->refbuf_on = on;
    }
}

static void check_command_allowed(fake_ads1299_t *f)
{
    if (!f->powered) {
        violate(f, "SPI traffic while powered down");
    } else if (!f->have_reset) {
        violate(f, "command before the first reset pulse");
    } else if (f->t_ns < f->t_reset_done_ns) {
        violate(f, "command %llu ns after reset, need 18 tCLK",
                (unsigned long long)(f->t_ns - (f->t_reset_done_ns - 18u * f->tclk_ns)));
    }
}

/* One byte clocked in (tx) and out (return value). t_ns is already at the byte's end. */
static uint8_t clock_byte(fake_ads1299_t *f, uint8_t tx)
{
    uint8_t out = 0;
    int pos = f->pos++;
    if (pos == 0) {
        f->op = tx;
        f->data_read = false;
        check_command_allowed(f);
        if (f->rdatac) {
            if (tx == ADS1299_CMD_SDATAC) {
                f->rdatac = false;
            } else if (tx == 0x00u) {
                f->data_read = true; /* clocking out conversion data */
                out = f->frame[0];
            } else {
                f->ignored_cmds++;
                f->op = 0xFFu; /* ignore the rest of this transaction */
            }
        } else if (tx == ADS1299_CMD_RDATAC) {
            f->rdatac = true;
        } else if (tx == ADS1299_CMD_RESET) {
            reset_regs(f);
            f->rdatac = true;
            f->t_reset_done_ns = f->t_ns + 18u * f->tclk_ns;
            f->resets++;
        } else if (tx == ADS1299_CMD_RDATA) {
            f->data_read = true;
        }
    } else if (f->data_read) {
        int idx = f->op == ADS1299_CMD_RDATA ? pos - 1 : pos;
        if (idx >= 0 && idx < (int)ADS1299_FRAME_BYTES) {
            out = f->frame[idx];
        }
        if (idx == (int)ADS1299_FRAME_BYTES - 1) {
            f->frames_read++;
        }
    } else if (f->op != 0xFFu && (f->op & 0xE0u) == ADS1299_CMD_RREG) {
        if (pos == 1) {
            f->count = (uint8_t)((tx & 0x1Fu) + 1u);
        } else if (pos - 2 < f->count) {
            uint8_t addr = (uint8_t)((f->op & ADS1299_REG_ADDR_MASK) + (unsigned)(pos - 2));
            out = addr < ADS1299_NUM_REGS ? f->regs[addr] : 0u;
        }
    } else if (f->op != 0xFFu && (f->op & 0xE0u) == ADS1299_CMD_WREG) {
        if (pos == 1) {
            f->count = (uint8_t)((tx & 0x1Fu) + 1u);
        } else if (pos - 2 < f->count) {
            write_reg(f, (uint8_t)((f->op & ADS1299_REG_ADDR_MASK) + (unsigned)(pos - 2)), tx);
        }
    }
    return out;
}

static void hal_cs(void *ctx, bool asserted)
{
    fake_ads1299_t *f = ctx;
    if (asserted) {
        if (f->cs_low) {
            violate(f, "CS asserted twice");
        }
        if (f->t_cs_high_ns != 0 && f->t_ns - f->t_cs_high_ns < 2u * f->tclk_ns) {
            violate(f, "tCSH: CS high only %llu ns",
                    (unsigned long long)(f->t_ns - f->t_cs_high_ns));
        }
        f->cs_low = true;
        f->pos = 0;
        f->t_last_byte_end_ns = 0;
    } else {
        if (!f->cs_low) {
            violate(f, "CS released twice");
        }
        if (f->pos > 0 && f->t_ns - f->t_last_byte_end_ns < 4u * f->tclk_ns) {
            violate(f, "tSCCS: CS high %llu ns after last SCLK",
                    (unsigned long long)(f->t_ns - f->t_last_byte_end_ns));
        }
        f->cs_low = false;
        f->t_cs_high_ns = f->t_ns;
    }
}

static void hal_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    fake_ads1299_t *f = ctx;
    if (!f->cs_low) {
        violate(f, "transfer with CS high");
    }
    uint64_t byte_ns = 8000000000ull / f->sclk_hz;
    for (size_t i = 0; i < n; i++) {
        f->t_ns += byte_ns;
        bool command_bytes =
            !f->data_read && f->pos > 0 && f->op != 0xFFu &&
            ((f->op & 0xE0u) == ADS1299_CMD_RREG || (f->op & 0xE0u) == ADS1299_CMD_WREG);
        if (command_bytes && f->t_ns - f->t_last_byte_end_ns < 4u * f->tclk_ns) {
            violate(f, "tSDECODE: byte %d ended %llu ns after the previous one", f->pos,
                    (unsigned long long)(f->t_ns - f->t_last_byte_end_ns));
        }
        uint8_t out = clock_byte(f, tx ? tx[i] : 0u);
        if (rx) {
            rx[i] = out;
        }
        f->t_last_byte_end_ns = f->t_ns;
    }
}

static void hal_pin(void *ctx, ads1299_pin_t pin, bool high)
{
    fake_ads1299_t *f = ctx;
    switch (pin) {
    case ADS1299_PIN_PWDN:
        if (high && !f->pwdn_high) {
            f->powered = true;
            f->t_power_ns = f->t_ns;
        }
        if (!high) {
            f->powered = false;
            f->have_reset = false;
        }
        f->pwdn_high = high;
        break;
    case ADS1299_PIN_RESET:
        if (!high && f->reset_high) {
            f->t_reset_low_ns = f->t_ns;
        } else if (high && !f->reset_high && f->t_reset_low_ns != 0) {
            if (f->t_ns - f->t_reset_low_ns < 2u * f->tclk_ns) {
                violate(f, "tRST: RESET low only %llu ns",
                        (unsigned long long)(f->t_ns - f->t_reset_low_ns));
            }
            if (!f->powered || f->t_reset_low_ns - f->t_power_ns < (1ull << 18) * f->tclk_ns) {
                violate(f, "reset before tPOR");
            }
            reset_regs(f);
            f->have_reset = true;
            f->t_reset_done_ns = f->t_ns + 18u * f->tclk_ns;
            f->resets++;
        }
        f->reset_high = high;
        break;
    case ADS1299_PIN_START:
        if (high && !f->start_high) {
            f->start_rises++;
            if (!f->refbuf_on) {
                violate(f, "START with the internal reference off");
            } else if (f->t_ns - f->t_refbuf_on_ns < 150000000ull) {
                violate(f, "START %llu ns after reference on, need 150 ms",
                        (unsigned long long)(f->t_ns - f->t_refbuf_on_ns));
            }
        }
        f->start_high = high;
        break;
    }
}

static void hal_delay_us(void *ctx, uint32_t us)
{
    fake_ads1299_t *f = ctx;
    f->t_ns += (uint64_t)us * 1000u;
}

ads1299_hal_t fake_ads1299_hal(fake_ads1299_t *f)
{
    ads1299_hal_t h = {f, hal_cs, hal_xfer, hal_pin, hal_delay_us};
    return h;
}
