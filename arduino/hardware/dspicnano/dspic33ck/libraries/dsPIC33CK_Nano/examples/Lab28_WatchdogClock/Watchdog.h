/*
 * Watchdog.h - watchdog with a timeout you choose, for Lab 28
 *
 *   WDTset(1000);       watchdog ON: reset the chip if WDTclear() is not
 *                       called for 1000 ms (1 ms .. 3 600 000 ms)
 *   WDTclear();         "I am still alive" - call it in loop()
 *   WDTstop();          watchdog off
 *   WDTisOn()           true while it runs        WDTgetTimeout()  the ms value
 *
 *   After a reset, at the start of setup():
 *   uint16_t rcon = RCON;
 *   if (WDTwasTimeout(rcon)) ...   true = the last reset was a WDTset() timeout
 *   WDTlastLate()                  how many ms WDTclear() was missing then
 *
 *   Lower level, the chip's own watchdog (used by the w / W / e / h commands):
 *   wdtHwStart(window);  wdtHwStop();  wdtHwFeed();
 *
 * ---------------------------------------------------------------------------
 * WHY TWO LAYERS
 *   The chip's watchdog (WDT) has a FIXED period. It is set by the config bit
 *   RWDTPS when the chip is programmed (this board: 1:32768 of the 32 kHz
 *   LPRC = ~1 s nominal), and the program cannot change it.
 *   WDTCONL.RUNDIV only REPORTS that setting.
 *   So WDTset(ms) adds a second, adjustable watchdog in front of it:
 *
 *     loop() --WDTclear()--> lastClear = millis()
 *     SCCP3 interrupt, every 1 ms:
 *        millis() - lastClear >= timeout ?  -> save the details, RESET
 *        otherwise                          -> feed the chip's WDT
 *     chip WDT (~1 s): fed ONLY by that interrupt
 *
 *   - loop() stuck or too slow: the 1 ms check sees it -> reset after
 *     exactly "timeout" ms (to about 1 ms).
 *   - interrupts dead too (stuck with interrupts off, or in a higher
 *     interrupt): nobody feeds the chip's WDT -> the hardware resets ~1 s later.
 *   The first is a software RESET (RCON.SWR) with a note left in RAM that
 *   survives the reset; WDTwasTimeout() reads that note.
 *   The second is a real WDT reset (RCON.WDTO).
 *
 * RESOURCES
 *   SCCP3 as a 32-bit timer (NanoTimer Timer3 / analogWrite D7 / NanoServo:
 *   don't use them together with WDTset). The check uses millis(), so it
 *   stays right after a clock switch (Lab 28 re-times millis()). Only how
 *   often it checks changes: every 1 ms at the compile-time clock, every
 *   50 ms at 2 MIPS if compiled for 100 MIPS.
 *   The note for after the reset is a 'persistent' variable at 0x4F20
 *   (not cleared by the start-up code; Lab 28's own data is at 0x4F00).
 * ---------------------------------------------------------------------------
 */
#ifndef LAB28_WATCHDOG_H
#define LAB28_WATCHDOG_H

#include <Arduino.h>

/* ================= the chip's watchdog (fixed period) ================= */
inline void wdtHwFeed(void) { WDTCONH = 0x5743; }    /* the feed key */

/* window = true: feeding is only allowed in the last 25 % of the period */
void wdtHwStart(bool window)
{
    WDTCONLbits.ON = 0;                     /* change the mode only while it is off */
    WDTCONLbits.WDTWINEN = window;
    WDTCONLbits.ON = 1;
    if (!window) wdtHwFeed();               /* window mode: a feed NOW would be "too early" */
}

void wdtHwStop(void) { WDTCONLbits.ON = 0; WDTCONLbits.WDTWINEN = 0; }

/* ================= WDTset(): your timeout ================= */
struct WdtNote {                            /* survives the reset */
    uint16_t magic;                         /* 0x5744 = valid */
    uint16_t fired;                         /* 1 = the timeout caused the reset */
    unsigned long timeoutMs;
    unsigned long lateMs;                   /* ms since the last WDTclear() */
};
WdtNote wdtNote __attribute__((persistent, address(0x4F20)));

volatile unsigned long wdtLastClear = 0;
unsigned long wdtTimeoutMs = 0;             /* 0 = off */
unsigned long wdtResetLate = 0, wdtResetTimeout = 0;   /* filled by WDTwasTimeout() */

/* the 1 ms check (SCCP3 timer interrupt, lowest priority) */
extern "C" void __attribute__((interrupt, auto_psv)) _CCT3Interrupt(void)
{
    IFS2bits.CCT3IF = 0;
    unsigned long since = millis() - wdtLastClear;
    if (since >= wdtTimeoutMs) {            /* WDTclear() missing for too long */
        wdtNote.magic = 0x5744;
        wdtNote.fired = 1;
        wdtNote.timeoutMs = wdtTimeoutMs;
        wdtNote.lateMs = since;
        asm volatile ("reset");
    }
    wdtHwFeed();                            /* all fine: keep the chip's WDT quiet */
}

void WDTclear(void)
{
    unsigned long now = millis();
    noInterrupts();                         /* 32-bit write: the check must not see half of it */
    wdtLastClear = now;
    interrupts();
}

void WDTstop(void)
{
    CCP3CON1Lbits.CCPON = 0;
    IEC2bits.CCT3IE = 0;
    wdtHwStop();
    wdtTimeoutMs = 0;
}

void WDTset(unsigned long ms)
{
    if (ms < 1) ms = 1;
    if (ms > 3600000UL) ms = 3600000UL;
    WDTstop();
    wdtTimeoutMs = ms;
    WDTclear();

    /* SCCP3: one 32-bit timer at FCY, interrupt every 1 ms */
    PMD2bits.CCP3MD = 0;
    CCP3CON1L = 0; CCP3CON1H = 0; CCP3CON2L = 0; CCP3CON2H = 0; CCP3CON3H = 0;
    CCP3CON1L = 0x0020;                     /* T32, timer mode, clock FCY, 1:1 */
    CCP3TMRL = 0; CCP3TMRH = 0;
    CCP3PRL = (uint16_t)(FCY / 1000UL - 1);
    CCP3PRH = (uint16_t)((FCY / 1000UL - 1) >> 16);
    IPC9bits.CCT3IP = 1;                    /* lowest: any other interrupt goes first */
    IFS2bits.CCT3IF = 0;
    IEC2bits.CCT3IE = 1;

    wdtHwStart(false);                      /* the chip's WDT as the backup */
    CCP3CON1Lbits.CCPON = 1;
}

bool          WDTisOn(void)       { return wdtTimeoutMs != 0; }
unsigned long WDTgetTimeout(void) { return wdtTimeoutMs; }

/* true if the last reset was a WDTset() timeout. rcon = RCON read at the
 * very start of setup(). Also fills WDTlastLate() / WDTlastTimeout(). */
bool WDTwasTimeout(uint16_t rcon)
{
    bool yes = (rcon & 0x0040)              /* SWR: our reset instruction ... */
            && !(rcon & 0x0003)             /* ... and not a power-on / brown-out */
            && wdtNote.magic == 0x5744 && wdtNote.fired == 1;
    if (yes) { wdtResetLate = wdtNote.lateMs; wdtResetTimeout = wdtNote.timeoutMs; }
    wdtNote.magic = 0x5744;                 /* clear the note for the next time */
    wdtNote.fired = 0;
    return yes;
}

unsigned long WDTlastLate(void)    { return wdtResetLate; }
unsigned long WDTlastTimeout(void) { return wdtResetTimeout; }

#endif /* LAB28_WATCHDOG_H */
