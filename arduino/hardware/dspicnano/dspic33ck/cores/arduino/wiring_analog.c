/*
 * wiring_analog.c - Analog I/O for dsPIC33CK Arduino core
 *
 * Implements: analogRead (12-bit ADC), analogWrite (PWM via SCCP)
 *
 * ADC: Uses dedicated ADC core or shared ADC on dsPIC33CK
 *      12-bit resolution, returns 0-4095 (scaled to 0-1023 for Arduino compat)
 *
 * PWM: Uses SCCP1-SCCP4 modules for PWM output on D5-D8
 */

#include "Arduino.h"
#include "wiring_private.h"

#ifdef __cplusplus
extern "C" {
#endif

static uint8_t _analog_reference = DEFAULT;
static uint8_t _adc_initialized = 0;

static void _adc_init(void)
{
    if (_adc_initialized) return;

    PMD1bits.ADC1MD = 0;            /* Enable ADC module clock */

    ADCON1Lbits.ADON = 0;           /* Turn off ADC during config */

    ADCON1Hbits.SHRRES = 0b11;      /* 12-bit resolution */
    ADCON1Hbits.FORM = 0;           /* Integer format */

    ADCON2Lbits.SHRADCS = 0x01;     /* Shared core TAD = 2*Tcy */
    ADCON2Hbits.SHRSAMC = 15;       /* Sample time: 15 TAD */

    ADCON3Hbits.SHREN = 1;          /* Enable shared ADC core */
    ADCON3Hbits.CLKSEL = 0b01;      /* Clock source: FOSC */
#if (F_CPU > 100000000UL)
    /* FOSC = 200 MHz at 100 MIPS. Undivided, TAD = 2/200 MHz = 10 ns, faster
     * than the shared core allows (the MPLAB simulator warns "Tad time is less
     * than 14.3 ns", i.e. > 70 MHz). Divide by 4: 50 MHz, TAD = 40 ns. */
    ADCON3Hbits.CLKDIV = 3;         /* CLKDIV field 3 = divide by 4 */
#else
    ADCON3Hbits.CLKDIV = 0;         /* 8 MHz FRC: TAD = 250 ns, no divider needed */
#endif

    ADCON5Hbits.WARMTIME = 0b1111;  /* Max warm-up time */

    /* Power-up sequence: ADON first, then power shared core */
    ADCON1Lbits.ADON = 1;

    ADCON5Lbits.SHRPWR = 1;
    while (ADCON5Lbits.SHRRDY == 0);

    _adc_initialized = 1;
}

void analogReference(uint8_t mode)
{
    _analog_reference = mode;
}

int analogRead(uint8_t pin)
{
    int8_t channel;

    /* Resolve pin to ADC channel */
    if (pin >= NUM_DIGITAL_PINS) return 0;
    channel = g_pin_map[pin].adc_channel;
    if (channel < 0) return 0;

    /* Ensure pin is in analog mode */
    if (g_pin_map[pin].ansel_reg != NULL) {
        *(g_pin_map[pin].ansel_reg) |= (1U << g_pin_map[pin].bit);
    }
    /* Set as input */
    *(g_pin_map[pin].tris_reg) |= (1U << g_pin_map[pin].bit);

    _adc_init();

    /* Clear ANxRDY flag before triggering */
    if (channel >= 16) {
        ADSTATH = (1U << (channel - 16));
    } else {
        ADSTATL = (1U << channel);
    }

    /* Trigger conversion on selected channel */
    ADCON3Lbits.CNVCHSEL = channel;
    ADCON3Lbits.CNVRTCH = 1;

    /* Wait for ANxRDY (conversion complete) */
    volatile uint32_t timeout = 100000UL;
    if (channel >= 16) {
        while (!(ADSTATH & (1U << (channel - 16))) && --timeout);
    } else {
        while (!(ADSTATL & (1U << channel)) && --timeout);
    }

    /* Read result from contiguous ADCBUF array */
    volatile uint16_t *adc_buf = &ADCBUF0;
    uint16_t result = adc_buf[channel];

    /* Scale 12-bit (0-4095) to 10-bit (0-1023) for Arduino compatibility */
    return (int)(result >> 2);
}

static void _pps_out_set(uint16_t rp_num, uint8_t func)
{
    volatile uint16_t *rpor_base = &RPOR0;
    uint8_t idx;
    uint8_t odd;

    if (rp_num >= 176) {
        /* PORTE remappable pins: RP176-RP181 → RPOR24-RPOR26 */
        idx = 24 + (rp_num - 176) / 2;
        odd = (rp_num - 176) % 2;
    } else {
        /* PORTB/C/D remappable pins: RP32-RP79 → RPOR0-RPOR23 */
        idx = (rp_num - 32) / 2;
        odd = (rp_num - 32) % 2;
    }

    if (odd == 0) {
        rpor_base[idx] = (rpor_base[idx] & 0xFF00) | (func & 0x3F);
    } else {
        rpor_base[idx] = (rpor_base[idx] & 0x00FF) | ((uint16_t)(func & 0x3F) << 8);
    }
}

static void _pps_out_clear(uint16_t rp_num)
{
    _pps_out_set(rp_num, 0);
}

/*
 * PWM frequency target: ~490 Hz (Arduino standard).
 * SCCP prescaler options: 1:1 (00), 1:4 (01), 1:16 (10), 1:64 (11)
 * Period register is 16-bit (max 65535).
 *
 * Formula: PWM_PERIOD = FCY / (PRESCALER * TARGET_FREQ) - 1
 *
 * At FCY=100MHz, prescaler 1:64: period = 100e6/(64*490)-1 = 3188  -> 490.5 Hz
 * At FCY=4MHz,   prescaler 1:16: period = 4e6/(16*490)-1   = 509   -> 490.7 Hz
 */
#if (FCY > 50000000UL)
    #define PWM_PRESCALER       0b11        /* 1:64 */
    #define PWM_PRESCALER_VAL   64UL
#elif (FCY > 10000000UL)
    #define PWM_PRESCALER       0b10        /* 1:16 */
    #define PWM_PRESCALER_VAL   16UL
#elif (FCY > 2000000UL)
    #define PWM_PRESCALER       0b01        /* 1:4 */
    #define PWM_PRESCALER_VAL   4UL
#else
    #define PWM_PRESCALER       0b00        /* 1:1 */
    #define PWM_PRESCALER_VAL   1UL
#endif

#define PWM_TARGET_FREQ     490UL
#define PWM_PERIOD          ((uint16_t)(FCY / (PWM_PRESCALER_VAL * PWM_TARGET_FREQ) - 1))

static uint8_t _pwm_initialized = 0;
static uint8_t _pwm_active = 0;    /* bitmask: bit0=CCP1, bit1=CCP2, etc. */

/*
 * Clear the Peripheral Module Disable bit for every SCCP the device has.
 *
 * A module whose PMD bit is set has no clock, and writes to its registers are
 * silently dropped -- no fault, no trap, the value simply does not land. So
 * this has to run before the first CCPx write and not one instruction later.
 *
 * Declared in wiring_private.h because tone() borrows SCCP4 and needs the same
 * gate opened; it is idempotent, so both callers can just call it.
 */
void _sccp_pmd_enable(void)
{
    if (_pwm_initialized) return;

    PMD2bits.CCP1MD = 0;
    PMD2bits.CCP2MD = 0;
    PMD2bits.CCP3MD = 0;
    PMD2bits.CCP4MD = 0;
#ifdef PWM5_PIN
    PMD2bits.CCP5MD = 0;
#endif
    _pwm_initialized = 1;
}

/*
 * Stop PWM on one pin and give the pin back to the GPIO: CCPON off, then the
 * PPS output mapping torn down so the module no longer drives the pad.
 *
 * A no-op for a pin with no PWM running, and for a pin that is not a PWM pin at
 * all, which is what lets the callers invoke it unconditionally.
 *
 * This used to be two byte-identical switch statements inlined in
 * analogWrite() -- one in the val<=0 path, one in the val>=255 path. They were
 * a divergence bug waiting to happen: any fix applied to one copy and not the
 * other would produce a PWM that tears down correctly at duty 0 but not at duty
 * 255, or vice versa. tone() needs a third caller, which settled it.
 */
void _pwm_disable_pin(uint8_t pin)
{
    switch (pin) {
        case PWM1_PIN:
            if (_pwm_active & 0x01) {
                CCP1CON1Lbits.CCPON = 0;
                __builtin_write_RPCON(0x0000);
                _pps_out_clear(PWM1_RP);
                __builtin_write_RPCON(0x0800);
                _pwm_active &= ~0x01;
            }
            break;
        case PWM2_PIN:
            if (_pwm_active & 0x02) {
                CCP2CON1Lbits.CCPON = 0;
                __builtin_write_RPCON(0x0000);
                _pps_out_clear(PWM2_RP);
                __builtin_write_RPCON(0x0800);
                _pwm_active &= ~0x02;
            }
            break;
        case PWM3_PIN:
            if (_pwm_active & 0x04) {
                CCP3CON1Lbits.CCPON = 0;
                __builtin_write_RPCON(0x0000);
                _pps_out_clear(PWM3_RP);
                __builtin_write_RPCON(0x0800);
                _pwm_active &= ~0x04;
            }
            break;
        case PWM4_PIN:
            if (_pwm_active & 0x08) {
                CCP4CON1Lbits.CCPON = 0;
                __builtin_write_RPCON(0x0000);
                _pps_out_clear(PWM4_RP);
                __builtin_write_RPCON(0x0800);
                _pwm_active &= ~0x08;
            }
            break;
#ifdef PWM5_PIN
        case PWM5_PIN:
            if (_pwm_active & 0x10) {
                CCP5CON1Lbits.CCPON = 0;
                __builtin_write_RPCON(0x0000);
                _pps_out_clear(PWM5_RP);
                __builtin_write_RPCON(0x0800);
                _pwm_active &= ~0x10;
            }
            break;
#endif
        default:
            break;
    }
}

void analogWrite(uint8_t pin, int val)
{
    /* The mid-range path below writes through g_pin_map[pin].ansel_reg and
     * .tris_reg without checking them, so an out-of-range pin would scribble
     * through whatever pointers happen to follow the table. (The val<=0 and
     * val>=255 paths only ever got away with it because they end in
     * digitalWrite(), which does bounds-check.) */
    if (pin >= NUM_DIGITAL_PINS) return;

    /* Hand SCCP4 back before we look at anything else. This has to come before
     * the val<=0 test: with a tone playing on D8, analogWrite(8, 0) would find
     * _pwm_active & 0x08 clear, skip the teardown entirely, and digitalWrite()
     * the pin LOW while the tone ISR was still toggling it. See
     * wiring_private.h for the policy this implements. */
    _tone_release();

    if (val <= 0) {
        _pwm_disable_pin(pin);
        digitalWrite(pin, LOW);
        return;
    }
    if (val >= 255) {
        _pwm_disable_pin(pin);
        digitalWrite(pin, HIGH);
        return;
    }

    /* Scale 0-255 duty to 0-PWM_PERIOD */
    uint16_t duty = (uint16_t)((uint32_t)val * PWM_PERIOD / 255);

    _sccp_pmd_enable();

    /* Set pin as output, clear analog mode */
    if (g_pin_map[pin].ansel_reg != NULL) {
        *(g_pin_map[pin].ansel_reg) &= ~(1U << g_pin_map[pin].bit);
    }
    *(g_pin_map[pin].tris_reg) &= ~(1U << g_pin_map[pin].bit);

    switch (pin) {
        case PWM1_PIN:
            if (_pwm_active & 0x01) {
                /* Already running — just update duty (glitch-free) */
                CCP1RB = duty;
            } else {
                __builtin_write_RPCON(0x0000);
                _pps_out_set(PWM1_RP, PPS_OUT_OCM1);
                __builtin_write_RPCON(0x0800);

                CCP1CON1Lbits.CCPON = 0;
                CCP1CON1Lbits.MOD = 0b0101;
                CCP1CON1Lbits.TMRPS = PWM_PRESCALER;
                CCP1CON1Lbits.CLKSEL = 0b000;
                CCP1CON1Hbits.OPSRC = 0b00;
                CCP1CON2Hbits.OCAEN = 1;
                CCP1PRL = PWM_PERIOD;
                CCP1RA = 0;
                CCP1RB = duty;
                CCP1CON1Lbits.CCPON = 1;
                _pwm_active |= 0x01;
            }
            break;

        case PWM2_PIN:
            if (_pwm_active & 0x02) {
                CCP2RB = duty;
            } else {
                __builtin_write_RPCON(0x0000);
                _pps_out_set(PWM2_RP, PPS_OUT_OCM2);
                __builtin_write_RPCON(0x0800);

                CCP2CON1Lbits.CCPON = 0;
                CCP2CON1Lbits.MOD = 0b0101;
                CCP2CON1Lbits.TMRPS = PWM_PRESCALER;
                CCP2CON1Lbits.CLKSEL = 0b000;
                CCP2CON1Hbits.OPSRC = 0b00;
                CCP2CON2Hbits.OCAEN = 1;
                CCP2PRL = PWM_PERIOD;
                CCP2RA = 0;
                CCP2RB = duty;
                CCP2CON1Lbits.CCPON = 1;
                _pwm_active |= 0x02;
            }
            break;

        case PWM3_PIN:
            if (_pwm_active & 0x04) {
                CCP3RB = duty;
            } else {
                __builtin_write_RPCON(0x0000);
                _pps_out_set(PWM3_RP, PPS_OUT_OCM3);
                __builtin_write_RPCON(0x0800);

                CCP3CON1Lbits.CCPON = 0;
                CCP3CON1Lbits.MOD = 0b0101;
                CCP3CON1Lbits.TMRPS = PWM_PRESCALER;
                CCP3CON1Lbits.CLKSEL = 0b000;
                CCP3CON1Hbits.OPSRC = 0b00;
                CCP3CON2Hbits.OCAEN = 1;
                CCP3PRL = PWM_PERIOD;
                CCP3RA = 0;
                CCP3RB = duty;
                CCP3CON1Lbits.CCPON = 1;
                _pwm_active |= 0x04;
            }
            break;

        case PWM4_PIN:
            if (_pwm_active & 0x08) {
                CCP4RB = duty;
            } else {
                __builtin_write_RPCON(0x0000);
                _pps_out_set(PWM4_RP, PPS_OUT_OCM4);
                __builtin_write_RPCON(0x0800);

                CCP4CON1Lbits.CCPON = 0;
                CCP4CON1Lbits.MOD = 0b0101;
                CCP4CON1Lbits.TMRPS = PWM_PRESCALER;
                CCP4CON1Lbits.CLKSEL = 0b000;
                CCP4CON1Hbits.OPSRC = 0b00;
                CCP4CON2Hbits.OCAEN = 1;
                CCP4PRL = PWM_PERIOD;
                CCP4RA = 0;
                CCP4RB = duty;
                CCP4CON1Lbits.CCPON = 1;
                _pwm_active |= 0x08;
            }
            break;

#ifdef PWM5_PIN
        case PWM5_PIN:
            if (_pwm_active & 0x10) {
                CCP5RB = duty;
            } else {
                __builtin_write_RPCON(0x0000);
                _pps_out_set(PWM5_RP, PPS_OUT_OCM5);
                __builtin_write_RPCON(0x0800);

                CCP5CON1Lbits.CCPON = 0;
                CCP5CON1Lbits.MOD = 0b0101;
                CCP5CON1Lbits.TMRPS = PWM_PRESCALER;
                CCP5CON1Lbits.CLKSEL = 0b000;
                CCP5CON1Hbits.OPSRC = 0b00;
                CCP5CON2Hbits.OCAEN = 1;
                CCP5PRL = PWM_PERIOD;
                CCP5RA = 0;
                CCP5RB = duty;
                CCP5CON1Lbits.CCPON = 1;
                _pwm_active |= 0x10;
            }
            break;
#endif

        default:
            digitalWrite(pin, (val >= 128) ? HIGH : LOW);
            break;
    }
}

#ifdef __cplusplus
}
#endif
