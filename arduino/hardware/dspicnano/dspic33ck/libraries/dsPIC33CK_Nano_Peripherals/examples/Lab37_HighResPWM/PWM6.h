/*
 * PWM6.h - six independent PWM outputs at the finest resolution the
 *          dsPIC33CK256MC005 offers
 *
 *   pwm6Begin(10000);                 // 10 kHz on all six outputs
 *   pwm6Percent(0, 25.0);             // channel 1 = 25.00 %
 *   pwm6Pulse(3, startTicks, widthTicks); // any edge position, in PWM ticks
 *   pwm6Ns(2, 1234.5);                // pulse width in nanoseconds
 *   pwm6Dither(0, 37);                // add 37/16 tick on average (sub-tick)
 *
 * ----------------------------------------------------------------------------
 * "HIGH RESOLUTION" ON THIS CHIP - read this first
 *   The dsPIC33CK *MP* parts have a true high-resolution PWM: 250 ps edges
 *   (the HRPWM library in this core is for them). This board's *MC005* does
 *   not have that hardware. What it does have:
 *     1. a fast PWM clock: the generators can count at FVCO / 2 (400 MHz
 *        nominal here) instead of FCY (100 MHz): 2.5 ns steps instead of 10 ns,
 *        so 4x the resolution of a plain PWM at the same frequency;
 *     2. dithering (pwm6Dither): change the duty by one tick on some cycles, so
 *        the AVERAGE duty moves in 1/16-tick steps (156 ps). That is what an
 *        RC filter, a motor winding or an LED (by eye) sees.
 *   Resolution in bits = log2(period in ticks):
 *       6.1 kHz -> 16.0 bits    10 kHz -> 15.3    50 kHz -> 13.0    200 kHz -> 11.0
 *   Below 6.1 kHz the 16-bit period register is full; the clock divider
 *   (1:2 .. 1:16) is used and the resolution stays between 15 and 16 bits
 *   (e.g. 2 kHz: 1:4, 50000 ticks of 10 ns = 15.6 bits).
 *
 * SIX OUTPUTS FROM THREE GENERATORS
 *   Each generator PG1..PG3 runs in "independent edge, dual output" mode
 *   (PGxCONL.MODSEL = 010, PGxIOCONH.PMOD = 01 independent):
 *     PWMxH  rises when the counter = PGxPHASE, falls at PGxDC
 *     PWMxL  rises when the counter = PGxTRIGA, falls at PGxTRIGB
 *   so every output has its own start (phase) and its own width.
 *   PG2 and PG3 start each period together with PG1 (SOCS = 1): all six are
 *   locked to one time base.
 *
 *   channel  output  pin   header      channel  output  pin   header
 *     1      PWM1H   RB14  J2-8          2      PWM1L   RB15  J2-9
 *     3      PWM2H   RB12  J2-6          4      PWM2L   RB13  J2-7
 *     5      PWM3H   RB10  J2-4          6      PWM3L   RB11  J2-5
 *   (fixed PWM pins, no PPS; RB10-RB12 are also Serial2 in Labs 21/24/29)
 *
 * 0 % and 100 % cannot be made by edges (rise and fall at the same count),
 * so they use the output OVERRIDE (PGxIOCONL.OVRENH/L, OVRDAT): clean low/high.
 *
 * NOT VERIFIED: the MPLAB simulator does not model the PWM generators and the
 * board is not built yet. Two things to check on the first board:
 *   - the real PWM clock: pwm6Measure() times PWM1H with SCCP2 and
 *     pwm6Calibrate() corrects every ns conversion from it;
 *   - a pulse that wraps past the end of the period (start + width > period)
 *     relies on the set/clear compare logic carrying the output across the
 *     period boundary. Look at it with a scope before you rely on it.
 * ----------------------------------------------------------------------------
 */
#ifndef LAB37_PWM6_H
#define LAB37_PWM6_H

#include <Arduino.h>

#define PWM6_CH 6
const double PWM6_CLK_NOMINAL = 400000000.0;    /* FVCO / 2 */

/* register set of one generator. Kept in RAM: a const table of SFR pointers
 * would land in flash (PSV) on this compiler. */
struct Pwm6Gen {
    volatile uint16_t *CONL, *CONH, *STAT, *IOCONL, *IOCONH, *EVTL, *EVTH;
    volatile uint16_t *PER, *DC, *PHASE, *TRIGA, *TRIGB;
};
Pwm6Gen pwm6Gen[3] = {
    { &PG1CONL, &PG1CONH, &PG1STAT, &PG1IOCONL, &PG1IOCONH, &PG1EVTL, &PG1EVTH, &PG1PER, &PG1DC, &PG1PHASE, &PG1TRIGA, &PG1TRIGB },
    { &PG2CONL, &PG2CONH, &PG2STAT, &PG2IOCONL, &PG2IOCONH, &PG2EVTL, &PG2EVTH, &PG2PER, &PG2DC, &PG2PHASE, &PG2TRIGA, &PG2TRIGB },
    { &PG3CONL, &PG3CONH, &PG3STAT, &PG3IOCONL, &PG3IOCONH, &PG3EVTL, &PG3EVTH, &PG3PER, &PG3DC, &PG3PHASE, &PG3TRIGA, &PG3TRIGB },
};
const char *const PWM6_PIN[PWM6_CH] = { "PWM1H RB14 J2-8", "PWM1L RB15 J2-9", "PWM2H RB12 J2-6",
                                        "PWM2L RB13 J2-7", "PWM3H RB10 J2-4", "PWM3L RB11 J2-5" };

#define CONL_ON         0x8000u
#define CONL_CLK_MCLK   (1u << 3)           /* CLKSEL = 01: master clock */
#define CONL_CLK_DIV    (2u << 3)           /* CLKSEL = 10: master clock / divider */
#define CONL_IND_DUAL   0x0002u             /* MODSEL = 010: independent edge, dual output */
#define CONH_SOC_PG1    0x0001u             /* SOCS = 1: start with PG1 */
#define IOCONH_PENH     0x0008u
#define IOCONH_PENL     0x0004u
#define IOCONH_PMOD_IND 0x0010u             /* PMOD = 01: H and L independent */
#define IOCONL_OVRENH   0x2000u
#define IOCONL_OVRENL   0x1000u
#define IOCONL_OVRDATH  0x0800u             /* OVRDAT<1>: level of H while overridden */
#define IOCONL_OVRDATL  0x0400u             /* OVRDAT<0>: level of L */
#define STAT_UPDREQ     0x0008u

/* ---------------- state ---------------- */
double   pwm6ClkHz = PWM6_CLK_NOMINAL;      /* ticks per second (after the divider) */
uint16_t pwm6Per = 0;                       /* PGxPER: period = PER + 1 ticks */
uint8_t  pwm6DivShift = 0;                  /* 0 = no divider, 1..4 = 1:2 .. 1:16 */
uint16_t pwm6Start[PWM6_CH], pwm6Width[PWM6_CH];
volatile uint8_t  pwm6DitherCh = 0xFF;      /* channel being dithered, 0xFF = none */
volatile uint16_t pwm6DitherBase = 0, pwm6DitherFrac = 0, pwm6DitherAcc = 0;
bool pwm6Running = false;                   /* false = all outputs forced low */

inline uint32_t pwm6Ticks() { return (uint32_t)pwm6Per + 1; }      /* ticks per period */
inline double pwm6Hz()      { return pwm6ClkHz / pwm6Ticks(); }
inline double pwm6TickNs()  { return 1e9 / pwm6ClkHz; }
double pwm6Bits()           { return log((double)pwm6Ticks()) / log(2.0); }

/* ---------------- edges of one channel ---------------- */
void pwm6Write(uint8_t ch)
{
    Pwm6Gen &g = pwm6Gen[ch >> 1];
    bool L = ch & 1;
    uint32_t T = pwm6Ticks();
    uint16_t w = pwm6Width[ch], s = pwm6Start[ch] % T;
    uint16_t ovrEn = L ? IOCONL_OVRENL : IOCONL_OVRENH;
    uint16_t ovrHi = L ? IOCONL_OVRDATL : IOCONL_OVRDATH;
    uint16_t io = *g.IOCONL;
    if (w == 0 || !pwm6Running) io = (io | ovrEn) & ~ovrHi;     /* 0 % or stopped: hold low */
    else if (w >= T)       io = io | ovrEn | ovrHi;             /* 100 %: hold high */
    else {
        uint16_t e = (uint16_t)((s + (uint32_t)w) % T);         /* falling edge, may wrap */
        if (L) { *g.TRIGA = s; *g.TRIGB = e; }
        else   { *g.PHASE = s; *g.DC = e; }
        io &= ~ovrEn;
    }
    *g.IOCONL = io;
    *g.STAT |= STAT_UPDREQ;                 /* load the new edges at the next period start */
}

/* start and width in PWM ticks */
void pwm6Pulse(uint8_t ch, uint16_t start, uint16_t width)
{
    if (ch >= PWM6_CH) return;
    if (ch == pwm6DitherCh) pwm6DitherCh = 0xFF;    /* a new setting ends dithering */
    pwm6Start[ch] = start; pwm6Width[ch] = width;
    pwm6Write(ch);
}

/* duty in percent (0.0 .. 100.0), keeps the channel's phase */
void pwm6Percent(uint8_t ch, double percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    pwm6Pulse(ch, pwm6Start[ch], (uint16_t)(percent / 100.0 * pwm6Ticks() + 0.5));
}

/* pulse width in nanoseconds (rounded to whole ticks) */
void pwm6Ns(uint8_t ch, double ns)
{
    double t = ns / pwm6TickNs() + 0.5;
    if (t < 0) t = 0;
    if (t > pwm6Ticks()) t = pwm6Ticks();
    pwm6Pulse(ch, pwm6Start[ch], (uint16_t)t);
}

/* phase = where the pulse starts, in degrees of the period */
void pwm6Phase(uint8_t ch, double deg)
{
    while (deg < 0) deg += 360;
    while (deg >= 360) deg -= 360;
    if (ch >= PWM6_CH) return;
    pwm6Start[ch] = (uint16_t)(deg / 360.0 * pwm6Ticks());
    pwm6Write(ch);
}

/* Dithering: width = whole + frac16/16 ticks on average. The PG1 period
 * interrupt adds frac16/16 per period and uses whole+1 when the sum overflows. */
void pwm6Dither(uint8_t ch, uint32_t widthSixteenths)
{
    if (ch >= PWM6_CH) return;
    noInterrupts();
    pwm6DitherCh = 0xFF;
    pwm6Width[ch] = (uint16_t)(widthSixteenths >> 4);
    pwm6DitherBase = pwm6Width[ch];
    pwm6DitherFrac = widthSixteenths & 15;
    pwm6DitherAcc = 0;
    pwm6Write(ch);
    if (pwm6DitherFrac) pwm6DitherCh = ch;
    _PWM1IF = 0;
    _PWM1IE = (pwm6DitherCh != 0xFF);       /* the period interrupt runs only while dithering */
    interrupts();
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _PWM1Interrupt(void)
{
    uint8_t ch = pwm6DitherCh;
    if (ch < PWM6_CH) {
        pwm6DitherAcc += pwm6DitherFrac;
        uint16_t w = pwm6DitherBase;
        if (pwm6DitherAcc >= 16) { pwm6DitherAcc -= 16; w++; }
        Pwm6Gen &g = pwm6Gen[ch >> 1];
        uint16_t e = (uint16_t)((pwm6Start[ch] + (uint32_t)w) % pwm6Ticks());
        if (ch & 1) *g.TRIGB = e; else *g.DC = e;
        *g.STAT |= STAT_UPDREQ;
    }
    _PWM1IF = 0;
}

/* ---------------- all outputs on / forced low ---------------- */
void pwm6Outputs(bool on)
{
    pwm6Running = on;
    for (uint8_t i = 0; i < 3; i++) {
        if (on) { *pwm6Gen[i].IOCONL &= ~(IOCONL_OVRENH | IOCONL_OVRENL); }
        else    { *pwm6Gen[i].IOCONL = (*pwm6Gen[i].IOCONL & ~(IOCONL_OVRDATH | IOCONL_OVRDATL)) | IOCONL_OVRENH | IOCONL_OVRENL; }
    }
    if (on) for (uint8_t ch = 0; ch < PWM6_CH; ch++) pwm6Write(ch);    /* re-apply 0/100 % overrides */
}

/* ---------------- set the frequency (all six) ---------------- */
/* Keeps every channel's duty and phase as a FRACTION of the period. */
void pwm6Frequency(double hz)
{
    double undivided = pwm6ClkHz * (1 << pwm6DivShift);    /* master clock (calibrated) */
    double ticksF = undivided / hz;                         /* undivided ticks per period */
    uint8_t shift = 0;
    while (ticksF / (1 << shift) > 65536.0 && shift < 4) shift++;
    double ticks = ticksF / (1 << shift);
    if (ticks > 65536.0) ticks = 65536.0;
    if (ticks < 16) ticks = 16;

    double fr[PWM6_CH], ph[PWM6_CH];
    uint32_t oldT = pwm6Per ? pwm6Ticks() : 1;
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) { fr[ch] = (double)pwm6Width[ch] / oldT; ph[ch] = (double)pwm6Start[ch] / oldT; }

    pwm6DivShift = shift;
    pwm6ClkHz = undivided / (1 << shift);
    pwm6Per = (uint16_t)(ticks + 0.5 - 1);

    for (uint8_t i = 0; i < 3; i++) *pwm6Gen[i].CONL &= ~CONL_ON;  /* clock change: generators off */
    PCLKCON = (PCLKCON & ~0x0030u) | ((shift ? shift - 1 : 0) << 4);  /* DIVSEL: 1:2 .. 1:16 */
    for (uint8_t i = 0; i < 3; i++) {
        *pwm6Gen[i].CONL = (shift ? CONL_CLK_DIV : CONL_CLK_MCLK) | CONL_IND_DUAL;
        *pwm6Gen[i].PER = pwm6Per;
    }
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) {
        pwm6Start[ch] = (uint16_t)(ph[ch] * pwm6Ticks());
        pwm6Width[ch] = (uint16_t)(fr[ch] * pwm6Ticks() + 0.5);
        pwm6Write(ch);
    }
    pwm6DitherCh = 0xFF; _PWM1IE = 0;
    /* clients first, then the master, so they wait for PG1's start of cycle */
    *pwm6Gen[2].CONL |= CONL_ON;
    *pwm6Gen[1].CONL |= CONL_ON;
    *pwm6Gen[0].CONL |= CONL_ON;
}

void pwm6Begin(double hz)
{
    _PWMMD = 0;                             /* power the PWM module (PMD1) */
    PCLKCON = 0x0001;                       /* MCLKSEL = 01: FVCO / 2, DIVSEL 1:2 (unused) */
    for (uint8_t i = 0; i < 3; i++) {
        Pwm6Gen &g = pwm6Gen[i];
        *g.CONL = 0;
        *g.CONH = (i == 0) ? 0x0000u : CONH_SOC_PG1;        /* UPDMOD 000: at start of cycle */
        *g.IOCONL = IOCONL_OVRENH | IOCONL_OVRENL;          /* start forced low */
        *g.IOCONH = IOCONH_PMOD_IND | IOCONH_PENH | IOCONH_PENL;
        *g.EVTL = 0;
        *g.EVTH = (i == 0) ? 0x0000u : 0x0300u;             /* PG1: interrupt at end of cycle */
        *g.PHASE = *g.DC = *g.TRIGA = *g.TRIGB = 0;
    }
    for (uint8_t ch = 0; ch < PWM6_CH; ch++) { pwm6Start[ch] = 0; pwm6Width[ch] = 0; }
    pwm6ClkHz = PWM6_CLK_NOMINAL; pwm6DivShift = 0; pwm6Per = 0;
    pwm6Frequency(hz);
    _PWM1IP = 4; _PWM1IF = 0; _PWM1IE = 0;
    pwm6Outputs(true);
}

/* ---------------- measure PWM1H (channel 1) with SCCP2 ---------------- */
/* Returns Hz, 0 if no edges (channel 1 at 0 % or 100 %). */
double pwm6Measure()
{
    __builtin_write_RPCON(0x0000);
    _ICM2R = 46;                            /* SCCP2 capture input <- RP46 = RB14 */
    __builtin_write_RPCON(0x0800);
    _CCP2MD = 0;
    CCP2CON1L = 0; CCP2CON1H = 0; CCP2CON2L = 0; CCP2CON2H = 0; CCP2CON3H = 0;
    CCP2CON1L = 0x0031u;                    /* capture every rising edge, 32-bit, FCY */
    CCP2CON1Lbits.CCPON = 1;
    uint32_t t[17]; uint8_t n = 0;
    unsigned long t0 = millis();
    while (n < 17 && millis() - t0 < 100) {
        if (CCP2STATLbits.ICBNE) { uint16_t lo = CCP2BUFL; uint16_t hi = CCP2BUFH; t[n++] = ((uint32_t)hi << 16) | lo; }
    }
    CCP2CON1Lbits.CCPON = 0;
    if (n < 17) return 0;
    uint32_t span = t[16] - t[0];           /* 16 periods */
    return span ? 16.0 * FCY / span : 0;
}

/* Correct the tick length from a measurement. Call with channel 1 running. */
bool pwm6Calibrate()
{
    double f = pwm6Measure();
    if (f <= 0) return false;
    pwm6ClkHz = f * pwm6Ticks();            /* real ticks per second */
    return true;
}

#endif
