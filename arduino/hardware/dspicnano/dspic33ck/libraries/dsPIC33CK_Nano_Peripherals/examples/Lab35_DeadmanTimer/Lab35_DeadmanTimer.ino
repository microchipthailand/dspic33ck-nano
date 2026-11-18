/*
 * Lab 35 - DMT: the Deadman Timer
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *        Tools > Bootloader: None (upload with a PICkit on J4) - see below
 *
 * Goal:  1. Meet the second watchdog of this chip: one that counts
 *           INSTRUCTIONS instead of time, and has a window.
 *        2. Clear it correctly: two-step key, only while the window is open.
 *        3. Break the rules on purpose - too late, too early, wrong key - and
 *           see each one caught and reported after the reset.
 *        4. Use the counter as a free instruction counter: how many
 *           instructions does a float division, an analogRead() ... cost?
 *
 * Hardware: nothing to wire. Serial Monitor on J5 (115200).
 *   USER LED: blinks while loop() is alive, flashes on every DMT clear.
 *
 * Serial commands:
 *   ?   status: config words, counter, window, last event
 *   d   start the DMT. Cannot be stopped again - only a reset stops it.
 *       From then on loop() clears it once per pass, when the window opens.
 *   h   HANG: stop clearing -> DMT event at the time-out (2 s here)
 *   e   clear EARLY (window still closed) -> DMT event at once
 *   k   wrong KEY (bad STEP1 value) -> DMT event at once (BAD1)
 *   n   count instructions of a few operations with the DMT counter
 *
 * ----------------------------------------------------------------------------
 * WATCHDOG (Lab 28) vs DEADMAN TIMER
 *                     WDT                         DMT
 *   counts            LPRC 32 kHz (time)          instruction cycles (work done)
 *   in Sleep          keeps running, can wake     stops with the CPU
 *   too late          reset                       DMT event
 *   too early         only in window mode         always: window opens at DMTIVT
 *   clear key         WDTCONH = 0x5743            0x40 -> DMTPRECLR, 0x08 -> DMTCLR
 *   can be stopped    yes (FWDTEN = ON_SW)        no, once on it stays on
 *   Using both is common in safety code: the WDT checks "still alive in real
 *   time", the DMT checks "the right amount of code ran between two clears".
 *
 * THE CONFIG WORDS THIS SKETCH SETS (with #pragma config below)
 *   DMTDIS = OFF        off at reset, the 'd' command switches it on
 *   DMTCNT  = 2 s worth of instructions   (200 000 000 at 100 MIPS)
 *   DMTIVT  = 1 s worth of instructions   (100 000 000 at 100 MIPS)
 *   -> loop() may clear it between 1 s and 2 s after the last clear.
 *   Config words are only programmed by a PICkit upload (Bootloader: None).
 *   After a serial-bootloader upload they are the bootloader's, i.e. erased:
 *   DMTCNT = DMTIVT = 0xFFFFFFFF, the window never opens before the time-out
 *   and the DMT cannot be cleared. The sketch says so and still lets you try
 *   'd' (event after 0xFFFFFFFF instructions = 43 s at 100 MIPS).
 *
 * NOT VERIFIED on hardware or in the simulator (the simulator has no DMT).
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include "DMT.h"

#pragma config DMTDIS = OFF                 /* software switches it on */
#if FCY == 100000000UL
#pragma config DMTCNTL = 0xC200             /* 200 000 000 = 0x0BEBC200 -> 2 s */
#pragma config DMTCNTH = 0x0BEB
#pragma config DMTIVTL = 0xE100             /* 100 000 000 = 0x05F5E100 -> 1 s */
#pragma config DMTIVTH = 0x05F5
#else
#pragma config DMTCNTL = 0x1200             /*   8 000 000 = 0x007A1200 -> 2 s */
#pragma config DMTCNTH = 0x007A
#pragma config DMTIVTL = 0x0900             /*   4 000 000 = 0x003D0900 -> 1 s */
#pragma config DMTIVTH = 0x003D
#endif

const unsigned long BAUD = 115200;

bool clearing = true;                       /* false after 'h' */
uint32_t clears = 0;
unsigned long lastClearMs = 0, lastClearGapMs = 0;
unsigned long ledFlashUntil = 0;

float secondsOf(uint32_t instructions) { return (float)instructions / FCY; }

void printStat(uint16_t st)
{
    Serial.print("DMTSTAT=0x"); Serial.print(st, HEX);
    if (st & 0x80) Serial.print(" BAD1(wrong STEP1)");
    if (st & 0x40) Serial.print(" BAD2(wrong STEP2 / bad clear)");
    if (st & 0x20) Serial.print(" DMTEVENT");
    if (st & 0x01) Serial.print(" WINOPN");
}

void status()
{
    Serial.println("# Deadman Timer");
    Serial.print("  time-out DMTCNT = "); Serial.print(dmtTimeout());
    Serial.print(" instructions = "); Serial.print(secondsOf(dmtTimeout()), 3); Serial.println(" s at this clock");
    Serial.print("  window   DMTIVT = "); Serial.print(dmtWindow());
    Serial.print(" instructions = "); Serial.print(secondsOf(dmtWindow()), 3); Serial.println(" s");
    if (!dmtUsable())
        Serial.println("  ! the window opens only at the time-out: these are erased config words\n"
                       "    (serial bootloader upload). Upload with a PICkit, Bootloader: None.");
    Serial.print("  state: "); Serial.print(dmtIsOn() ? "ON" : "off");
    if (dmtIsOn()) {
        Serial.print("  counter="); Serial.print(dmtCount());
        Serial.print("  "); printStat(DMTSTAT);
        Serial.print("  clears="); Serial.print(clears);
        if (clears) { Serial.print("  last gap "); Serial.print(lastClearGapMs); Serial.print(" ms"); }
    }
    Serial.println();
    Serial.print("  DMT events since power-on: "); Serial.println(dmtNote.events);
}

/* ---------------- 'n': instruction counting ---------------- */
volatile float fa = 3.0f, fb = 7.0f, fr;
volatile long la = 300000L, lb = 7L, lr;

void countOne(const char *name, void (*fn)())
{
    noInterrupts();                         /* interrupts would add their own instructions */
    uint32_t a = dmtCount();
    fn();
    uint32_t b = dmtCount();
    interrupts();
    Serial.print("  "); Serial.print(name); Serial.print(": ");
    Serial.print(b - a); Serial.println(" instructions");
}
void opNothing()  { }
void opLongDiv()  { lr = la / lb; }
void opFloatDiv() { fr = fa / fb; }
void opSqrt()     { fr = sqrtf(fa); }
void opSin()      { fr = sinf(fa); }

void countInstructions()
{
    if (!dmtIsOn()) { Serial.println("# start the DMT with 'd' first - its counter is the clock here"); return; }
    if (dmtClear()) clears++;
    Serial.println("# instructions, counted by the DMT (includes ~10 for the two counter reads):");
    countOne("nothing      ", opNothing);
    countOne("long  a / b  ", opLongDiv);
    countOne("float a / b  ", opFloatDiv);
    countOne("sqrtf(a)     ", opSqrt);
    countOne("sinf(a)      ", opSin);
    noInterrupts(); uint32_t a = dmtCount(); interrupts();
    analogRead(A0);
    noInterrupts(); uint32_t b = dmtCount(); interrupts();
    Serial.print("  analogRead(A0): "); Serial.print(b - a); Serial.println(" instructions (waits for the ADC)");
}

void help()
{
    Serial.println();
    Serial.println("Lab 35 - Deadman Timer   ? status  d start  h hang  e clear early  k bad key  n count instructions");
}

void setup()
{
    uint16_t rcon = RCON;
    RCON = 0;
    bool wasEvent = dmtBootCheck(rcon);
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(BAUD);
    help();
    Serial.print("# reset cause RCON=0x"); Serial.print(rcon, HEX);
    if (wasEvent) {
        Serial.print("  -> DMT EVENT. ");
        printStat(dmtNote.stat);
        Serial.print(", counter "); Serial.print(dmtNote.count);
        Serial.print(" (= "); Serial.print(secondsOf(dmtNote.count), 3); Serial.println(" s after the last clear)");
        Serial.print("  you had asked for: ");
        switch (dmtNote.tag) {
        case 'h': Serial.println("'h' stop clearing -> too LATE, counter reached DMTCNT"); break;
        case 'e': Serial.println("'e' clear while the window was closed -> too EARLY"); break;
        case 'k': Serial.println("'k' wrong STEP1 key -> BAD1"); break;
        default:  Serial.println("nothing - a real fault in the program?"); break;
        }
    }
    Serial.println();
    dmtNote.tag = 0;
    status();
}

void loop()
{
    /* the job of a real program: clear once per pass, inside the window */
    if (dmtIsOn() && clearing && dmtWindowOpen()) {
        if (dmtClear()) {
            clears++;
            lastClearGapMs = millis() - lastClearMs;
            lastClearMs = millis();
            ledFlashUntil = millis() + 60;
        }
    }

    /* LED: slow blink = alive; short flash = a DMT clear */
    bool flash = millis() < ledFlashUntil;
    digitalWrite(LED_BUILTIN, (flash || ((millis() / 500) & 1)) ? LED_ON : LED_OFF);

    if (!Serial.available()) return;
    char c = Serial.read();
    switch (c) {
    case '?': status(); break;
    case 'd':
        if (dmtIsOn()) { Serial.println("# already on"); break; }
        dmtStart();
        lastClearMs = millis();
        Serial.println("# DMT ON - it cannot be switched off again. loop() clears it when the window opens.");
        break;
    case 'h':
        if (!dmtIsOn()) { Serial.println("# start it with 'd' first"); break; }
        dmtNote.tag = 'h'; clearing = false;
        Serial.print("# stopped clearing: event in about ");
        Serial.print(secondsOf(dmtTimeout() - dmtCount()), 2); Serial.println(" s");
        break;
    case 'e':
        if (!dmtIsOn()) { Serial.println("# start it with 'd' first"); break; }
        if (dmtWindowOpen()) dmtClear();    /* a good clear closes the window again */
        Serial.println("# clearing while the window is CLOSED...");
        Serial.flush();
        dmtNote.tag = 'e';
        dmtClearEarly();
        delay(100);
        Serial.println("# (still running: this chip did not treat an early clear as an event)");
        dmtNote.tag = 0;
        break;
    case 'k':
        if (!dmtIsOn()) { Serial.println("# start it with 'd' first"); break; }
        Serial.println("# wrong STEP1 key...");
        Serial.flush();
        dmtNote.tag = 'k';
        dmtClearBadKey();
        delay(100);
        Serial.println("# (still running: no event from the bad key?)");
        dmtNote.tag = 0;
        break;
    case 'n': countInstructions(); break;
    default: break;
    }
}
