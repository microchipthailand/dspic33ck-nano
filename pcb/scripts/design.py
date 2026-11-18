"""Single source of truth for the dsPIC33CK256MC005 Nano-style board.
Each part: ref, lib_id, value, footprint, {pin_number: net}, schematic (x, y) in mm, extra fields.
"""

# dsPIC33CK256MC005-I/PT pin names, from Microchip's DFP device file DSPIC33CK256MC005.PIC
DSPIC_PINS = {
    1: "RP46/PWM1H/RB14", 2: "RP47/PWM1L/RB15", 3: "RP60/RC12", 4: "RP61/RC13",
    5: "~{MCLR}", 6: "ANN0/RP77/RD13", 7: "AN12/RP48/RC0", 8: "OA1OUT/AN0/CMP1A/IBIAS0/RA0",
    9: "OA1IN-/AN16/RA1", 10: "OA1IN+/AN9/RA2", 11: "DACOUT1/AN3/CMP1C/RA3", 12: "OA3OUT/AN4/IBIAS3/RA4",
    13: "AVDD", 14: "AVSS", 15: "OA3IN-/AN13/CMP1B/ISRC0/RP49/RC1", 16: "OA3IN+/AN14/CMP2B/ISRC1/RP50/RC2",
    17: "AN19/IBIAS1/RP54/RC6", 18: "VDD", 19: "VSS", 20: "AN15/CMP2A/IBIAS2/RP51/RC3",
    21: "OSCI/CLKI/AN5/RP32/RB0", 22: "OSCO/CLKO/AN6/RP33/RB1", 23: "AN18/ISRC3/RP74/RD10", 24: "AN7/ISRC2/RP55/RC7",
    25: "OA2OUT/AN1/CMP1D/CMP2D/RP34/INT0/RB2", 26: "PGD2/OA2IN-/AN8/RP35/RB3", 27: "PGC2/OA2IN+/AN17/RP36/RB4",
    28: "RP56/ASDA1/SCK2/RC8", 29: "RP57/ASCL1/SDI2/RC9", 30: "RP72/SDO2/PCI19/RD8", 31: "VSS", 32: "VDD",
    33: "PGD3/RP37/RB5", 34: "PGC3/RP38/RB6", 35: "TDO/AN2/RP39/RB7", 36: "PGD1/AN10/RP40/SCL1/RB8",
    37: "PGC1/AN11/RP41/SDA1/RB9", 38: "RP52/RC4", 39: "RP53/RC5", 40: "RP58/RC10", 41: "RP59/RC11",
    42: "VSS", 43: "VDD", 44: "RP65/PWM4H/RD1", 45: "TMS/RP42/PWM3H/RB10", 46: "TCK/RP43/PWM3L/RB11",
    47: "TDI/RP44/PWM2H/RB12", 48: "RP45/PWM2L/RB13",
}
POWER = {13: "+3V3A", 14: "GND", 18: "+3V3", 19: "GND", 31: "GND", 32: "+3V3", 42: "GND", 43: "+3V3", 5: "MCLR"}


def port(p):
    return POWER.get(p) or DSPIC_PINS[p].split("/")[-1]


u1_nets = {str(p): port(p) for p in range(1, 49)}

# Header order follows the chip's pin order around the package so routing stays untangled.
LEFT = ["+5V", "GND"] + [port(p) for p in [44, 45, 46, 47, 48, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 17]]
RIGHT = ["+3V3", "GND"] + [port(p) for p in [41, 40, 39, 38, 37, 36, 35, 34, 33, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20]]
assert len(LEFT) == 22 and len(RIGHT) == 22

R0805 = "Resistor_SMD:R_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"

PARTS = [
    # ref, lib_id, value, footprint, pins, (x, y), mpn
    ("U1", "Board:dsPIC33CK256MC005-I_PT", "dsPIC33CK256MC005-I/PT", "Package_QFP:TQFP-48_7x7mm_P0.5mm", u1_nets, (200, 140), "dsPIC33CK256MC005-I/PT"),
    ("U2", "Board:MCP1703A-3302_SOT89", "MCP1703A-3302E/MB", "Package_TO_SOT_SMD:SOT-89-3", {"1": "GND", "2": "+5V", "3": "+3V3"}, (90, 60), "MCP1703AT-3302E/MB"),
    ("J1", "Connector:USB_C_Receptacle_PowerOnly_6P", "USB-C power", "Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x-0190_6P_TopMnt_Horizontal",
     {"A5": "CC1", "B5": "CC2", "A9": "VBUS", "B9": "VBUS", "A12": "GND", "B12": "GND", "SH": "GND"}, (35, 60), "GCT USB4125-GF-A"),
    ("D1", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", {"1": "+5V", "2": "VBUS"}, (60, 40), "B5819W"),
    ("R1", "Device:R", "5.1k", R0805, {"1": "CC1", "2": "GND"}, (55, 70), "0805 5.1k 1%"),
    ("R2", "Device:R", "5.1k", R0805, {"1": "CC2", "2": "GND"}, (62, 70), "0805 5.1k 1%"),
    ("C1", "Device:C", "1uF", C0805, {"1": "+5V", "2": "GND"}, (75, 70), "0805 1uF 16V X7R"),
    ("C2", "Device:C", "1uF", C0805, {"1": "+3V3", "2": "GND"}, (105, 70), "0805 1uF 16V X7R"),
    ("C3", "Device:C", "10uF", C0805, {"1": "+3V3", "2": "GND"}, (112, 70), "0805 10uF 10V X5R"),
    ("C4", "Device:C", "100nF", C0805, {"1": "+3V3", "2": "GND"}, (130, 100), "0805 100nF 16V X7R"),
    ("C5", "Device:C", "100nF", C0805, {"1": "+3V3", "2": "GND"}, (137, 100), "0805 100nF 16V X7R"),
    ("C6", "Device:C", "100nF", C0805, {"1": "+3V3", "2": "GND"}, (144, 100), "0805 100nF 16V X7R"),
    ("FB1", "Device:FerriteBead_Small", "600R@100MHz", "Inductor_SMD:L_0603_1608Metric", {"1": "+3V3", "2": "+3V3A"}, (130, 125), "BLM18AG601SN1D"),
    ("C7", "Device:C", "100nF", C0805, {"1": "+3V3A", "2": "GND"}, (140, 135), "0805 100nF 16V X7R"),
    ("C8", "Device:C", "1uF", C0805, {"1": "+3V3A", "2": "GND"}, (147, 135), "0805 1uF 16V X7R"),
    # MCLR: 10k pull-up on the pin; RC + button sit behind 470R so the PICkit's VPP edge is not loaded by C9.
    ("R3", "Device:R", "10k", R0805, {"1": "+3V3", "2": "MCLR"}, (125, 175), "0805 10k 1%"),
    ("R7", "Device:R", "470", R0805, {"1": "MCLR", "2": "MCLR_RC"}, (135, 175), "0805 470R 1%"),
    ("C9", "Device:C", "100nF", C0805, {"1": "MCLR_RC", "2": "GND"}, (145, 190), "0805 100nF 16V X7R"),
    ("SW1", "Switch:SW_Push", "RESET", "Button_Switch_SMD:SW_SPST_TL3342", {"1": "MCLR_RC", "2": "GND"}, (155, 200), "E-Switch TL3342 series (4.2x3.2mm SMD tact)"),
    ("R4", "Device:R", "1k", R0805, {"1": "+3V3", "2": "PWR_LED"}, (90, 110), "0805 1k 1%"),
    ("D2", "Device:LED", "PWR green", "LED_SMD:LED_0603_1608Metric", {"1": "GND", "2": "PWR_LED"}, (90, 125), "0603 green LED"),
    ("R5", "Device:R", "1k", R0805, {"1": "+3V3", "2": "USR_LED"}, (120, 110), "0805 1k 1%"),
    ("D3", "Device:LED", "USER yellow", "LED_SMD:LED_0603_1608Metric", {"1": "RC13", "2": "USR_LED"}, (120, 125), "0603 yellow LED"),
    ("R6", "Device:R", "10k", R0805, {"1": "+3V3", "2": "RC12"}, (110, 160), "0805 10k 1%"),
    ("SW2", "Switch:SW_Push", "USER", "Button_Switch_SMD:SW_SPST_TL3342", {"1": "RC12", "2": "GND"}, (110, 180), "E-Switch TL3342 series (4.2x3.2mm SMD tact)"),
    ("J2", "Connector_Generic:Conn_01x22", "LEFT", "Connector_PinHeader_2.54mm:PinHeader_1x22_P2.54mm_Vertical",
     {str(i + 1): n for i, n in enumerate(LEFT)}, (290, 90), "1x22 2.54mm header"),
    ("J3", "Connector_Generic:Conn_01x22", "RIGHT", "Connector_PinHeader_2.54mm:PinHeader_1x22_P2.54mm_Vertical",
     {str(i + 1): n for i, n in enumerate(RIGHT)}, (340, 90), "1x22 2.54mm header"),
    ("J4", "Connector_Generic:Conn_01x06", "ICSP", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
     {"1": "MCLR", "2": "+3V3", "3": "GND", "4": "RB5", "5": "RB6", "6": None}, (60, 160), "1x6 2.54mm header (PICkit)"),
    # UART header for a 3.3 V USB-serial adapter: 1=VDD(3V3) 2=TX(RC10, MCU output) 3=RX(RC11, MCU input) 4=GND
    ("J5", "Connector_Generic:Conn_01x04", "UART", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
     {"1": "+3V3", "2": "RC10", "3": "RC11", "4": "GND"}, (60, 195), "1x4 2.54mm header (UART)"),
    ("#FLG01", "power:PWR_FLAG", "PWR_FLAG", "", {"1": "+5V"}, (40, 110), None),
    ("#FLG02", "power:PWR_FLAG", "PWR_FLAG", "", {"1": "GND"}, (50, 110), None),
    ("#FLG03", "power:PWR_FLAG", "PWR_FLAG", "", {"1": "+3V3A"}, (60, 110), None),
]
