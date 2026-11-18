/*
 * Lab 34 - CRC: the hardware CRC engine
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * Goal:  1. Learn what a CRC is and how one CRC differs from another (the six
 *           numbers in CRC.h).
 *        2. Let the chip's CRC engine compute CRC-8, CRC-16 and CRC-32 and
 *           check each against a software version and against the standard
 *           test value.
 *        3. Measure: hardware vs software speed.
 *        4. Use it for real: check a Modbus frame (Lab 24), give the
 *           program flash a "fingerprint", and type in your own text.
 *
 * Hardware: nothing to wire. Serial Monitor on J5 (115200), line ending "Newline".
 *
 * Serial commands:
 *   ?        help + self-test of every CRC in the catalogue
 *   t        self-test again
 *   s        speed test: 1024 bytes, hardware vs software (bitwise and table)
 *   m        Modbus RTU frame check (good frame, then one with a flipped bit)
 *   f        flash fingerprint: CRC-32 of this sketch in program flash
 *   a        probe the engine's "alternate" mode (MOD = 1) on your chip
 *   any text CRC-8, CRC-16/CCITT, CRC-16/MODBUS and CRC-32 of the text
 *
 * ----------------------------------------------------------------------------
 * WHAT A CRC IS
 *   The data is treated as one long binary number and divided by a fixed
 *   "generator polynomial" with XOR instead of subtraction. The remainder is
 *   the CRC. It is cheap to compute bit by bit with a shift register, and it
 *   catches every single-bit and two-bit error, every odd number of bit errors
 *   (if the polynomial has a factor x+1) and every burst shorter than the CRC.
 *   A checksum (adding the bytes) misses swapped bytes and many double errors.
 *
 *   Sender: append CRC(data). Receiver: compute CRC(data) again and compare.
 *
 * WHY HARDWARE
 *   A bit-by-bit loop costs ~10 instructions per bit. The engine shifts one bit
 *   per instruction cycle in the background, and the FIFO lets the CPU keep
 *   writing. The 's' test shows the difference.
 *
 * KEY REGISTERS - see the CRC.h tab for details
 *   CRCCONL  CRCEN, CRCGO, MOD, LENDIAN, CRCFUL, CRCMPT
 *   CRCCONH  PLEN (poly length - 1), DWIDTH (data width - 1)
 *   CRCXORL/H polynomial      CRCWDATL/H start value / result    CRCDATL FIFO
 *   IFS3bits.CRCIF  set when the last bit has been shifted
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "CRC.h"

const unsigned long BAUD = 115200;
const uint8_t TEST9[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

const CrcSpec *const CATALOGUE[] = { &CRC8, &CRC8_MAXIM, &CRC16_CCITT, &CRC16_XMODEM,
                                     &CRC16_MODBUS, &CRC32, &CRC32_MPEG2 };
const uint8_t N_CAT = sizeof(CATALOGUE) / sizeof(CATALOGUE[0]);

void printHex(uint32_t v, uint8_t width)
{
    uint8_t digits = (width + 3) / 4;
    Serial.print("0x");
    for (int8_t d = digits - 1; d >= 0; d--) Serial.print((uint8_t)((v >> (4 * d)) & 0xF), HEX);
}

void pad(const char *s, uint8_t w)
{
    Serial.print(s);
    for (uint8_t n = strlen(s); n < w; n++) Serial.print(' ');
}

/* ---------------- 1. self-test ---------------- */
bool selfTest()
{
    Serial.println("# CRC of \"123456789\"      hardware    software    expected");
    uint8_t bad = 0;
    for (uint8_t i = 0; i < N_CAT; i++) {
        const CrcSpec &s = *CATALOGUE[i];
        uint32_t hw = crcHw(s, TEST9, 9);
        uint32_t sw = crcSw(s, TEST9, 9);
        bool ok = (hw == s.check) && (sw == s.check) && !crcTimedOut;
        if (!ok) bad++;
        Serial.print("  "); pad(s.name, 24);
        printHex(hw, s.width); Serial.print("  ");
        printHex(sw, s.width); Serial.print("  ");
        printHex(s.check, s.width);
        Serial.println(ok ? "  OK" : (crcTimedOut ? "  ENGINE TIMEOUT" : "  MISMATCH"));
    }
    Serial.print("# self-test: "); Serial.print(N_CAT - bad); Serial.print("/"); Serial.print(N_CAT);
    Serial.println(" pass");
    return bad == 0;
}

/* ---------------- 2. speed ---------------- */
uint8_t buf[1024];

void speedTest()
{
    for (uint16_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(i * 7 + 3);
    unsigned long t0, tHw, tBit, tTab;
    uint32_t a, b, c;

    t0 = micros(); a = crcHw(CRC32, buf, sizeof(buf));  tHw  = micros() - t0;
    t0 = micros(); b = crcSw(CRC32, buf, sizeof(buf));  tBit = micros() - t0;
    t0 = micros(); c = crc32Nibble(buf, sizeof(buf));   tTab = micros() - t0;

    Serial.println("# CRC-32 of 1024 bytes:");
    Serial.print("  hardware engine     "); Serial.print(tHw);  Serial.print(" us  "); printHex(a, 32); Serial.println();
    Serial.print("  software bitwise    "); Serial.print(tBit); Serial.print(" us  "); printHex(b, 32); Serial.println();
    Serial.print("  software 16-entry   "); Serial.print(tTab); Serial.print(" us  "); printHex(c, 32); Serial.println();
    Serial.print("  hardware is "); Serial.print((float)tBit / (tHw ? tHw : 1), 1);
    Serial.print("x faster than bitwise, "); Serial.print((float)tTab / (tHw ? tHw : 1), 1);
    Serial.println("x faster than the table");
    Serial.println((a == b && b == c) ? "  all three agree" : "  RESULTS DIFFER");
}

/* ---------------- 3. Modbus frame ---------------- */
/* Modbus RTU: CRC-16/MODBUS over address..data, sent LOW byte first. */
bool modbusCheck(const uint8_t *f, uint8_t len)
{
    uint16_t crc = (uint16_t)crcHw(CRC16_MODBUS, f, len - 2);
    return f[len - 2] == (uint8_t)crc && f[len - 1] == (uint8_t)(crc >> 8);
}

void modbusDemo()
{
    /* "read 2 holding registers from 0x0000, slave 1" -> 01 03 00 00 00 02 C4 0B */
    uint8_t frame[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0, 0 };
    uint16_t crc = (uint16_t)crcHw(CRC16_MODBUS, frame, 6);
    frame[6] = (uint8_t)crc; frame[7] = (uint8_t)(crc >> 8);
    Serial.print("# Modbus frame: ");
    for (uint8_t i = 0; i < 8; i++) { if (frame[i] < 16) Serial.print('0'); Serial.print(frame[i], HEX); Serial.print(' '); }
    Serial.println(frame[6] == 0xC4 && frame[7] == 0x0B ? " (C4 0B is the textbook value)" : " (expected ... C4 0B!)");
    Serial.print("  receiver check: "); Serial.println(modbusCheck(frame, 8) ? "GOOD" : "BAD");
    frame[3] ^= 0x04;                       /* one bit flipped on the wire */
    Serial.print("  one bit flipped in byte 3: "); Serial.println(modbusCheck(frame, 8) ? "GOOD (missed!)" : "BAD -> frame rejected");
}

/* ---------------- 4. flash fingerprint ---------------- */
#ifdef SERIAL_BOOTLOADER_ENTRY
const uint32_t APP_START = 0x001800;        /* the serial bootloader's APP_BASE */
#else
const uint32_t APP_START = 0x000000;
#endif
const uint32_t APP_LIMIT = 0x02B700;        /* below the bootloader's signature row */

void flashFingerprint()
{
    /* find the last programmed word: the image ends there */
    uint32_t end = APP_START;
    for (uint32_t a = APP_LIMIT - 2; a >= APP_START + 2; a -= 2) {
        if (flashReadWord(a) != 0xFFFFFFUL) { end = a + 2; break; }
    }
    uint32_t len = end - APP_START;
    unsigned long t0 = micros();
    uint32_t crc = crcFlash(APP_START, len);
    unsigned long t = micros() - t0;
    Serial.print("# flash 0x"); Serial.print(APP_START, HEX); Serial.print("-0x"); Serial.print(end, HEX);
    Serial.print(" ("); Serial.print(len / 2); Serial.print(" instruction words) CRC-32 = ");
    printHex(crc, 32); Serial.print("  in "); Serial.print(t / 1000.0, 1); Serial.println(" ms");
#ifdef SERIAL_BOOTLOADER_ENTRY
    Serial.println("  compare it with the \"CRC-32 0x...\" the uploader printed: same span, same algorithm");
#else
    Serial.println("  change one line of the sketch and upload again: the fingerprint changes");
#endif
}

/* ---------------- alternate mode probe ---------------- */
/* The driver uses legacy mode (MOD = 0), which needs the zero padding and the
 * indirect seed. Newer documents describe an "alternate" mode (MOD = 1). Is it
 * a plain direct CRC on this chip - no padding, init written straight in?
 * CRC-16/XMODEM of "123456789" must be 0x31C3. (The MPLAB simulator treats
 * both modes the same: "no".) */
void alternateProbe()
{
    _CRCMD = 0; CRCCONL = 0; CRCCONH = 0;
    CRCCONLbits.CRCEN = 1; CRCCONLbits.MOD = 1;
    CRCCONHbits.PLEN = 15; CRCCONHbits.DWIDTH = 7;
    CRCXORL = 0x1020; CRCXORH = 0;
    CRCWDATL = 0; CRCWDATH = 0; _CRCIF = 0;
    CRCCONLbits.CRCGO = 1;
    crcHwFeed(TEST9, 9);
    uint32_t guard = 100000UL;
    while (!CRCCONLbits.CRCMPT && --guard) { }
    while (!_CRCIF && --guard) { }
    uint16_t r = CRCWDATL;
    CRCCONLbits.CRCGO = 0;
    Serial.print("# alternate mode, no padding: XMODEM = "); printHex(r, 16);
    Serial.println(r == 0x31C3 ? "  -> YES, MOD = 1 is a direct CRC on this chip"
                               : "  -> no (0x31C3 expected), keep legacy mode + padding");
}

/* ---------------- your own text ---------------- */
void textCrc(const char *t)
{
    uint16_t n = strlen(t);
    const uint8_t *d = (const uint8_t *)t;
    Serial.print("# \""); Serial.print(t); Serial.print("\" ("); Serial.print(n); Serial.println(" bytes)");
    const CrcSpec *list[] = { &CRC8, &CRC16_CCITT, &CRC16_MODBUS, &CRC32 };
    for (uint8_t i = 0; i < 4; i++) {
        Serial.print("  "); pad(list[i]->name, 24);
        printHex(crcHw(*list[i], d, n), list[i]->width); Serial.println();
    }
}

void help()
{
    Serial.println();
    Serial.println("Lab 34 - hardware CRC engine");
    Serial.println("  ?/t self-test   s speed   m Modbus frame   f flash fingerprint   a alternate mode   <text> CRC of text");
}

void setup()
{
    Serial.begin(BAUD);
    help();
    selfTest();
}

char line[64];
uint8_t lineLen = 0;

void loop()
{
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (lineLen < sizeof(line) - 1) line[lineLen++] = c; continue; }
        line[lineLen] = 0;
        if (lineLen == 1) {
            switch (line[0]) {
            case '?': help(); selfTest(); break;
            case 't': selfTest(); break;
            case 's': speedTest(); break;
            case 'm': modbusDemo(); break;
            case 'f': flashFingerprint(); break;
            case 'a': alternateProbe(); break;
            default:  textCrc(line); break;
            }
        } else if (lineLen > 1) textCrc(line);
        lineLen = 0;
    }
}
