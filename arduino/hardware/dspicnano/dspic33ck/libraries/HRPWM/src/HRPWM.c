/*
 * HRPWM.c - High-Resolution PWM implementation for dsPIC33CK
 */

#include "HRPWM.h"
#include <xc.h>

/* HRPWM.h already emits a clear #error on parts with no high-resolution PWM.
 * Skip the body there so that #error is the only diagnostic, instead of being
 * buried under a cascade of undeclared-register errors. */
#if HRPWM_SUPPORTED

#ifndef FCY
#define FCY (F_CPU / 2)
#endif

/* PWM master clock = AFPLLO = 500 MHz (from Auxiliary PLL) */
#define HRPWM_MCLK_HZ  500000000UL

/* Register pointer table for PG1-PGn.
 * Entries beyond the generators this device implements are compiled out —
 * HRPWM_CH_MAX (HRPWM.h) is derived from the same PGxCONL self-#defines. */
typedef struct {
    volatile uint16_t *CONL;
    volatile uint16_t *CONH;
    volatile uint16_t *STAT;
    volatile uint16_t *IOCONL;
    volatile uint16_t *IOCONH;
    volatile uint16_t *EVTL;
    volatile uint16_t *EVTH;
    volatile uint16_t *DC;
    volatile uint16_t *DCA;
    volatile uint16_t *PER;
    volatile uint16_t *PHASE;
    volatile uint16_t *DTL;
    volatile uint16_t *DTH;
    volatile uint16_t *TRIGA;
    volatile uint16_t *TRIGB;
    volatile uint16_t *TRIGC;
    volatile uint16_t *FPCIL;
    volatile uint16_t *FPCIH;
} hrpwm_regs_t;

static const hrpwm_regs_t _pg[HRPWM_CH_MAX] = {
#if HRPWM_CH_MAX >= 1
    { &PG1CONL, &PG1CONH, &PG1STAT, &PG1IOCONL, &PG1IOCONH, &PG1EVTL, &PG1EVTH,
      &PG1DC, &PG1DCA, &PG1PER, &PG1PHASE, &PG1DTL, &PG1DTH,
      &PG1TRIGA, &PG1TRIGB, &PG1TRIGC, &PG1FPCIL, &PG1FPCIH },
#endif
#if HRPWM_CH_MAX >= 2
    { &PG2CONL, &PG2CONH, &PG2STAT, &PG2IOCONL, &PG2IOCONH, &PG2EVTL, &PG2EVTH,
      &PG2DC, &PG2DCA, &PG2PER, &PG2PHASE, &PG2DTL, &PG2DTH,
      &PG2TRIGA, &PG2TRIGB, &PG2TRIGC, &PG2FPCIL, &PG2FPCIH },
#endif
#if HRPWM_CH_MAX >= 3
    { &PG3CONL, &PG3CONH, &PG3STAT, &PG3IOCONL, &PG3IOCONH, &PG3EVTL, &PG3EVTH,
      &PG3DC, &PG3DCA, &PG3PER, &PG3PHASE, &PG3DTL, &PG3DTH,
      &PG3TRIGA, &PG3TRIGB, &PG3TRIGC, &PG3FPCIL, &PG3FPCIH },
#endif
#if HRPWM_CH_MAX >= 4
    { &PG4CONL, &PG4CONH, &PG4STAT, &PG4IOCONL, &PG4IOCONH, &PG4EVTL, &PG4EVTH,
      &PG4DC, &PG4DCA, &PG4PER, &PG4PHASE, &PG4DTL, &PG4DTH,
      &PG4TRIGA, &PG4TRIGB, &PG4TRIGC, &PG4FPCIL, &PG4FPCIH },
#endif
#if HRPWM_CH_MAX >= 5
    { &PG5CONL, &PG5CONH, &PG5STAT, &PG5IOCONL, &PG5IOCONH, &PG5EVTL, &PG5EVTH,
      &PG5DC, &PG5DCA, &PG5PER, &PG5PHASE, &PG5DTL, &PG5DTH,
      &PG5TRIGA, &PG5TRIGB, &PG5TRIGC, &PG5FPCIL, &PG5FPCIH },
#endif
#if HRPWM_CH_MAX >= 6
    { &PG6CONL, &PG6CONH, &PG6STAT, &PG6IOCONL, &PG6IOCONH, &PG6EVTL, &PG6EVTH,
      &PG6DC, &PG6DCA, &PG6PER, &PG6PHASE, &PG6DTL, &PG6DTH,
      &PG6TRIGA, &PG6TRIGB, &PG6TRIGC, &PG6FPCIL, &PG6FPCIH },
#endif
#if HRPWM_CH_MAX >= 7
    { &PG7CONL, &PG7CONH, &PG7STAT, &PG7IOCONL, &PG7IOCONH, &PG7EVTL, &PG7EVTH,
      &PG7DC, &PG7DCA, &PG7PER, &PG7PHASE, &PG7DTL, &PG7DTH,
      &PG7TRIGA, &PG7TRIGB, &PG7TRIGC, &PG7FPCIL, &PG7FPCIH },
#endif
#if HRPWM_CH_MAX >= 8
    { &PG8CONL, &PG8CONH, &PG8STAT, &PG8IOCONL, &PG8IOCONH, &PG8EVTL, &PG8EVTH,
      &PG8DC, &PG8DCA, &PG8PER, &PG8PHASE, &PG8DTL, &PG8DTH,
      &PG8TRIGA, &PG8TRIGB, &PG8TRIGC, &PG8FPCIL, &PG8FPCIH },
#endif
};

/* CONL bit positions */
#define CONL_ON         (1u << 15)
#define CONL_HREN       (1u << 7)
#define CONL_CLKSEL_MCLK (0b01u << 3)  /* MCLK from PCLKCON */
#define CONL_MODSEL_EDGE   0b000        /* Edge-aligned */
#define CONL_MODSEL_CENTER 0b100        /* Center-aligned */

/* CONH bit positions */
#define CONH_SOCS_SELF  (0b0001u)       /* Self-triggered */
#define CONH_UPDMOD_IMM (0b001u << 8)   /* Immediate update */

/* IOCONH bit positions */
#define IOCONH_PENH     (1u << 3)
#define IOCONH_PENL     (1u << 2)
#define IOCONH_PMOD_SHIFT 4
#define IOCONH_PMOD_MASK (0x03u << 4)

/* STAT bit positions */
#define STAT_UPDREQ     (1u << 3)

/* FPCIL: PSS field selects fault source */
#define FPCIL_PSS_PCI1  0b00001         /* PCI source 1 */
#define FPCIL_TERM_AUTO (0b001u << 12)  /* Auto-terminate on source removal */

static uint8_t _module_enabled = 0;
static uint8_t _channel_running = 0;    /* bitmask: bit0=PG1 ... bit7=PG8 */

static inline uint8_t _ch_idx(uint8_t ch)
{
    if (ch < HRPWM_CH_MIN || ch > HRPWM_CH_MAX) return 0xFF;
    return ch - 1;
}

static void _apll_init(void)
{
    /*
     * Configure Auxiliary PLL for 500 MHz output:
     *   AFPLLO = FRC * APLLFBDIV / (APLLPRE * APOST1DIV * APOST2DIV)
     *          = 8 MHz * 125 / (1 * 2 * 1) = 500 MHz
     */
    ACLKCON1bits.FRCSEL = 1;        /* Source = FRC (8 MHz) */
    ACLKCON1bits.APLLPRE = 1;       /* Pre-divider N1 = 1 */
    APLLFBD1bits.APLLFBDIV = 125;   /* Feedback divider M = 125 */
    APLLDIV1bits.APOST1DIV = 2;     /* Post-divider N2 = 2 */
    APLLDIV1bits.APOST2DIV = 1;     /* Post-divider N3 = 1 */
    ACLKCON1bits.APLLEN = 1;        /* Enable Auxiliary PLL */

    /* Wait for APLL lock */
    volatile uint32_t timeout = 100000UL;
    while (!ACLKCON1bits.APLLCK && --timeout);
}

static void _module_init(void)
{
    if (_module_enabled) return;

    PMD1bits.PWMMD = 0;         /* Enable PWM module (clear PMD bit) */

    /* Start Auxiliary PLL at 500 MHz */
    _apll_init();

    /* Select AFPLLO (500 MHz) as PWM master clock, no divider */
    PCLKCONbits.MCLKSEL = 0b11; /* AFPLLO = Auxiliary PLL output */
    PCLKCONbits.DIVSEL = 0b00;  /* 1:1 */

    /* Wait for high-resolution ready */
    volatile uint32_t timeout = 100000UL;
    while (!PCLKCONbits.HRRDY && --timeout);

    _module_enabled = 1;
}

static void _hrpwm_begin(uint8_t channel, uint32_t freq_hz)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;
    if (freq_hz == 0) return;

    _module_init();

    const hrpwm_regs_t *pg = &_pg[idx];

    /* Turn off while configuring */
    *pg->CONL &= ~CONL_ON;

    /* Calculate period: PER = MCLK / freq_hz (MCLK = AFPLLO = 500 MHz) */
    uint16_t per = (uint16_t)(HRPWM_MCLK_HZ / freq_hz);
    if (per < 2) per = 2;

    *pg->PER = per;
    *pg->DC = per / 2;          /* Default 50% duty */
    *pg->PHASE = 0;
    *pg->DTL = 0;
    *pg->DTH = 0;

    /* CONL: center-aligned, MCLK, HR enabled */
    *pg->CONL = CONL_CLKSEL_MCLK | CONL_HREN | CONL_MODSEL_CENTER;

    /* CONH: self-triggered, immediate update */
    *pg->CONH = CONH_SOCS_SELF | CONH_UPDMOD_IMM;

    /* IOCONH: complementary mode, both outputs enabled */
    *pg->IOCONH = IOCONH_PENH | IOCONH_PENL;   /* PMOD=00 = complementary */

    /* IOCONL: default fault data = both low */
    *pg->IOCONL = 0x0000;

    /* EVTL: trigger from PGxPER match for ADC (default off) */
    *pg->EVTL = 0x0000;

    /* Turn on */
    *pg->CONL |= CONL_ON;

    _channel_running |= (1u << idx);
}

static void _hrpwm_stop(uint8_t channel)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];

    *pg->CONL &= ~CONL_ON;
    *pg->IOCONH &= ~(IOCONH_PENH | IOCONH_PENL);

    _channel_running &= ~(1u << idx);
}

static void _hrpwm_duty(uint8_t channel, uint16_t val16)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];
    uint16_t per = *pg->PER;

    /* Scale 0-65535 to 0-PER */
    uint16_t dc = (uint16_t)((uint32_t)val16 * per / 65535UL);
    *pg->DC = dc;
    *pg->STAT |= STAT_UPDREQ;
}

static void _hrpwm_dutyRaw(uint8_t channel, uint16_t raw_dc)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];
    *pg->DC = raw_dc;
    *pg->STAT |= STAT_UPDREQ;
}

static uint16_t _hrpwm_getPeriod(uint8_t channel)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return 0;
    return *_pg[idx].PER;
}

static void _hrpwm_setMode(uint8_t channel, uint8_t mode)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];
    uint16_t val = *pg->IOCONH;
    val &= ~IOCONH_PMOD_MASK;
    val |= ((uint16_t)(mode & 0x03) << IOCONH_PMOD_SHIFT);

    /* In complementary/push-pull, enable both; in independent, user may want only H */
    val |= IOCONH_PENH;
    if (mode != HRPWM_INDEPENDENT) {
        val |= IOCONH_PENL;
    }

    *pg->IOCONH = val;
    *pg->STAT |= STAT_UPDREQ;
}

static void _hrpwm_setAlignment(uint8_t channel, uint8_t alignment)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];
    uint16_t val = *pg->CONL;
    val &= ~0x07u;  /* Clear MODSEL[2:0] */

    if (alignment == HRPWM_CENTER_ALIGNED) {
        val |= CONL_MODSEL_CENTER;
    }
    /* else edge-aligned = 0b000 (already cleared) */

    *pg->CONL = val;
    *pg->STAT |= STAT_UPDREQ;
}

static void _hrpwm_setDeadTime(uint8_t channel, uint16_t rise_ns, uint16_t fall_ns)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];

    /*
     * Dead-time is clocked by MCLK (500 MHz = 2ns per tick).
     * DTH/DTL are 14-bit (max 16383 = ~32.7us at 500MHz).
     *
     * Formula: ticks = ns * MCLK_MHz / 1000 = ns * 500 / 1000 = ns / 2
     */
    uint16_t dth = (uint16_t)((uint32_t)rise_ns * (HRPWM_MCLK_HZ / 1000000UL) / 1000UL);
    uint16_t dtl = (uint16_t)((uint32_t)fall_ns * (HRPWM_MCLK_HZ / 1000000UL) / 1000UL);

    /* Clamp to 14-bit max */
    if (dth > 16383u) dth = 16383u;
    if (dtl > 16383u) dtl = 16383u;

    *pg->DTH = dth;
    *pg->DTL = dtl;
    *pg->STAT |= STAT_UPDREQ;
}

static void _hrpwm_setPhase(uint8_t channel, uint16_t phase_ticks)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    *_pg[idx].PHASE = phase_ticks;
    *_pg[idx].STAT |= STAT_UPDREQ;
}

static void _hrpwm_setADCTrigger(uint8_t channel, uint8_t trig_source, uint16_t compare_val)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];

    /* Write compare value to appropriate trigger register */
    switch (trig_source) {
        case HRPWM_TRIGA: *pg->TRIGA = compare_val; break;
        case HRPWM_TRIGB: *pg->TRIGB = compare_val; break;
        case HRPWM_TRIGC: *pg->TRIGC = compare_val; break;
        default: return;
    }

    /* Enable ADC trigger 1 from the selected source (ADTR1EN1/2/3 in EVTL) */
    uint16_t evtl = *pg->EVTL;
    evtl |= (1u << (8 + trig_source));  /* ADTR1EN1=bit8, EN2=bit9, EN3=bit10 */
    *pg->EVTL = evtl;

    *pg->STAT |= STAT_UPDREQ;
}

static void _hrpwm_enableFault(uint8_t channel, uint8_t fault_action)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];

    /* Set FLTDAT to desired output state during fault (bits [7:6]) */
    uint16_t ioconl = *pg->IOCONL;
    ioconl &= ~(0x03u << 6);                       /* Clear FLTDAT */
    ioconl |= ((uint16_t)(fault_action & 0x03) << 6);
    *pg->IOCONL = ioconl;

    /* Configure fault PCI: source = PCI1 (software-settable), auto-terminate */
    *pg->FPCIL = FPCIL_PSS_PCI1 | FPCIL_TERM_AUTO;
}

static void _hrpwm_clearFault(uint8_t channel)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return;

    const hrpwm_regs_t *pg = &_pg[idx];

    /* Clear fault event flag by writing FLTEVT and FLTACT in STAT */
    *pg->STAT &= ~((1u << 14) | (1u << 10));   /* FLTEVT=bit14, FLTACT=bit10 */

    /* Terminate fault via SWTERM in FPCIL */
    *pg->FPCIL |= (1u << 7);   /* SWTERM = bit 7 */
}

static uint8_t _hrpwm_isFaulted(uint8_t channel)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return 0;
    return (*_pg[idx].STAT & (1u << 10)) ? 1 : 0;  /* FLTACT = bit 10 */
}

static uint8_t _hrpwm_isRunning(uint8_t channel)
{
    uint8_t idx = _ch_idx(channel);
    if (idx == 0xFF) return 0;
    return (_channel_running & (1u << idx)) ? 1 : 0;
}

/* Global API struct */
HRPWMClass_t HRPWM = {
    .begin          = _hrpwm_begin,
    .stop           = _hrpwm_stop,
    .duty           = _hrpwm_duty,
    .dutyRaw        = _hrpwm_dutyRaw,
    .getPeriod      = _hrpwm_getPeriod,
    .setMode        = _hrpwm_setMode,
    .setAlignment   = _hrpwm_setAlignment,
    .setDeadTime    = _hrpwm_setDeadTime,
    .setPhase       = _hrpwm_setPhase,
    .setADCTrigger  = _hrpwm_setADCTrigger,
    .enableFault    = _hrpwm_enableFault,
    .clearFault     = _hrpwm_clearFault,
    .isFaulted      = _hrpwm_isFaulted,
    .isRunning      = _hrpwm_isRunning,
};

#endif /* HRPWM_SUPPORTED */
