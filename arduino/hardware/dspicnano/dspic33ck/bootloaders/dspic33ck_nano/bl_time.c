/*
 * bl_time.c - Timer1 as a free-running tick source. See bl_time.h.
 */

#include <xc.h>
#include "bl_time.h"

void bl_time_init(void)
{
    T1CON = 0x0000;
    TMR1  = 0;
    PR1   = 0xFFFF;             /* free-running: wrap, never match */
    T1CONbits.TCKPS = 0b11;     /* 1:256 -> 15625 Hz from FCY = 4 MHz */
    T1CONbits.TCS   = 0;        /* internal clock */
    IFS0bits.T1IF   = 0;
    T1CONbits.TON   = 1;
}

void bl_time_deinit(void)
{
    T1CONbits.TON = 0;
    T1CON = 0x0000;
    TMR1  = 0;
    PR1   = 0xFFFF;             /* the reset default, so the sketch starts clean */
    IFS0bits.T1IF = 0;
}

uint16_t bl_ticks(void)
{
    return TMR1;
}
