/*
 * DMA.h - small driver for the 4 DMA channels of the dsPIC33CK256MC005
 * (sketch tab of Lab 14; the .ino only says WHAT each channel moves)
 *
 * Every channel has the same 5 registers, 10 bytes apart in SFR space:
 *
 *     DMACHn   control   CHEN SIZE TRMODE DAMODE SAMODE CHREQ RELOAD NULLW
 *     DMAINTn  trigger   CHSEL<6:0> + flags HALFIF DONEIF OVRUNIF ... HALFEN
 *     DMASRCn  source address        (RAM or SFR)
 *     DMADSTn  destination address   (RAM or SFR)
 *     DMACNTn  transfers left        (counts DOWN while the channel runs)
 *
 * so dmaReg(n) overlays a struct on DMACH0 + 5*n words and one set of
 * functions drives channel 0..3. Channel 0 has the HIGHEST fixed priority and
 * channel 3 the lowest: put the transfer that must never be late on ch0.
 *
 * Transfer modes (DMACHn.TRMODE):
 *   DMA_ONE_SHOT          1 unit per trigger, channel stops when CNT hits 0
 *   DMA_REPEAT_ONE_SHOT   1 unit per trigger, and with reload: endless
 *   DMA_CONTINUOUS        the WHOLE block on one trigger, then stop
 *   DMA_REPEAT_CONT       the whole block on every trigger
 *
 * Trigger numbers (DMAINTn.CHSEL) are from the MCC device data for this chip.
 */
#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

/* ---------- trigger sources (CHSEL) used by Lab 14 ---------- */
#define DMA_TRG_SCCP1      0x01    /* SCCP1 OC/IC event                  */
#define DMA_TRG_UART1_RX   0x04
#define DMA_TRG_UART1_TX   0x05    /* UART1 transmit event               */
#define DMA_TRG_SCCP2      0x0C
#define DMA_TRG_SCCP3      0x12    /* SCCP3 OC/IC event                  */
#define DMA_TRG_SCCP4      0x15
#define DMA_TRG_CRC        0x18    /* CRC module - unused here, so it is
                                      a "never" trigger for software GO  */
#define DMA_TRG_ADC_AN(n)  (0x28 + (n))   /* ADC ANn conversion complete  */

/* ---------- modes ---------- */
enum { DMA_ONE_SHOT = 0, DMA_REPEAT_ONE_SHOT = 1, DMA_CONTINUOUS = 2, DMA_REPEAT_CONT = 3 };
enum { DMA_FIXED = 0, DMA_INC = 1, DMA_DEC = 2 };           /* SAMODE / DAMODE   */
enum { DMA_16BIT = 0, DMA_8BIT = 1 };                       /* SIZE              */
enum { DMA_STOP_AT_END = 0, DMA_RELOAD = 1 };               /* RELOAD            */

/* ---------- DMACHn / DMAINTn bit masks ---------- */
#define DMACH_CHEN    0x0001
#define DMACH_CHREQ   0x0100
#define DMAINT_HALFEN  0x0001
#define DMAINT_OVRUNIF 0x0008
#define DMAINT_HALFIF  0x0010
#define DMAINT_DONEIF  0x0020

typedef struct {
    volatile uint16_t CH, INT, SRC, DST, CNT;
} DmaRegs;

static inline DmaRegs *dmaReg(uint8_t ch)
{
    return (DmaRegs *)((volatile uint16_t *)&DMACH0 + 5 * ch);
}

/* Power up all 4 channels and the DMA controller. Call once. */
void dmaBegin(void)
{
    PMD6bits.DMA0MD = 0;
    PMD6bits.DMA1MD = 0;
    PMD6bits.DMA2MD = 0;
    PMD6bits.DMA3MD = 0;
    DMACONbits.DMAEN = 1;
    DMACONbits.PRSSEL = 0;               /* fixed priority: ch0 > ch1 > ch2 > ch3 */
    DMAL = 0x1000;                       /* RAM window the DMA may touch        */
    DMAH = 0x8FFF;                       /* (SFRs below 0x1000 always allowed)  */
}

/*
 * Configure one channel (it is left OFF - call dmaStart).
 *   src/dst   addresses: a RAM array or an SFR such as &U1TXREG
 *   count     number of transfers (bytes or words, see size)
 *   trigger   DMA_TRG_xxx
 */
void dmaSetup(uint8_t ch, const volatile void *src, volatile void *dst, uint16_t count,
              uint8_t trigger, uint8_t trmode, uint8_t srcStep, uint8_t dstStep,
              uint8_t size, uint8_t reload)
{
    DmaRegs *r = dmaReg(ch);
    r->CH  = 0;                          /* channel off while it is changed */
    r->INT = 0;                          /* clears all flags and HALFEN     */
    r->SRC = (uint16_t)(uintptr_t)src;
    r->DST = (uint16_t)(uintptr_t)dst;
    r->CNT = count;
    r->INT = (uint16_t)trigger << 8;
    r->CH  = (uint16_t)((size & 1) << 1) | ((trmode & 3) << 2) | ((dstStep & 3) << 4)
           | ((srcStep & 3) << 6) | ((reload & 1) << 9);
}

void dmaHalfInterrupt(uint8_t ch, bool on)   /* also interrupt at CNT = count/2 */
{
    if (on) dmaReg(ch)->INT |= DMAINT_HALFEN; else dmaReg(ch)->INT &= ~DMAINT_HALFEN;
}

/* CPU interrupt for a channel (half / done / overrun). prio 0 = off. */
void dmaInterrupt(uint8_t ch, uint8_t prio)
{
    switch (ch) {
    case 0: _DMA0IE = 0; _DMA0IP = prio; _DMA0IF = 0; _DMA0IE = prio != 0; break;
    case 1: _DMA1IE = 0; _DMA1IP = prio; _DMA1IF = 0; _DMA1IE = prio != 0; break;
    case 2: _DMA2IE = 0; _DMA2IP = prio; _DMA2IF = 0; _DMA2IE = prio != 0; break;
    case 3: _DMA3IE = 0; _DMA3IP = prio; _DMA3IF = 0; _DMA3IE = prio != 0; break;
    }
}

static inline void dmaStart(uint8_t ch)   { dmaReg(ch)->CH |= DMACH_CHEN; }
static inline void dmaStop(uint8_t ch)    { dmaReg(ch)->CH &= ~DMACH_CHEN; }
static inline void dmaKick(uint8_t ch)    { dmaReg(ch)->CH |= DMACH_CHREQ; }  /* software trigger */
static inline bool dmaRunning(uint8_t ch) { return (dmaReg(ch)->CH & DMACH_CHEN) != 0; }
static inline uint16_t dmaLeft(uint8_t ch){ return dmaReg(ch)->CNT; }
static inline bool dmaDone(uint8_t ch)    { return (dmaReg(ch)->INT & DMAINT_DONEIF) != 0; }

/* Read and clear one channel's flags (call this from its interrupt). */
static inline uint16_t dmaTakeFlags(uint8_t ch)
{
    DmaRegs *r = dmaReg(ch);
    uint16_t f = r->INT & (DMAINT_HALFIF | DMAINT_DONEIF | DMAINT_OVRUNIF);
    if (f) r->INT &= ~f;
    return f;
}

/* ===================================================================
 *  Channel 1 helper: block copy RAM -> RAM, started by software
 * =================================================================== */
void dmaCopyStart(uint8_t ch, void *dst, const void *src, uint16_t words)
{
    dmaSetup(ch, src, dst, words, DMA_TRG_CRC, DMA_CONTINUOUS,
             DMA_INC, DMA_INC, DMA_16BIT, DMA_STOP_AT_END);
    dmaStart(ch);
    dmaKick(ch);                          /* one request moves the whole block */
}

bool dmaCopyWait(uint8_t ch)              /* false = timed out (DMA broken) */
{
    unsigned long t0 = micros();
    while (!dmaDone(ch)) {
        if (micros() - t0 > 20000UL) { dmaStop(ch); return false; }
    }
    dmaStop(ch);
    return true;
}

/* ===================================================================
 *  Channel 3 helper: send a text with DMA -> U1TXREG (Serial's UART)
 *
 *  UART1 TX event with UTXISEL = 000 ("TX buffer empty"): each time the
 *  UART hands a byte to its shift register, the DMA writes the next one.
 *  The first byte is a software kick. The core's Serial.write() polls the
 *  same UART, so the sketch must call dmaSerialWait() before printing with
 *  Serial while a DMA message may still be going out.
 * =================================================================== */
#define DMA_TX_SIZE 160
static char     dmaTxBuf[DMA_TX_SIZE];
static uint8_t  dmaTxCh = 3;
static uint16_t dmaTxLen = 0;
static bool     dmaTxBusy = false;
static bool     dmaTxWorks = true;        /* false: fell back to Serial.write */
static unsigned long dmaTxBytes = 0;      /* sent by the DMA (statistics)     */

void dmaSerialBegin(uint8_t ch)
{
    dmaTxCh = ch;
    U1STAHbits.UTXISEL = 0;               /* TX event when the TX buffer is empty */
}

/* Wait until the last DMA message is out. If the DMA stalls (trigger not
 * working on this silicon) the rest is printed by the CPU and DMA TX is
 * switched off, so no text is ever lost. */
void dmaSerialWait(void)
{
    if (!dmaTxBusy) return;
    uint16_t last = dmaLeft(dmaTxCh);
    unsigned long t0 = micros();
    while (!dmaDone(dmaTxCh)) {
        uint16_t now = dmaLeft(dmaTxCh);
        if (now != last) { last = now; t0 = micros(); }
        else if (micros() - t0 > 3000UL) {           /* > 30 char times at 115200 */
            dmaStop(dmaTxCh);
            dmaTxWorks = false;
            for (uint16_t i = dmaTxLen - dmaLeft(dmaTxCh); i < dmaTxLen; i++)
                Serial.write((uint8_t)dmaTxBuf[i]);
            break;
        }
    }
    dmaStop(dmaTxCh);
    dmaTxBytes += dmaTxLen - dmaLeft(dmaTxCh);
    dmaTxBusy = false;
}

/* Queue a text; returns at once while the DMA sends it. */
void dmaPrint(const char *s)
{
    dmaSerialWait();                      /* the buffer is reused */
    uint16_t n = 0;
    while (s[n] && n < DMA_TX_SIZE) { dmaTxBuf[n] = s[n]; n++; }
    if (n == 0) return;
    if (!dmaTxWorks) { Serial.write((const uint8_t *)dmaTxBuf, n); return; }

    while (U1STAHbits.UTXBF) { }          /* room for the kicked first byte */
    dmaTxLen = n;
    dmaSetup(dmaTxCh, dmaTxBuf, &U1TXREG, n, DMA_TRG_UART1_TX, DMA_ONE_SHOT,
             DMA_INC, DMA_FIXED, DMA_8BIT, DMA_STOP_AT_END);
    dmaStart(dmaTxCh);
    dmaTxBusy = true;
    dmaKick(dmaTxCh);
}

/* printf through the DMA: dmaPrintf("x=%d\r\n", x); (no %f - print ints) */
void dmaPrintf(const char *fmt, ...)
{
    char tmp[DMA_TX_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    dmaPrint(tmp);
}
