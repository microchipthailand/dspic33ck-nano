/*
 * Comparator.h - analog comparator CMP1 with its 12-bit DAC threshold
 *
 *   cmpBegin(CMP_IN_A);          // input A = RA0, threshold DAC on, interrupt on
 *   cmpThresholdMv(1650);        // trip point
 *   cmpOut();                    // 1 = input above the threshold
 *   cmpHysteresis(2);            // 0 / 15 / 30 / 45 mV
 *   cmpOutputToPin(36);          // comparator output -> RB4 (PPS code 23)
 *   cmpMeasureMv();              // use DAC + comparator as a 12-bit SAR ADC
 *
 * ----------------------------------------------------------------------------
 * THE COMPARATOR
 *          input pin (A/B/C/D) ---|+\
 *                                 |  >---[polarity]--[filter]--+--> CMPSTAT
 *       12-bit DAC (threshold) ---|-/                          +--> PPS "CMP1" (code 23)
 *                                                              +--> interrupt (edges)
 *                                                              +--> PWM fault / PTG input 9
 *   It answers one question - "is the input above the threshold?" - in
 *   about 15 ns, continuously, without the CPU. The ADC takes ~1 us per sample
 *   and needs the CPU (or DMA) to look at every result.
 *
 * INPUTS on this board
 *   A = RA0  A0   J2-15      B = RC1  D22  J2-20
 *   C = RA3  A3   J2-18 (also DACOUT1, avoid)      D = RB2  D7   J3-17
 *
 * REGISTERS (the DAC and the comparator are one module, "DAC1")
 *   DACCTRL1L  DACON master on, CLKSEL = 3 (FPLLO): the module needs the PLL
 *   DAC1CONL   DACEN on   INSEL input A-D   HYSSEL 0/15/30/45 mV
 *              CMPPOL invert   FLTREN digital filter   IRQM interrupt edges
 *              CMPSTAT the output (read only)
 *   DAC1DATH   threshold: Vth = 3.3 V * DATH / 4096
 *   IFS4.CMP1IF / IEC4.CMP1IE / IPC19.CMP1IP   the interrupt
 * ----------------------------------------------------------------------------
 */
#ifndef LAB31_COMPARATOR_H
#define LAB31_COMPARATOR_H

#include <Arduino.h>

enum { CMP_IN_A = 0, CMP_IN_B = 1, CMP_IN_C = 2, CMP_IN_D = 3 };
const uint8_t CMP_PIN[4] = { 0, 22, 3, 7 };            /* Arduino pin of input A..D */
const char *const CMP_PIN_NAME[4] = { "A = RA0 (A0, J2-15)", "B = RC1 (D22, J2-20)",
                                      "C = RA3 (A3, J2-18)", "D = RB2 (D7, J3-17)" };
const uint16_t CMP_HYST_MV[4] = { 0, 15, 30, 45 };
const uint16_t AVDD_MV = 3300;

volatile unsigned long cmpEdges = 0;        /* every output change */
volatile unsigned long cmpRises = 0;        /* low -> high only */
uint16_t cmpThresholdCode = 2048;
uint8_t cmpInput = CMP_IN_A;

extern "C" void __attribute__((interrupt, no_auto_psv)) _CMP1Interrupt(void)
{
    cmpEdges++;
    if (DAC1CONLbits.CMPSTAT) cmpRises++;
    _CMP1IF = 0;
}

inline bool cmpOut() { return DAC1CONLbits.CMPSTAT; }

void cmpThresholdCodeSet(uint16_t code) { cmpThresholdCode = code & 0x0FFF; DAC1DATH = cmpThresholdCode; }
void cmpThresholdMv(uint16_t mv) { cmpThresholdCodeSet((uint16_t)(((uint32_t)mv * 4096UL + AVDD_MV / 2) / AVDD_MV)); }
uint16_t cmpThresholdGetMv() { return (uint16_t)((uint32_t)cmpThresholdCode * AVDD_MV / 4096UL); }

/* Make the input pin an analog input */
void cmpPinAnalog(uint8_t input)
{
    uint8_t pin = CMP_PIN[input];
    pinMode(pin, INPUT);
    if (g_pin_map[pin].ansel_reg) *(g_pin_map[pin].ansel_reg) |= (1U << g_pin_map[pin].bit);
}

void cmpSelectInput(uint8_t input)
{
    cmpInput = input & 3;
    cmpPinAnalog(cmpInput);
    DAC1CONLbits.INSEL = cmpInput;
}

void cmpHysteresis(uint8_t level) { DAC1CONLbits.HYSSEL = level & 3; }
void cmpInvert(bool on)           { DAC1CONLbits.CMPPOL = on; }
void cmpFilter(bool on)           { DAC1CONLbits.FLTREN = on; }

void cmpBegin(uint8_t input)
{
    _CMP1MD = 0;                            /* power the module (PMD7) */
    DACCTRL1L = 0;
    DACCTRL1Lbits.CLKSEL = 3;               /* FPLLO */
    DACCTRL1Lbits.CLKDIV = 0;
    DACCTRL1Lbits.FCLKDIV = 0;
    DACCTRL2H = 0x8A;                       /* slope-mode timings: MCC defaults (unused) */
    DACCTRL2L = 0x55;
    DAC1CONL = 0;
    DAC1CONH = 0;
    DAC1DATL = 0;
    cmpSelectInput(input);
    cmpThresholdCodeSet(cmpThresholdCode);
    DAC1CONLbits.IRQM = 3;                  /* interrupt on both edges */
    DAC1CONLbits.DACEN = 1;
    DACCTRL1Lbits.DACON = 1;
    _CMP1IP = 3;
    _CMP1IF = 0;
    _CMP1IE = 1;
}

/* Comparator output -> any remappable pin (RP number), or off (code 0) */
void cmpOutputToPin(uint8_t rp, bool on = true)
{
    volatile uint8_t *rpor = (volatile uint8_t *)&RPOR0;    /* RP32 = byte 0 */
    __builtin_write_RPCON(0x0000);
    rpor[rp - 32] = on ? 23 : 0;            /* 23 = CMP1 */
    __builtin_write_RPCON(0x0800);
}

/* Successive approximation: find the DAC code where the comparator flips,
 * one bit at a time, MSB first - exactly how a SAR ADC works inside. 12 steps.
 * Hysteresis, inversion and the interrupt are switched off meanwhile. */
uint16_t cmpMeasureCode()
{
    uint16_t saved = DAC1CONL;
    _CMP1IE = 0;
    DAC1CONLbits.HYSSEL = 0; DAC1CONLbits.CMPPOL = 0; DAC1CONLbits.IRQM = 0;
    uint16_t code = 0;
    for (int8_t bit = 11; bit >= 0; bit--) {
        uint16_t trial = code | (1U << bit);
        DAC1DATH = trial;
        delayMicroseconds(2);               /* DAC + comparator settle */
        if (DAC1CONLbits.CMPSTAT) code = trial;    /* input still above: keep the bit */
    }
    DAC1DATH = cmpThresholdCode;            /* threshold back */
    delayMicroseconds(2);
    DAC1CONL = saved;
    _CMP1IF = 0;
    _CMP1IE = 1;
    return code;
}
uint16_t cmpMeasureMv() { return (uint16_t)((uint32_t)cmpMeasureCode() * AVDD_MV / 4096UL); }

#endif
