/* ADS1299-4 register map, commands and timing.
 *
 * Source: TI ADS1299/ADS1299-4/ADS1299-6 datasheet SBAS499C (Jan 2017),
 * docs/datasheets/ads1299-sbas499.pdf. Every value cites its section.
 */
#ifndef ADS1299_REGS_H
#define ADS1299_REGS_H

#include <stdint.h>

/* --- SPI commands: DS §9.5.3, Table 10 ------------------------------------------------- */
#define ADS1299_CMD_WAKEUP    0x02u
#define ADS1299_CMD_STANDBY   0x04u
#define ADS1299_CMD_RESET     0x06u
#define ADS1299_CMD_START     0x08u
#define ADS1299_CMD_STOP      0x0Au
#define ADS1299_CMD_RDATAC    0x10u
#define ADS1299_CMD_SDATAC    0x11u
#define ADS1299_CMD_RDATA     0x12u
#define ADS1299_CMD_RREG      0x20u /* 001r rrrr; second byte 000n nnnn = count - 1 */
#define ADS1299_CMD_WREG      0x40u /* 010r rrrr; second byte 000n nnnn = count - 1 */
#define ADS1299_REG_ADDR_MASK 0x1Fu

/* --- Register addresses: DS §9.6, Table 11 --------------------------------------------- */
#define ADS1299_REG_ID         0x00u
#define ADS1299_REG_CONFIG1    0x01u
#define ADS1299_REG_CONFIG2    0x02u
#define ADS1299_REG_CONFIG3    0x03u
#define ADS1299_REG_LOFF       0x04u
#define ADS1299_REG_CH1SET     0x05u /* CHnSET = 0x05 + (n - 1); ADS1299-4 has n = 1..4 */
#define ADS1299_REG_BIAS_SENSP 0x0Du
#define ADS1299_REG_BIAS_SENSN 0x0Eu
#define ADS1299_REG_LOFF_SENSP 0x0Fu
#define ADS1299_REG_LOFF_SENSN 0x10u
#define ADS1299_REG_LOFF_FLIP  0x11u
#define ADS1299_REG_LOFF_STATP 0x12u /* read only */
#define ADS1299_REG_LOFF_STATN 0x13u /* read only */
#define ADS1299_REG_GPIO       0x14u
#define ADS1299_REG_MISC1      0x15u
#define ADS1299_REG_MISC2      0x16u
#define ADS1299_REG_CONFIG4    0x17u
#define ADS1299_NUM_REGS       0x18u

/* --- ID register: DS §9.6.1.1, Table 12 ------------------------------------------------ */
/* bit 4 always 1, DEV_ID[3:2] = 11 (ADS1299-x), NU_CH[1:0] = 00 (4 channels). */
#define ADS1299_ID_MASK      0x1Fu
#define ADS1299_ID_ADS1299_4 0x1Cu

/* --- CONFIG1: DS §9.6.1.2 ------------------------------------------------------------- */
/* bit 7 reserved = 1, bits 4:3 reserved = 10b, DAISY_EN bit 6, CLK_EN bit 5, DR[2:0]. */
#define ADS1299_CONFIG1_RESERVED 0x90u
#define ADS1299_CONFIG1_DAISY_EN 0x40u
#define ADS1299_CONFIG1_CLK_EN   0x20u
#define ADS1299_CONFIG1_DR_MASK  0x07u

/* --- CONFIG2: DS §9.6.1.3 ------------------------------------------------------------- */
#define ADS1299_CONFIG2_RESERVED      0xC0u /* bits 7:5 reserved = 110b */
#define ADS1299_CONFIG2_INT_CAL       0x10u /* test signal generated internally */
#define ADS1299_CONFIG2_CAL_AMP       0x04u /* 2 x -(VREFP - VREFN) / 2400 */
#define ADS1299_CONFIG2_CAL_FREQ_MASK 0x03u

/* --- CONFIG3: DS §9.6.1.4 ------------------------------------------------------------- */
#define ADS1299_CONFIG3_PD_REFBUF      0x80u /* 1 = internal reference buffer enabled */
#define ADS1299_CONFIG3_RESERVED       0x60u /* bits 6:5 reserved = 11b */
#define ADS1299_CONFIG3_BIAS_MEAS      0x10u
#define ADS1299_CONFIG3_BIASREF_INT    0x08u /* BIASREF = (AVDD + AVSS) / 2 internally */
#define ADS1299_CONFIG3_PD_BIAS        0x04u /* 1 = BIAS buffer enabled */
#define ADS1299_CONFIG3_BIAS_LOFF_SENS 0x02u
#define ADS1299_CONFIG3_BIAS_STAT      0x01u /* read only */

/* --- CHnSET: DS §9.6.1.6 -------------------------------------------------------------- */
#define ADS1299_CHSET_PD         0x80u
#define ADS1299_CHSET_GAIN_SHIFT 4u
#define ADS1299_CHSET_GAIN_MASK  0x70u
#define ADS1299_CHSET_SRB2       0x08u
#define ADS1299_CHSET_MUX_MASK   0x07u
#define ADS1299_MUX_NORMAL       0x00u
#define ADS1299_MUX_SHORTED      0x01u
#define ADS1299_MUX_TEST         0x05u

/* --- MISC1: DS §9.6.1.15 -------------------------------------------------------------- */
#define ADS1299_MISC1_SRB1 0x20u /* SRB1 to all inverting inputs */

/* --- CONFIG4: DS §9.6.1.17 ------------------------------------------------------------ */
#define ADS1299_CONFIG4_SINGLE_SHOT  0x08u
#define ADS1299_CONFIG4_PD_LOFF_COMP 0x02u /* 1 = lead-off comparators enabled */

/* --- Data: DS §9.4.4 and §9.5.1 ------------------------------------------------------- */
/* 24 status bits (1100 + LOFF_STATP + LOFF_STATN + GPIO[7:4]) then 24 bits per channel. */
#define ADS1299_NUM_CH      4u
#define ADS1299_FRAME_BYTES (3u + 3u * ADS1299_NUM_CH)
#define ADS1299_STATUS_SYNC 0xCu
#define ADS1299_VREF_UV     4500000.0 /* internal reference 4.5 V, DS §7.5 */

/* --- Timing ---------------------------------------------------------------------------- */
/* Delays use the slowest allowed master clock so they hold for any valid fCLK. */
#define ADS1299_TCLK_MAX_NS     666u       /* DS §7.6: tCLK 414..666 ns */
#define ADS1299_TCLK_NOM_NS     488u       /* internal oscillator 2.048 MHz, DS §7.5 */
#define ADS1299_SCLK_MAX_HZ     20000000u  /* tSCLK >= 50 ns, DS §7.6 (DVDD 2.7..3.6 V) */
#define ADS1299_TSDECODE_TCLK   4u         /* DS §7.6, §9.5.3.1 */
#define ADS1299_TCSH_TCLK       2u         /* CS high pulse, DS §7.6 */
#define ADS1299_TSCCS_TCLK      4u         /* final SCLK to CS high, DS §7.6 */
#define ADS1299_TPOR_TCLK       (1u << 18) /* power-up to reset, DS §11.1 Table 30 */
#define ADS1299_TRST_TCLK       2u         /* RESET low duration, DS §11.1 Table 30 */
#define ADS1299_RESET_INIT_TCLK 18u        /* after reset, DS §9.4.2 */
#define ADS1299_REF_STARTUP_MS  150u       /* internal reference start-up time, DS §7.5 */

#endif /* ADS1299_REGS_H */
