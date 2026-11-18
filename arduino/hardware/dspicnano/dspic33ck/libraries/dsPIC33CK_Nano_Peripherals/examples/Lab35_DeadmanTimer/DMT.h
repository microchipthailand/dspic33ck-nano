/*
 * DMT.h - Deadman Timer driver for the dsPIC33CK
 *
 *   dmtStart();              // switch it on. ONE WAY: only a reset stops it
 *   if (dmtWindowOpen()) dmtClear();   // clear it, only inside the open window
 *   dmtCount();              // the counter = instructions since the last clear
 *   dmtTimeout(), dmtWindow()          // the two config values, in instructions
 *
 * ----------------------------------------------------------------------------
 * THE DEADMAN TIMER
 *   A 32-bit counter that counts INSTRUCTION CYCLES (FCY), not time and not
 *   the LPRC like the watchdog. The program must clear it with a two-step key
 *   inside a window:
 *
 *     count 0 ..... DMTIVT ............ DMTCNT
 *     |-- closed --|------ open --------|-> DMT event
 *        clear here = event   clear here = OK     too late = event
 *
 *   - too late (counter reaches DMTCNT): DMT event
 *   - too early (window still closed):   DMT event
 *   - wrong key:                         DMT event (BAD1 / BAD2 in DMTSTAT)
 *   So it catches code that runs too SLOWLY and code that runs too FAST
 *   (a loop that skips its work but still reaches the clear).
 *
 *   The DMT event raises the DMT interrupt (vector 45, IFS2.DMTIF). The handler
 *   in this file records what happened in RAM that survives a reset and then
 *   resets the chip, like a watchdog would.
 *
 * REGISTERS
 *   DMTCON.ON      1 = on. Software can set it but NOT clear it.
 *   DMTPRECLR      STEP1: write 0x40 into bits 15:8   (value 0x4000)
 *   DMTCLR         STEP2: write 0x08 into bits 7:0    (value 0x0008), right after
 *   DMTSTAT        BAD1 (wrong STEP1), BAD2 (wrong STEP2), DMTEVENT, WINOPN
 *   DMTCNTL/H      the counter (two 16-bit halves: see dmtCount())
 *   DMTPSCNTL/H    copy of the DMTCNT config word  = time-out, instructions
 *   DMTPSINTVL/H   copy of the DMTIVT config word  = window start, instructions
 *
 * CONFIG WORDS (fixed when the chip is programmed)
 *   FDMT.DMTDIS    OFF = off at reset, software may switch it on (erased value)
 *                  ON  = always on from reset, cannot be stopped
 *   FDMTCNTL/H     time-out count       FDMTIVTL/H  window interval
 *   The sketch sets them with #pragma config. They are programmed only when
 *   the sketch is uploaded with a PICkit ("Bootloader: None"). With the serial
 *   bootloader the config words belong to the bootloader and are left erased:
 *   DMTCNT = DMTIVT = 0xFFFFFFFF. dmtUsable() detects that case.
 *
 * NOT VERIFIED: the MPLAB simulator does not model the DMT (the counter stays
 * at 0), so this lab is written from the data sheet. The 'n' and '?' commands
 * print what your chip really does.
 * ----------------------------------------------------------------------------
 */
#ifndef LAB35_DMT_H
#define LAB35_DMT_H

#include <Arduino.h>

/* ---------------- what survives the reset ---------------- */
struct DmtNote {
    uint16_t magic;                     /* 0xD317 = valid */
    uint16_t reason;                    /* DMT_xxx below */
    uint16_t stat;                      /* DMTSTAT at the event */
    uint32_t count;                     /* counter at the event */
    uint16_t events;                    /* DMT events since power-on */
    uint16_t tag;                       /* what the sketch was doing (the sketch sets it) */
};
DmtNote dmtNote __attribute__((persistent, address(0x4F40)));
enum { DMT_NONE, DMT_EVENT };

/* ---------------- config / status ---------------- */
inline uint32_t dmtTimeout() { return ((uint32_t)DMTPSCNTH << 16) | DMTPSCNTL; }
inline uint32_t dmtWindow()  { return ((uint32_t)DMTPSINTVH << 16) | DMTPSINTVL; }
inline bool dmtIsOn()        { return DMTCONbits.ON; }
inline bool dmtWindowOpen()  { return DMTSTATbits.WINOPN; }

/* The counter, read as two halves. Read high, low, high again: if the high
 * half changed in between, the low half wrapped - read again. */
inline uint32_t dmtCount()
{
    uint16_t hi, lo;
    do { hi = DMTCNTH; lo = DMTCNTL; } while (hi != DMTCNTH);
    return ((uint32_t)hi << 16) | lo;
}

/* Can loop() clear it at all? Only if the window opens before the time-out. */
inline bool dmtUsable() { return dmtWindow() < dmtTimeout(); }

/* ---------------- control ---------------- */
void dmtStart()
{
    _DMTIF = 0;
    _DMTIP = 7;                         /* top priority: nothing may hold it off */
    _DMTIE = 1;
    DMTCONbits.ON = 1;                  /* one way */
}

/* The two-step key. Returns false (and does nothing) while the window is
 * closed, because clearing then would be an error. */
bool dmtClear()
{
    if (!DMTSTATbits.WINOPN) return false;
    DMTPRECLR = 0x4000;                 /* STEP1 = 0x40 */
    DMTCLR = 0x0008;                    /* STEP2 = 0x08 */
    return true;
}

/* Deliberate mistakes, for the lab */
void dmtClearEarly()  { DMTPRECLR = 0x4000; DMTCLR = 0x0008; }   /* ignores WINOPN */
void dmtClearBadKey() { DMTPRECLR = 0x4100; DMTCLR = 0x0008; }   /* STEP1 wrong -> BAD1 */

/* ---------------- the event ---------------- */
extern "C" void __attribute__((interrupt, no_auto_psv)) _DMTInterrupt(void)
{
    dmtNote.count = dmtCount();
    dmtNote.stat = DMTSTAT;
    dmtNote.reason = DMT_EVENT;
    dmtNote.events++;
    dmtNote.magic = 0xD317;
    _DMTIF = 0;
    asm volatile ("reset");             /* RCON shows SWR after the restart */
}

/* Call once at start-up. Returns true if the last reset was a DMT event. */
bool dmtBootCheck(uint16_t rcon)
{
    if (dmtNote.magic != 0xD317 || (rcon & 0x0003)) {   /* bad note or power-on/brown-out */
        dmtNote.magic = 0xD317; dmtNote.reason = DMT_NONE; dmtNote.events = 0; dmtNote.tag = 0;
        return false;
    }
    bool was = (dmtNote.reason == DMT_EVENT) && (rcon & 0x0040);   /* SWR */
    dmtNote.reason = DMT_NONE;          /* report it once */
    return was;
}

#endif
