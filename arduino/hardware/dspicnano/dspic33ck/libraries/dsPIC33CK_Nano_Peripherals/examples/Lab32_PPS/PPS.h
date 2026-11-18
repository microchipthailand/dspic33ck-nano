/*
 * PPS.h - Peripheral Pin Select helpers for the dsPIC33CK256MC005
 *
 *   ppsOutput(61, PPS_OCM1);     // SCCP1 output -> RC13 (RP61)
 *   ppsOutput(61, PPS_NONE);     // RC13 back to a plain port pin
 *   ppsInput(_INT1R, 60);        // INT1 input  <- RC12 (RP60)   (a macro)
 *   ppsDump();                   // print every mapping that is set
 *
 * ----------------------------------------------------------------------------
 * PPS IN ONE PICTURE
 *   OUTPUTS: every remappable pin RPn has a byte "RPnR" (in RPOR0..26). The
 *            number written there picks which peripheral output drives the
 *            pin. 0 = none: the pin is an ordinary port pin (LATx).
 *            One output signal may drive several pins at the same time.
 *   INPUTS:  every peripheral input has a byte (in RPINR0..49), e.g. INT1R,
 *            U1RXR. The number written there is the RP number of the pin it
 *            listens to. 0 = VSS. (The MPLAB simulator shows 0xFF for an
 *            unused input; either way ppsDump() leaves it out.)
 *            Several inputs may listen to the same pin.
 *   RP numbers on this chip
 *     RP32-RP47 = RB0-RB15     RP48-RP61 = RC0-RC13
 *     RP65 = RD1  RP72 = RD8  RP74 = RD10  RP77 = RD13
 *     RP176-RP181 = RPV0-RPV5: VIRTUAL pins with no pad. An output written to
 *                   RP176 can be read back by an input set to 176: a wire
 *                   inside the chip.
 *     RA0-RA4 are not remappable (no RP number).
 *   LOCK:    RPCON.IOLOCK = 1 blocks every write to RPORx / RPINRx.
 *            __builtin_write_RPCON(0x0000) unlocks, (0x0800) locks. The
 *            unlock needs a key sequence, which the builtin does for you.
 *            This chip has no "one-way" lock: unlock again at any time.
 *   The pin still needs its TRIS bit (output = 0) and, for a pin with an
 *   analog function, ANSEL = 0. PPS only connects the signal.
 * ----------------------------------------------------------------------------
 */
#ifndef LAB32_PPS_H
#define LAB32_PPS_H

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

/* output function codes (the device header's _RPOUT_xxx values) */
enum {
    PPS_NONE = 0, PPS_U1TX = 1, PPS_U1RTS = 2, PPS_U2TX = 3, PPS_U2RTS = 4,
    PPS_SDO1 = 5, PPS_SCK1 = 6, PPS_SS1 = 7, PPS_SDO2 = 8, PPS_SCK2 = 9, PPS_SS2 = 10,
    PPS_REFO = 14, PPS_OCM1 = 15, PPS_OCM2 = 16, PPS_OCM3 = 17, PPS_OCM4 = 18,
    PPS_CAN1TX = 21, PPS_CMP1 = 23, PPS_U3TX = 27, PPS_U3RTS = 28,
    PPS_PWM4H = 34, PPS_PWM4L = 35, PPS_CLC1 = 40, PPS_CLC2 = 41,
    PPS_PTGTRG24 = 46, PPS_PTGTRG25 = 47, PPS_SENT1 = 48, PPS_CLC3 = 59, PPS_CLC4 = 60
};

const char *ppsOutputName(uint8_t code)
{
    switch (code) {
    case 0: return "-";          case 1: return "U1TX";       case 2: return "U1RTS";
    case 3: return "U2TX";       case 4: return "U2RTS";      case 5: return "SDO1";
    case 6: return "SCK1";       case 7: return "SS1";        case 8: return "SDO2";
    case 9: return "SCK2";       case 10: return "SS2";       case 14: return "REFO";
    case 15: return "OCM1 (SCCP1)"; case 16: return "OCM2 (SCCP2)"; case 17: return "OCM3 (SCCP3)";
    case 18: return "OCM4 (SCCP4)"; case 21: return "CAN1TX";  case 23: return "CMP1";
    case 24: return "CMP2";      case 27: return "U3TX";      case 28: return "U3RTS";
    case 34: return "PWM4H";     case 35: return "PWM4L";     case 36: return "PWMEA";
    case 37: return "PWMEB";     case 40: return "CLC1OUT";   case 41: return "CLC2OUT";
    case 44: return "PWMEC";     case 45: return "PWMED";     case 46: return "PTGTRG24";
    case 47: return "PTGTRG25";  case 48: return "SENT1OUT";  case 59: return "CLC3OUT";
    case 60: return "CLC4OUT";   case 61: return "U1DTR";     case 62: return "U2DTR";
    case 63: return "U3DTR";     default: return "?";
    }
}

/* RP number -> "RC13" etc. Returns false if the RP number does not exist. */
bool ppsPinName(uint8_t rp, char *out)
{
    if (rp >= 32 && rp <= 47)       { sprintf(out, "RB%u", rp - 32); return true; }
    if (rp >= 48 && rp <= 61)       { sprintf(out, "RC%u", rp - 48); return true; }
    if (rp == 65) { strcpy(out, "RD1");  return true; }
    if (rp == 72) { strcpy(out, "RD8");  return true; }
    if (rp == 74) { strcpy(out, "RD10"); return true; }
    if (rp == 77) { strcpy(out, "RD13"); return true; }
    if (rp >= 176 && rp <= 181)     { sprintf(out, "RPV%u", rp - 176); return true; }
    if (rp == 0)  { strcpy(out, "VSS");  return true; }
    sprintf(out, "RP%u", rp);
    return false;
}

/* The RPnR byte of a pin. RPOR0 holds RP32 (low byte) and RP33 (high byte),
 * and so on without gaps up to RP77 (RPOR22); the virtual pins RP176-181
 * follow in RPOR24-26. Checked against the addresses in p33CK256MC005.gld. */
volatile uint8_t *ppsOutReg(uint8_t rp)
{
    volatile uint8_t *base = (volatile uint8_t *)&RPOR0;
    if (rp >= 32 && rp <= 77)   return base + (rp - 32);
    if (rp >= 176 && rp <= 181) return base + 48 + (rp - 176);
    return 0;
}

inline void ppsUnlock() { __builtin_write_RPCON(0x0000); }
inline void ppsLock()   { __builtin_write_RPCON(0x0800); }
inline bool ppsLocked() { return RPCONbits.IOLOCK; }

/* Connect output function `code` to pin RPn (unlocks and locks again). */
bool ppsOutput(uint8_t rp, uint8_t code)
{
    volatile uint8_t *r = ppsOutReg(rp);
    if (!r) return false;
    bool wasLocked = ppsLocked();
    ppsUnlock();
    *r = code;
    if (wasLocked) ppsLock();
    return true;
}

inline uint8_t ppsOutputOf(uint8_t rp) { volatile uint8_t *r = ppsOutReg(rp); return r ? *r : 0; }

/* Inputs are named bit fields, so this is a macro: ppsInput(_INT1R, 60) */
#define ppsInput(field, rp) do { bool _l = ppsLocked(); ppsUnlock(); field = (rp); if (_l) ppsLock(); } while (0)

/* Print every output and input that is connected. */
void ppsDump()
{
    char pin[8];
    Serial.println("# PPS outputs (pin <- peripheral):");
    const uint8_t rps[] = { 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
                            48,49,50,51,52,53,54,55,56,57,58,59,60,61, 65,72,74,77, 176,177,178,179,180,181 };
    uint8_t n = 0;
    for (uint8_t i = 0; i < sizeof(rps); i++) {
        uint8_t code = ppsOutputOf(rps[i]);
        if (!code) continue;
        ppsPinName(rps[i], pin);
        Serial.print("    RP"); Serial.print(rps[i]); Serial.print(" = "); Serial.print(pin);
        Serial.print("  <- "); Serial.print(ppsOutputName(code)); Serial.print(" ("); Serial.print(code); Serial.println(")");
        n++;
    }
    if (!n) Serial.println("    (none)");

    Serial.println("# PPS inputs (peripheral <- pin):");
    struct { const char *name; uint8_t rp; } in[] = {
        { "INT1", _INT1R }, { "INT2", _INT2R }, { "INT3", _INT3R }, { "T1CK", _T1CKR },
        { "ICM1", _ICM1R }, { "ICM2", _ICM2R }, { "ICM3", _ICM3R }, { "ICM4", _ICM4R },
        { "U1RX", _U1RXR }, { "U2RX", _U2RXR }, { "U3RX", _U3RXR },
        { "SDI1", _SDI1R }, { "SCK1IN", _SCK1R }, { "SS1IN", _SS1R },
        { "SDI2", _SDI2R }, { "CAN1RX", _CAN1RXR }, { "REFOI", _REFOIR },
    };
    n = 0;
    for (uint8_t i = 0; i < sizeof(in) / sizeof(in[0]); i++) {
        if (!ppsPinName(in[i].rp, pin) || !in[i].rp) continue;    /* VSS or not a pin: unused */
        Serial.print("    "); Serial.print(in[i].name); Serial.print(" <- RP"); Serial.print(in[i].rp);
        Serial.print(" = "); Serial.println(pin);
        n++;
    }
    if (!n) Serial.println("    (none)");
    Serial.print("# IOLOCK = "); Serial.println(ppsLocked() ? "1 (locked)" : "0 (open)");
}

#endif
