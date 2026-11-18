/*
 * wiring.c - Timing functions for dsPIC33CK Arduino core
 *
 * Implements: millis, micros, delay, delayMicroseconds
 *
 * Uses Timer1 configured for 1ms interrupt to maintain a
 * millisecond tick counter.
 *
 * FCY = F_CPU / 2 (instruction cycle frequency)
 */

#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

static volatile unsigned long _millis_count = 0;
static volatile unsigned long _micros_overflow = 0;

/*
 * Timer1 ISR - fires every 1ms
 */
void __attribute__((interrupt, auto_psv)) _T1Interrupt(void)
{
    _millis_count++;
    IFS0bits.T1IF = 0;  /* Clear interrupt flag */
}

/*
 * Initialize Timer1 for 1ms tick
 * PR1 = (FCY / prescaler / 1000) - 1
 *
 * At FCY = 4 MHz (8 MHz FOSC / 2), prescaler 1:1:
 *   PR1 = (4000000 / 1 / 1000) - 1 = 3999
 *
 * At FCY = 50 MHz (100 MHz FOSC / 2), prescaler 1:8:
 *   PR1 = (50000000 / 8 / 1000) - 1 = 6249
 */
static void _timer1_init(void)
{
    T1CONbits.TON = 0;      /* Stop timer */
    T1CONbits.TCS = 0;      /* Internal clock (FCY) */
    T1CONbits.TGATE = 0;    /* Gated mode disabled */

#if (FCY <= 8000000UL)
    /* Use 1:1 prescaler for low clock speeds */
    T1CONbits.TCKPS = 0b00; /* 1:1 */
    PR1 = (FCY / 1000UL) - 1;
#elif (FCY <= 32000000UL)
    /* Use 1:8 prescaler */
    T1CONbits.TCKPS = 0b01; /* 1:8 */
    PR1 = (FCY / 8UL / 1000UL) - 1;
#else
    /* Use 1:64 prescaler for high clock speeds */
    T1CONbits.TCKPS = 0b10; /* 1:64 */
    PR1 = (FCY / 64UL / 1000UL) - 1;
#endif

    TMR1 = 0;              /* Clear timer */
    IPC0bits.T1IP = 4;    /* Priority level 4 */
    IFS0bits.T1IF = 0;    /* Clear flag */
    IEC0bits.T1IE = 1;    /* Enable interrupt */
    T1CONbits.TON = 1;    /* Start timer */
}

unsigned long millis(void)
{
    unsigned long m;
    /* Atomic read of 32-bit value */
    __builtin_disi(0x3FFF);  /* Disable interrupts */
    m = _millis_count;
    __builtin_disi(0x0000);  /* Re-enable interrupts */
    return m;
}

unsigned long micros(void)
{
    unsigned long m;
    uint16_t t;

    __builtin_disi(0x3FFF);
    m = _millis_count;
    t = TMR1;
    if (IFS0bits.T1IF && t < (PR1 / 2)) {
        m++;
    }
    __builtin_disi(0x0000);

    /* Convert timer count to microseconds */
#if (FCY <= 8000000UL)
    return (m * 1000UL) + (t / (FCY / 1000000UL));
#elif (FCY <= 32000000UL)
    return (m * 1000UL) + ((t * 8UL) / (FCY / 1000000UL));
#else
    return (m * 1000UL) + ((t * 64UL) / (FCY / 1000000UL));
#endif
}

void delay(unsigned long ms)
{
    unsigned long start = millis();
    while ((millis() - start) < ms) {
        /* Could add yield() here for cooperative multitasking */
    }
}

void delayMicroseconds(unsigned int us)
{
    /*
     * For short delays, use instruction cycle counting.
     * Each NOP is 1 instruction cycle = 1/FCY seconds.
     * Cycles per microsecond = FCY / 1000000
     */
    unsigned long cycles = (unsigned long)us * (FCY / 1000000UL);

    /* Subtract overhead (~10 cycles for function call) */
    if (cycles > 10) cycles -= 10;

    /* Burn cycles with NOPs (each iteration ~4 cycles) */
    while (cycles > 4) {
        __builtin_nop();
        cycles -= 4;
    }
}

long map(long value, long fromLow, long fromHigh, long toLow, long toHigh)
{
    return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}

/*
 * System initialization - called before setup()
 */
void init(void)
{
    /* Initialize timing (Timer1 for millis/micros) */
    _timer1_init();

    /* Global interrupt enable, the equivalent of sei() in the AVR core's init().
     * The device file (DSPIC33CK256MC005.PIC, INTCON2 por/mclr = 000----0----0000)
     * gives GIE a reset value of 0, and the MPLAB simulator agrees: without this
     * line Timer1 never interrupts there, millis() stays 0 and delay() hangs.
     * If the silicon really resets GIE to 1 this changes nothing. */
    INTCON2bits.GIE = 1;
}

#ifdef __cplusplus
}
#endif
