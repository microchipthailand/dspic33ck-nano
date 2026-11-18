/*
 * ADC.h - the complete 12-bit ADC of the dsPIC33CK256MC005, as plain functions
 *
 *   adcBegin();                          // power up, 12 bit, integer, all channels idle
 *   int v  = adcRead(0);                 // one conversion of AN0, 0..4095
 *   int mv = adcReadMv(0);               // the same in millivolts
 *   adcScan(ADC_ALL_PINS, buf);          // every channel at once (common software trigger)
 *   adcResolution(10);                   // 6, 8, 10 or 12 bit
 *   adcTrigger(0, ADC_TRG_SCCP3);        // AN0 converts on every SCCP3 timer event
 *   adcAttachInterrupt(0, myFunction);   // myFunction(ch, value) on every result
 *   adcCompare(0, 0, 1000, 3000, ADC_CMP_OUTSIDE);  // hardware window check on AN0
 *   adcFilter(0, 0, ADC_FLT_OVERSAMPLE, 16);        // 16x oversampling -> 14-bit result
 *   adcMeasureVdd();                     // the real supply, from the 1.2 V band gap
 *
 * ----------------------------------------------------------------------------
 * THE ADC OF THIS CHIP
 *   One 12-bit successive-approximation core (the "shared core"; the MC005
 *   has no dedicated cores) behind a multiplexer with 22 inputs:
 *     AN0-AN19  on pins (table below)     AN20 temperature sensor    AN21 band gap 1.2 V
 *   Each channel has its OWN result register (ADCBUF0-21), ready bit
 *   (ADSTATL/H), trigger source (ADTRIGx, 5 bits per channel), output format
 *   (ADMODx: signed / differential) and interrupt enable (ADIEL/H). Triggers
 *   that arrive together are converted one after another, lowest channel first.
 *   Extra hardware: 4 digital comparators (ADCMP0-3) that check results
 *   against limits without the CPU, and 4 oversampling / averaging filters
 *   (ADFL0-3) that add up to 4 bits of resolution.
 *
 *   ch  pin   Arduino  header   |  ch  pin   Arduino  header
 *    0  RA0   A0/D0    J2-15    |  11  RB9   A12/D14  J3-7  (SDA)
 *    1  RB2   A7/D7    J3-17    |  12  RC0   A13/D21  J2-14
 *    2  RB7   A10/D12  J3-9     |  13  RC1   A14/D22  J2-20
 *    3  RA3   A3/D3    J2-18    |  14  RC2   A15/D23  J2-21
 *    4  RA4   A4/D4    J2-19    |  15  RC3   A16/D24  J3-22
 *    5  RB0   A5/D5    J3-21    |  16  RA1   A1/D1    J2-16
 *    6  RB1   A6/D6    J3-20    |  17  RB4   A9/D9    J3-15
 *    7  RC7   A18/D28  J3-18    |  18  RD10  A19/D37  J3-19
 *    8  RB3   A8/D8    J3-16    |  19  RC6   A17/D27  J2-22
 *    9  RA2   A2/D2    J2-17    |  20  temperature sensor (internal)
 *   10  RB8   A11/D13  J3-8 (SCL)|  21  band gap 1.2 V (internal)
 *   ANN0 = RD13 (J2-13) is the negative input for differential mode.
 *
 * TIMING (set by adcBegin)
 *   ADC clock = FOSC / CLKDIV (50 MHz at 100 MIPS, 8 MHz at 4 MIPS),
 *   TAD = 2 clocks (40 ns / 250 ns), sample time SHRSAMC + 2 TAD,
 *   12-bit conversion ~ sample + 14 TAD: about 1.3 us at 100 MIPS.
 *
 * analogRead() of the core uses the same ADC, but returns 10 bits (0-1023)
 * like a classic Arduino. These functions give the full 12 bits.
 * This file uses the vectors _ADCInterrupt, _ADCMP0-3Interrupt and
 * _CCT3Interrupt, and the timer SCCP3: do not combine it with NanoTimer's
 * Timer3, Lab 13 (SoftwareSerial) or analogWrite on D7.
 * ----------------------------------------------------------------------------
 */
#ifndef LAB38_ADC_H
#define LAB38_ADC_H

#include <Arduino.h>

#define ADC_CHANNELS        22
#define ADC_CH_TEMP         20
#define ADC_CH_BANDGAP      21
#define ADC_ALL_PINS        0x000FFFFFUL      /* AN0..AN19 */
#define ADC_ALL             0x003FFFFFUL      /* AN0..AN21 */

/* trigger sources (ADTRIGx.TRGSRCx) */
enum {
    ADC_TRG_NONE = 0, ADC_TRG_SW_COMMON = 1, ADC_TRG_SW_LEVEL = 2,
    ADC_TRG_PWM1_1 = 4, ADC_TRG_PWM1_2 = 5, ADC_TRG_PWM2_1 = 6, ADC_TRG_PWM2_2 = 7,
    ADC_TRG_PWM3_1 = 8, ADC_TRG_PWM3_2 = 9, ADC_TRG_PWM4_1 = 10, ADC_TRG_PWM4_2 = 11,
    ADC_TRG_SCCP1_TRIG = 12, ADC_TRG_SCCP2_TRIG = 13, ADC_TRG_SCCP3_TRIG = 14, ADC_TRG_SCCP4_TRIG = 15,
    ADC_TRG_CLC3 = 17, ADC_TRG_CLC4 = 18,
    ADC_TRG_SCCP1 = 20, ADC_TRG_SCCP2 = 21, ADC_TRG_SCCP3 = 22, ADC_TRG_SCCP4 = 23,
    ADC_TRG_CLC1 = 28, ADC_TRG_CLC2 = 29, ADC_TRG_PTG = 30, ADC_TRG_ADCTRG31 = 31
};

const char *const ADC_PIN_NAME[ADC_CHANNELS] = {
    "RA0  J2-15", "RB2  J3-17", "RB7  J3-9 ", "RA3  J2-18", "RA4  J2-19", "RB0  J3-21",
    "RB1  J3-20", "RC7  J3-18", "RB3  J3-16", "RA2  J2-17", "RB8  J3-8 ", "RB9  J3-7 ",
    "RC0  J2-14", "RC1  J2-20", "RC2  J2-21", "RC3  J3-22", "RA1  J2-16", "RB4  J3-15",
    "RD10 J3-19", "RC6  J2-22", "temperature", "band gap  " };

/* ---------------- state ---------------- */
uint8_t  adcBits = 12;                      /* current resolution */
uint16_t adcAvddMv = 3300;                  /* reference = AVDD; adcMeasureVdd() updates it */
bool     adcTimedOut = false;

static volatile uint16_t *const adcBuf = &ADCBUF0;          /* ADCBUF0..21, one per channel */
inline volatile uint8_t *adcTrgReg(uint8_t ch) { return (volatile uint8_t *)&ADTRIG0L + ch; }

inline uint16_t adcMaxCode() { return (uint16_t)((1U << adcBits) - 1); }
inline bool adcReady(uint8_t ch) { return ch < 16 ? (ADSTATL >> ch) & 1 : (ADSTATH >> (ch - 16)) & 1; }
inline uint16_t adcResult(uint8_t ch) { return adcBuf[ch]; }      /* reading clears the ready bit */
inline int32_t adcToMv(int32_t raw) { return (raw * adcAvddMv + (1L << (adcBits - 1))) >> adcBits; }

/* Arduino pin of a channel, -1 for the internal channels */
int8_t adcPinOf(uint8_t ch)
{
    for (uint8_t p = 0; p < NUM_DIGITAL_PINS; p++) if (g_pin_map[p].adc_channel == ch) return p;
    return -1;
}

/* make the channel's pin an analog input (does nothing for AN20 / AN21) */
void adcPinAnalog(uint8_t ch)
{
    int8_t p = adcPinOf(ch);
    if (p < 0) return;
    *(g_pin_map[p].tris_reg) |= (1U << g_pin_map[p].bit);
    if (g_pin_map[p].ansel_reg) *(g_pin_map[p].ansel_reg) |= (1U << g_pin_map[p].bit);
}

/* ---------------- power and clock ---------------- */
static void adcPowerUp()
{
    ADCON1Lbits.ADON = 1;
    ADCON5Lbits.SHRPWR = 1;                 /* power the shared core ... */
    uint32_t g = 200000UL;
    while (!ADCON5Lbits.SHRRDY && --g) { }  /* ... and wait until it is ready */
    g = 200000UL;
    while (!ADCON2Hbits.REFRDY && --g) { }  /* band gap / reference ready */
}

void adcBegin()
{
    _ADC1MD = 0;                            /* power the module (PMD1) */
    ADCON1L = 0;                            /* ADON = 0 while configuring */
    ADCON1H = 0;
    ADCON1Hbits.SHRRES = 3;                 /* 12 bit */
    ADCON1Hbits.FORM = 0;                   /* integer */
    ADCON2L = 0;
    ADCON2Lbits.SHRADCS = 1;                /* TAD = 2 ADC clocks */
    ADCON2H = 0;
    ADCON2Hbits.SHRSAMC = 15;               /* sample time 17 TAD */
    ADCON3L = 0;
    ADCON3H = 0;
    ADCON3Hbits.CLKSEL = 1;                 /* ADC clock source = FOSC */
#if F_CPU > 100000000UL
    ADCON3Hbits.CLKDIV = 3;                 /* 200 MHz / 4 = 50 MHz (TAD 40 ns; limit 70 MHz) */
#else
    ADCON3Hbits.CLKDIV = 0;                 /* 8 MHz */
#endif
    ADCON3Hbits.SHREN = 1;                  /* shared core on */
    ADCON5H = 0;
    ADCON5Hbits.WARMTIME = 15;
    ADMOD0L = ADMOD0H = ADMOD1L = 0;        /* all unsigned, single-ended */
    ADIEL = ADIEH = 0;
    ADLVLTRGL = ADLVLTRGH = 0;
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) *adcTrgReg(ch) = ADC_TRG_NONE;
    adcBits = 12;
    adcPowerUp();
}

/* 6, 8, 10 or 12 bit. The result stays right-aligned: max code 63 / 255 / 1023 / 4095. */
void adcResolution(uint8_t bits)
{
    uint8_t code = bits >= 12 ? 3 : bits >= 10 ? 2 : bits >= 8 ? 1 : 0;
    ADCON1Lbits.ADON = 0;                   /* may only be changed while the ADC is off */
    ADCON1Hbits.SHRRES = code;
    adcBits = 6 + 2 * code;
    adcPowerUp();
}

/* sample time in TAD (2..1025): longer = more time for the input capacitor to charge */
void adcSampleTime(uint16_t tad) { ADCON2Hbits.SHRSAMC = (tad < 2 ? 0 : (tad > 1025 ? 1023 : tad - 2)); }

/* integer (right-aligned) or fractional (left-aligned, Q15-style) results */
void adcFractional(bool on) { ADCON1Lbits.ADON = 0; ADCON1Hbits.FORM = on; adcPowerUp(); }

/* per channel: signed result (two's complement around mid-scale) and/or
 * differential (channel minus ANN0) */
void adcMode(uint8_t ch, bool isSigned, bool differential)
{
    volatile uint16_t *mod = &ADMOD0L + (ch >> 3);          /* ADMOD0L, ADMOD0H, ADMOD1L */
    uint8_t sh = (ch & 7) * 2;
    uint16_t v = *mod & ~(3U << sh);
    if (isSigned) v |= 1U << sh;
    if (differential) v |= 2U << sh;
    *mod = v;
    if (differential) { pinMode(38, INPUT); ANSELDbits.ANSELD13 = 1; }   /* ANN0 = RD13 */
}

/* ---------------- one conversion ---------------- */
/* Converts one channel now (individual software trigger, like analogRead) and
 * returns the raw result: 0..adcMaxCode() unsigned, or signed if adcMode() said so. */
int16_t adcRead(uint8_t ch)
{
    adcPinAnalog(ch);
    (void)adcBuf[ch];                       /* reading clears a stale ready bit */
    ADCON3Lbits.CNVCHSEL = ch;
    ADCON3Lbits.CNVRTCH = 1;                /* convert it now */
    uint32_t g = 100000UL;
    while (!adcReady(ch) && --g) { }
    adcTimedOut = (g == 0);
    return (int16_t)adcBuf[ch];
}

int32_t adcReadMv(uint8_t ch) { return adcToMv((uint16_t)adcRead(ch)); }

/* ---------------- many channels at once ---------------- */
/* Common software trigger: every channel in `mask` is converted, one after
 * another, from a single trigger. Results go to out[ch]. Returns false on time-out. */
bool adcScan(uint32_t mask, uint16_t *out)
{
    uint8_t old[ADC_CHANNELS];
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
        old[ch] = *adcTrgReg(ch);
        if (mask & (1UL << ch)) { adcPinAnalog(ch); (void)adcBuf[ch]; *adcTrgReg(ch) = ADC_TRG_SW_COMMON; }
    }
    ADCON3Lbits.SWCTRG = 1;                 /* one trigger for all of them */
    bool ok = true;
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) {
        if (!(mask & (1UL << ch))) continue;
        uint32_t g = 100000UL;
        while (!adcReady(ch) && --g) { }
        if (!g) ok = false;
        out[ch] = adcBuf[ch];
    }
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++) *adcTrgReg(ch) = old[ch];
    adcTimedOut = !ok;
    return ok;
}

/* ---------------- hardware triggers ---------------- */
void adcTrigger(uint8_t ch, uint8_t source) { adcPinAnalog(ch); *adcTrgReg(ch) = source; }

/* level software trigger: the channels with ADC_TRG_SW_LEVEL convert again and
 * again for as long as it is on (continuous conversion) */
void adcContinuous(bool on) { ADCON3Lbits.SWLCTRG = on; }

/* SCCP3 as a periodic timer: the trigger for timed sampling. 32-bit, FCY. */
volatile unsigned long adcTimerSoftCount = 0;
volatile uint8_t adcSoftTriggerCh = 0xFF;    /* fallback: the timer interrupt starts a conversion */

void adcTimerStart(unsigned long hz)
{
    _CCP3MD = 0;
    CCP3CON1L = 0; CCP3CON1H = 0; CCP3CON2L = 0; CCP3CON2H = 0; CCP3CON3H = 0;
    CCP3CON1Lbits.T32 = 1;                  /* 32-bit timer, MOD = 0000, clock FCY */
    unsigned long per = FCY / hz - 1;
    CCP3PRL = (uint16_t)per; CCP3PRH = (uint16_t)(per >> 16);
    CCP3TMRL = 0; CCP3TMRH = 0;
    _CCT3IF = 0; _CCT3IP = 2;
    _CCT3IE = (adcSoftTriggerCh != 0xFF);
    CCP3CON1Lbits.CCPON = 1;
}
void adcTimerStop() { CCP3CON1Lbits.CCPON = 0; _CCT3IE = 0; }

extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT3Interrupt(void)
{
    adcTimerSoftCount++;
    if (adcSoftTriggerCh < ADC_CHANNELS) { ADCON3Lbits.CNVCHSEL = adcSoftTriggerCh; ADCON3Lbits.CNVRTCH = 1; }
    _CCT3IF = 0;
}

/* PWM generator 1 as the trigger: TRIGA at `point` (0..1) of each period
 * raises "PWM1 Trigger 1". The PWM pins are NOT driven (PENH = PENL = 0). */
void adcPwmTriggerStart(unsigned long hz, float point)
{
    _PWMMD = 0;
    PG1CONL = 0;
    PCLKCON = 0x0000;                       /* PWM clock = FCY */
    uint16_t per = (uint16_t)(FCY / hz - 1);
    PG1PER = per; PG1DC = per / 2; PG1PHASE = 0;
    PG1TRIGA = (uint16_t)(per * point);
    PG1IOCONH = 0;                          /* pins stay ordinary I/O */
    PG1IOCONL = 0;
    PG1EVTL = 0x0100;                       /* ADTR1EN1: TRIGA -> ADC trigger 1 */
    PG1EVTH = 0x0300;                       /* no PWM interrupts */
    PG1CONH = 0;
    PG1CONL = 0x8008;                       /* ON, CLKSEL = master clock, edge-aligned */
}
void adcPwmTriggerStop() { PG1CONL = 0; }

/* ---------------- interrupts with a callback ---------------- */
typedef void (*AdcCallback)(uint8_t ch, uint16_t value);
AdcCallback adcCallback[ADC_CHANNELS];
volatile unsigned long adcIrqCount = 0;

/* Every result of `ch` calls fn(ch, value) - from the ADC interrupt, or from
 * adcPoll() if you call that in loop() instead. */
void adcAttachInterrupt(uint8_t ch, AdcCallback fn, uint8_t priority = 3)
{
    adcCallback[ch] = fn;
    if (ch < 16) ADIEL |= (1U << ch); else ADIEH |= (1U << (ch - 16));
    _ADCIF = 0; _ADCIP = priority; _ADCIE = 1;
}
void adcDetachInterrupt(uint8_t ch)
{
    adcCallback[ch] = 0;
    if (ch < 16) ADIEL &= ~(1U << ch); else ADIEH &= ~(1U << (ch - 16));
}

/* deliver every ready result of a channel that has a callback */
void adcPoll()
{
    for (uint8_t ch = 0; ch < ADC_CHANNELS; ch++)
        if (adcCallback[ch] && adcReady(ch)) adcCallback[ch](ch, adcBuf[ch]);
}

/* common ADC interrupt: one vector for every channel enabled in ADIEL/H */
extern "C" void __attribute__((interrupt, no_auto_psv)) _ADCInterrupt(void)
{
    _ADCIF = 0;
    adcIrqCount++;
    adcPoll();
}

/* ---------------- digital comparators ADCMP0..3 ---------------- */
enum { ADC_CMP_BELOW = 0x01,        /* LOLO: result <  lo        */
       ADC_CMP_ABOVE = 0x08,        /* HIHI: result >= hi        */
       ADC_CMP_OUTSIDE = 0x09,      /* below lo OR from hi up    */
       ADC_CMP_INSIDE = 0x10 };     /* BTWN: lo <= result < hi   */
volatile unsigned long adcCmpEvents[4];
volatile uint8_t adcCmpLastCh[4];

inline volatile uint16_t *adcCmpCon(uint8_t n) { return &ADCMP0CON + 2 * n; }   /* 4-byte stride */
inline volatile uint16_t *adcCmpEn(uint8_t n)  { return &ADCMP0ENL + 4 * n; }   /* ENL, ENH, LO, HI */

/* Compare every result of `ch` with lo / hi in hardware; an event sets the
 * comparator's interrupt (counted in adcCmpEvents[n]). mode 0 = off. */
void adcCompare(uint8_t n, uint8_t ch, uint16_t lo, uint16_t hi, uint8_t mode)
{
    volatile uint16_t *con = adcCmpCon(n), *en = adcCmpEn(n);
    *con = 0;
    en[0] = ch < 16 ? (1U << ch) : 0;       /* ADCMPxENL */
    en[1] = ch < 16 ? 0 : (1U << (ch - 16));/* ADCMPxENH */
    en[2] = lo; en[3] = hi;                 /* ADCMPxLO, ADCMPxHI */
    if (!mode) return;
    *con = (uint16_t)mode | 0x0040 | 0x0080;   /* mode bits, IE, CMPEN */
    switch (n) {
    case 0: _ADCMP0IF = 0; _ADCMP0IE = 1; break;
    case 1: _ADCMP1IF = 0; _ADCMP1IE = 1; break;
    case 2: _ADCMP2IF = 0; _ADCMP2IE = 1; break;
    case 3: _ADCMP3IF = 0; _ADCMP3IE = 1; break;
    }
}
inline bool adcCompareHit(uint8_t n) { return (*adcCmpCon(n) >> 5) & 1; }   /* STAT */

static inline void adcCmpIsr(uint8_t n)
{
    volatile uint16_t *con = adcCmpCon(n);
    adcCmpLastCh[n] = (*con >> 8) & 0x1F;   /* CHNL: which channel */
    adcCmpEvents[n]++;
    *con &= ~0x0020;                        /* clear STAT */
}
extern "C" void __attribute__((interrupt, no_auto_psv)) _ADCMP0Interrupt(void) { adcCmpIsr(0); _ADCMP0IF = 0; }
extern "C" void __attribute__((interrupt, no_auto_psv)) _ADCMP1Interrupt(void) { adcCmpIsr(1); _ADCMP1IF = 0; }
extern "C" void __attribute__((interrupt, no_auto_psv)) _ADCMP2Interrupt(void) { adcCmpIsr(2); _ADCMP2IF = 0; }
extern "C" void __attribute__((interrupt, no_auto_psv)) _ADCMP3Interrupt(void) { adcCmpIsr(3); _ADCMP3IF = 0; }

/* ---------------- oversampling / averaging filters ADFL0..3 ---------------- */
enum { ADC_FLT_OVERSAMPLE = 0, ADC_FLT_AVERAGE = 3 };
inline volatile uint16_t *adcFltDat(uint8_t n) { return &ADFL0DAT + 2 * n; }
inline volatile uint16_t *adcFltCon(uint8_t n) { return &ADFL0CON + 2 * n; }

/* OVERSAMPLE: sums `ratio` results into a result with more bits:
 *   4x -> 13 bit, 16x -> 14 bit, 64x -> 15 bit, 256x -> 16 bit (also 2/8/32/128x)
 * AVERAGE: the mean of `ratio` results (2..256), still 12 bit.
 * The filter only collects - the channel needs triggers (timer, continuous). */
void adcFilter(uint8_t n, uint8_t ch, uint8_t mode, uint16_t ratio)
{
    static const uint16_t OVS[8] = { 4, 16, 64, 256, 2, 8, 32, 128 };     /* MODE 0 codes */
    static const uint16_t AVG[8] = { 2, 4, 8, 16, 32, 64, 128, 256 };     /* MODE 3 codes */
    const uint16_t *t = (mode == ADC_FLT_AVERAGE) ? AVG : OVS;
    uint8_t code = 0;
    for (uint8_t i = 0; i < 8; i++) if (t[i] == ratio) code = i;
    volatile uint16_t *con = adcFltCon(n);
    *con = 0;
    (void)*adcFltDat(n);
    *con = (uint16_t)(ch & 0x1F) | ((uint16_t)code << 10) | ((uint16_t)mode << 13) | 0x8000;   /* FLEN */
}
void adcFilterOff(uint8_t n) { *adcFltCon(n) = 0; }
inline bool adcFilterReady(uint8_t n) { return (*adcFltCon(n) >> 8) & 1; }   /* RDY */
inline uint16_t adcFilterResult(uint8_t n) { return *adcFltDat(n); }

/* bits of an oversampled result: 12 + log4(ratio) */
uint8_t adcOversampleBits(uint16_t ratio)
{
    uint8_t b = 12;
    while (ratio >= 4) { ratio /= 4; b++; }
    return b;
}

/* ---------------- internal channels ---------------- */
/* The band gap is a fixed ~1.2 V inside the chip. Measured against AVDD it
 * gives AVDD itself: AVDD = 1.2 V * 4096 / raw. Updates adcAvddMv. */
uint16_t adcMeasureVdd()
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 16; i++) sum += (uint16_t)adcRead(ADC_CH_BANDGAP);
    uint32_t raw16 = sum;                   /* 16 x raw */
    if (raw16 == 0) return adcAvddMv;
    adcAvddMv = (uint16_t)((1200UL * (1UL << adcBits) * 16UL + raw16 / 2) / raw16);
    return adcAvddMv;
}

/* temperature sensor voltage in mV (uncalibrated: see the lab) */
int32_t adcTemperatureMv()
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 16; i++) sum += (uint16_t)adcRead(ADC_CH_TEMP);
    return adcToMv((int32_t)(sum / 16));
}

#endif
