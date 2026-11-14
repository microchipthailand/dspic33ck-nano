/*
 * NanoServo.cpp - servo pulses from SCCP1-4 (see NanoServo.h)
 *
 * SCCP set-up, same mode the core's analogWrite() uses:
 *   CCPxCON1L  MOD = 0101 dual-edge compare, buffered   TMRPS = prescaler
 *              CLKSEL = 000 (FCY)   T32 = 0 (16-bit timer, period CCPxPRL)
 *   CCPxCON2H  OCAEN = 1  -> output OCMx, routed to the pin by PPS
 *   CCPxRA = 0      the output goes HIGH when the timer passes 0 (frame start)
 *   CCPxRB = width  the output goes LOW  when the timer reaches RB
 *   CCPxPRL         frame length (20 ms) - 1
 * RA/RB are double-buffered in mode 0101: a new width is loaded at the next
 * timer roll-over, so the pulse already on the wire is finished first.
 *
 * PPS output codes (p33CK256MC005.h): _RPOUT_OCM1..4 = 15..18.
 * Register access goes through a switch, not a const table of SFR pointers
 * (such a table would be placed in flash / PSV).
 */
#include "NanoServo.h"

/* prescaler: the smallest one that fits a 20 ms frame into 16 bits */
#if   (FCY / 50UL) <= 65535UL
  #define SV_TMRPS  0b00
  #define SV_PS     1UL
#elif (FCY / 4UL / 50UL) <= 65535UL
  #define SV_TMRPS  0b01
  #define SV_PS     4UL
#elif (FCY / 16UL / 50UL) <= 65535UL
  #define SV_TMRPS  0b10
  #define SV_PS     16UL
#else
  #define SV_TMRPS  0b11
  #define SV_PS     64UL
#endif

#define SV_TICK_HZ      (FCY / SV_PS)                    /* 1 562 500 at 100 MIPS */
#define SV_FRAME_TICKS  (SV_TICK_HZ / 50UL)              /* 31 250 = 20 ms */
#define SV_TICK_HZ_100  (SV_TICK_HZ / 100UL)             /* 15 625 at 100 MIPS, exact */

#define SV_US_LO   400
#define SV_US_HI  2600

static uint8_t _used;                                    /* bit n-1 = SCCPn taken */
/* hand-out order 3, 2, 1, 4 (tone() lives on SCCP4, so it goes last) */

static uint16_t us_to_ticks(int us)
{
    return (uint16_t)(((uint32_t)us * SV_TICK_HZ_100 + 5000UL) / 10000UL);
}

static void pps_out(int16_t rp, uint8_t fn)
{
    volatile uint16_t *rpor = &RPOR0 + ((rp - 32) / 2);
    __builtin_write_RPCON(0x0000);
    if (rp & 1) *rpor = (uint16_t)((*rpor & 0x00FFu) | ((uint16_t)fn << 8));
    else        *rpor = (uint16_t)((*rpor & 0xFF00u) | fn);
    __builtin_write_RPCON(0x0800);
}

#define SV_SELECT(i, BODY)                \
    switch (i) {                          \
    case 1: { BODY(1) } break;            \
    case 2: { BODY(2) } break;            \
    case 3: { BODY(3) } break;            \
    case 4: { BODY(4) } break;            \
    default: break;                       \
    }

#define SV_START(n)                                                        \
    PMD2bits.CCP##n##MD = 0;                                               \
    CCP##n##CON1L = 0; CCP##n##CON1H = 0; CCP##n##CON2L = 0;               \
    CCP##n##CON2H = 0; CCP##n##CON3H = 0;                                  \
    CCP##n##TMRL = 0;                                                      \
    CCP##n##PRL = (uint16_t)(SV_FRAME_TICKS - 1UL);                        \
    CCP##n##PRH = 0xFFFF;                                                  \
    CCP##n##RA = 0;                                                        \
    CCP##n##RB = ticks;                                                    \
    CCP##n##CON1Lbits.MOD = 0b0101;                                        \
    CCP##n##CON1Lbits.TMRPS = SV_TMRPS;                                    \
    CCP##n##CON2Hbits.OCAEN = 1;                                           \
    CCP##n##CON1Lbits.CCPON = 1;

#define SV_STOP(n)    CCP##n##CON1Lbits.CCPON = 0; CCP##n##CON2Hbits.OCAEN = 0;
#define SV_WIDTH(n)   CCP##n##RB = ticks;

uint8_t NanoServo::attach(uint8_t pin)
{
    return attach(pin, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
}

uint8_t NanoServo::attach(uint8_t pin, int minUs, int maxUs)
{
    int16_t rp = pinToRP(pin);
    if (rp < 0) return 0;                          /* RA0-RA4 have no PPS */

    if (_sccp) detach();                           /* re-attach: start clean */

    uint8_t m = 0;
    for (uint8_t k = 0; k < NANOSERVO_MAX; k++) {
        uint8_t c = (uint8_t)(k < 3 ? 3 - k : 4);
        if (!(_used & (1u << (c - 1)))) { m = c; break; }
    }
    if (!m) return 0;

    if (minUs < SV_US_LO) minUs = SV_US_LO;
    if (maxUs > SV_US_HI) maxUs = SV_US_HI;
    if (maxUs <= minUs) { minUs = MIN_PULSE_WIDTH; maxUs = MAX_PULSE_WIDTH; }
    _min = (int16_t)minUs;
    _max = (int16_t)maxUs;
    if (_us < _min || _us > _max) _us = DEFAULT_PULSE_WIDTH;   /* first attach: centre */
    if (_us < _min) _us = _min;
    if (_us > _max) _us = _max;

    _used |= (uint8_t)(1u << (m - 1));
    _sccp = m;
    _pin = pin;
    _rp = rp;

    digitalWrite(pin, LOW);                        /* idle low before it drives */
    pinMode(pin, OUTPUT);                          /* also clears ANSEL */
    pps_out(rp, (uint8_t)(14 + m));                /* OCM1..4 = 15..18 */

    uint16_t ticks = us_to_ticks(_us);
    SV_SELECT(m, SV_START)
    return m;
}

void NanoServo::detach(void)
{
    if (!_sccp) return;
    SV_SELECT(_sccp, SV_STOP)
    pps_out(_rp, 0);                               /* pin back to LATx */
    digitalWrite(_pin, LOW);
    _used &= (uint8_t)~(1u << (_sccp - 1));
    _sccp = 0;
}

void NanoServo::writeMicroseconds(int us)
{
    int lo = _sccp ? _min : SV_US_LO;
    int hi = _sccp ? _max : SV_US_HI;
    if (us < lo) us = lo;
    if (us > hi) us = hi;
    _us = (int16_t)us;                             /* kept for the next attach too */
    if (!_sccp) return;
    uint16_t ticks = us_to_ticks(us);
    SV_SELECT(_sccp, SV_WIDTH)
}

void NanoServo::write(int value)
{
    if (value < 200) {                             /* Arduino rule: small = degrees */
        if (value < 0) value = 0;
        if (value > 180) value = 180;
        int lo = _sccp ? _min : MIN_PULSE_WIDTH;
        int hi = _sccp ? _max : MAX_PULSE_WIDTH;
        value = lo + (int)((long)(hi - lo) * value / 180L);
    }
    writeMicroseconds(value);
}

int NanoServo::read(void) const
{
    int lo = _sccp ? _min : MIN_PULSE_WIDTH;
    int hi = _sccp ? _max : MAX_PULSE_WIDTH;
    if (hi <= lo) return 90;
    long a = ((long)(_us - lo) * 180L + (hi - lo) / 2) / (hi - lo);
    return a < 0 ? 0 : a > 180 ? 180 : (int)a;
}
