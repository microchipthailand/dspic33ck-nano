/*
 * CRC.h - the dsPIC33CK programmable CRC engine, plus a software reference
 *
 *   CrcSpec spec = CRC32;                 // or CRC16_MODBUS, CRC8, ... (table below)
 *   uint32_t c = crcHw(spec, data, len);  // hardware engine
 *   uint32_t s = crcSw(spec, data, len);  // bit-by-bit software, same answer
 *   uint32_t f = crcFlash(0x1800, 0x2000);// CRC-32 of program flash (3 bytes/word)
 *
 * Any CRC from 1 to 32 bits can be described by six numbers ("Rocksoft model"):
 *   width   number of bits                 poly    generator polynomial, top bit left out
 *   init    start value of the register    refin   feed each byte LSb first?
 *   refout  bit-reverse the result?        xorout  XOR applied to the result
 * plus "check" = the CRC of the 9 ASCII bytes "123456789", used to test a
 * driver. The catalogue below lists the common ones.
 *
 * ----------------------------------------------------------------------------
 * THE HARDWARE (CRC module, CRCxxx registers)
 *   CRCCONL  CRCEN  module on                  CRCGO   start shifting
 *            MOD    0 = legacy mode (used here), 1 = alternate mode (see 'a')
 *            LENDIAN 1 = data shifted LSb first (reflected input, "refin")
 *            CRCFUL / CRCMPT  FIFO full / empty      VWORD  words in the FIFO
 *            CRCISEL 0 = CRCIF when the last bit has been shifted
 *   CRCCONH  PLEN   polynomial length - 1      DWIDTH  data word width - 1
 *   CRCXORL/H  the polynomial. Bit 0 (x^0) is always 1 and cannot be written.
 *   CRCWDATL/H the shift register: write the start value, read the result
 *   CRCDATL/H  the data FIFO. With DWIDTH = 7 it takes BYTE writes (16 deep).
 *
 *   Legacy mode is the textbook "augmented" shift register: the message is
 *   divided by the polynomial exactly as written, so two things differ from
 *   the usual table-driven CRC and crcHw() takes care of both:
 *     - the end: after the data, <width> zero bits must be pushed through
 *       ("augmentation"), so crcHwEnd() writes width/8 zero bytes;
 *     - the start: the usual init value (the "direct" one, e.g. 0xFFFF) must
 *       be converted to the matching "indirect" seed (0xFFFF -> 0x84CF for
 *       CRC-16/CCITT) by running the register backwards: crcIndirectSeed().
 *   The shift register is always MSb first. So for a reflected CRC (refin =
 *   refout = 1, e.g. CRC-32, MODBUS) the data goes in LSb first (LENDIAN = 1)
 *   and the result comes out bit-reversed: crcHw() reverses it back.
 *   This driver handles widths of 8, 16, 24 and 32 bits (whole zero bytes).
 *   The engine shifts 1 bit per instruction cycle, so a byte costs 8 cycles
 *   no matter how many bits the polynomial has.
 *   Checked in the MPLAB simulator (all catalogue entries pass). Not yet on a
 *   real chip - the self-test at start-up tells you.
 * ----------------------------------------------------------------------------
 */
#ifndef LAB34_CRC_H
#define LAB34_CRC_H

#include <Arduino.h>

struct CrcSpec {
    const char *name;
    uint8_t  width;
    uint32_t poly, init;
    bool     refin, refout;
    uint32_t xorout, check;
};

/*                     name                width poly        init        refin  refout xorout      check("123456789") */
const CrcSpec CRC8           = { "CRC-8",              8, 0x07,       0x00,       false, false, 0x00,       0xF4 };
const CrcSpec CRC8_MAXIM     = { "CRC-8/MAXIM (1-Wire)",8, 0x31,      0x00,       true,  true,  0x00,       0xA1 };
const CrcSpec CRC16_CCITT    = { "CRC-16/CCITT-FALSE", 16, 0x1021,     0xFFFF,     false, false, 0x0000,     0x29B1 };
const CrcSpec CRC16_XMODEM   = { "CRC-16/XMODEM",     16, 0x1021,     0x0000,     false, false, 0x0000,     0x31C3 };
const CrcSpec CRC16_MODBUS   = { "CRC-16/MODBUS",     16, 0x8005,     0xFFFF,     true,  true,  0x0000,     0x4B37 };
const CrcSpec CRC32          = { "CRC-32 (zip, Ethernet)", 32, 0x04C11DB7, 0xFFFFFFFF, true, true, 0xFFFFFFFF, 0xCBF43926 };
const CrcSpec CRC32_MPEG2    = { "CRC-32/MPEG-2",     32, 0x04C11DB7, 0xFFFFFFFF, false, false, 0x00000000, 0x0376E6E7 };

/* ---------------- helpers ---------------- */
inline uint32_t crcMask(uint8_t width) { return width >= 32 ? 0xFFFFFFFFUL : ((1UL << width) - 1); }

uint32_t crcReflect(uint32_t v, uint8_t bits)
{
    uint32_t r = 0;
    for (uint8_t i = 0; i < bits; i++) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

/* ---------------- software reference: one bit at a time ---------------- */
uint32_t crcSw(const CrcSpec &s, const uint8_t *data, uint16_t len)
{
    const uint32_t mask = crcMask(s.width);
    uint32_t crc;
    if (s.refin) {                          /* reflected: shift right, reflected poly */
        const uint32_t rpoly = crcReflect(s.poly, s.width);
        crc = crcReflect(s.init, s.width);
        for (uint16_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (uint8_t b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ rpoly : (crc >> 1);
        }
        if (!s.refout) crc = crcReflect(crc, s.width);
    } else {                                /* normal: shift left through the top bit */
        const uint32_t top = 1UL << (s.width - 1);
        crc = s.init;
        for (uint16_t i = 0; i < len; i++) {
            for (int8_t b = 7; b >= 0; b--) {
                bool in = ((data[i] >> b) & 1) ^ ((crc & top) != 0);
                crc = (crc << 1) & mask;
                if (in) crc ^= s.poly;
            }
        }
        if (s.refout) crc = crcReflect(crc, s.width);
    }
    return (crc ^ s.xorout) & mask;
}

/* ---------------- hardware engine ---------------- */
volatile bool crcTimedOut = false;          /* set if the engine never finished */

/* The legacy engine needs the "indirect" start value: the register contents
 * that turn into the direct init value after <width> zero bits. Found by
 * running the shift register backwards <width> steps. */
uint32_t crcIndirectSeed(uint32_t init, uint32_t poly, uint8_t width)
{
    const uint32_t top = 1UL << (width - 1);
    uint32_t r = init;
    for (uint8_t i = 0; i < width; i++) {
        if (r & 1) r = ((r ^ poly) >> 1) | top;   /* bit 0 set: a feedback step happened */
        else       r >>= 1;
    }
    return r & crcMask(width);
}

/* Set the engine up for a CRC (no data yet). */
void crcHwBegin(const CrcSpec &s)
{
    _CRCMD = 0;                             /* power the module (PMD3) */
    CRCCONL = 0;                            /* off: clears the FIFO and the state */
    CRCCONH = 0;
    CRCCONLbits.CRCEN = 1;
    CRCCONLbits.MOD = 0;                    /* legacy: augmented shift register */
    CRCCONLbits.LENDIAN = s.refin ? 1 : 0;
    CRCCONLbits.CRCISEL = 0;                /* CRCIF = last bit shifted */
    CRCCONHbits.PLEN = s.width - 1;
    CRCCONHbits.DWIDTH = 8 - 1;             /* we feed bytes */
    CRCXORL = (uint16_t)(s.poly & 0xFFFE);  /* bit 0 is implied */
    CRCXORH = (uint16_t)(s.poly >> 16);
    uint32_t seed = crcIndirectSeed(s.init, s.poly, s.width);
    CRCWDATL = (uint16_t)seed;              /* start value */
    CRCWDATH = (uint16_t)(seed >> 16);
    _CRCIF = 0;
    CRCCONLbits.CRCGO = 1;
}

/* Feed bytes. Can be called many times (streaming). */
void crcHwFeed(const uint8_t *data, uint16_t len)
{
    volatile uint8_t *fifo = (volatile uint8_t *)&CRCDATL;
    for (uint16_t i = 0; i < len; i++) {
        while (CRCCONLbits.CRCFUL) { }      /* 16-byte FIFO: wait for room */
        *fifo = data[i];                    /* BYTE write, DWIDTH = 7 */
    }
}

/* Wait until every bit has been shifted, then return the finished CRC. */
uint32_t crcHwEnd(const CrcSpec &s)
{
    static const uint8_t zeros[4] = { 0, 0, 0, 0 };
    crcHwFeed(zeros, s.width / 8);          /* augmentation: <width> zero bits */
    uint32_t guard = 100000UL;
    while (!CRCCONLbits.CRCMPT && --guard) { }   /* FIFO empty ...           */
    while (!_CRCIF && --guard) { }               /* ... and last bit shifted */
    crcTimedOut = (guard == 0);
    uint32_t raw = ((uint32_t)CRCWDATH << 16) | CRCWDATL;
    CRCCONLbits.CRCGO = 0;
    _CRCIF = 0;
    raw &= crcMask(s.width);
    if (s.refout) raw = crcReflect(raw, s.width);    /* the register is MSb first */
    return (raw ^ s.xorout) & crcMask(s.width);
}

uint32_t crcHw(const CrcSpec &s, const uint8_t *data, uint16_t len)
{
    crcHwBegin(s);
    crcHwFeed(data, len);
    return crcHwEnd(s);
}

/* ---------------- program flash ---------------- */
/* One 24-bit instruction word at a program address (even). */
uint32_t flashReadWord(uint32_t addr)
{
    uint16_t savedPag = TBLPAG;
    TBLPAG = (uint16_t)(addr >> 16);
    uint16_t lo = __builtin_tblrdl((uint16_t)addr);
    uint16_t hi = __builtin_tblrdh((uint16_t)addr);
    TBLPAG = savedPag;
    return ((uint32_t)(hi & 0xFF) << 16) | lo;
}

/* CRC-32 over program flash, 3 bytes per instruction word, low byte first:
 * exactly what the serial bootloader's READ_CRC command computes, so the
 * number can be compared with the one the uploader gets. len = address units
 * (2 per instruction word). Uses the hardware engine. */
uint32_t crcFlash(uint32_t addr, uint32_t len)
{
    crcHwBegin(CRC32);
    for (uint32_t a = addr; a < addr + len; a += 2) {
        uint32_t w = flashReadWord(a);
        uint8_t b[3] = { (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)(w >> 16) };
        crcHwFeed(b, 3);
    }
    return crcHwEnd(CRC32);
}

/* The same, in software, with the bootloader's 16-entry nibble table. */
uint32_t crc32NibbleByte(uint32_t crc, uint8_t b)
{
    static const uint32_t T[16] = {
        0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL, 0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
        0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL, 0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL };
    crc ^= b;
    crc = (crc >> 4) ^ T[crc & 0x0F];
    crc = (crc >> 4) ^ T[crc & 0x0F];
    return crc;
}

uint32_t crc32Nibble(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t i = 0; i < len; i++) crc = crc32NibbleByte(crc, data[i]);
    return ~crc;
}

#endif
