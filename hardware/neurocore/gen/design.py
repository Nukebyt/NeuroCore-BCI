"""NeuroCore Rev A: design source of truth -> KiCad schematic + JSON netlist.

Every pin assignment below comes from the datasheets in docs/datasheets/ (see
docs/datasheets/NOTES.md for page references). Run:

    python hardware/neurocore/gen/design.py

Writes hardware/neurocore/{neurocore.kicad_sch, neurocore.kicad_pro, design.json}.
The schematic connects pins with net labels (one label per pin) so it is machine-checkable;
it is readable but not pretty. Status: UNVERIFIED DRAFT (ADR-0006).
"""

from __future__ import annotations

import json
import math
import uuid
from dataclasses import dataclass, field
from pathlib import Path

OUT = Path(__file__).resolve().parents[1]
NS = uuid.UUID("6f1c5e1e-2b1f-4c3e-9a51-6e6575726f43")
ROOT_UUID = str(uuid.uuid5(NS, "root-sheet"))
GRID = 2.54

# ----------------------------------------------------------------------------- symbols


@dataclass
class Pin:
    num: str
    name: str
    etype: str  # KiCad electrical type


@dataclass
class Sym:
    name: str
    prefix: str
    left: list[Pin]
    right: list[Pin] = field(default_factory=list)


def pins(spec: str, etype: str = "passive") -> list[Pin]:
    """'1:IN 2:GND' -> pins. Per-pin type override with 'num:NAME:type'."""
    out = []
    for tok in spec.split():
        parts = tok.split(":")
        out.append(Pin(parts[0], parts[1], parts[2] if len(parts) > 2 else etype))
    return out


SYMBOLS: dict[str, Sym] = {}


def sym(name: str, prefix: str, left: str, right: str = "") -> None:
    SYMBOLS[name] = Sym(name, prefix, pins(left), pins(right))


# Generic parts
sym("R", "R", "1:~", "2:~")
sym("C", "C", "1:~", "2:~")
sym("LED", "D", "1:K", "2:A")
# Dual diode, common anode (BAW56): cathodes on pins 1 and 2, anode pin 3.
# BAW56 DS Rev. C, Connection Diagram (p. 2).
sym("D_DUAL_CA", "D", "1:K1 2:K2", "3:A")
sym("SW", "SW", "1:~", "2:~")
sym("SW_SPDT", "SW", "1:A 3:B", "2:C")
sym("TP", "TP", "1:~")
sym("HOLE", "H", "")
sym("PWR_FLAG", "#FLG", "1:~:power_out")
for n in (3, 4, 6):
    sym(f"CONN_{n}", "J", " ".join(f"{i}:P{i}" for i in range(1, n + 1)))
sym(
    "CONN_2x8",
    "J",
    " ".join(f"{i}:P{i}" for i in range(1, 16, 2)),
    " ".join(f"{i}:P{i}" for i in range(2, 17, 2)),
)
sym("CONN_BAT", "J", "1:BAT+ 2:BAT-")
sym("QWIIC", "J", "1:GND 2:3V3 3:SDA 4:SCL MP:MP")

# ADS1299-4, TQFP-64 PAG. Pin numbers: ADS1299 DS SBAS499C §6 Pin Functions (p. 6-7).
sym(
    "ADS1299-4",
    "U",
    "16:IN1P:input 15:IN1N:input 14:IN2P:input 13:IN2N:input 12:IN3P:input 11:IN3N:input "
    "10:IN4P:input 9:IN4N:input 8:IN5P:input 7:IN5N:input 6:IN6P:input 5:IN6N:input "
    "4:IN7P:input 3:IN7N:input 2:IN8P:input 1:IN8N:input 17:SRB1 18:SRB2 "
    "62:BIASIN:input 61:BIASINV 63:BIASOUT:output 60:BIASREF:input 24:VREFP 25:VREFN:input "
    "28:VCAP1 30:VCAP2 55:VCAP3 26:VCAP4 64:RESERVED:no_connect 27:NC:no_connect 29:NC:no_connect",
    "19:AVDD:power_in 21:AVDD:power_in 22:AVDD:power_in 56:AVDD:power_in 59:AVDD:power_in "
    "54:AVDD1:power_in 20:AVSS:power_in 23:AVSS:power_in 32:AVSS:power_in 57:AVSS:power_in "
    "58:AVSS:power_in 53:AVSS1:power_in 48:DVDD:power_in 50:DVDD:power_in 33:DGND:power_in "
    "49:DGND:power_in 51:DGND:power_in 39:~CS:input 40:SCLK:input 34:DIN:input "
    "43:DOUT:tri_state 47:~DRDY:output 38:START:input 36:~RESET:input 35:~PWDN:input "
    "37:CLK:bidirectional 52:CLKSEL:input 41:DAISY_IN:input 31:RESV1:input "
    "42:GPIO1:bidirectional 44:GPIO2:bidirectional 45:GPIO3:bidirectional 46:GPIO4:bidirectional",
)
# ADS131M04, TSSOP-20 PW. ADS131M04 DS SBAS890D Table 5-1 (p. 4).
sym(
    "ADS131M04",
    "U",
    "3:AIN0P:input 4:AIN0N:input 6:AIN1P:input 5:AIN1N:input 7:AIN2P:input 8:AIN2N:input "
    "10:AIN3P:input 9:AIN3N:input 1:AVDD:power_in 2:AGND:power_in",
    "20:DVDD:power_in 19:DGND:power_in 18:CAP:power_out 12:~CS:input 14:SCLK:input "
    "16:DIN:input 15:DOUT:tri_state 13:~DRDY:output 11:~SYNC/RESET:input 17:CLKIN:input",
)
# OPA2333, SOIC-8 D. OPA2333 DS SBOS351E Pin Functions (p. 4).
sym(
    "OPA2333",
    "U",
    "3:+INA:input 2:-INA:input 5:+INB:input 6:-INB:input",
    "1:OUTA:output 7:OUTB:output 8:V+:power_in 4:V-:power_in",
)
# ESP32-S3-WROOM-1 (N16R8). ESP32-S3-WROOM-1 DS v1.8 Table 3-1 (p. 11-12).
_esp_l = (
    "2:3V3:power_in 3:EN:input 27:IO0 39:IO1 38:IO2 15:IO3 4:IO4 5:IO5 6:IO6 7:IO7 12:IO8 "
    "17:IO9 18:IO10 19:IO11 20:IO12 21:IO13 22:IO14 8:IO15 9:IO16 10:IO17 11:IO18"
)
_esp_r = (
    "1:GND:power_in 40:GND:power_in 41:GND:power_in 13:IO19 14:IO20 23:IO21 28:IO35 29:IO36 "
    "30:IO37 31:IO38 32:IO39 33:IO40 34:IO41 35:IO42 36:RXD0 37:TXD0 26:IO45 16:IO46 "
    "24:IO47 25:IO48"
)
sym("ESP32-S3-WROOM-1", "U", _esp_l.replace("IO", "IO", 1), _esp_r)
for p in SYMBOLS["ESP32-S3-WROOM-1"].left + SYMBOLS["ESP32-S3-WROOM-1"].right:
    if p.etype == "passive":
        p.etype = "bidirectional"
# BMI270, LGA-14. BMI270 DS BST-BMI270-DS000 Table 22 (p. 135).
sym(
    "BMI270",
    "U",
    "8:VDD:power_in 5:VDDIO:power_in 7:GND:power_in 6:GNDIO:power_in 12:CSB:input",
    "13:SCx:input 14:SDx:bidirectional 1:SDO:passive 4:INT1:output 9:INT2:output "
    "2:ASDx:bidirectional 3:ASCx:bidirectional 10:OCSB:input 11:OSDO:output",
)
# SOT-23-5 LDOs (TLV757P DBV and TPS7A20 DBV share this pinout):
# TLV757P DS SBVS322C Table 4-1 (p. 3); TPS7A20 DS Pin Functions (p. 4).
sym("LDO_DBV", "U", "1:IN:power_in 3:EN:input 2:GND:power_in", "5:OUT:power_out 4:NC:no_connect")
# LM27761 WSON-8 DSG. LM27761 DS SNVSA85D Table 4-1 (p. 3).
sym(
    "LM27761",
    "U",
    "1:VIN:power_in 6:EN:input 8:C1+ 7:C1- 2:GND:power_in 9:EP:power_in",
    "3:CPOUT:power_out 4:VOUT:power_out 5:VFB:input",
)
# BQ21040 SOT-23-6 DBV. BQ21040 DS SLUSCE2D Pin Functions (p. 3).
sym(
    "BQ21040",
    "U",
    "6:VIN:power_in 1:TS:input 4:ISET:passive 5:GND:power_in",
    "2:OUT:power_out 3:~CHG:open_collector",
)
# TPD4E02B04 / TPD4EUSB30, USON-10 DQA. TPD4E02B04 DS SLVSD85B Pin Functions;
# TPD4EUSB30 DS SLVSAC2G Table 5-1 (p. 3).
sym(
    "ESD4_DQA",
    "U",
    "1:IO1 2:IO2 4:IO3 5:IO4",
    "3:GND 8:GND 6:NC:no_connect 7:NC:no_connect 9:NC:no_connect 10:NC:no_connect",
)
# USB-C receptacle (HRO TYPE-C-31-M-12 footprint pad names).
sym(
    "USB_C",
    "J",
    "A4:VBUS B4:VBUS A9:VBUS B9:VBUS A5:CC1 B5:CC2 A6:D+ B6:D+ A7:D- B7:D-",
    "A1:GND B1:GND A12:GND B12:GND S1:SHIELD A8:SBU1:no_connect B8:SBU2:no_connect",
)

# ----------------------------------------------------------------------------- footprints

FP = {
    "R": "Resistor_SMD:R_0603_1608Metric",
    "C": "Capacitor_SMD:C_0603_1608Metric",
    "C0805": "Capacitor_SMD:C_0805_2012Metric",
    "C1206": "Capacitor_SMD:C_1206_3216Metric",
    "LED": "LED_SMD:LED_0603_1608Metric",
    "TQFP64": "Package_QFP:TQFP-64_10x10mm_P0.5mm",
    "TSSOP20": "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm",
    "SOIC8": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "SOT23": "Package_TO_SOT_SMD:SOT-23",
    "SOT235": "Package_TO_SOT_SMD:SOT-23-5",
    "SOT236": "Package_TO_SOT_SMD:SOT-23-6",
    "WSON8": "Package_SON:WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm",
    "USON10": "Package_SON:USON-10_2.5x1.0mm_P0.5mm",
    "LGA14": "Package_LGA:Bosch_LGA-14_3x2.5mm_P0.5mm",
    "ESP": "RF_Module:ESP32-S3-WROOM-1",
    "USBC": "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
    "PH2": "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
    "SH4": "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
    "H1x3": "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
    "H1x4": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
    "H1x6": "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
    "H2x8": "Connector_PinHeader_2.54mm:PinHeader_2x08_P2.54mm_Vertical",
    "PCM12": "Button_Switch_SMD:SW_SPDT_PCM12",
    "BTN": "Button_Switch_SMD:SW_SPST_PTS810",
    "TP": "TestPoint:TestPoint_Pad_D1.0mm",
    "HOLE": "MountingHole:MountingHole_2.7mm_M2.5",
}

# ----------------------------------------------------------------------------- parts + nets


@dataclass
class Part:
    ref: str
    sym: str
    value: str
    fp: str
    nets: dict[str, str]  # pin number -> net
    note: str = ""
    dnp: bool = False


PARTS: list[Part] = []
_count: dict[str, int] = {}


def add(
    sym_name: str,
    value: str,
    fp: str,
    nets: dict,
    note: str = "",
    ref: str | None = None,
    dnp=False,
) -> str:
    prefix = SYMBOLS[sym_name].prefix
    if ref is None:
        _count[prefix] = _count.get(prefix, 0) + 1
        ref = f"{prefix}{_count[prefix]}"
    PARTS.append(
        Part(
            ref,
            sym_name,
            value,
            FP.get(fp, fp),
            {str(k): v for k, v in nets.items()},
            note,
            dnp,
        )
    )
    return ref


def r(a: str, b: str, value: str, note: str = "") -> str:
    return add("R", value, "R", {1: a, 2: b}, note)


def c(a: str, b: str, value: str, fp: str = "C", note: str = "") -> str:
    return add("C", value, fp, {1: a, 2: b}, note)


NC = None  # explicit no-connect

# --- ICs (refs fixed for readability) -------------------------------------------------
ads1299 = {
    # unused IN5-IN8: "Connect unused analog inputs directly to AVDD" (DS p. 6 note 2, §10.1.1)
    1: "AVDD_EEG",
    2: "AVDD_EEG",
    3: "AVDD_EEG",
    4: "AVDD_EEG",
    5: "AVDD_EEG",
    6: "AVDD_EEG",
    7: "AVDD_EEG",
    8: "AVDD_EEG",
    16: "E1_IN",
    14: "E2_IN",
    12: "E3_IN",
    10: "E4_IN",
    15: NC,
    13: NC,
    11: NC,
    9: NC,  # INxN internally routed to SRB1 (MISC1.SRB1)
    17: "REF_IN",
    18: NC,  # SRB2 unused: may float (§10.1.1)
    62: NC,
    60: NC,  # BIASIN float; BIASREF float with internal BIASREF_INT (§10.1.1)
    61: "BIAS_INV",
    63: "BIAS_OUT",
    24: "VREFP",
    25: "AVSS_EEG",  # VREFN to AVSS (§7.3)
    28: "VCAP1",
    30: "VCAP2",
    55: "VCAP3",
    26: "VCAP4",
    64: NC,
    27: NC,
    29: NC,
    19: "AVDD_EEG",
    21: "AVDD_EEG",
    22: "AVDD_EEG",
    56: "AVDD_EEG",
    59: "AVDD_EEG",
    54: "AVDD_EEG",  # AVDD1 star-connected to AVDD (§11)
    20: "AVSS_EEG",
    23: "AVSS_EEG",
    32: "AVSS_EEG",
    57: "AVSS_EEG",
    58: "AVSS_EEG",
    53: "AVSS_EEG",
    48: "3V3",
    50: "3V3",
    33: "GND",
    49: "GND",
    51: "GND",
    39: "EEG_CS",
    40: "SPI_SCLK",
    34: "SPI_MOSI",
    43: "SPI_MISO",
    47: "EEG_DRDY",
    38: "EEG_START",
    36: "EEG_RESET",
    35: "EEG_PWDN",
    37: "EEG_CLK",
    52: "EEG_CLKSEL",
    41: "GND",  # DAISY_IN to DGND when not daisy-chaining (§10.1.1)
    31: "GND",  # RESV1 to DGND (p. 7)
    42: "EEG_GPIO1",
    44: "EEG_GPIO2",
    45: "EEG_GPIO3",
    46: "EEG_GPIO4",
}
add("ADS1299-4", "ADS1299-4PAG", "TQFP64", ads1299, "EEG ADC, bipolar ±2.5 V", ref="U1")
add(
    "ADS131M04",
    "ADS131M04IPWR",
    "TSSOP20",
    {
        3: "M1P_IN",
        4: "M1N_IN",
        6: "M2P_IN",
        5: "M2N_IN",
        7: "S1_ADC",
        8: "SREF_ADC",
        10: "S2_ADC",
        9: "SREF_ADC",
        1: "AVDD_M",
        2: "GND",
        20: "3V3",
        19: "GND",
        18: "EMG_CAP",
        12: "EMG_CS",
        14: "SPI_SCLK",
        16: "SPI_MOSI",
        15: "SPI_MISO",
        13: "EMG_DRDY",
        11: "EMG_SYNC",
        17: "EMG_CLKIN",
    },
    "EMG + shadow-EEG ADC",
    ref="U2",
)
esp = {
    2: "3V3",
    3: "ESP_EN",
    27: "ESP_BOOT",
    39: "I2C_SDA",
    38: "I2C_SCL",
    15: NC,
    4: "VBAT_SENSE",
    5: "IMU_INT",
    6: "LED_STATUS",
    7: "BTN_USER",
    12: "EMG_SYNC",
    17: "EMG_CS",
    18: "EEG_CS",
    19: "SPI_MOSI",
    20: "SPI_SCLK",
    21: "SPI_MISO",
    22: "EEG_DRDY",
    8: "FPGA_CS",
    9: "FPGA_IRQ",
    10: "FPGA_MOSI",
    11: "EMG_CLKIN",
    1: "GND",
    40: "GND",
    41: "GND",
    13: "USB_DN",
    14: "USB_DP",
    23: "EMG_DRDY",
    28: NC,
    29: NC,
    30: NC,  # IO35-37 used by octal PSRAM on N16R8 (Table 3-1 note b)
    31: "EEG_PWDN",
    32: "FPGA_SCLK",
    33: "FPGA_MISO",
    34: "FPGA_AUX",
    35: "EEG_START",
    36: "UART_RX",
    37: "UART_TX",
    26: NC,
    16: NC,  # IO45/IO46 strapping: left free
    24: "EEG_RESET",
    25: NC,
}
add("ESP32-S3-WROOM-1", "ESP32-S3-WROOM-1-N16R8", "ESP", esp, "MCU", ref="U3")
add(
    "BMI270",
    "BMI270",
    "LGA14",
    # I2C mode per Table 22: CSB to VDDIO, SDO to GND (default address), aux/OIS unused.
    {
        8: "3V3",
        5: "3V3",
        7: "GND",
        6: "GND",
        12: "3V3",
        13: "I2C_SCL",
        14: "I2C_SDA",
        1: "GND",
        4: "IMU_INT",
        9: NC,
        2: NC,
        3: NC,
        10: NC,
        11: NC,
    },
    "IMU (I2C)",
    ref="U4",
)
# Shadow-lane buffers on ±2.5 V (unity-gain followers). U6B spare: follower on GND.
add(
    "OPA2333",
    "OPA2333AIDR",
    "SOIC8",
    {
        3: "S1_TAP",
        2: "S1_BUF",
        1: "S1_BUF",
        5: "S2_TAP",
        6: "S2_BUF",
        7: "S2_BUF",
        8: "AVDD_EEG",
        4: "AVSS_EEG",
    },
    "shadow-lane buffers (DNP-able)",
    ref="U5",
)
add(
    "OPA2333",
    "OPA2333AIDR",
    "SOIC8",
    {
        3: "SREF_TAP",
        2: "SREF_BUF",
        1: "SREF_BUF",
        5: "GND",
        6: "U6B_OUT",
        7: "U6B_OUT",
        8: "AVDD_EEG",
        4: "AVSS_EEG",
    },
    "shadow-lane REF buffer + spare",
    ref="U6",
)
add(
    "LM27761",
    "LM27761DSGR",
    "WSON8",
    {
        1: "VSYS",
        6: "VSYS",
        8: "CP_C1P",
        7: "CP_C1N",
        2: "GND",
        9: "GND",
        3: "CP_NEG",
        4: "AVSS_EEG",
        5: "NEG_FB",
    },
    "-2.5 V analog rail",
    ref="U7",
)
add(
    "BQ21040",
    "BQ21040DBVR",
    "SOT236",
    {6: "VBUS", 1: "CHG_TS", 4: "CHG_ISET", 5: "GND", 2: "VBAT", 3: "CHG_N"},
    "1S Li-ion charger, ~270 mA",
    ref="U8",
)
add(
    "LDO_DBV",
    "TLV75733PDBVR",
    "SOT235",
    {1: "VSYS", 3: "VSYS", 2: "GND", 5: "3V3", 4: NC},
    "3.3 V digital, 1 A",
    ref="U9",
)
add(
    "ESD4_DQA",
    "TPD4E02B04DQAR",
    "USON10",
    {
        1: "E1_EL",
        2: "E2_EL",
        4: "E3_EL",
        5: "E4_EL",
        3: "GND",
        8: "GND",
        6: NC,
        7: NC,
        9: NC,
        10: NC,
    },
    "EEG electrode ESD (bidirectional)",
    ref="U10",
)
add(
    "ESD4_DQA",
    "TPD4E02B04DQAR",
    "USON10",
    {1: "REF_EL", 2: "BIAS_EL", 4: NC, 5: NC, 3: "GND", 8: "GND", 6: NC, 7: NC, 9: NC, 10: NC},
    "REF/BIAS ESD",
    ref="U11",
)
add(
    "ESD4_DQA",
    "TPD4E02B04DQAR",
    "USON10",
    {
        1: "M1P_EL",
        2: "M1N_EL",
        4: "M2P_EL",
        5: "M2N_EL",
        3: "GND",
        8: "GND",
        6: NC,
        7: NC,
        9: NC,
        10: NC,
    },
    "EMG electrode ESD",
    ref="U12",
)
add(
    "LDO_DBV",
    "TPS7A2025PDBVR",
    "SOT235",
    {1: "VSYS", 3: "VSYS", 2: "GND", 5: "AVDD_EEG", 4: NC},
    "+2.5 V analog (ADS1299), 7 uVrms",
    ref="U13",
)
add(
    "LDO_DBV",
    "TPS7A2033PDBVR",
    "SOT235",
    {1: "VSYS", 3: "VSYS", 2: "GND", 5: "AVDD_M", 4: NC},
    "+3.3 V analog (ADS131M04)",
    ref="U14",
)
add(
    "ESD4_DQA",
    "TPD4EUSB30DQAR",
    "USON10",
    {
        1: "USB_DP",
        2: "USB_DN",
        4: "USB_CC1",
        5: "USB_CC2",
        3: "GND",
        8: "GND",
        6: NC,
        7: NC,
        9: NC,
        10: NC,
    },
    "USB ESD",
    ref="U15",
)

# --- connectors, switches --------------------------------------------------------------
add(
    "CONN_6",
    "EEG: E1 E2 E3 E4 REF BIAS",
    "H1x6",
    {1: "E1_EL", 2: "E2_EL", 3: "E3_EL", 4: "E4_EL", 5: "REF_EL", 6: "BIAS_EL"},
    ref="J1",
)
add(
    "CONN_4",
    "EMG: M1+ M1- M2+ M2-",
    "H1x4",
    {1: "M1P_EL", 2: "M1N_EL", 3: "M2P_EL", 4: "M2N_EL"},
    ref="J2",
)
add(
    "CONN_2x8",
    "FPGA (DE10 GPIO)",
    "H2x8",
    {
        1: "3V3",
        2: "GND",
        3: "FPGA_SCLK",
        4: "GND",
        5: "FPGA_MOSI",
        6: "GND",
        7: "FPGA_MISO",
        8: "GND",
        9: "FPGA_CS",
        10: "GND",
        11: "FPGA_IRQ",
        12: "GND",
        13: "FPGA_AUX",
        14: "GND",
        15: "EEG_DRDY",
        16: "GND",
    },
    ref="J3",
)
add(
    "QWIIC", "Qwiic", "SH4", {1: "GND", 2: "3V3", 3: "I2C_SDA", 4: "I2C_SCL", "MP": "GND"}, ref="J4"
)
add("CONN_BAT", "1S Li-ion (check pack polarity!)", "PH2", {1: "VBAT", 2: "GND"}, ref="J5")
add(
    "USB_C",
    "USB-C (charge/flash)",
    "USBC",
    {
        "A4": "VBUS",
        "B4": "VBUS",
        "A9": "VBUS",
        "B9": "VBUS",
        "A5": "USB_CC1",
        "B5": "USB_CC2",
        "A6": "USB_DP",
        "B6": "USB_DP",
        "A7": "USB_DN",
        "B7": "USB_DN",
        "A1": "GND",
        "B1": "GND",
        "A12": "GND",
        "B12": "GND",
        "S1": "GND",
        "A8": NC,
        "B8": NC,
    },
    ref="J6",
)
add("CONN_3", "UART: GND TX RX", "H1x3", {1: "GND", 2: "UART_TX", 3: "UART_RX"}, ref="J7")
add("SW_SPDT", "Power", "PCM12", {2: "VBAT", 1: "VSYS", 3: NC}, ref="SW1")
add("SW", "Reset", "BTN", {1: "ESP_EN", 2: "GND"}, ref="SW2")
add("SW", "Boot", "BTN", {1: "ESP_BOOT", 2: "GND"}, ref="SW3")
add("SW", "User", "BTN", {1: "BTN_USER", 2: "GND"}, ref="SW4")

# --- ADS1299 support -------------------------------------------------------------------
for net in ("AVDD_EEG",) * 5:  # 1 uF per AVDD pin to AVSS (pin table p. 6)
    c(net, "AVSS_EEG", "1uF")
c("AVDD_EEG", "AVSS_EEG", "1uF", note="AVDD1-AVSS1 (pin 54)")
c("AVDD_EEG", "AVSS_EEG", "10uF", "C0805", note="bulk, §11")
c("AVDD_EEG", "AVSS_EEG", "100nF", note="§11")
c("3V3", "GND", "1uF", note="DVDD pin 48")
c("3V3", "GND", "1uF", note="DVDD pin 50")
c("VCAP1", "AVSS_EEG", "100uF", "C1206", note="pin 28; C0G/tantalum if vibration (§11)")
c("VCAP2", "AVSS_EEG", "1uF")
c("VCAP3", "AVSS_EEG", "1uF")
c("VCAP3", "AVSS_EEG", "100nF")
c("VCAP4", "AVSS_EEG", "1uF")
c("VREFP", "AVSS_EEG", "10uF", "C0805", note="VREFP to VREFN(=AVSS), min 10 uF (p. 7)")
r("BIAS_OUT", "BIAS_INV", "1M", note="bias feedback, DS Fig. on p. 63")
c("BIAS_OUT", "BIAS_INV", "1.5nF", note="bias feedback, DS Fig. on p. 63")
r("EEG_CLKSEL", "3V3", "10k", note="internal osc; two-state pin via >=10k (p. 6 note 1)")
r("EEG_CLK", "GND", "10k", note="don't float digital inputs (§10.1.1)")
for net in ("EEG_GPIO1", "EEG_GPIO2", "EEG_GPIO3", "EEG_GPIO4"):
    r(net, "GND", "10k", note="unused GPIO to DGND via >=10k (p. 6)")
for net in ("EEG_START", "EEG_RESET", "EEG_PWDN"):
    r(net, "GND", "10k", note="held low until MCU drives (power-up §11.1)")
r("EEG_CS", "3V3", "10k")
# Electrode inputs: connector -> ESD -> 10k series (ROADMAP §3.2) -> ADC; C0G to REF_IN.
for i in (1, 2, 3, 4):
    r(f"E{i}_EL", f"E{i}_IN", "10k", note="electrode series resistor (safety rule 2)")
    c(f"E{i}_IN", "REF_IN", "1nF", note="C0G, differential (§12.1)")
r("REF_EL", "REF_IN", "10k", note="electrode series resistor")
r("BIAS_OUT", "BIAS_EL", "10k", note="bias electrode series resistor")

# --- shadow lane (ADS131M04 CH2/CH3) ---------------------------------------------------
r("E1_EL", "S1_TAP", "10k", note="shadow tap series R")
r("E2_EL", "S2_TAP", "10k", note="shadow tap series R")
r("REF_EL", "SREF_TAP", "10k", note="shadow tap series R")
r("S1_BUF", "S1_ADC", "4.99k", note="limits clamp current if a buffer rails")
r("S2_BUF", "S2_ADC", "4.99k")
r("SREF_BUF", "SREF_ADC", "4.99k")
c("S1_ADC", "SREF_ADC", "1nF", note="C0G")
c("S2_ADC", "SREF_ADC", "1nF", note="C0G")
# Clamp each shadow-lane ADC input to GND. A buffer on +-2.5 V can swing to -2.5 V, below the
# ADS131M04 abs max of AGND - 1.6 V (DS §6.1); its own input diode only clamps negative
# inputs at reverse breakdown (§8.3.1). BAW56 VF <= 715 mV at 1 mA (DS p. 2), and the fault
# current is (2.5 - 0.7) V / 4.99 kOhm ~ 0.36 mA. ADR-0006 issue 1.
add(
    "D_DUAL_CA",
    "BAW56",
    "SOT23",
    {1: "S1_ADC", 2: "S2_ADC", 3: "GND"},
    "shadow-lane clamp",
    ref="D4",
)
add(
    "D_DUAL_CA",
    "BAW56",
    "SOT23",
    {1: "SREF_ADC", 2: None, 3: "GND"},
    "shadow-lane clamp (K2 unused)",
    ref="D5",
)
for _ in range(2):
    c("AVDD_EEG", "GND", "100nF", note="OPA2333 V+")
    c("AVSS_EEG", "GND", "100nF", note="OPA2333 V-")

# --- EMG lane (ADS131M04 CH0/CH1) ------------------------------------------------------
for el, inp in (("M1P", "M1P"), ("M1N", "M1N"), ("M2P", "M2P"), ("M2N", "M2N")):
    r(f"{el}_EL", f"{inp}_IN", "10k", note="electrode series resistor")
c("M1P_IN", "M1N_IN", "1nF", note="C0G")
c("M2P_IN", "M2N_IN", "1nF", note="C0G")
c("AVDD_M", "GND", "1uF", note="AVDD 1 uF (Table 5-1)")
c("AVDD_M", "GND", "100nF")
c("EMG_CAP", "GND", "220nF", note="CAP 220 nF to DGND (Table 5-1)")
c("3V3", "GND", "1uF", note="ADS131M04 DVDD")
r("EMG_CS", "3V3", "10k")
r("EMG_SYNC", "3V3", "10k")

# --- ESP32 -----------------------------------------------------------------------------
c("3V3", "GND", "10uF", "C0805", note="module supply")
c("3V3", "GND", "100nF")
r("ESP_EN", "3V3", "10k", note="EN RC 10k/1uF (DS §9)")
c("ESP_EN", "GND", "1uF", note="EN RC")
r("ESP_BOOT", "3V3", "10k")
r("I2C_SDA", "3V3", "4.7k")
r("I2C_SCL", "3V3", "4.7k")
r("LED_STATUS", "LED_STATUS_A", "1k")
add("LED", "green", "LED", {1: "GND", 2: "LED_STATUS_A"}, "status")
c("3V3", "GND", "100nF", note="BMI270 VDD (§7.2)")
c("3V3", "GND", "100nF", note="BMI270 VDDIO (§7.2)")

# --- power -----------------------------------------------------------------------------
c("VBUS", "GND", "1uF", note="BQ21040 VIN 1-10 uF")
c("VBAT", "GND", "4.7uF", "C0805", note="BQ21040 OUT 1-10 uF")
r("CHG_ISET", "GND", "2.0k", note="I = 540 AΩ / 2.0k ≈ 270 mA (KISET, DS §7)")
r("CHG_TS", "GND", "10k", note="no NTC: TS to VSS via 10k (pin table)")
r("VBUS", "CHG_LED_A", "2.2k")
add("LED", "red (charging)", "LED", {1: "CHG_N", 2: "CHG_LED_A"})
r("VBUS", "USB_LED_A", "2.2k")
add("LED", "yellow: USB connected - check isolation", "LED", {1: "GND", 2: "USB_LED_A"})
r("USB_CC1", "GND", "5.1k", note="USB-C sink Rd")
r("USB_CC2", "GND", "5.1k", note="USB-C sink Rd")
c("VSYS", "GND", "10uF", "C0805", note="TLV757 IN")
c("3V3", "GND", "10uF", "C0805", note="TLV757 OUT")
c("VSYS", "GND", "1uF", note="TPS7A2025 IN (CIN 1 uF)")
c("AVDD_EEG", "GND", "1uF", note="TPS7A2025 OUT (COUT >= 1 uF)")
c("VSYS", "GND", "1uF", note="TPS7A2033 IN")
c("AVDD_M", "GND", "1uF", note="TPS7A2033 OUT")
c("CP_C1P", "CP_C1N", "1uF", note="LM27761 C1 flying")
c("VSYS", "GND", "4.7uF", "C0805", note="LM27761 C2 input")
c("CP_NEG", "GND", "4.7uF", "C0805", note="LM27761 C3 CPOUT")
c("AVSS_EEG", "GND", "2.2uF", note="LM27761 C4 LDO out")
r("AVSS_EEG", "NEG_FB", "105k", note="R1: VOUT = -1.22(R1+R2)/R2 = -2.50 V")
r("NEG_FB", "GND", "100k", note="R2 >= 50k (DS 7.2.2.4)")
r("VBAT", "VBAT_SENSE", "100k")
r("VBAT_SENSE", "GND", "100k")
c("VBAT_SENSE", "GND", "100nF")

# --- test points, holes, flags -----------------------------------------------------------
for net in (
    "3V3",
    "VSYS",
    "AVDD_EEG",
    "AVSS_EEG",
    "AVDD_M",
    "GND",
    "BIAS_OUT",
    "EEG_DRDY",
    "SPI_SCLK",
    "GND",
    "VBAT",
):
    add("TP", net, "TP", {1: net})
for _ in range(4):
    add("HOLE", "M2.5", "HOLE", {})
# Nets driven only through passive parts need a PWR_FLAG for ERC.
for net in ("GND", "VBUS", "VSYS"):
    add("PWR_FLAG", "PWR_FLAG", "", {1: net})

# ----------------------------------------------------------------------------- schematic writer


def q(s: str) -> str:
    return '"' + s.replace('"', "'") + '"'


def uid(*key: str) -> str:
    return str(uuid.uuid5(NS, "/".join(key)))


def fmt(v: float) -> str:
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def sym_geometry(s: Sym) -> tuple[float, float, list[tuple[Pin, float, float, int]]]:
    """Body half-width, half-height and pin (pin, x, y, angle) in symbol coords (Y up)."""
    longest = max([len(p.name) for p in s.left + s.right] + [1])
    hw = max(2.54, math.ceil((longest * 0.85 + 1.5) / GRID) * GRID)
    if s.name in ("R", "C", "LED", "SW", "TP", "PWR_FLAG"):
        hw = 2.54
    n = max(len(s.left), len(s.right), 1)
    hh = (n + 1) * GRID / 2
    hh = math.ceil(hh / 1.27) * 1.27
    out = []
    for side, plist in (("L", s.left), ("R", s.right)):
        for i, p in enumerate(plist):
            y = ((len(plist) - 1) / 2 - i) * GRID
            x = -hw - GRID if side == "L" else hw + GRID
            out.append((p, x, y, 0 if side == "L" else 180))
    return hw, hh, out


def lib_symbol(s: Sym) -> str:
    hw, hh, geo = sym_geometry(s)
    power = s.name == "PWR_FLAG"
    hide_names = s.name in ("R", "C", "SW", "TP", "PWR_FLAG")
    lines = [
        f'(symbol "nd:{s.name}"' + (" (power)" if power else ""),
        (
            f"  (pin_numbers {'hide' if hide_names else ''})".replace("( ", "(").replace(" )", ")")
            if hide_names
            else ""
        ),
        f"  (pin_names (offset 1.016){' hide' if hide_names else ''})",
        f"  (exclude_from_sim no) (in_bom {'no' if power or s.name in ('TP', 'HOLE') else 'yes'}) "
        f"(on_board {'no' if power else 'yes'})",
        f'  (property "Reference" {q(s.prefix)} (at 0 {fmt(hh + 1.27)} 0)'
        " (effects (font (size 1.27 1.27))))",
        f'  (property "Value" {q(s.name)} (at 0 {fmt(-hh - 1.27)} 0)'
        " (effects (font (size 1.27 1.27))))",
        '  (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
        '  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
        f'  (symbol "{s.name}_0_1"'
        f" (rectangle (start {fmt(-hw)} {fmt(hh)}) (end {fmt(hw)} {fmt(-hh)})"
        " (stroke (width 0.254) (type default)) (fill (type background))))",
        f'  (symbol "{s.name}_1_1"',
    ]
    for p, x, y, ang in geo:
        lines.append(
            f"    (pin {p.etype} line (at {fmt(x)} {fmt(y)} {ang}) (length 2.54)"
            f" (name {q(p.name)} (effects (font (size 1.27 1.27))))"
            f" (number {q(p.num)} (effects (font (size 1.27 1.27)))))"
        )
    lines += ["  )", ")"]
    return "\n".join(line for line in lines if line)


def write_schematic() -> dict:
    used = sorted({p.sym for p in PARTS})
    body: list[str] = []
    x0, x, y, row_h = 25.4, 25.4, 30.48, 0.0
    page_w = 800.0
    netmap: dict[str, dict[str, str | None]] = {}
    for part in PARTS:
        s = SYMBOLS[part.sym]
        hw, hh, geo = sym_geometry(s)
        label_room = 30.48 if part.sym not in ("R", "C", "LED", "SW", "TP", "PWR_FLAG") else 22.86
        w = 2 * (hw + GRID) + 2 * label_room
        h = 2 * hh + 10.16
        if x + w > x0 + page_w:
            x, y, row_h = x0, y + row_h, 0.0
        cx = math.floor((x + w / 2) / GRID) * GRID
        cy = math.floor((y + h / 2) / GRID) * GRID
        row_h = max(row_h, h)
        x += w
        su = uid("sym", part.ref)
        is_flag = part.sym == "PWR_FLAG"
        in_bom = "no" if is_flag or part.sym in ("TP", "HOLE") else "yes"
        body.append(
            f'(symbol (lib_id "nd:{part.sym}") (at {fmt(cx)} {fmt(cy)} 0) (unit 1)'
            f" (exclude_from_sim no) (in_bom {in_bom})"
            f" (on_board {'no' if is_flag else 'yes'}) (dnp {'yes' if part.dnp else 'no'})"
            f" (uuid {q(su)})\n"
            f'  (property "Reference" {q(part.ref)} (at {fmt(cx)} {fmt(cy - hh - 1.27)} 0)'
            " (effects (font (size 1.27 1.27))))\n"
            f'  (property "Value" {q(part.value)} (at {fmt(cx)} {fmt(cy + hh + 1.27)} 0)'
            " (effects (font (size 1.27 1.27))))\n"
            f'  (property "Footprint" {q(part.fp)} (at {fmt(cx)} {fmt(cy)} 0)'
            " (effects (font (size 1.27 1.27)) hide))\n"
            f'  (property "Datasheet" "" (at {fmt(cx)} {fmt(cy)} 0)'
            " (effects (font (size 1.27 1.27)) hide))\n"
            f'  (property "Note" {q(part.note)} (at {fmt(cx)} {fmt(cy)} 0)'
            " (effects (font (size 1.27 1.27)) hide))\n"
            + "".join(
                f'  (pin {q(p.num)} (uuid {q(uid("pin", part.ref, p.num))}))\n' for p, *_ in geo
            )
            + f'  (instances (project "neurocore" (path "/{ROOT_UUID}"'
            + f" (reference {q(part.ref)}) (unit 1))))\n)"
        )
        pinnets: dict[str, str | None] = {}
        for p, px, py, ang in geo:
            if p.num not in part.nets:
                raise KeyError(f"{part.ref} pin {p.num} ({p.name}) not assigned")
            net = part.nets[p.num]
            pinnets[p.num] = net
            sx, sy = cx + px, cy - py
            if net is None:
                body.append(
                    f"(no_connect (at {fmt(sx)} {fmt(sy)}) (uuid {q(uid('nc', part.ref, p.num))}))"
                )
                continue
            angle = 180 if ang == 0 else 0
            just = "right bottom" if angle == 180 else "left bottom"
            body.append(
                f"(label {q(net)} (at {fmt(sx)} {fmt(sy)} {angle})"
                f" (effects (font (size 1.27 1.27)) (justify {just}))"
                f" (uuid {q(uid('lbl', part.ref, p.num))}))"
            )
        extra = set(part.nets) - {p.num for p, *_ in geo}
        if extra:
            raise KeyError(f"{part.ref}: nets for unknown pins {sorted(extra)}")
        if not is_flag:
            netmap[part.ref] = pinnets

    sch = "\n".join(
        [
            '(kicad_sch (version 20231120) (generator "neurodrive_gen") (generator_version "1.0")',
            f"  (uuid {q(ROOT_UUID)})",
            '  (paper "A0")',
            '  (title_block (title "NeuroCore Rev A - UNVERIFIED DRAFT") (date "2026-11-19")'
            ' (rev "A0")'
            ' (comment 1 "Generated by hardware/neurocore/gen/design.py - see ADR-0006"))',
            "  (lib_symbols",
            *(lib_symbol(SYMBOLS[n]) for n in used),
            "  )",
            *body,
            '  (sheet_instances (path "/" (page "1")))',
            ")",
        ]
    )
    (OUT / "neurocore.kicad_sch").write_text(sch + "\n", encoding="utf-8", newline="\n")
    return netmap


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    netmap = write_schematic()
    lib = "\n".join(
        lib_symbol(SYMBOLS[n]).replace('(symbol "nd:', '(symbol "', 1) for n in sorted(SYMBOLS)
    )
    (OUT / "nd.kicad_sym").write_text(
        '(kicad_symbol_lib (version 20231120) (generator "neurodrive_gen")\n' + lib + "\n)\n",
        encoding="utf-8",
        newline="\n",
    )
    (OUT / "sym-lib-table").write_text(
        "(sym_lib_table\n  (version 7)\n"
        '  (lib (name "nd")(type "KiCad")(uri "${KIPRJMOD}/nd.kicad_sym")'
        '(options "")(descr "NeuroCore"))\n)\n',
        encoding="utf-8",
        newline="\n",
    )
    pro = OUT / "neurocore.kicad_pro"
    if not pro.exists():
        pro.write_text(
            json.dumps({"meta": {"filename": "neurocore.kicad_pro", "version": 1}}, indent=2) + "\n"
        )
    design = {
        "root_uuid": ROOT_UUID,
        "parts": [
            {
                "ref": p.ref,
                "value": p.value,
                "footprint": p.fp,
                "symbol_uuid": uid("sym", p.ref),
                "note": p.note,
                "dnp": p.dnp,
                "pins": netmap.get(p.ref, {}),
            }
            for p in PARTS
            if p.sym != "PWR_FLAG"
        ],
    }
    (OUT / "design.json").write_text(
        json.dumps(design, indent=1) + "\n", encoding="utf-8", newline="\n"
    )
    nets = {n for p in design["parts"] for n in p["pins"].values() if n}
    print(f"{len(design['parts'])} parts, {len(nets)} nets -> {OUT}")


if __name__ == "__main__":
    main()
