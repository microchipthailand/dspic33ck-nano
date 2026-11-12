/*
 * Lab 17 - EEPROM emulation in Flash (settings that survive power-off)
 * Board: dsPIC33CK Nano
 *
 * Goal:  the dsPIC33CK has NO data EEPROM. Real products store settings in
 *        program Flash instead. Learn how: erase pages, double-word writes,
 *        the NVMKEY unlock, wear levelling, and a CRC so you never trust a
 *        half-written record.
 *
 * Hardware: nothing to wire. USB-serial adapter on J5 (115200).
 *
 * What it stores (a "Settings" struct, 8 bytes):
 *   bootCount  - incremented on every reset / power-up
 *   blinkMs    - LED blink period, change it with commands
 *   userValue  - any number you type
 *
 * Serial commands:
 *   ?          show settings + where the record lives in Flash
 *   b<ms>      set blink period, e.g. b250      (saved immediately)
 *   v<num>     set user value,   e.g. v1234     (saved immediately)
 *   d          dump the used part of the EEPROM pages
 *   f          factory reset (erase both pages)
 *   USER button: save the current settings again (watch the write counter)
 *
 * Power-cycle the board: bootCount keeps counting, blinkMs is remembered.
 *
 * ---------------------------------------------------------------------------
 * HOW IT WORKS
 *   Flash facts for this chip (same numbers as the bootloader, bl_config.h):
 *     - 1 instruction word = 24 bits = 2 program-address units.
 *       We use only the low 16 bits of each word (like the old dsPIC EEPROM
 *       emulation app note), so one "slot" = 2 words = 4 bytes of data.
 *     - Erase granularity: we reserve and align 0x800 address units and erase
 *       with a 0x400 step - correct whatever the real page size is.
 *     - Smallest write: a DOUBLE WORD (2 instruction words) - NVMOP 0001.
 *     - Each word may be written ONCE after an erase.
 *   Log-structured storage (simple wear levelling):
 *     A record = [magic+seq | bootCount | blinkMs | userValue | crc] =
 *     5 double words. Records are APPENDED to the active page. Reading finds
 *     the LAST record whose CRC is good. Only when the page is full is the
 *     other page erased and the newest record copied there. Two pages
 *     (ping-pong) means a power cut during an erase never loses everything.
 *   Endurance: a flash page survives ~10 000 erases. With 102 records per
 *   page and 2 pages, that is roughly 2 000 000 saves before wear-out.
 *
 *   Where: two pages at a FIXED address, 0x2A000-0x2AFFF, just below the
 *   bootloader's signature page (0x2B000). They are declared space(prog) +
 *   noload: the linker reserves the space (so no code is ever put there), but
 *   NOTHING goes into the .hex. That matters for the Serial bootloader: it
 *   CRC-checks the application from 0x1800 up to the last address in the hex
 *   before every start, so changing data must never sit inside that range.
 *   Being near the top of flash and absent from the hex, these pages never are.
 *
 *   Settings survive RESET and power-off. Any NEW UPLOAD (PICkit or Serial
 *   bootloader) erases them, because both erase the whole application area.
 * ---------------------------------------------------------------------------
 */

#include <Arduino.h>

/* ---------------- flash geometry (address units) ---------------- */
#define EE_PAGE_SIZE   0x800UL           /* one reserved page = 1024 instruction words */
#define EE_ERASE_STEP  0x400UL           /* same conservative step as the bootloader  */
#define EE_DW          4UL               /* one double word = 2 instr. words = 4 units */
#define REC_DW         5                 /* double words per record                    */
#define REC_SIZE       (REC_DW * EE_DW)
#define RECS_PER_PAGE  (EE_PAGE_SIZE / REC_SIZE)        /* 102 */

#define NVMOP_WRITE_DW 0x4001u           /* WREN | double-word program */
#define NVMOP_ERASE_PG 0x4003u           /* WREN | page erase          */
#define NVM_LATCH_PAGE 0x00FAu
#define REC_MAGIC      0xEE00u

/* Reserve 2 pages of program memory at a FIXED address, NOT loaded from the
 * hex (see "Where" above). 0x800 address units = 1024 instruction words, one
 * uint16_t element per word. Not 'const': this core compiles everything as
 * C++, which refuses an uninitialised const - space(prog) is what places it
 * in Flash, and the sketch only ever reads it through TBLRD. */
#define EE_BASE 0x2A000UL                /* page 0; page 1 = 0x2A800; ends below 0x2B000 */
uint16_t eeArea[2 * 1024] __attribute__((space(prog), address(EE_BASE), noload));

/* Also fill the gap from 0x2B000 to the end of the program region, so the linker
 * cannot drop small code sections up there. Code placed above the reserved pages
 * would be too far for the short calls used by the low code ("PC Relative branch
 * out of range" at link time). The program region ends at 0x2B700 with the
 * Serial bootloader (its signature row lives there) and at 0x2BF00 without it.
 * Nothing in this gap is ever erased or written by the sketch. */
#ifdef SERIAL_BOOTLOADER_ENTRY
uint16_t eeGap[0x700 / 2] __attribute__((space(prog), address(0x2B000), noload, keep));
#else
uint16_t eeGap[0xF00 / 2] __attribute__((space(prog), address(0x2B000), noload, keep));
#endif

struct Settings {
    uint16_t bootCount;
    uint16_t blinkMs;
    uint16_t userValue;
};
Settings cfg;

uint32_t pageAddr[2];
int      activePage = -1;                /* -1: none valid yet */
int      nextSlot   = 0;                 /* free record slot in the active page */
uint16_t seq        = 0;                 /* record sequence number */
unsigned long writes = 0;

/* ---------------- low-level NVM ---------------- */
static uint16_t readWord(uint32_t a)      /* low 16 bits of an instruction word */
{
    uint16_t saved = TBLPAG;
    TBLPAG = (uint16_t)(a >> 16);
    uint16_t v = __builtin_tblrdl((uint16_t)a);
    TBLPAG = saved;
    return v;
}

static bool nvmGo(void)
{
    __builtin_disi(7);
    __builtin_write_NVM();               /* 0x55, 0xAA -> NVMKEY, then WR = 1 */
    while (NVMCONbits.WR) { }
    return !NVMCONbits.WRERR;
}

static bool erasePage(uint32_t a)
{
    for (uint32_t off = 0; off < EE_PAGE_SIZE; off += EE_ERASE_STEP) {
        while (NVMCONbits.WR) { }
        NVMADRU = (uint16_t)((a + off) >> 16);
        NVMADR  = (uint16_t)(a + off);
        NVMCON  = NVMOP_ERASE_PG;
        if (!nvmGo()) return false;
    }
    return true;
}

/* write two 16-bit values into one double word; the unused upper byte of each
 * 24-bit instruction word is written as 0xFF (= left erased) */
static bool writeDW(uint32_t a, uint16_t w0, uint16_t w1)
{
    while (NVMCONbits.WR) { }
    NVMADRU = (uint16_t)(a >> 16);
    NVMADR  = (uint16_t)a;
    NVMCON  = NVMOP_WRITE_DW;
    uint16_t saved = TBLPAG;
    TBLPAG = NVM_LATCH_PAGE;
    __builtin_tblwtl(0, w0); __builtin_tblwth(0, 0x00FF);
    __builtin_tblwtl(2, w1); __builtin_tblwth(2, 0x00FF);
    bool ok = nvmGo();
    TBLPAG = saved;
    return ok;
}

/* ---------------- CRC-16/CCITT over the record payload ---------------- */
static uint16_t crc16(const uint16_t *w, int n)
{
    uint16_t c = 0xFFFF;
    for (int i = 0; i < n; i++)
        for (int b = 1; b >= 0; b--) {
            c ^= (uint16_t)((w[i] >> (8 * b)) & 0xFF) << 8;
            for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        }
    return c;
}

/* record layout, 10 words (5 double words):
 *   0 magic   1 seq   2 bootCount   3 blinkMs   4 userValue
 *   5 0x0000  6 0x0000 7 0x0000     8 crc       9 ~crc          */
static void packRecord(uint16_t w[10], uint16_t s, const Settings &c)
{
    w[0] = REC_MAGIC; w[1] = s;
    w[2] = c.bootCount; w[3] = c.blinkMs; w[4] = c.userValue;
    w[5] = 0; w[6] = 0; w[7] = 0;
    w[8] = crc16(w, 8); w[9] = (uint16_t)~w[8];
}

static uint32_t slotAddr(int page, int slot) { return pageAddr[page] + (uint32_t)slot * REC_SIZE; }

/* reads a slot: 1 = valid record, 0 = erased (free), -1 = garbage */
static int readSlot(int page, int slot, uint16_t w[10])
{
    uint32_t a = slotAddr(page, slot);
    bool erased = true;
    for (int i = 0; i < 10; i++) { w[i] = readWord(a + 2UL * i); if (w[i] != 0xFFFF) erased = false; }
    if (erased) return 0;
    if (w[0] != REC_MAGIC || w[9] != (uint16_t)~w[8] || w[8] != crc16(w, 8)) return -1;
    return 1;
}

static bool appendRecord(int page, int slot)
{
    uint16_t w[10];
    packRecord(w, seq, cfg);
    uint32_t a = slotAddr(page, slot);
    for (int i = 0; i < REC_DW; i++)
        if (!writeDW(a + EE_DW * i, w[2 * i], w[2 * i + 1])) return false;
    uint16_t back[10];
    return readSlot(page, slot, back) == 1;   /* verify */
}

/* ---------------- EEPROM API ---------------- */
bool eeLoad(void)
{
    uint16_t bestSeq = 0; bool found = false;
    for (int p = 0; p < 2; p++) {
        int last = -1, free = RECS_PER_PAGE;
        uint16_t w[10];
        for (int s = 0; s < (int)RECS_PER_PAGE; s++) {
            int r = readSlot(p, s, w);
            if (r == 0) { free = s; break; }
            if (r == 1) last = s;
        }
        if (last >= 0) {
            readSlot(p, last, w);
            /* newer = larger seq, with wrap-around (int16 difference) */
            if (!found || (int16_t)(w[1] - bestSeq) > 0) {
                found = true; bestSeq = w[1];
                activePage = p; nextSlot = free;
                cfg.bootCount = w[2]; cfg.blinkMs = w[3]; cfg.userValue = w[4];
            }
        }
    }
    seq = found ? (uint16_t)(bestSeq + 1) : 0;
    return found;
}

bool eeSave(void)
{
    if (activePage < 0) {                       /* first ever save */
        if (!erasePage(pageAddr[0])) return false;
        activePage = 0; nextSlot = 0;
    }
    if (nextSlot >= (int)RECS_PER_PAGE) {       /* page full: move to the other one */
        int other = activePage ^ 1;
        if (!erasePage(pageAddr[other])) return false;
        if (!appendRecord(other, 0)) return false;
        seq++; writes++;
        int old = activePage;
        activePage = other; nextSlot = 1;
        erasePage(pageAddr[old]);               /* optional: old page no longer needed */
        return true;
    }
    if (!appendRecord(activePage, nextSlot)) {  /* slot was not clean (power cut?) */
        nextSlot = RECS_PER_PAGE;               /* force a page swap next time */
        return eeSave();
    }
    nextSlot++; seq++; writes++;
    return true;
}

void eeFactoryReset(void)
{
    erasePage(pageAddr[0]); erasePage(pageAddr[1]);
    activePage = -1; nextSlot = 0; seq = 0;
}

/* ---------------- demo ---------------- */
void printSettings(void)
{
    Serial.print("bootCount="); Serial.print(cfg.bootCount);
    Serial.print("  blinkMs="); Serial.print(cfg.blinkMs);
    Serial.print("  userValue="); Serial.println(cfg.userValue);
    Serial.print("  page "); Serial.print(activePage);
    Serial.print(" @0x"); Serial.print(activePage >= 0 ? pageAddr[activePage] : 0, HEX);
    Serial.print("  slot "); Serial.print(nextSlot); Serial.print("/"); Serial.print((int)RECS_PER_PAGE);
    Serial.print("  seq "); Serial.print(seq);
    Serial.print("  writes this run "); Serial.println(writes);
}

void dumpPages(void)
{
    for (int p = 0; p < 2; p++) {
        Serial.print("# page "); Serial.print(p); Serial.print(" @0x"); Serial.println(pageAddr[p], HEX);
        uint16_t w[10];
        for (int s = 0; s < (int)RECS_PER_PAGE; s++) {
            int r = readSlot(p, s, w);
            if (r == 0) { Serial.print("#   slots "); Serial.print(s); Serial.println("+ erased"); break; }
            Serial.print("#   slot "); Serial.print(s);
            Serial.print(r == 1 ? " ok  seq=" : " BAD seq=");
            Serial.print(w[1]); Serial.print(" boot="); Serial.print(w[2]);
            Serial.print(" blink="); Serial.print(w[3]); Serial.print(" val="); Serial.println(w[4]);
        }
    }
}

long readNumber(void)
{
    long v = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < 50) {               /* digits arrive back-to-back */
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (c < '0' || c > '9') break;
            v = v * 10 + (Serial.read() - '0');
            t0 = millis();
        }
    }
    return v;
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);

    pageAddr[0] = __builtin_tbladdress(&eeArea[0]) & 0xFFFFFFUL;   /* 24-bit program address (bit 24 is a PSV flag) */
    pageAddr[1] = pageAddr[0] + EE_PAGE_SIZE;
    (void)__builtin_tbladdress(&eeGap[0]);   /* keeps the gap filler from being garbage-collected */

    Serial.println("Lab 17 - EEPROM emulation in Flash.  ? b<ms> v<num> d f");
    if (!eeLoad()) {
        Serial.println("# no valid record - first start, using defaults");
        cfg.bootCount = 0; cfg.blinkMs = 500; cfg.userValue = 0;
    }
    cfg.bootCount++;
    Serial.println(eeSave() ? "# boot counted and saved" : "# SAVE FAILED");
    printSettings();
}

void loop()
{
    digitalWrite(LED_BUILTIN, (millis() / cfg.blinkMs) % 2 ? LED_ON : LED_OFF);

    static int lastBtn = HIGH;
    int btn = digitalRead(BUTTON_BUILTIN);
    if (btn == LOW && lastBtn == HIGH) {
        Serial.println(eeSave() ? "# saved (button)" : "# SAVE FAILED");
        printSettings();
        delay(30);
    }
    lastBtn = btn;

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c == '?') printSettings();
        else if (c == 'd') dumpPages();
        else if (c == 'f') { eeFactoryReset(); Serial.println("# both pages erased - reset to start over"); }
        else if (c == 'b') {
            long v = readNumber();
            if (v >= 20 && v <= 10000) { cfg.blinkMs = (uint16_t)v; Serial.println(eeSave() ? "# saved" : "# SAVE FAILED"); }
            else Serial.println("# blink must be 20..10000 ms");
            printSettings();
        } else if (c == 'v') {
            cfg.userValue = (uint16_t)readNumber();
            Serial.println(eeSave() ? "# saved" : "# SAVE FAILED");
            printSettings();
        }
    }
}
