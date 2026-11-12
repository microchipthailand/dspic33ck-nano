/*
 * system_config.c - dsPIC33CK system clock and peripheral initialization
 *
 * Configures:
 *   - Clock source (FRC at 8 MHz default, or PLL for higher speeds)
 *   - GPIO port initialization
 *   - Unlock sequence for PPS
 */

#include "system_config.h"
#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Configuration Bits - device-specific
 * ============================================================ */

/* Common oscillator / clock config (all dsPIC33CK devices) */
#pragma config FNOSC = FRC      /* Fast RC Oscillator (FRC) */
#pragma config IESO = OFF       /* Two-speed start-up disabled */
#pragma config POSCMD = NONE    /* Primary oscillator disabled */
#pragma config FCKSM = CSECMD   /* Clock switching enabled, Fail-safe disabled */
#pragma config JTAGEN = OFF     /* JTAG disabled */
#pragma config FWDTEN = ON_SW   /* WDT controlled by software (off at startup) */

#if defined(__dsPIC33CK256MP508__)
/* dsPIC33CK256MP508 - 80-pin (DM330030 Curiosity Board)
 * Boot config (BTMODE, BSEN, BSS, etc.) left at erased defaults:
 * all-1s = SINGLE boot, no protection — avoids 0x400C00 FBOOT issue */
#pragma config OSCIOFNC = OFF   /* OSC2 is clock output */
#pragma config PLLKEN = ON      /* PLL lock used to gate output */
#pragma config RWDTPS = PS1     /* WDT prescaler (WDT off via FWDTEN) */
#pragma config RCLKSEL = LPRC   /* WDT clock source */
#pragma config WINDIS = OFF     /* WDT window mode off */
#pragma config SWDTPS = PS1     /* Sleep WDT prescaler */
#pragma config ICS = PGD3       /* PKOB4 on DM330030 uses PGC3/PGD3 */
#pragma config DMTDIS = ON      /* Dead Man Timer disabled */

#elif defined(__dsPIC33CK256MC005__)
/* dsPIC33CK256MC005 - 48-pin (EV08P02A Curiosity Nano)
 * Cross-checked against Microchip's Out-of-Box Demo config_bits.c for this kit.
 *
 * ICS MUST be PGD3 here. The on-board nEDBG is wired to PGC3/PGD3 (RB6/RB5),
 * NOT to PGC1/PGD1 like the 28-pin boards, so the shared 28-pin `#else` branch
 * below would point the debug channel at pins nothing is connected to and cost
 * debug access on this board. */
#pragma config OSCIOFNC = ON    /* OSC2 pin is digital I/O */
#pragma config RWDTPS = PS32768 /* WDT period */
#pragma config WINDIS = ON      /* WDT window disabled (standard mode) */
#pragma config ICS = PGD3       /* nEDBG on EV08P02A uses PGC3/PGD3 */

#else
/* dsPIC33CK32MP102 / dsPIC33CK256MC002 - 28-pin devices */
#pragma config OSCIOFNC = ON    /* OSC2 pin is digital I/O */
#pragma config RWDTPS = PS32768 /* WDT period */
#pragma config WINDIS = ON      /* WDT window disabled (standard mode) */
#pragma config ICS = PGD1       /* Program/debug on PGC1/PGD1 */
#endif

void system_config_init(void)
{
    /* Configure oscillator */

#if (F_CPU == 8000000UL)
    /* Use internal FRC directly at 8 MHz */
    /* FNOSC is already set to FRC via config bits */
    CLKDIVbits.FRCDIV = 0b000;   /* FRC divide by 1 -> 8 MHz */

#elif (F_CPU == 200000000UL)
    /* Configure PLL for 200 MHz FOSC from 8 MHz FRC
     * FVCO = (FIN / N1) * M = (8 / 2) * 100 = 400 MHz
     * FOSC = FVCO / (N2 * N3) = 400 / (2 * 1) = 200 MHz
     * FCY = FOSC / 2 = 100 MHz
     */
    CLKDIVbits.PLLPRE = 0b001;    /* N1 = 2 */
    PLLFBDbits.PLLFBDIV = 100;    /* M = 100 */
    PLLDIVbits.POST1DIV = 2;      /* N2 = 2 */
    PLLDIVbits.POST2DIV = 1;      /* N3 = 1 */

    /* Initiate clock switch to PLL */
    __builtin_write_OSCCONH(0x01); /* NOSC = FRCPLL */
    __builtin_write_OSCCONL(OSCCON | 0x01); /* Request switch */

    /* Wait for clock switch and PLL lock */
    while (OSCCONbits.COSC != 0b001);
    while (OSCCONbits.LOCK != 1);

#endif

    /* Disable all analog functions by default (make pins digital) */
    ANSELA = 0x0000;
    ANSELB = 0x0000;
#ifdef ANSELC
    ANSELC = 0x0000;
#endif
#ifdef ANSELD
    ANSELD = 0x0000;
#endif
#ifdef ANSELE
    ANSELE = 0x0000;
#endif

    /* All pins start as inputs */
    TRISA = 0xFFFF;
    TRISB = 0xFFFF;
#ifdef TRISC
    TRISC = 0xFFFF;
#endif
#ifdef TRISD
    TRISD = 0xFFFF;
#endif
#ifdef TRISE
    TRISE = 0xFFFF;
#endif

    /* Clear all port latches */
    LATA = 0x0000;
    LATB = 0x0000;
#ifdef LATC
    LATC = 0x0000;
#endif
#ifdef LATD
    LATD = 0x0000;
#endif
#ifdef LATE
    LATE = 0x0000;
#endif
}

#ifdef __cplusplus
}
#endif
