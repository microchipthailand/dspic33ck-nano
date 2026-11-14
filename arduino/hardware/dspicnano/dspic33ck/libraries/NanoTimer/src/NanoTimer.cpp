/*
 * NanoTimer.cpp - Timer1/2/3 on SCCP1/2/3 of the dsPIC33CK256MC005
 *
 * Each SCCP runs as ONE 32-bit timer (T32 = 1), clock = FCY, prescaler 1:1:
 *   CCPxCON1L  CCPON (bit 15), T32 (bit 5), MOD = 0000 (timer), CLKSEL = 000 (FCY)
 *   CCPxPRH:PRL  period - 1          CCPxTMRH:TMRL  running count
 *   interrupt  _CCTxInterrupt (CCTxIF, timer period match) - NOT _CCPxInterrupt
 * 32 bits at 1:1 covers every period from 10 us to ~42 s at 100 MIPS, so there
 * is no prescaler to choose and every period is exact to one FCY clock.
 *
 * Register access goes through a switch instead of a table of SFR pointers:
 * a const pointer table would be placed in flash (PSV).
 */
#include "NanoTimer.h"

NanoTimer Timer1;
NanoTimer Timer2;
NanoTimer Timer3;

uint8_t NanoTimer::idx(void) const
{
    return this == &Timer1 ? 0 : this == &Timer2 ? 1 : 2;
}

#define TICKS_PER_US   (FCY / 1000000UL)          /* 100 at 100 MIPS, 4 at 4 MIPS */
#define MIN_TICKS      (10UL * TICKS_PER_US)      /* 10 us */

/* one macro body per module, so the three cases stay identical */
#define SCCP_SELECT(i, BODY)                                   \
    switch (i) {                                               \
    case 0: { BODY(1) } break;                                 \
    case 1: { BODY(2) } break;                                 \
    default: { BODY(3) } break;                                \
    }

#define OFF(n)        CCP##n##CON1Lbits.CCPON = 0;
#define ON(n)         CCP##n##CON1Lbits.CCPON = 1;
#define CLEAR(n)      CCP##n##TMRL = 0; CCP##n##TMRH = 0;
#define WRITE_PR(n)   CCP##n##PRL = (uint16_t)(_ticks - 1); CCP##n##PRH = (uint16_t)((_ticks - 1) >> 16);
#define SETUP(n)      PMD2bits.CCP##n##MD = 0;                                     \
                      CCP##n##CON1L = 0; CCP##n##CON1H = 0; CCP##n##CON2L = 0;     \
                      CCP##n##CON2H = 0; CCP##n##CON3H = 0;                        \
                      CCP##n##CON1L = 0x0020;   /* T32, timer mode, FCY, 1:1 */

void NanoTimer::begin(unsigned long periodUs, void (*callback)(void))
{
    _running = false;
    SCCP_SELECT(idx(), SETUP)
    setPriority(3);
    _fn = callback;
    _count = 0;
    setPeriod(periodUs);
    start();
}

void NanoTimer::setTicks(uint32_t ticks)
{
    if (ticks < MIN_TICKS) ticks = MIN_TICKS;
    _ticks = ticks;
    /* Stop, load the new period and restart from 0. Changing PR on the fly
     * could leave the count above the new period, and it would then run the
     * whole 32-bit range (42 s) before the next match. */
    SCCP_SELECT(idx(), OFF)
    SCCP_SELECT(idx(), WRITE_PR)
    SCCP_SELECT(idx(), CLEAR)
    if (_running) { SCCP_SELECT(idx(), ON) }
}

void NanoTimer::setPeriod(unsigned long periodUs)
{
    uint64_t t = (uint64_t)periodUs * TICKS_PER_US;
    setTicks(t > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (uint32_t)t);
}

void NanoTimer::setFrequency(float hz)
{
    if (hz <= 0.0f) return;
    float t = (float)FCY / hz + 0.5f;
    setTicks(t >= 4294967295.0f ? 0xFFFFFFFFUL : (uint32_t)t);
}

unsigned long NanoTimer::getPeriod(void) const
{
    return (_ticks + TICKS_PER_US / 2) / TICKS_PER_US;
}

void NanoTimer::enableIrq(bool on)
{
    switch (idx()) {
    case 0:  IFS0bits.CCT1IF = 0; IEC0bits.CCT1IE = on; break;
    case 1:  IFS1bits.CCT2IF = 0; IEC1bits.CCT2IE = on; break;
    default: IFS2bits.CCT3IF = 0; IEC2bits.CCT3IE = on; break;
    }
}

void NanoTimer::start(void)
{
    SCCP_SELECT(idx(), OFF)
    SCCP_SELECT(idx(), CLEAR)
    enableIrq(true);
    _running = true;
    SCCP_SELECT(idx(), ON)
}

void NanoTimer::stop(void)
{
    SCCP_SELECT(idx(), OFF)
    _running = false;
}

void NanoTimer::resume(void)
{
    enableIrq(true);
    _running = true;
    SCCP_SELECT(idx(), ON)
}

void NanoTimer::attachInterrupt(void (*callback)(void)) { _fn = callback; }
void NanoTimer::detachInterrupt(void)                   { _fn = 0; }

void NanoTimer::setPriority(uint8_t level)
{
    if (level < 1) level = 1;
    if (level > 7) level = 7;
    switch (idx()) {
    case 0:  IPC1bits.CCT1IP = level; break;
    case 1:  IPC6bits.CCT2IP = level; break;
    default: IPC9bits.CCT3IP = level; break;
    }
}

/* 32-bit values are read as two 16-bit halves: read again if one changed in between */
unsigned long NanoTimer::read(void)
{
    uint16_t hi, lo;
#define READ_TMR(n) do { hi = CCP##n##TMRH; lo = CCP##n##TMRL; } while (hi != CCP##n##TMRH);
    SCCP_SELECT(idx(), READ_TMR)
#undef READ_TMR
    return (((uint32_t)hi << 16) | lo) / TICKS_PER_US;
}

unsigned long NanoTimer::count(void)
{
    unsigned long a, b;
    do { a = _count; b = _count; } while (a != b);
    return a;
}

void NanoTimer::resetCount(void) { _count = 0; }

/* ---------------- interrupt handlers ---------------- */
extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT1Interrupt(void)
{
    IFS0bits.CCT1IF = 0;
    Timer1._isr();
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT2Interrupt(void)
{
    IFS1bits.CCT2IF = 0;
    Timer2._isr();
}

extern "C" void __attribute__((interrupt, no_auto_psv)) _CCT3Interrupt(void)
{
    IFS2bits.CCT3IF = 0;
    Timer3._isr();
}
