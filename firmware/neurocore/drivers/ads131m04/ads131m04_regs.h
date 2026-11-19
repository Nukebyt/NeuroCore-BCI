/* ADS131M04 registers, commands, framing and timing.
 *
 * Source: TI ADS131M04 datasheet SBAS890D (May 2021), docs/datasheets/ads131m04-sbas890.pdf.
 * Every value cites its section. Registers and commands are 16-bit; with the default 24-bit
 * word length they are MSB-aligned and zero-padded (§8.5.1.8).
 */
#ifndef ADS131M04_REGS_H
#define ADS131M04_REGS_H

#include <stdint.h>

/* --- Commands: §8.5.1.10, Table 8-11 --------------------------------------------------- */
#define ADS131M04_CMD_NULL    0x0000u
#define ADS131M04_CMD_RESET   0x0011u
#define ADS131M04_CMD_STANDBY 0x0022u
#define ADS131M04_CMD_WAKEUP  0x0033u
#define ADS131M04_CMD_RREG    0xA000u /* 101a aaaa annn nnnn */
#define ADS131M04_CMD_WREG    0x6000u /* 011a aaaa annn nnnn */
#define ADS131M04_RESP_RESET  0xFF24u
#define ADS131M04_RESP_WREG   0x4000u /* 010a aaaa ammm mmmm */
#define ADS131M04_ADDR_SHIFT  7u
#define ADS131M04_ADDR_MASK   0x3Fu
#define ADS131M04_COUNT_MASK  0x7Fu

/* --- Registers: §8.6, Table 8-12 ------------------------------------------------------- */
#define ADS131M04_REG_ID          0x00u
#define ADS131M04_REG_STATUS      0x01u
#define ADS131M04_REG_MODE        0x02u
#define ADS131M04_REG_CLOCK       0x03u
#define ADS131M04_REG_GAIN1       0x04u
#define ADS131M04_REG_CFG         0x06u
#define ADS131M04_REG_THRSHLD_MSB 0x07u
#define ADS131M04_REG_THRSHLD_LSB 0x08u
#define ADS131M04_REG_CH0_CFG     0x09u /* CHn_CFG = 0x09 + 5 n */
#define ADS131M04_CH_REG_STRIDE   5u
#define ADS131M04_NUM_REGS        0x40u

/* ID: bits 15:12 read 0010b, CHANCNT bits 11:8 read 0100b (§8.6.1). */
#define ADS131M04_ID_MASK  0xFF00u
#define ADS131M04_ID_VALUE 0x2400u

/* STATUS (§8.6.2). */
#define ADS131M04_STATUS_LOCK      0x8000u
#define ADS131M04_STATUS_F_RESYNC  0x4000u
#define ADS131M04_STATUS_REG_MAP   0x2000u
#define ADS131M04_STATUS_CRC_ERR   0x1000u
#define ADS131M04_STATUS_RESET     0x0400u
#define ADS131M04_STATUS_DRDY_MASK 0x000Fu

/* MODE (§8.6.3); reset 0510h. */
#define ADS131M04_MODE_REG_CRC_EN    0x2000u
#define ADS131M04_MODE_RX_CRC_EN     0x1000u
#define ADS131M04_MODE_CRC_TYPE_ANSI 0x0800u
#define ADS131M04_MODE_RESET         0x0400u /* write 0 to clear STATUS.RESET */
#define ADS131M04_MODE_WLENGTH_24    0x0100u
#define ADS131M04_MODE_TIMEOUT       0x0010u

/* CLOCK (§8.6.4); reset 0F0Eh. */
#define ADS131M04_CLOCK_CH_EN_SHIFT 8u
#define ADS131M04_CLOCK_TBM         0x0020u
#define ADS131M04_CLOCK_OSR_SHIFT   2u
#define ADS131M04_CLOCK_PWR_HR      0x0002u

/* GAIN1 (§8.6.5): PGAGAINn[2:0] at bit 4 n; codes 0..7 = gain 1..128. */
#define ADS131M04_GAIN_SHIFT(ch) (4u * (ch))

/* THRSHLD_LSB (§8.6.9): DCBLOCK[3:0] in bits 3:0 (0 = DC block filter disabled). */
#define ADS131M04_DCBLOCK_MASK 0x000Fu

/* CHn_CFG (§8.6.10): MUXn[1:0]. */
#define ADS131M04_MUX_NORMAL   0x0u
#define ADS131M04_MUX_SHORTED  0x1u
#define ADS131M04_MUX_POS_TEST 0x2u
#define ADS131M04_MUX_NEG_TEST 0x3u
#define ADS131M04_MUX_MASK     0x0003u

/* --- Framing: §8.5.1.7 to §8.5.1.9 ----------------------------------------------------- */
#define ADS131M04_NUM_CH      4u
#define ADS131M04_WORD_BYTES  3u                      /* 24-bit words (default) */
#define ADS131M04_FRAME_WORDS (2u + ADS131M04_NUM_CH) /* response/cmd + 4 ch + CRC */
#define ADS131M04_FRAME_BYTES (ADS131M04_FRAME_WORDS * ADS131M04_WORD_BYTES)
#define ADS131M04_VREF_UV     1200000.0 /* internal reference 1.2 V, §8.3.4 */
/* Output CRC: CCITT x^16 + x^12 + x^5 + 1, seed FFFFh (§8.3.12, Table 8-7). */
#define ADS131M04_CRC_POLY 0x1021u
#define ADS131M04_CRC_SEED 0xFFFFu

/* --- Timing: §6.6, §6.7 ---------------------------------------------------------------- */
#define ADS131M04_SCLK_MAX_HZ        25000000u /* tc(SC) >= 40 ns, DVDD 2.7..3.6 V */
#define ADS131M04_CLKIN_MIN_HZ       300000u   /* high-resolution mode, §6.3 */
#define ADS131M04_CLKIN_MAX_HZ       8400000u
#define ADS131M04_TPOR_US            250u   /* supplies at 90 % to first DRDY rising edge */
#define ADS131M04_TREGACQ_US         5u     /* register default acquisition time */
#define ADS131M04_TW_RSL_TCLKIN      2048u  /* SYNC/RESET low for a reset */
#define ADS131M04_TW_SYL_MAX_TCLKIN  2047u  /* SYNC/RESET low for a synchronization: 1..2047 */
#define ADS131M04_SPI_TIMEOUT_TCLKIN 32768u /* whole frame within this when TIMEOUT = 1 */

#endif /* ADS131M04_REGS_H */
