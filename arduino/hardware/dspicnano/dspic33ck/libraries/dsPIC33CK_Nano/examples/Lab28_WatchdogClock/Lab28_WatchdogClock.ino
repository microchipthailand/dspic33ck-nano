/*
 * Lab 28 - Watchdog timer, software reset and oscillator switching
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * Goal:  1. WATCHDOG: switch it on, feed it, and let it rescue a program that
 *           hangs. After the reset, find out WHERE it hung and for how long.
 *        1b. WDTset(ms) / WDTclear(): a watchdog with the timeout YOU choose
 *           (functions in the Watchdog.h tab).
 *        2. WINDOW WATCHDOG: feeding too EARLY is also an error.
 *        3. SOFTWARE RESET: restart on purpose and remember why.
 *        4. OSCILLATOR SWITCH: change the CPU clock while the program runs
 *           (100 MIPS <-> 4 MIPS <-> 2 MIPS <-> 16 kIPS) and keep Serial and
 *           millis() working.
 *
 * Hardware: nothing to wire. Serial Monitor on J5 (115200).
 *   Optional: scope / frequency meter / LED + 1k on D9 = RB4 (J3-15). The
 *   reference clock output puts FOSC / 2000 there, so you can SEE the clock:
 *   100 kHz at 100 MIPS, 4 kHz at 4 MIPS, 2 kHz at 2 MIPS, 16 Hz on LPRC.
 *
 * Serial commands:
 *   ?   status: clock, watchdog, saved data
 *   t   WDTset(1000) on / off: loop() calls WDTclear() on every pass
 *   T   stop calling WDTclear() -> reset after 1000 ms, reported at the restart
 *   w   watchdog on / off (loop() feeds it every 100 ms)
 *   h   HANG: watchdog on, then stuck in while(1) -> watchdog reset (nominally ~1 s)
 *   W   window mode on / off (loop() then feeds it at 7/8 of the period)
 *   e   feed EARLY while in window mode -> watchdog reset at once
 *   r   software reset (RESET instruction), with a saved reason
 *   1   clock: FRC + PLL   FOSC 200 MHz  -> 100 MIPS
 *   2   clock: FRC         FOSC   8 MHz  ->   4 MIPS
 *   3   clock: FRC / 2     FOSC   4 MHz  ->   2 MIPS
 *   4   clock: LPRC        FOSC  32 kHz  ->  16 kIPS for 5 s, then back
 *   s   speed test: how much work the CPU does per millisecond
 *
 * ----------------------------------------------------------------------------
 * WATCHDOG (WDT)
 *   A counter clocked by the 32 kHz LPRC oscillator, independent of the CPU
 *   clock. If the program does not "feed" (clear) it in time, it resets the
 *   chip. A program that hangs stops feeding it -> automatic restart.
 *   Config bits (system_config.c, fixed when the chip is programmed):
 *     FWDTEN = ON_SW   off after reset; software turns it on: WDTCONLbits.ON
 *     RWDTPS = PS32768 period = 32768 / 32 kHz = ~1 s nominal (WDTCONL.RUNDIV = 15)
 *                      (the MPLAB simulator gives 8.2 s - the 'h' test shows your chip's)
 *     WINDIS = ON      window mode not forced; WDTCONLbits.WDTWINEN turns it on
 *     WDTWIN = WIN25   (erased default) the feeding window is the LAST 25 %
 *   Feed it:  WDTCONH = 0x5743   (a key, so a stray write cannot feed it)
 *   The LPRC is only accurate to a few %, so the sketch MEASURES the real
 *   period with the 'h' test and uses that for window-mode feeding.
 *
 * WDTset(ms) / WDTclear()  (Watchdog.h)
 *   The chip's WDT period is fixed by RWDTPS, so the program cannot pick it.
 *   WDTset(1000) adds a 1 ms check interrupt (SCCP3): if WDTclear() was not
 *   called for 1000 ms, it resets the chip at once and leaves a note in RAM
 *   that survives the reset. The check interrupt is also the only thing that
 *   feeds the chip's WDT, so if even interrupts stop, the hardware still
 *   resets ~1 s later. Details in Watchdog.h.
 *
 * WINDOW MODE
 *   Feeding is only allowed in the last 25 % of the period. Feeding earlier
 *   resets the chip, like a missed feed. This catches a program that is stuck
 *   in a short loop which still feeds the watchdog much too often.
 *
 * SOFTWARE RESET
 *   asm("reset") restarts the chip as if the RESET button was pressed, but
 *   RCON shows SWR instead of EXTR. RAM is not cleared by a reset, so a
 *   variable marked __attribute__((persistent)) (not zeroed by the start-up
 *   code) can carry information into the next run. Here it sits at a fixed
 *   address near the top of RAM, above anything the serial bootloader uses.
 *
 * OSCILLATOR SWITCHING
 *   OSCCON.NOSC = new source, OSCCON.OSWEN = 1 starts the switch, the hardware
 *   clears OSWEN when it is done (needs FCKSM = CSECMD: switching enabled).
 *   Unlock sequences: __builtin_write_OSCCONH / __builtin_write_OSCCONL.
 *     NOSC 0 = FRC 8 MHz, 1 = FRC + PLL, 5 = LPRC 32 kHz, 7 = FRC / FRCDIV
 *   The PLL may only be reprogrammed while the CPU is NOT running from it.
 *   Everything the core worked out from the clock at compile time (FCY) is
 *   now wrong. This sketch re-times the two things it uses:
 *     Timer1 (millis/delay): PR1 and prescaler       UART1 (Serial): U1BRG
 *   Still at compile-time speed after a switch: micros(), delayMicroseconds(),
 *   analogWrite() and tone() frequencies, the ADC clock, NanoTimer periods.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "Watchdog.h"

const unsigned long BAUD = 115200;
const uint8_t PIN_REFO = 9;                 /* D9 = RB4 = RP36 (J3-15) */

/* ---------------- data that survives a reset ---------------- */
struct Saved {
    uint16_t magic;                         /* 0xC0DE = contents are valid */
    uint16_t boots;                         /* starts since power-on */
    uint16_t swReason;                      /* why we did a software reset */
    uint16_t checkpoint;                    /* where the program was last */
    unsigned long hangMs;                   /* how long the hang lasted */
    unsigned long wdtPeriodMs;              /* measured watchdog period, 0 = not yet */
};
Saved saved __attribute__((persistent, address(0x4F00)));

enum { CP_NONE, CP_LOOP, CP_HANG, CP_EARLY };
const char *const CP_NAME[] = { "-", "loop()", "hang test: stuck in while(1)", "window test: fed too early" };
enum { SW_NONE, SW_COMMAND };

uint16_t bootRcon;
bool wdtTimeout = false;                    /* last reset = a WDTset() timeout */

/* ---------------- watchdog ---------------- */
bool wdtOn = false, wdtWindow = false;     /* the w / W modes (chip WDT only) */
unsigned long lastFeed = 0;
bool forgetClear = false;                   /* 'T': loop() stops calling WDTclear() */

inline void wdtFeed() { wdtHwFeed(); }

/* w / W modes: the chip's own WDT, fed by loop() */
void wdtEnable(bool on, bool window)
{
    WDTstop();                              /* one watchdog mode at a time */
    if (on) wdtHwStart(window); else wdtHwStop();
    wdtOn = on; wdtWindow = on && window;
    lastFeed = millis();
}

unsigned long wdtPeriod() { return saved.wdtPeriodMs ? saved.wdtPeriodMs : 1024; }

void wdtService()
{
    if (!wdtOn) return;
    unsigned long since = millis() - lastFeed;
    /* normal: feed often; window mode: only after 7/8 of the period */
    if (since >= (wdtWindow ? wdtPeriod() * 7 / 8 : 100)) { wdtFeed(); lastFeed = millis(); }
}

/* ---------------- clock ---------------- */
enum { CLK_PLL, CLK_FRC, CLK_FRC2, CLK_LPRC };
const char *const CLK_NAME[] = { "FRC + PLL (FOSC 200 MHz)", "FRC (FOSC 8 MHz)", "FRC / 2 (FOSC 4 MHz)", "LPRC (FOSC 32 kHz)" };
const unsigned long CLK_FCY[] = { 100000000UL, 4000000UL, 2000000UL, 16000UL };
uint8_t clk = (FCY > 8000000UL) ? CLK_PLL : CLK_FRC;    /* what the core started with */
unsigned long fcyNow = FCY;

void oscSwitch(uint8_t nosc)
{
    __builtin_write_OSCCONH(nosc);
    __builtin_write_OSCCONL(OSCCON | 0x01); /* OSWEN = 1: go */
    while (OSCCONbits.OSWEN) { }            /* hardware clears it when done */
}

/* millis() and Serial depend on FCY: set them up again for the new clock */
void retime(unsigned long fcy)
{
    T1CONbits.TON = 0;
    if (fcy <= 8000000UL)       { T1CONbits.TCKPS = 0; PR1 = fcy / 1000UL - 1; }        /* 1:1 */
    else if (fcy <= 32000000UL) { T1CONbits.TCKPS = 1; PR1 = fcy / 8000UL - 1; }        /* 1:8 */
    else                        { T1CONbits.TCKPS = 2; PR1 = fcy / 64000UL - 1; }       /* 1:64, as the core */
    TMR1 = 0;
    T1CONbits.TON = 1;

    /* New baud divider. The line is idle (Serial.flush() ran before the switch),
     * so it is written with the UART left ON: switching UARTEN off and on again
     * would also need UTXEN/URXEN set again, or the transmitter stays dead. */
    unsigned long brg = (fcy + BAUD / 2) / BAUD;    /* fractional baud generator, as the core */
    U1BRG  = (uint16_t)brg;
    U1BRGH = (uint16_t)(brg >> 16);
    fcyNow = fcy;
}

void setClock(uint8_t c)
{
    Serial.flush();                         /* let the last byte leave at the old speed */
    if (c == CLK_PLL) {
        if (OSCCONbits.COSC != 1) {
            /* the PLL may only be changed while we are NOT running from it.
             * Same values as the core:  FRC 8 MHz / N1 1 x M 100 = FVCO 800 MHz,
             * / N2 2 / N3 1 = FPLLO 400 MHz, FOSC = FPLLO / 2 = 200 MHz, FCY 100 MHz.
             * Check it on the board: D9 must show 100 kHz in this mode. */
            CLKDIVbits.PLLPRE = 1;          /* N1 = 1 (on this family the field IS the divider) */
            PLLFBDbits.PLLFBDIV = 100;      /* M  = 100 */
            PLLDIVbits.POST1DIV = 2;        /* N2 = 2 */
            PLLDIVbits.POST2DIV = 1;        /* N3 = 1 */
            oscSwitch(1);
            while (!OSCCONbits.LOCK) { }    /* wait for the PLL to lock */
        }
    } else if (c == CLK_FRC) {
        oscSwitch(0);
    } else if (c == CLK_FRC2) {
        CLKDIVbits.FRCDIV = 1;              /* FRC / 2 */
        oscSwitch(7);                       /* FRCDIVN */
    } else {
        oscSwitch(5);                       /* LPRC */
    }
    clk = c;
    retime(CLK_FCY[c]);
}

/* 5 seconds on the 32 kHz LPRC: Serial cannot run at 16 kIPS, and a 1 ms
 * Timer1 interrupt would come every 16 instructions, so it is switched off.
 * Timer1 counts without an interrupt and paces the USER LED instead. */
void lprcDemo()
{
    uint8_t back = clk;
    bool wdtWas = wdtOn;
    unsigned long setWas = WDTgetTimeout(); /* WDTset() mode, 0 = off */
    Serial.println("# LPRC for 5 s: Serial is silent, USER LED toggles every 0.5 s, D9 shows 16 Hz.");
    if (wdtWas || setWas) Serial.println("#   (watchdog switched off for the demo)");
    wdtEnable(false, false);
    Serial.flush();
    IEC0bits.T1IE = 0;
    oscSwitch(5);
    T1CONbits.TON = 0; T1CONbits.TCKPS = 0; PR1 = 0xFFFF; TMR1 = 0; T1CONbits.TON = 1;
    for (uint8_t half = 0; half < 10; ) {
        if (TMR1 >= CLK_FCY[CLK_LPRC] / 2) {   /* 8000 counts = 0.5 s at 16 kHz */
            TMR1 = 0;
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            half++;
        }
    }
    IFS0bits.T1IF = 0; IEC0bits.T1IE = 1;
    setClock(back);
    Serial.print("# back on "); Serial.print(CLK_NAME[back]);
    Serial.println(". millis() did not count during those 5 s.");
    if (wdtWas) wdtEnable(true, false);
    if (setWas) WDTset(setWas);
}

/* reference clock output: FOSC / (2 x RODIV) on D9, so the clock is visible */
bool refoBegin()
{
    uint16_t n;
    REFOCONLbits.ROEN = 0;
    for (n = 0; REFOCONLbits.ROACTIVE && n < 50000; n++) { }
    REFOCONLbits.ROSEL = 0;                 /* source = FOSC: follows every switch */
    REFOCONH = 1000;                        /* RODIV: FOSC / 2000 */
    REFOCONLbits.ROSWEN = 1;                /* load the new divider ... */
    for (n = 0; REFOCONLbits.ROSWEN && n < 50000; n++) { }   /* ... hardware clears it (bounded wait) */
    if (REFOCONLbits.ROSWEN) return false;
    REFOCONLbits.ROOUT = 1;
    REFOCONLbits.ROEN = 1;
    pinMode(PIN_REFO, OUTPUT);
    __builtin_write_RPCON(0x0000);          /* unlock PPS */
    _RP36R = _RPOUT_REFO;                   /* RB4 = RP36 <- REFCLKO */
    __builtin_write_RPCON(0x0800);          /* lock */
    return true;
}

/* ---------------- report ---------------- */
void speedTest()
{
    unsigned long n = 0, t0 = millis();
    while (millis() == t0) { }              /* start at a tick edge */
    t0 = millis();
    while (millis() - t0 < 100) { n++; }
    Serial.print("# speed: "); Serial.print(n / 100); Serial.print(" loop passes per ms at ");
    Serial.print(fcyNow / 1000000.0, 3); Serial.println(" MIPS");
}

void status()
{
    Serial.print("# clock: "); Serial.print(CLK_NAME[clk]); Serial.print(", FCY = "); Serial.print(fcyNow);
    Serial.print(" Hz, OSCCON.COSC = "); Serial.print(OSCCONbits.COSC);
    Serial.print(", D9 = "); Serial.print(fcyNow / 1000UL); Serial.println(" Hz");
    Serial.print("# WDTset: ");
    if (WDTisOn()) { Serial.print("ON, timeout "); Serial.print(WDTgetTimeout()); Serial.println(forgetClear ? " ms, WDTclear() NOT called" : " ms, loop() calls WDTclear()"); }
    else Serial.println("off");
    Serial.print("# watchdog: "); Serial.print(wdtOn ? (wdtWindow ? "ON, window mode" : "ON") : (WDTisOn() ? "ON, fed by WDTset()" : "off"));
    Serial.print(", RUNDIV = "); Serial.print(WDTCONLbits.RUNDIV);
    Serial.print(" (1:"); Serial.print(1UL << WDTCONLbits.RUNDIV); Serial.print(" -> ~");
    Serial.print((1UL << WDTCONLbits.RUNDIV) / 32UL); Serial.print(" ms), measured ");
    if (saved.wdtPeriodMs) { Serial.print(saved.wdtPeriodMs); Serial.println(" ms"); }
    else Serial.println("- (send h)");
    Serial.print("# starts since power-on: "); Serial.println(saved.boots);
}

void reportBoot()
{
    Serial.print("# RCON = 0x"); Serial.print(bootRcon, HEX); Serial.print(" -> ");
    if (bootRcon & 0x0010) {
        Serial.println("WATCHDOG reset");
        Serial.print("#   last checkpoint: "); Serial.println(CP_NAME[saved.checkpoint < 4 ? saved.checkpoint : 0]);
        if (saved.checkpoint == CP_HANG) {
            Serial.print("#   the program hung for "); Serial.print(saved.hangMs);
            Serial.println(" ms before the watchdog fired = the real watchdog period");
            saved.wdtPeriodMs = saved.hangMs;
        }
    } else if (wdtTimeout) {
        Serial.print("WATCHDOG reset by WDTset("); Serial.print(WDTlastTimeout());
        Serial.print("): no WDTclear() for "); Serial.print(WDTlastLate()); Serial.println(" ms");
        Serial.print("#   last checkpoint: "); Serial.println(CP_NAME[saved.checkpoint < 4 ? saved.checkpoint : 0]);
    } else if (bootRcon & 0x0040) {
        Serial.print("SOFTWARE reset, saved reason: ");
        Serial.println(saved.swReason == SW_COMMAND ? "'r' command" : "none");
    } else if (bootRcon & 0x0001) Serial.println("POWER-ON reset");
    else if (bootRcon & 0x0080)   Serial.println("MCLR (RESET button / programmer)");
    else                          Serial.println("other - see Lab 18 for every RCON flag");
    saved.swReason = SW_NONE;
    saved.checkpoint = CP_NONE;
}

/* ---------------- commands ---------------- */
int nextCommand() { return Serial.available() > 0 ? Serial.read() : -1; }

void command(int c)
{
    switch (c) {
    case '?': status(); break;
    case 't': if (WDTisOn()) { WDTstop(); Serial.println("# WDTset off"); }
              else {
                  wdtEnable(false, false);  /* not together with w / W */
                  forgetClear = false;
                  WDTset(1000);
                  Serial.println("# WDTset(1000) ON: loop() calls WDTclear() - send T to stop clearing");
              }
              break;
    case 'T': if (!WDTisOn()) { Serial.println("# T only matters after t (WDTset)"); break; }
              Serial.println("# loop() keeps running but no longer calls WDTclear() -> reset in 1000 ms ...");
              Serial.flush();
              forgetClear = true;
              break;
    case 'w': wdtEnable(!wdtOn, false);
              Serial.println(wdtOn ? "# watchdog ON, fed every 100 ms" : "# watchdog off"); break;
    case 'W': if (wdtWindow) { wdtEnable(true, false); Serial.println("# window mode off"); }
              else {
                  wdtEnable(true, true);
                  Serial.print("# window mode ON: fed every "); Serial.print(wdtPeriod() * 7 / 8);
                  Serial.println(saved.wdtPeriodMs ? " ms" : " ms (period not measured yet - send h first if it resets)");
              }
              break;
    case 'e': if (!wdtWindow) { Serial.println("# e only matters in window mode (send W)"); break; }
              Serial.println("# feeding the watchdog EARLY ..."); Serial.flush();
              saved.checkpoint = CP_EARLY;
              wdtFeed();                    /* inside the closed part of the window */
              delay(50); wdtFeed();
              for (;;) { }
    case 'h': {
              Serial.println("# hanging in while(1) - the watchdog should reset us (nominally ~1 s) ..."); Serial.flush();
              wdtEnable(true, false);
              saved.checkpoint = CP_HANG;
              unsigned long t0 = millis();
              for (;;) { saved.hangMs = millis() - t0; }   /* the "bug" */
              }
    case 'r': Serial.println("# software reset ..."); Serial.flush();
              saved.swReason = SW_COMMAND;
              asm volatile ("reset");
              break;
    case '1': case '2': case '3':
              setClock(c - '1');
              Serial.print("# switched to "); Serial.print(CLK_NAME[clk]); Serial.print(", ");
              Serial.print(fcyNow / 1000000.0, 3); Serial.println(" MIPS"); break;
    case '4': lprcDemo(); break;
    case 's': speedTest(); break;
    }
}

void setup()
{
    bootRcon = RCON;                        /* read the reset reason FIRST ... */
    RCON = 0;                               /* ... then clear it for next time */
    wdtTimeout = WDTwasTimeout(bootRcon);   /* was it a WDTset() timeout? */
    if ((bootRcon & 0x0001) || saved.magic != 0xC0DE) {   /* power-on: RAM is random */
        saved.magic = 0xC0DE; saved.boots = 0; saved.swReason = SW_NONE;
        saved.checkpoint = CP_NONE; saved.hangMs = 0; saved.wdtPeriodMs = 0;
    }
    saved.boots++;

    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(BAUD);
    delay(10);
    Serial.println();
    Serial.println("Lab 28 - watchdog, software reset, clock switch.  ? t T w h W e r 1 2 3 4 s");
    reportBoot();
    if (!refoBegin()) Serial.println("# reference clock output did not start (ROSWEN stuck) - D9 stays low");
    status();
}

void loop()
{
    saved.checkpoint = CP_LOOP;
    if (WDTisOn() && !forgetClear) WDTclear();   /* "still alive" */
    wdtService();
    digitalWrite(LED_BUILTIN, (millis() / 500) % 2 ? LED_ON : LED_OFF);   /* 1 Hz if millis is right */
    int c = nextCommand();
    if (c > 0) command(c);
}
