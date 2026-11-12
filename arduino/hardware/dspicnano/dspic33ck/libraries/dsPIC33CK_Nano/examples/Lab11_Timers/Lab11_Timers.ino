/*
 * Lab 11 - Advanced timers: every hardware time base of the dsPIC33CK, with interrupts
 * Board: dsPIC33CK Nano      Tools > Clock: 100 MIPS or 4 MIPS (both work)
 *
 * "Timer 0..6" on an AVR Arduino means Timer0/1/2 (UNO) or Timer0..5 (MEGA).
 * The dsPIC33CK256MC005 is built differently. Its time bases are:
 *
 *   #  name      what it is                         used by the core for
 *   0  Timer1    16-bit timer (the only "Timer")    millis() / delay()   -> only READ here
 *   1  SCCP1     16/32-bit timer + capture/compare  analogWrite(D5)
 *   2  SCCP2     16/32-bit timer + capture/compare  analogWrite(D6)
 *   3  SCCP3     16/32-bit timer + capture/compare  analogWrite(D7)
 *   4  SCCP4     16/32-bit timer + capture/compare  analogWrite(D8) and tone()
 *   5  PG4       PWM generator 4, used as a timer   (free)
 *   6  SCCP1 in 32-bit mode                         a long period from one module
 *   There is no Timer2..Timer9 (T2CON) on this family - the SCCPs replace them.
 *
 * This lab runs FOUR independent periodic interrupts at the same time, each
 * toggling its own pin and counting, plus input capture to measure a signal:
 *   SCCP1  1000 Hz  -> toggles D9  = RB4 (J3-15)   -> 500 Hz square wave
 *   SCCP2   100 Hz  -> toggles D10 = RB5 (J3-11)   ->  50 Hz
 *   SCCP3    10 Hz  -> toggles D11 = RB6 (J3-10)   ->   5 Hz  (put an LED here)
 *   PG4       2 Hz  -> toggles the USER LED (RC13) ->   1 Hz blink
 *   Timer1: we only read TMR1 / millis() - it belongs to the core.
 *   SCCP4 is left alone: tone() and the core own its interrupt vector.
 *   (D9..D11 are free pins; RB5/RB6 are also the ICSP PGD/PGC lines - fine
 *    while the program runs, unplug the PICkit if it loads them.)
 *
 * Measuring: once a second the sketch counts how many interrupts each timer
 * produced, using Timer1 (millis) as the reference, and prints the real rate.
 * No jumper is needed. Send 'w' to switch SCCP1 to 32-bit mode for a very long
 * period (0.5 Hz) from a single module - the "Timer 6" row of the table.
 *
 * Wiring (optional):
 *   LED + 1k from D11 = RB6 (J3-10) to GND -> blinks at 5 Hz
 *   piezo from D9 = RB4 (J3-15) to GND     -> 500 Hz tone made by a timer ISR
 *   scope / logic analyser on J3-15, J3-11, J3-10
 *
 * Serial commands (115200):
 *   1..3 <Hz>  change SCCP1/2/3 rate, e.g. "1 2000" = SCCP1 at 2 kHz
 *   p          pause / resume all timer interrupts      ?  status
 *   j          jitter test: worst-case lateness of the SCCP1 ISR
 *   w          SCCP1 in 32-bit mode, 0.5 Hz (no prescaler needed), again = back to 16 bit
 *
 * ----------------------------------------------------------------------------
 * HOW AN SCCP WORKS AS A TIMER (registers from p33CK256MC005.h)
 *   CCPxCON1L  CCPON (bit 15), MOD = 0000 (timer), T32 (32-bit), CLKSEL = 000
 *              (FOSC/2 = FCY), TMRPS prescaler 1/4/16/64
 *   CCPxPRL    period: interrupt every (PRL + 1) clocks
 *   CCPxTMRL   the running count
 *   interrupt  _CCTxInterrupt  (CCTxIF = timer period match)  - NOT _CCPxInterrupt
 *   period     = FCY / (prescaler * Hz) - 1 ; the code picks the smallest
 *                prescaler that fits 16 bits -> the best resolution
 *   Priorities: SCCP1 = 5 (highest here), SCCP2 = 4, SCCP3 = 3, PG4 = 2.
 *   A higher-priority ISR interrupts a lower one ("nesting") - the jitter test
 *   shows how late SCCP1 can be at most.
 * PG4 AS A TIMER
 *   PWM generators count PG4PER clocks and can interrupt at the end of every
 *   cycle (PG4EVTH.IEVTSEL = 00). The output pins stay disabled (PENH/PENL = 0),
 *   so only the time base is used. Clock: PCLKCON = FCY (as in the PWM lab).
 *   The longest PG4 period is 65535 / FCY, so for 2 Hz we count 1 ms periods
 *   in the ISR (a "software postscaler").
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>

const uint8_t PIN_T1 = 9;     /* RB4 */
const uint8_t PIN_T2 = 10;    /* RB5 */
const uint8_t PIN_T3 = 11;    /* RB6 */

volatile unsigned long cnt[4];             /* SCCP1, SCCP2, SCCP3, PG4 */
volatile uint16_t pg4Div = 0;
uint16_t pg4Every = 500;                   /* 1 ms ticks per PG4 event -> 2 Hz */
unsigned long rateHz[3] = { 1000, 100, 10 };
volatile uint16_t worstLate = 0;           /* SCCP1 ISR entry: highest TMR value seen */

static volatile uint16_t *latB = &LATB;

/* ---------------- interrupt service routines ---------------- */
extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT1Interrupt(void)
{
    uint16_t late = CCP1TMRL;              /* counts since the period match = latency */
    if (late > worstLate && late < 0x8000) worstLate = late;
    IFS0bits.CCT1IF = 0;
    *latB ^= (1u << 4);                    /* RB4 */
    cnt[0]++;
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT2Interrupt(void)
{
    IFS1bits.CCT2IF = 0;
    *latB ^= (1u << 5);                    /* RB5 */
    cnt[1]++;
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT3Interrupt(void)
{
    IFS2bits.CCT3IF = 0;
    *latB ^= (1u << 6);                    /* RB6 */
    cnt[2]++;
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _PWM4Interrupt(void)
{
    IFS4bits.PWM4IF = 0;
    if (++pg4Div >= pg4Every) {            /* software postscaler */
        pg4Div = 0;
        LATCbits.LATC13 ^= 1;              /* USER LED */
        cnt[3]++;
    }
}

/* ---------------- SCCP timer setup ---------------- */
struct Sccp {
    volatile uint16_t *con1l, *con1h, *con2l, *con2h, *con3h, *tmrl, *tmrh, *prl, *prh;
};
Sccp SCCP[3] = {                         /* RAM, not const: a const table of SFR pointers goes to flash (PSV) */
    { &CCP1CON1L, &CCP1CON1H, &CCP1CON2L, &CCP1CON2H, &CCP1CON3H, &CCP1TMRL, &CCP1TMRH, &CCP1PRL, &CCP1PRH },
    { &CCP2CON1L, &CCP2CON1H, &CCP2CON2L, &CCP2CON2H, &CCP2CON3H, &CCP2TMRL, &CCP2TMRH, &CCP2PRL, &CCP2PRH },
    { &CCP3CON1L, &CCP3CON1H, &CCP3CON2L, &CCP3CON2H, &CCP3CON3H, &CCP3TMRL, &CCP3TMRH, &CCP3PRL, &CCP3PRH },
};

/* returns the real frequency that the chosen prescaler/period gives */
double sccpStart(int i, unsigned long hz)
{
    static const uint16_t PS[4] = { 1, 4, 16, 64 };
    if (hz < 1) hz = 1;
    const Sccp &c = SCCP[i];
    *c.con1l = 0;                          /* off while configuring */
    *c.con1h = 0; *c.con2l = 0; *c.con2h = 0; *c.con3h = 0;
    *c.tmrl = 0; *c.tmrh = 0;

    uint8_t ps = 0;
    unsigned long per = FCY / hz;
    while (per > 65536UL && ps < 3) { ps++; per = FCY / (PS[ps] * hz); }
    if (per > 65536UL) {
        /* even 1:64 does not fit 16 bits (e.g. 10 Hz at 100 MIPS = 156 250 counts):
         * use the SCCP as ONE 32-bit timer, PRH:PRL, prescaler 1:1 */
        per = FCY / hz;
        *c.prl = (uint16_t)((per - 1) & 0xFFFF);
        *c.prh = (uint16_t)((per - 1) >> 16);
        *c.con1l = 0x0020;                 /* T32 = 1, MOD = timer, CLKSEL = FCY */
        *c.con1l |= 0x8000;
        return (double)FCY / (double)per;
    }
    if (per < 2) per = 2;
    /* 16-bit mode. PRH is written 0xFFFF, as the core does for its SCCP timers:
     * with PRH = 0 the MPLAB simulator's SCCP model runs away and stalls the CPU.
     * In 16-bit mode PRH is not used for the period, so 0xFFFF is harmless. */
    *c.prl = (uint16_t)(per - 1); *c.prh = 0xFFFF;
    *c.con1l = (uint16_t)(ps << 6);        /* MOD = 0000 timer, CLKSEL = FCY, TMRPS */
    *c.con1l |= 0x8000;                    /* CCPON */
    return (double)FCY / ((double)PS[ps] * (double)per);
}

void timersBegin(void)
{
    PMD2bits.CCP1MD = 0; PMD2bits.CCP2MD = 0; PMD2bits.CCP3MD = 0;
    const uint8_t pins[3] = { PIN_T1, PIN_T2, PIN_T3 };
    for (int k = 0; k < 3; k++) { pinMode(pins[k], OUTPUT); digitalWrite(pins[k], LOW); }
    pinMode(LED_BUILTIN, OUTPUT);

    IPC1bits.CCT1IP = 5;  IFS0bits.CCT1IF = 0;  IEC0bits.CCT1IE = 1;
    IPC6bits.CCT2IP = 4;  IFS1bits.CCT2IF = 0;  IEC1bits.CCT2IE = 1;
    IPC9bits.CCT3IP = 3;  IFS2bits.CCT3IF = 0;  IEC2bits.CCT3IE = 1;
    for (int i = 0; i < 3; i++) sccpStart(i, rateHz[i]);

    /* PG4: 1 ms period, end-of-cycle interrupt, outputs not connected */
    PMD1bits.PWMMD = 0;
    PCLKCON = 0;                           /* PWM master clock = FCY */
    PG4CONL = 0;
    PG4CONL = (1u << 3);                   /* CLKSEL = master clock, edge-aligned */
    PG4CONH = 0;
    PG4IOCONH = 0;                         /* PENH = PENL = 0: pins stay GPIO */
    PG4IOCONL = 0;
    PG4PER = (uint16_t)(FCY / 1000UL - 1); /* 1 kHz: 99999 does not fit at 100 MIPS -> see below */
#if FCY > 65535000UL
    PG4PER = (uint16_t)(FCY / 2000UL - 1); /* 0.5 ms at 100 MIPS (49999 fits) */
    pg4Every = 1000;                       /* 1000 x 0.5 ms = 0.5 s -> 1 Hz blink */
#endif
    PG4DC = 0;
    PG4EVTL = 0;
    PG4EVTH = 0;                           /* IEVTSEL = 00: interrupt at end of cycle */
    IPC17bits.PWM4IP = 2; IFS4bits.PWM4IF = 0; IEC4bits.PWM4IE = 1;
    PG4CONL |= 0x8000;                     /* ON */
}

/* SCCP1 as ONE 32-bit timer: CCP1PRH:CCP1PRL = period, no prescaler needed */
void sccp1Start32(double hz)
{
    unsigned long per = (unsigned long)((double)FCY / hz);
    CCP1CON1L = 0;
    CCP1TMRL = 0; CCP1TMRH = 0;
    CCP1PRL = (uint16_t)((per - 1) & 0xFFFF);
    CCP1PRH = (uint16_t)((per - 1) >> 16);
    CCP1CON1L = 0x0020;                    /* T32 = 1, MOD = timer, FCY, 1:1 */
    CCP1CON1L |= 0x8000;
}

void pauseAll(bool pause)
{
    IEC0bits.CCT1IE = !pause; IEC1bits.CCT2IE = !pause;
    IEC2bits.CCT3IE = !pause; IEC4bits.PWM4IE = !pause;
}

/* ---------------- report ---------------- */
void status(unsigned long dtMs, const unsigned long *d)
{
    static const char *const N[4] = { "SCCP1", "SCCP2", "SCCP3", "PG4  " };
    Serial.print("# over "); Serial.print(dtMs); Serial.println(" ms (measured with Timer1 / millis):");
    for (int i = 0; i < 4; i++) {
        double hz = d[i] * 1000.0 / (double)dtMs;
        Serial.print("#   "); Serial.print(N[i]);
        Serial.print("  interrupts/s = "); Serial.print(hz, 2);
        if (i < 3) { Serial.print("   set "); Serial.print(rateHz[i]); Serial.print(" Hz, pin toggles at "); Serial.print(hz / 2, 2); Serial.print(" Hz"); }
        else Serial.print("   (USER LED, software postscaler)");
        Serial.println();
    }
    Serial.print("#   Timer1 TMR1 now = "); Serial.print(TMR1); Serial.print(" / PR1 = "); Serial.println(PR1);
}

long readNumber(void)
{
    long v = 0; unsigned long t0 = millis();
    while (millis() - t0 < 60) {
        if (Serial.available() > 0) {
            int c = Serial.peek();
            if (c == ' ') { Serial.read(); continue; }
            if (c < '0' || c > '9') break;
            v = v * 10 + (Serial.read() - '0'); t0 = millis();
        }
    }
    return v;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Lab 11 - advanced timers: SCCP1/2/3 + PG4 interrupts. 1..3 <Hz>, p, j, ?");
    timersBegin();
    for (int i = 0; i < 3; i++) {
        double real = sccpStart(i, rateHz[i]);
        Serial.print("# SCCP"); Serial.print(i + 1); Serial.print(" set "); Serial.print(rateHz[i]);
        Serial.print(" Hz -> exact "); Serial.print(real, 3); Serial.println(" Hz");
    }
}

void loop()
{
    static unsigned long last = 0, prev[4];
    static bool paused = false;
    if (millis() - last >= 1000) {
        unsigned long now = millis(), d[4];
        noInterrupts();
        for (int i = 0; i < 4; i++) { d[i] = cnt[i] - prev[i]; prev[i] = cnt[i]; }
        interrupts();
        status(now - last, d);
        last = now;
    }

    if (Serial.available() > 0) {
        int c = Serial.read();
        if (c >= '1' && c <= '3') {
            long hz = readNumber();
            if (hz >= 1 && hz <= 100000) {
                rateHz[c - '1'] = (unsigned long)hz;
                double real = sccpStart(c - '1', (unsigned long)hz);
                Serial.print("# SCCP"); Serial.print((char)c); Serial.print(" -> "); Serial.print(real, 3); Serial.println(" Hz");
            }
        } else if (c == 'p') {
            paused = !paused; pauseAll(paused);
            Serial.println(paused ? "# timer interrupts PAUSED" : "# timer interrupts running");
        } else if (c == 'j') {
            noInterrupts(); worstLate = 0; interrupts();
            delay(1000);
            uint16_t w = worstLate;
            Serial.print("# SCCP1 worst ISR latency over 1 s: "); Serial.print(w);
            static const uint16_t PS[4] = { 1, 4, 16, 64 };
            double us = (double)w * PS[CCP1CON1Lbits.TMRPS] * 1e6 / (double)FCY;
            Serial.print(" timer counts = "); Serial.print(us, 2);
            Serial.println(" us. Lower-priority ISRs cannot delay it; only DISI / noInterrupts() sections can.");
        } else if (c == 'w') {
            static bool wide = false;
            wide = !wide;
            if (wide) { sccp1Start32(0.5); Serial.println("# SCCP1 32-bit: 2 s period from one module (at 100 MIPS that is 200 000 000 counts)"); }
            else      { sccpStart(0, rateHz[0]); Serial.println("# SCCP1 back to 16-bit mode"); }
        } else if (c == '?') {
            Serial.println("# timer map: Timer1=millis, SCCP1-3=this lab, SCCP4=tone(), PG1-3=motor PWM, PG4=this lab");
        }
    }
}
