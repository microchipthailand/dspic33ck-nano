/*
 * FlashEEPROM.h - 256 bytes of EEPROM, emulated in program Flash (Lab 17)
 *
 * The dsPIC33CK has no data EEPROM, so this file makes one out of two
 * reserved Flash pages. Use it like the Arduino EEPROM:
 *
 *   EEPbegin();                     once, in setup() - loads the saved bytes
 *   EEPwrite(address, data);        store one byte   (address 0..255, data 0..255)
 *   EEPread(address);               read one byte    (never written = 255)
 *   EEPput(address, variable);      store any variable or struct, from address on
 *   EEPget(address, variable);      read it back
 *   EEPclear();                     erase everything (all bytes = 255)
 *   EEPdump();                      print all 256 bytes on Serial
 *
 *   EEPwrite() skips a byte that already has that value (no Flash wear).
 *   It returns true when the byte is stored and verified.
 *   Also readable: EEP_SIZE, eepActivePage, eepNextSlot, eepSeq, eepWrites.
 *
 * ---------------------------------------------------------------------------
 * HOW IT WORKS
 *   Flash facts for this chip (same numbers as the bootloader, bl_config.h):
 *     - 1 instruction word = 24 bits = 2 program-address units. We use only the
 *       low 16 bits of each word.
 *     - Erase: a whole page at a time. We reserve and align 0x800 address
 *       units per page and erase with a 0x400 step - correct whatever the
 *       real page size is.
 *     - Smallest write: a DOUBLE WORD (2 instruction words) - NVMOP 0001.
 *     - Each word may be written ONCE after an erase.
 *   So a byte cannot simply be overwritten in place. Instead every write
 *   APPENDS one double word, an "entry", to the active page:
 *       word 0 = 0xA500 | address        word 1 = data | (~data << 8)
 *   The newest entry for an address wins. EEPbegin() replays the page from
 *   the start into a 256-byte copy in RAM, so EEPread() is just a RAM read.
 *
 *   Page layout (512 double words):
 *       slot 0       header: 0xEE5A, sequence number
 *       slot 1..511  entries, appended in order; erased slots read 0xFFFF
 *   When the page is full: erase the OTHER page, write one entry for every
 *   byte that is not 255, write that page's header LAST, then erase the old
 *   page. A power cut at any moment leaves at least one page with a valid
 *   header and complete data: a page without a header is never trusted.
 *   If both have a header (the cut came before the old page was erased),
 *   the higher sequence number wins.
 *
 *   Wear: one Flash page survives ~10 000 erases. Each page holds 511 writes,
 *   minus the bytes copied over at a page swap. With 20 bytes in use that is
 *   about 490 writes per erase, so about 10 million byte writes in total.
 *   Do not write in every pass of loop(), only when a value changes.
 *
 *   EEPput() writes byte after byte. A power cut in the middle can leave a
 *   variable half old, half new. A real EEPROM does the same.
 *
 *   Where: two pages at a FIXED address, 0x2A000-0x2AFFF, just below the
 *   bootloader's signature page (0x2B000). They are declared space(prog) +
 *   noload: the linker reserves the space (so no code is ever put there), but
 *   NOTHING goes into the .hex. That matters for the Serial bootloader: it
 *   CRC-checks the application from 0x1800 up to the last address in the hex
 *   before every start, so changing data must never sit inside that range.
 *
 *   The data survives RESET and power-off. Any NEW UPLOAD (PICkit or Serial
 *   bootloader) erases it, because both erase the whole application area.
 * ---------------------------------------------------------------------------
 */
#ifndef FLASH_EEPROM_H
#define FLASH_EEPROM_H

#include <Arduino.h>

#define EEP_SIZE          256            /* bytes of emulated EEPROM, addresses 0..255 */

/* ---------------- flash geometry (address units) ---------------- */
#define EEP_PAGE_SIZE     0x800UL        /* one reserved page = 1024 instruction words */
#define EEP_ERASE_STEP    0x400UL        /* same conservative step as the bootloader  */
#define EEP_DW            4UL            /* one double word = 2 instr. words = 4 units */
#define EEP_SLOTS         ((int)(EEP_PAGE_SIZE / EEP_DW))   /* 512: header + 511 entries */

#define EEP_NVMOP_WRITE_DW 0x4001u       /* WREN | double-word program */
#define EEP_NVMOP_ERASE_PG 0x4003u       /* WREN | page erase          */
#define EEP_NVM_LATCH_PAGE 0x00FAu
#define EEP_HDR_MAGIC      0xEE5Au
#define EEP_ENTRY_TAG      0xA500u

/* Reserve 2 pages of program memory at a FIXED address, NOT loaded from the
 * hex (see "Where" above). 0x800 address units = 1024 instruction words, one
 * uint16_t element per word. Not 'const': this core compiles everything as
 * C++, which refuses an uninitialised const - space(prog) is what places it
 * in Flash, and the code only ever reads it through TBLRD. */
#define EEP_BASE 0x2A000UL               /* page 0; page 1 = 0x2A800; ends below 0x2B000 */
uint16_t eepArea[2 * 1024] __attribute__((space(prog), address(EEP_BASE), noload));

/* Also fill the gap from 0x2B000 to the end of the program region, so the linker
 * cannot drop small code sections up there. Code placed above the reserved pages
 * would be too far for the short calls used by the low code ("PC Relative branch
 * out of range" at link time). The program region ends at 0x2B700 with the
 * Serial bootloader (its signature row lives there) and at 0x2BF00 without it.
 * Nothing in this gap is ever erased or written. */
#ifdef SERIAL_BOOTLOADER_ENTRY
uint16_t eepGap[0x700 / 2] __attribute__((space(prog), address(0x2B000), noload, keep));
#else
uint16_t eepGap[0xF00 / 2] __attribute__((space(prog), address(0x2B000), noload, keep));
#endif

/* ---------------- state (read it, don't write it) ---------------- */
uint8_t       eepData[EEP_SIZE];         /* RAM copy of the EEPROM contents */
uint32_t      eepPageAddr[2];
int           eepActivePage = -1;        /* -1: no valid page yet (all bytes 255) */
int           eepNextSlot   = 0;         /* next free slot in the active page */
uint16_t      eepSeq        = 0;         /* sequence number of the active page */
unsigned long eepWrites     = 0;         /* Flash writes since reset */

/* ---------------- low-level NVM ---------------- */
static uint16_t eepReadWord(uint32_t a)  /* low 16 bits of an instruction word */
{
    uint16_t saved = TBLPAG;
    TBLPAG = (uint16_t)(a >> 16);
    uint16_t v = __builtin_tblrdl((uint16_t)a);
    TBLPAG = saved;
    return v;
}

static bool eepNvmGo(void)
{
    __builtin_disi(7);
    __builtin_write_NVM();               /* 0x55, 0xAA -> NVMKEY, then WR = 1 */
    while (NVMCONbits.WR) { }
    return !NVMCONbits.WRERR;
}

static bool eepErasePage(int page)
{
    uint32_t a = eepPageAddr[page];
    for (uint32_t off = 0; off < EEP_PAGE_SIZE; off += EEP_ERASE_STEP) {
        while (NVMCONbits.WR) { }
        NVMADRU = (uint16_t)((a + off) >> 16);
        NVMADR  = (uint16_t)(a + off);
        NVMCON  = EEP_NVMOP_ERASE_PG;
        if (!eepNvmGo()) return false;
    }
    return true;
}

static uint32_t eepSlotAddr(int page, int slot) { return eepPageAddr[page] + (uint32_t)slot * EEP_DW; }

/* write two 16-bit values into one slot (double word) and read them back.
 * The unused upper byte of each 24-bit instruction word stays 0xFF. */
static bool eepWriteSlot(int page, int slot, uint16_t w0, uint16_t w1)
{
    uint32_t a = eepSlotAddr(page, slot);
    while (NVMCONbits.WR) { }
    NVMADRU = (uint16_t)(a >> 16);
    NVMADR  = (uint16_t)a;
    NVMCON  = EEP_NVMOP_WRITE_DW;
    uint16_t saved = TBLPAG;
    TBLPAG = EEP_NVM_LATCH_PAGE;
    __builtin_tblwtl(0, w0); __builtin_tblwth(0, 0x00FF);
    __builtin_tblwtl(2, w1); __builtin_tblwth(2, 0x00FF);
    bool ok = eepNvmGo();
    TBLPAG = saved;
    eepWrites++;
    return ok && eepReadWord(a) == w0 && eepReadWord(a + 2) == w1;
}

/* ---------------- page handling ---------------- */
/* valid header? returns its sequence number in *seq */
static bool eepHeader(int page, uint16_t *seq)
{
    if (eepReadWord(eepSlotAddr(page, 0)) != EEP_HDR_MAGIC) return false;
    *seq = eepReadWord(eepSlotAddr(page, 0) + 2);
    return true;
}

/* replay a page's entries into eepData[]; returns the first free slot */
static int eepReplay(int page)
{
    for (int i = 0; i < EEP_SIZE; i++) eepData[i] = 0xFF;
    int s;
    for (s = 1; s < EEP_SLOTS; s++) {
        uint32_t a = eepSlotAddr(page, s);
        uint16_t w0 = eepReadWord(a), w1 = eepReadWord(a + 2);
        if (w0 == 0xFFFF && w1 == 0xFFFF) break;              /* erased: end of the log */
        uint8_t addr = (uint8_t)w0, d = (uint8_t)w1;
        if ((w0 & 0xFF00) == EEP_ENTRY_TAG && (uint8_t)(w1 >> 8) == (uint8_t)~d)
            eepData[addr] = d;                                /* else: damaged entry, skip */
    }
    return s;
}

/* start a fresh page (or move to the other page): copy every byte != 255,
 * header last, then erase the old page */
static bool eepNewPage(void)
{
    int newp = eepActivePage < 0 ? 0 : eepActivePage ^ 1;
    if (!eepErasePage(newp)) return false;
    int s = 1;
    for (int i = 0; i < EEP_SIZE; i++) {
        if (eepData[i] == 0xFF) continue;
        if (!eepWriteSlot(newp, s++, EEP_ENTRY_TAG | i, eepData[i] | ((uint16_t)(uint8_t)~eepData[i] << 8)))
            return false;
    }
    uint16_t newSeq = eepActivePage < 0 ? 0 : (uint16_t)(eepSeq + 1);
    if (!eepWriteSlot(newp, 0, EEP_HDR_MAGIC, newSeq)) return false;   /* page is now valid */
    if (eepActivePage >= 0) eepErasePage(eepActivePage);             /* old page not needed */
    eepActivePage = newp; eepNextSlot = s; eepSeq = newSeq;
    return true;
}

/* ================= the functions you use ================= */

/* once, in setup(): find the pages and load the saved bytes */
void EEPbegin(void)
{
    eepPageAddr[0] = __builtin_tbladdress(&eepArea[0]) & 0xFFFFFFUL;  /* 24-bit address (bit 24 is a PSV flag) */
    eepPageAddr[1] = eepPageAddr[0] + EEP_PAGE_SIZE;
    (void)__builtin_tbladdress(&eepGap[0]);      /* keeps the gap filler from being garbage-collected */

    uint16_t s0 = 0, s1 = 0;
    bool v0 = eepHeader(0, &s0), v1 = eepHeader(1, &s1);
    if (v0 && v1) {                              /* a page swap was cut short: keep the newer */
        int older = (int16_t)(s1 - s0) > 0 ? 0 : 1;
        eepErasePage(older);
        if (older == 0) v0 = false; else v1 = false;
    }
    if (v0)      { eepActivePage = 0; eepSeq = s0; }
    else if (v1) { eepActivePage = 1; eepSeq = s1; }
    else         { eepActivePage = -1; eepSeq = 0; }

    if (eepActivePage >= 0) eepNextSlot = eepReplay(eepActivePage);
    else { for (int i = 0; i < EEP_SIZE; i++) eepData[i] = 0xFF; eepNextSlot = 0; }
}

/* read one byte (an address that was never written reads 255) */
uint8_t EEPread(int address)
{
    if (address < 0 || address >= EEP_SIZE) return 0xFF;
    return eepData[address];
}

/* write one byte; true = stored and verified (or it already had that value) */
bool EEPwrite(int address, uint8_t data)
{
    if (address < 0 || address >= EEP_SIZE) return false;
    if (eepData[address] == data && eepActivePage >= 0) return true;   /* no change: no wear */
    uint8_t old = eepData[address];
    eepData[address] = data;
    for (int tries = 0; tries < 3; tries++) {
        if (eepActivePage < 0 || eepNextSlot >= EEP_SLOTS) {
            /* new page: eepNewPage() copies eepData[], which already holds the new value */
            if (eepNewPage()) return true;
            continue;
        }
        if (eepWriteSlot(eepActivePage, eepNextSlot++, EEP_ENTRY_TAG | address,
                         data | ((uint16_t)(uint8_t)~data << 8)))
            return true;
        /* that slot did not verify: the loop tries the next one */
    }
    eepData[address] = old;
    return false;
}

/* write len bytes from data, starting at address */
bool EEPwriteBlock(int address, const void *data, int len)
{
    const uint8_t *p = (const uint8_t *)data;
    bool ok = true;
    for (int i = 0; i < len; i++) ok &= EEPwrite(address + i, p[i]);
    return ok;
}

/* read len bytes into data, starting at address */
void EEPreadBlock(int address, void *data, int len)
{
    uint8_t *p = (uint8_t *)data;
    for (int i = 0; i < len; i++) p[i] = EEPread(address + i);
}

/* any variable or struct:  EEPput(0, settings);  EEPget(0, settings); */
template <typename T> bool EEPput(int address, const T &value) { return EEPwriteBlock(address, &value, sizeof(T)); }
template <typename T> T   &EEPget(int address, T &value)       { EEPreadBlock(address, &value, sizeof(T)); return value; }

/* erase everything: all bytes read 255 again */
void EEPclear(void)
{
    eepErasePage(0); eepErasePage(1);
    for (int i = 0; i < EEP_SIZE; i++) eepData[i] = 0xFF;
    eepActivePage = -1; eepNextSlot = 0; eepSeq = 0;
}

/* print the 256 bytes as a hex table, plus where the log stands */
void EEPdump(void)
{
    Serial.print("# EEPROM "); Serial.print(EEP_SIZE); Serial.print(" bytes, page ");
    Serial.print(eepActivePage); Serial.print(" @0x");
    Serial.print(eepActivePage >= 0 ? eepPageAddr[eepActivePage] : 0, HEX);
    Serial.print(", slot "); Serial.print(eepNextSlot); Serial.print("/"); Serial.print(EEP_SLOTS);
    Serial.print(", page swaps "); Serial.println(eepSeq);
    for (int row = 0; row < EEP_SIZE; row += 16) {
        Serial.print("# ");
        if (row < 16) Serial.print('0');
        Serial.print(row, HEX); Serial.print(':');
        for (int i = 0; i < 16; i++) {
            uint8_t v = eepData[row + i];
            Serial.print(' ');
            if (v < 16) Serial.print('0');
            Serial.print(v, HEX);
        }
        Serial.println();
    }
}

#endif /* FLASH_EEPROM_H */
