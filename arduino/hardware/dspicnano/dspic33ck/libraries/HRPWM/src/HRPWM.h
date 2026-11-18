/*
 * HRPWM.h - High-Resolution PWM library for dsPIC33CK
 *
 * Provides access to PWM Generators PG1-PG8 with:
 *   - 250ps edge resolution (high-resolution mode)
 *   - 16-bit duty cycle and period
 *   - Complementary/Independent/Push-Pull output modes
 *   - Hardware dead-time (independent rise/fall)
 *   - ADC trigger synchronization
 *   - Hardware fault protection
 *
 * Device support: this library requires a part with the high-resolution PWM
 * option, i.e. an Auxiliary PLL (ACLKCON1/APLLFBD1/APLLDIV1) feeding the PWM
 * master clock and a PCLKCON.HRRDY status bit. The MP family (e.g. 33CK32MP102,
 * 33CK256MP508) has both; the MC Value Line parts (e.g. 33CK256MC002,
 * 33CK256MC005) have neither register — their PWM generators are
 * standard-resolution only and PCLKCON carries just MCLKSEL/DIVSEL/LOCK.
 * Including this header on such a part is a hard error (see HRPWM_SUPPORTED
 * below); use analogWrite() for standard-resolution PWM instead.
 *
 * Pin mapping (dsPIC33CK256MP508, 80-pin):
 *   PG1: PWM1H=RB14(D19), PWM1L=RB15(D20)
 *   PG2: PWM2H=RB12(D17), PWM2L=RB13(D18)
 *   PG3: PWM3H=RB10(D15), PWM3L=RB11(D16)
 *   PG4: PPS routable (any RP pin)
 *   PG5: PWM5H=RC0(D21),  PWM5L=RC1(D22)
 *   PG6: PWM6H=RC2(D23),  PWM6L=RC3(D24)
 *   PG7: PWM7H=RC4(D25),  PWM7L=RC5(D26)
 *   PG8: PWM8H=RC6(D27),  PWM8L=RC7(D28)
 */

#ifndef HRPWM_H
#define HRPWM_H

#include <stdint.h>
#include <xc.h>     /* needed before HRPWM_SUPPORTED / HRPWM_CH_MAX: the
                     * ACLKCON1 and PGxCONL self-#defines in the device header
                     * are what tell us which peripherals this part has */

/* Does this device have the high-resolution PWM hardware at all?
 * ACLKCON1 is the Auxiliary PLL control register; on every dsPIC33CK checked,
 * its presence coincides exactly with PCLKCON.HRRDY, so one test covers both
 * the 500 MHz AFPLLO clock source and the high-resolution-ready status. */
#if defined(ACLKCON1) && defined(PG1CONL)
#define HRPWM_SUPPORTED         1
#else
#define HRPWM_SUPPORTED         0
#endif

#if !HRPWM_SUPPORTED
#error "HRPWM: the selected dsPIC33CK has no high-resolution PWM (no Auxiliary PLL / no PCLKCON.HRRDY). Use analogWrite() for standard-resolution PWM, or select an MP-family device such as dsPIC33CK256MP508."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Output modes (PGxIOCONH.PMOD) */
#define HRPWM_COMPLEMENTARY     0   /* PWMxH and PWMxL are complementary */
#define HRPWM_INDEPENDENT       1   /* PWMxH and PWMxL are independent */
#define HRPWM_PUSH_PULL         2   /* Push-pull (alternating cycles) */

/* ADC trigger source selection */
#define HRPWM_TRIGA             0   /* Trigger A compare */
#define HRPWM_TRIGB             1   /* Trigger B compare */
#define HRPWM_TRIGC             2   /* Trigger C compare */

/* Fault output action (what outputs do during fault) */
#define HRPWM_FAULT_LOW         0   /* Both outputs driven LOW */
#define HRPWM_FAULT_HIGH        3   /* Both outputs driven HIGH */
#define HRPWM_FAULT_TRISTATE    0   /* Outputs tri-stated (same as LOW with PENH/PENL cleared) */

/* PWM alignment modes */
#define HRPWM_EDGE_ALIGNED      0   /* Standard edge-aligned (MODSEL=0b000) */
#define HRPWM_CENTER_ALIGNED    4   /* Center-aligned (MODSEL=0b100) */

/* Channel range — not every dsPIC33CK has eight PWM generators.
 * MP508 has PG1-PG8; the 48-pin Value Line parts (e.g. 33CK256MC005 on the
 * EV08P02A Curiosity Nano) have only PG1-PG4. */
#define HRPWM_CH_MIN            1
#if   defined(PG8CONL)
#define HRPWM_CH_MAX            8
#elif defined(PG7CONL)
#define HRPWM_CH_MAX            7
#elif defined(PG6CONL)
#define HRPWM_CH_MAX            6
#elif defined(PG5CONL)
#define HRPWM_CH_MAX            5
#elif defined(PG4CONL)
#define HRPWM_CH_MAX            4
#elif defined(PG3CONL)
#define HRPWM_CH_MAX            3
#elif defined(PG2CONL)
#define HRPWM_CH_MAX            2
#else
#define HRPWM_CH_MAX            1
#endif

/* Library API struct (dot-notation via function pointers) */
typedef struct {
    /* Core control */
    void (*begin)(uint8_t channel, uint32_t freq_hz);
    void (*stop)(uint8_t channel);
    void (*duty)(uint8_t channel, uint16_t val16);
    void (*dutyRaw)(uint8_t channel, uint16_t raw_dc);
    uint16_t (*getPeriod)(uint8_t channel);

    /* Configuration */
    void (*setMode)(uint8_t channel, uint8_t mode);
    void (*setAlignment)(uint8_t channel, uint8_t alignment);
    void (*setDeadTime)(uint8_t channel, uint16_t rise_ns, uint16_t fall_ns);
    void (*setPhase)(uint8_t channel, uint16_t phase_ticks);

    /* ADC trigger */
    void (*setADCTrigger)(uint8_t channel, uint8_t trig_source, uint16_t compare_val);

    /* Fault protection */
    void (*enableFault)(uint8_t channel, uint8_t fault_action);
    void (*clearFault)(uint8_t channel);
    uint8_t (*isFaulted)(uint8_t channel);

    /* Status */
    uint8_t (*isRunning)(uint8_t channel);
} HRPWMClass_t;

extern HRPWMClass_t HRPWM;

#ifdef __cplusplus
}
#endif

#endif /* HRPWM_H */
