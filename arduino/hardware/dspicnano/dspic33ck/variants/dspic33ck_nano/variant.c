/*
 * variant.c - Pin map table for dsPIC33CK256MC005
 *
 * Maps Arduino pin numbers to physical dsPIC33CK port/bit/ADC channel.
 *
 * Source of truth for this table is the device pin database shipped in the
 * DFP: edc/DSPIC33CK256MC005.PIC (48-pin package, 39 bonded I/O). It was
 * cross-checked against Microchip's Curiosity Nano out-of-box demo, whose
 * MCC-generated pins.c writes exactly the analog-capable masks below:
 *   ANSELA = 0x001F  -> RA0-RA4
 *   ANSELB = 0x039F  -> RB0-RB4, RB7-RB9
 *   ANSELC = 0x00CF  -> RC0-RC3, RC6, RC7
 *   ANSELD = 0x2400  -> RD10 (AN18), RD13 (ANN0)
 *
 * Key differences from MC002/MP508:
 *   - PORTC stops at RC13 and PORTD is sparse (only RD1, RD8, RD10, RD13),
 *     so pin numbers above D34 do NOT follow the MP508 formula.
 *   - The ADC channel assignment is genuinely different from both other
 *     parts (e.g. RA1=AN16, RB4=AN17, RC6=AN19, RC7=AN7).
 *   - RD13 carries ANN0 (ADC negative input) so it has an ANSEL bit but no
 *     usable positive channel; adc_channel is -1 while ansel_reg is set so
 *     digitalRead()/pinMode() still clear analog mode correctly.
 */

#include <xc.h>
#include <stddef.h>
#include "pins_arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

const pin_map_t g_pin_map[NUM_DIGITAL_PINS] = {
    /* D0:  RA0  */ { &PORTA, &TRISA, &LATA, &ANSELA,  0,  0 },  /* AN0  */
    /* D1:  RA1  */ { &PORTA, &TRISA, &LATA, &ANSELA,  1, 16 },  /* AN16 */
    /* D2:  RA2  */ { &PORTA, &TRISA, &LATA, &ANSELA,  2,  9 },  /* AN9  */
    /* D3:  RA3  */ { &PORTA, &TRISA, &LATA, &ANSELA,  3,  3 },  /* AN3  */
    /* D4:  RA4  */ { &PORTA, &TRISA, &LATA, &ANSELA,  4,  4 },  /* AN4  */

    /* D5:  RB0  */ { &PORTB, &TRISB, &LATB, &ANSELB,  0,  5 },  /* AN5, PWM1  */
    /* D6:  RB1  */ { &PORTB, &TRISB, &LATB, &ANSELB,  1,  6 },  /* AN6, PWM2  */
    /* D7:  RB2  */ { &PORTB, &TRISB, &LATB, &ANSELB,  2,  1 },  /* AN1, PWM3  */
    /* D8:  RB3  */ { &PORTB, &TRISB, &LATB, &ANSELB,  3,  8 },  /* AN8, PWM4  */
    /* D9:  RB4  */ { &PORTB, &TRISB, &LATB, &ANSELB,  4, 17 },  /* AN17 */
    /* D10: RB5  */ { &PORTB, &TRISB, &LATB, NULL,     5, -1 },  /* PGD3 - ICSP J4-4 */
    /* D11: RB6  */ { &PORTB, &TRISB, &LATB, NULL,     6, -1 },  /* PGC3 - ICSP J4-5 */
    /* D12: RB7  */ { &PORTB, &TRISB, &LATB, &ANSELB,  7,  2 },  /* AN2  */
    /* D13: RB8  */ { &PORTB, &TRISB, &LATB, &ANSELB,  8, 10 },  /* AN10, SCL1 */
    /* D14: RB9  */ { &PORTB, &TRISB, &LATB, &ANSELB,  9, 11 },  /* AN11, SDA1 */
    /* D15: RB10 */ { &PORTB, &TRISB, &LATB, NULL,    10, -1 },  /* PWM3H */
    /* D16: RB11 */ { &PORTB, &TRISB, &LATB, NULL,    11, -1 },  /* PWM3L */
    /* D17: RB12 */ { &PORTB, &TRISB, &LATB, NULL,    12, -1 },  /* PWM2H */
    /* D18: RB13 */ { &PORTB, &TRISB, &LATB, NULL,    13, -1 },  /* PWM2L */
    /* D19: RB14 */ { &PORTB, &TRISB, &LATB, NULL,    14, -1 },  /* PWM1H */
    /* D20: RB15 */ { &PORTB, &TRISB, &LATB, NULL,    15, -1 },  /* PWM1L */

    /* D21: RC0  */ { &PORTC, &TRISC, &LATC, &ANSELC,  0, 12 },  /* AN12 */
    /* D22: RC1  */ { &PORTC, &TRISC, &LATC, &ANSELC,  1, 13 },  /* AN13 */
    /* D23: RC2  */ { &PORTC, &TRISC, &LATC, &ANSELC,  2, 14 },  /* AN14 */
    /* D24: RC3  */ { &PORTC, &TRISC, &LATC, &ANSELC,  3, 15 },  /* AN15 */
    /* D25: RC4  */ { &PORTC, &TRISC, &LATC, NULL,     4, -1 },  /* SPI SCK */
    /* D26: RC5  */ { &PORTC, &TRISC, &LATC, NULL,     5, -1 },  /* SPI MOSI */
    /* D27: RC6  */ { &PORTC, &TRISC, &LATC, &ANSELC,  6, 19 },  /* AN19 */
    /* D28: RC7  */ { &PORTC, &TRISC, &LATC, &ANSELC,  7,  7 },  /* AN7  */
    /* D29: RC8  */ { &PORTC, &TRISC, &LATC, NULL,     8, -1 },  /* SPI MISO (J3-14) */
    /* D30: RC9  */ { &PORTC, &TRISC, &LATC, NULL,     9, -1 },  /* SPI SS (J3-13) */
    /* D31: RC10 */ { &PORTC, &TRISC, &LATC, NULL,    10, -1 },  /* U1TX (J5-2) */
    /* D32: RC11 */ { &PORTC, &TRISC, &LATC, NULL,    11, -1 },  /* U1RX (J5-3) */
    /* D33: RC12 */ { &PORTC, &TRISC, &LATC, NULL,    12, -1 },  /* USER button SW2 (active low) */
    /* D34: RC13 */ { &PORTC, &TRISC, &LATC, NULL,    13, -1 },  /* USER LED D3 (active low) */

    /* D35: RD1  */ { &PORTD, &TRISD, &LATD, NULL,     1, -1 },  /* PWM4H */
    /* D36: RD8  */ { &PORTD, &TRISD, &LATD, NULL,     8, -1 },  /* SDO2 */
    /* D37: RD10 */ { &PORTD, &TRISD, &LATD, &ANSELD, 10, 18 },  /* AN18 */
    /* D38: RD13 */ { &PORTD, &TRISD, &LATD, &ANSELD, 13, -1 },  /* ANN0 only */
};

#ifdef __cplusplus
}
#endif
