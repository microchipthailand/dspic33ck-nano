/*
 * Lab 18 - Power saving (Doze / Idle / Sleep) and reset sources (POR, BOR, WDT ...)
 * Board: dsPIC33CK Nano
 *
 * Goal:  1. find out WHY the chip started: read the RCON reset-flag register
 *           (power-on, brown-out, MCLR button, watchdog, software reset, trap...)
 *        2. cut the CPU's power with the three low-power modes and see what
 *           keeps running in each one
 *        3. cause each kind of reset on purpose and watch RCON report it
 *
 * Hardware: nothing to wire. Serial Monitor on J5 (115200).
 *   For current measurements: put a multimeter (mA range) in series with the
 *   USB-C 5 V supply, e.g. with a USB power meter / "USB doctor". The board's
 *   PWR LED (~1.3 mA) and the LDO's quiescent current stay in every reading.
 *
 * Serial commands:
 *   ?  reset reason + RCON again          r  software reset (RESET instruction)
 *   1  RUN   : busy loop, full speed      w  watchdog reset (enable WDT, stop feeding it)
 *   2  DOZE  : CPU 1:128, peripherals full speed (millis, UART keep working)
 *   3  IDLE  : CPU stopped, peripherals run; Timer1 wakes it every 1 ms
 *   4  SLEEP : everything stops; press USER to wake up
 *   MCLR reset   : press the RESET button (SW1)
 *   POR          : unplug USB-C, wait 2 s, plug in again
 *   BOR          : turn the supply down slowly (bench supply on 5V pin) -
 *                  below the BOR threshold the chip is held in reset
 *
 * ----------------------------------------------------------------------------
 * RESET SOURCES  (RCON register, all flags are SET by hardware, cleared by us)
 *   bit 0  POR    Power-on reset: VDD rose from ~0 V
 *   bit 1  BOR    Brown-out reset: VDD dipped below the BOR trip point
 *                 (on this family the BOR is always enabled; it is also set
 *                 together with POR at power-up)
 *   bit 2  IDLE   the device was in Idle when a RESET hit it
 *   bit 3  SLEEP  the device was in Sleep when a RESET hit it
 *                 (an ordinary interrupt wake-up does NOT set IDLE/SLEEP;
 *                  the MPLAB simulator agrees)
 *   bit 4  WDTO   watchdog time-out reset
 *   bit 6  SWR    software RESET instruction
 *   bit 7  EXTR   external reset: the MCLR pin (RESET button, PICkit)
 *   bit 8  VREGS  keep the voltage regulator on in Sleep (faster wake-up)
 *   bit 9  CM     configuration mismatch
 *   bit 14 IOPUWR illegal opcode / uninitialised W register used as pointer
 *   bit 15 TRAPR  trap conflict reset
 *   Rule: read RCON first thing, then CLEAR it - otherwise old flags pile up
 *   and the next reset looks like several at once.
 *
 * LOW-POWER MODES
 *   DOZE  CLKDIV.DOZE = ratio, DOZEN = 1: the CPU clock is divided (1:2..1:128),
 *         peripherals keep FCY. Code runs slower, timing stays correct.
 *         CLKDIV.ROI = 1 would end Doze automatically on any interrupt.
 *   IDLE  PWRSAV #1: CPU stops, peripherals keep running. Any enabled
 *         interrupt wakes it (here: Timer1, every 1 ms -> almost no saving
 *         unless you disable Timer1 too - try it, see 'IDLE_DEEP' below).
 *   SLEEP PWRSAV #0: the whole clock system stops. Only asynchronous events
 *         (change notification on a pin, INTx, WDT in sleep) wake it.
 *         millis() does NOT advance while asleep.
 *   Wake-up from sleep: the CPU first runs the ISR of the waking interrupt
 *   (if its priority is above the CPU's), then continues after PWRSAV.
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>

uint16_t bootRcon;                       /* RCON as it was at this start */

const char *const RCON_NAMES[16] = {
    "POR power-on", "BOR brown-out", "IDLE (reset in Idle)", "SLEEP (reset in Sleep)", "WDTO watchdog",
    "(5)", "SWR software", "EXTR MCLR pin", "VREGS", "CM config mismatch",
    "(10)", "(11)", "(12)", "(13)", "IOPUWR illegal opcode", "TRAPR trap conflict" };

void printRcon(uint16_t r)
{
    Serial.print("# RCON = 0x"); Serial.print(r, HEX); Serial.print(" :");
    for (int b = 0; b < 16; b++)
        if ((r & (1u << b)) && b != 8) { Serial.print(" ["); Serial.print(RCON_NAMES[b]); Serial.print("]"); }
    Serial.println();

    /* the most likely single cause, in priority order */
    const char *why = "unknown";
    if (r & 0x8000)      why = "TRAP CONFLICT reset";
    else if (r & 0x4000) why = "ILLEGAL OPCODE / bad pointer reset";
    else if (r & 0x0200) why = "CONFIGURATION MISMATCH reset";
    else if (r & 0x0010) why = "WATCHDOG reset - the program stopped feeding the WDT";
    else if (r & 0x0040) why = "SOFTWARE reset (RESET instruction)";
    else if ((r & 0x0003) == 0x0003) why = "POWER-ON (POR+BOR are both set at power-up)";
    else if (r & 0x0001) why = "POWER-ON reset";
    else if (r & 0x0002) why = "BROWN-OUT reset - the supply dipped";
    else if (r & 0x0080) why = "MCLR pin: RESET button or programmer";
    Serial.print("# reset reason: "); Serial.println(why);
}

/* ---------------- wake-up source for Sleep ---------------- */
volatile unsigned long wakeups = 0;
void onButton() { wakeups++; }

/* ---------------- tests ---------------- */
unsigned long countFor(unsigned long ms)          /* how much work in 'ms' of real time */
{
    unsigned long n = 0, t0 = millis();
    while (millis() - t0 < ms) n++;
    return n;
}

void testRun(void)
{
    Serial.print("# RUN: loops in 1 s = "); Serial.println(countFor(1000));
}

void testDoze(void)
{
    Serial.println("# DOZE 1:128 for 3 s - the LED blink still has the right timing,");
    Serial.println("#   the loop counter is ~128x smaller. Measure the current now.");
    Serial.flush();
    CLKDIVbits.ROI = 0;                 /* stay in Doze even when interrupts happen */
    CLKDIVbits.DOZE = 7;                /* 1:128 */
    CLKDIVbits.DOZEN = 1;
    unsigned long n = 0, t0 = millis();
    while (millis() - t0 < 3000) {      /* millis() is a peripheral (Timer1): still right */
        n++;
        digitalWrite(LED_BUILTIN, (millis() / 250) % 2 ? LED_ON : LED_OFF);
    }
    CLKDIVbits.DOZEN = 0;
    Serial.print("# DOZE done, loops in 3 s = "); Serial.print(n);
    Serial.println(" (compare with RUN x3)");
}

void testIdle(void)
{
    Serial.println("# IDLE for 3 s: the CPU sleeps between Timer1 ticks (1 ms).");
    Serial.flush();
    unsigned long wakes = 0, t0 = millis();
    while (millis() - t0 < 3000) {
        Idle();                         /* = PWRSAV #1, returns after any interrupt */
        wakes++;
    }
    Serial.print("# IDLE done, woke "); Serial.print(wakes);
    Serial.println(" times (= 1 per Timer1 tick)");
    Serial.print("# RCON.IDLE = "); Serial.print(RCONbits.IDLE);
    Serial.println(" (0 is right: only a RESET during Idle sets it)");
}

void testSleep(void)
{
    Serial.println("# SLEEP: everything stops. Press USER to wake up...");
    Serial.flush();                     /* let the last byte leave before the UART clock stops */
    digitalWrite(LED_BUILTIN, LED_OFF);
    unsigned long before = millis();
    unsigned long w0 = wakeups;
    while (wakeups == w0) {
        Sleep();                        /* = PWRSAV #0 */
        /* any other wake source (e.g. a CN glitch) just loops back to sleep */
    }
    unsigned long slept = millis() - before;
    delay(50);                          /* debounce the button */
    Serial.print("# woke up! millis() advanced only "); Serial.print(slept);
    Serial.println(" ms while asleep (Timer1 was stopped)");
}

void testWatchdog(void)
{
    Serial.println("# enabling the watchdog and NOT feeding it -> reset in ~1 ms.");
    Serial.println("#   (period is set by config bit RWDTPS = PS1 in system_config.c)");
    Serial.flush();
    WDTCONLbits.ON = 1;                 /* FWDTEN = ON_SW: software may switch it on */
    for (;;) { }                        /* hang - like a bug that locks up the loop */
}

void testSoftwareReset(void)
{
    Serial.println("# executing RESET instruction...");
    Serial.flush();
    asm volatile ("reset");
}

void setup()
{
    bootRcon = RCON;                    /* 1. read the reason FIRST ... */
    RCON = 0;                           /* 2. ... then clear it for the next time */

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_BUILTIN, INPUT);
    Serial.begin(115200);
    delay(10);
    Serial.println();
    Serial.println("Lab 18 - power modes and reset sources.  ? 1 2 3 4 w r");
    printRcon(bootRcon);

    /* USER button (RC12) as Sleep wake-up source: attachInterrupt uses
     * change notification, which works without a clock = wakes from Sleep. */
    attachInterrupt(digitalPinToInterrupt(BUTTON_BUILTIN), onButton, FALLING);

    static unsigned int boots __attribute__((persistent));   /* survives reset, not power-off */
    if (bootRcon & 0x0001) boots = 0;                         /* power-on: RAM content is random */
    boots++;
    Serial.print("# starts since power-on: "); Serial.println(boots);
}

void loop()
{
    digitalWrite(LED_BUILTIN, (millis() / 500) % 2 ? LED_ON : LED_OFF);

    if (Serial.available() > 0) {
        int c = Serial.read();
        switch (c) {
        case '?': printRcon(bootRcon); break;
        case '1': testRun(); break;
        case '2': testDoze(); break;
        case '3': testIdle(); break;
        case '4': testSleep(); break;
        case 'w': testWatchdog(); break;
        case 'r': testSoftwareReset(); break;
        default: break;
        }
    }
}
