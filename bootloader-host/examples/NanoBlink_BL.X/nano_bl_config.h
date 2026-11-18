/*
 * nano_bl_config.h - configuration words of the dsPIC33CK Nano serial bootloader.
 *
 * The bootloader owns the config words: they are programmed once with the bootloader and are NEVER written over
 * serial (they share a flash page with nothing the bootloader may erase). Include this file in exactly ONE C file of
 * your application so that:
 *   - your HEX states the same values the board really has (NanoBootloader then reports no config warning), and
 *   - a debug session with a PICkit programs the same values, so debugging and bootloading behave identically.
 *
 * Important consequence: the device always starts on FRC (8 MHz, FCY = 4 MHz). Switch to the PLL at run time
 * (clock switching is enabled: FCKSM = CSECMD) - MCC's clock initialisation does exactly that. The watchdog is
 * software controlled (FWDTEN = ON_SW), off until your code enables it.
 *
 * Source: bootloaders/dspic33ck_nano/bl_main.c. Resulting words: FOSCSEL 0xFFFF78, FOSC 0xFFFF7B, FWDT 0xFF7FEF,
 * FICD 0xFFFFDD; all other config words erased.
 */
#ifndef NANO_BL_CONFIG_H
#define NANO_BL_CONFIG_H

#pragma config FNOSC = FRC          /* Fast RC oscillator at reset */
#pragma config IESO = OFF           /* Two-speed start-up disabled */
#pragma config POSCMD = NONE        /* Primary oscillator disabled */
#pragma config FCKSM = CSECMD       /* Clock switching enabled, fail-safe clock monitor disabled */
#pragma config OSCIOFNC = ON        /* OSC2 pin is digital I/O */
#pragma config FWDTEN = ON_SW       /* WDT controlled by software (off at start-up) */
#pragma config RWDTPS = PS32768     /* WDT period */
#pragma config WINDIS = ON          /* WDT window disabled (standard mode) */
#pragma config ICS = PGD3           /* ICSP header J4 uses PGC3/PGD3 */
#pragma config JTAGEN = OFF         /* JTAG disabled */

#endif /* NANO_BL_CONFIG_H */
