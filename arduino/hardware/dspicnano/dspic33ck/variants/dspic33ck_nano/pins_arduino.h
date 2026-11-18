/*
 * pins_arduino.h - Pin mapping for dsPIC33CK256MC005
 *
 * dsPIC33CK256MC005 is a 48-pin TQFP Value Line Motor Control device with:
 *   - PORTA: RA0-RA4        (5 pins, all analog-capable, NOT remappable)
 *   - PORTB: RB0-RB15       (16 pins, RP32-RP47)
 *   - PORTC: RC0-RC13       (14 pins, RP48-RP61)
 *   - PORTD: RD1, RD8, RD10, RD13 only (RP65, RP72, RP74, RP77)
 *   - 256KB Flash (ECC), 16KB RAM
 *   - SCCP1-4, PWM generators PG1-PG4, U1-U3, SPI1-2, I2C1, DAC1
 *   - DFP: dsPIC33CK-MC_DFP >= 1.10.386 (earlier packs have no MC005)
 *   - Board: dsPIC33CK Nano (custom breadboard board)
 *
 * Board: dsPIC33CK Nano (custom, 31.4 x 74.5 mm) - NOT the EV08P02A.
 * Wiring taken from the board's KiCad netlist (pcb_scripts/design.py):
 *   USER LED  = RC13 (D34), ACTIVE LOW: 3V3 -> R5 1k -> LED -> RC13
 *   USER SW2  = RC12 (D33), EXTERNAL 10k pull-up (R6), reads LOW when pressed
 *   RESET SW1 = MCLR (R3 10k pull-up, R7 470R + C9 100nF)
 *   UART J5   = 1:3V3  2:TX = U1TX RC10 (RP58)  3:RX = U1RX RC11 (RP59)  4:GND
 *               -> connect a 3.3 V USB-serial adapter (adapter RX to J5-2)
 *   ICSP J4   = 1:MCLR 2:3V3 3:GND 4:PGD3=RB5 5:PGC3=RB6 6:nc
 *               RB5/RB6 are free I/O when no debugger is attached, but the
 *               debugger drives them while programming.
 *   PWR LED   = hard-wired to 3V3 (not controllable)
 *   No crystal: RB0/RB1 (OSCI/OSCO) are ordinary I/O.
 *   ALTI2C1   = OFF, so I2C1 uses SDA1 = RB9, SCL1 = RB8
 *
 * Arduino Pin Mapping (contiguous over bonded pins only):
 *   D0-D4   = RA0-RA4
 *   D5-D20  = RB0-RB15
 *   D21-D34 = RC0-RC13
 *   D35     = RD1
 *   D36     = RD8
 *   D37     = RD10  (LED0)
 *   D38     = RD13  (SW0)
 *
 * Analog Inputs (20 channels, mapping taken from the device's edc pin database
 * DSPIC33CK256MC005.PIC — note it differs from MP508 and MC002):
 *   A0 =RA0(AN0)   A1 =RA1(AN16)  A2 =RA2(AN9)   A3 =RA3(AN3)   A4 =RA4(AN4)
 *   A5 =RB0(AN5)   A6 =RB1(AN6)   A7 =RB2(AN1)   A8 =RB3(AN8)   A9 =RB4(AN17)
 *   A10=RB7(AN2)   A11=RB8(AN10)  A12=RB9(AN11)
 *   A13=RC0(AN12)  A14=RC1(AN13)  A15=RC2(AN14)  A16=RC3(AN15)
 *   A17=RC6(AN19)  A18=RC7(AN7)   A19=RD10(AN18)
 *   RD13 is ANN0 (ADC negative input only) and is NOT an analog input here.
 *
 * PWM Pins: D5(RB0), D6(RB1), D7(RB2), D8(RB3) via SCCP1-4.
 *           This part has only CCP1-4 — there is no PWM5 (unlike MP508).
 * UART TX:  D31 (RC10/RP58 via PPS - header J5 pin 2)
 * UART RX:  D32 (RC11/RP59 via PPS - header J5 pin 3)
 * SPI:      SCK=D25(RC4) MOSI=D26(RC5) MISO=D29(RC8) SS=D30(RC9)
 *           (EV08P02A used RC12/RC13 for MISO/SS - on this board those are
 *           the USER button and LED, so MISO/SS moved to RC8/RC9.)
 * I2C:      SDA=D14(RB9), SCL=D13(RB8) — dedicated I2C1 pins, no PPS
 */

#ifndef PINS_ARDUINO_H
#define PINS_ARDUINO_H

#include <stdint.h>

#define NUM_DIGITAL_PINS    39
#define NUM_ANALOG_INPUTS   20
#define NUM_PWM_PINS        4

/* Board identification */
#define ARDUINO_DSPIC33CK_NANO_BOARD 1

/* LED_BUILTIN - RC13 (USER LED D3). ACTIVE LOW: the LED's cathode is on the
 * pin, so digitalWrite(LED_BUILTIN, LOW) turns it ON. Stock Blink still blinks. */
#define LED_BUILTIN             34
#define LED_USER                34
#define LED0                    34
#define LED_BUILTIN_ACTIVE_LOW  1
#define LED_ON                  LOW
#define LED_OFF                 HIGH

/* USER button SW2 - RC12. External 10k pull-up (R6) to 3V3, reads LOW when
 * pressed. INPUT_PULLUP is harmless but not required. */
#define BUTTON_BUILTIN          33
#define BUTTON_USER             33
#define SW0                     33
#define BUTTON_BUILTIN_ACTIVE_LOW 1

/* Header J5 (UART) and J4 (ICSP) pins, by Arduino number */
#define PIN_J5_TX               31  /* RC10 */
#define PIN_J5_RX               32  /* RC11 */
#define PIN_PGD3                10  /* RB5 - J4 pin 4 */
#define PIN_PGC3                11  /* RB6 - J4 pin 5 */

/* Analog pin aliases (PORTA) */
#define A0  0   /* RA0 = AN0  */
#define A1  1   /* RA1 = AN16 */
#define A2  2   /* RA2 = AN9  */
#define A3  3   /* RA3 = AN3  */
#define A4  4   /* RA4 = AN4  */

/* Analog pin aliases (PORTB) */
#define A5  5   /* RB0 = AN5  */
#define A6  6   /* RB1 = AN6  */
#define A7  7   /* RB2 = AN1  */
#define A8  8   /* RB3 = AN8  */
#define A9  9   /* RB4 = AN17 */
#define A10 12  /* RB7 = AN2  */
#define A11 13  /* RB8 = AN10 — also I2C1 SCL1 */
#define A12 14  /* RB9 = AN11 — also I2C1 SDA1 */

/* Analog pin aliases (PORTC) */
#define A13 21  /* RC0 = AN12 */
#define A14 22  /* RC1 = AN13 */
#define A15 23  /* RC2 = AN14 */
#define A16 24  /* RC3 = AN15 */
#define A17 27  /* RC6 = AN19 */
#define A18 28  /* RC7 = AN7  */

/* Analog pin alias (PORTD) */
#define A19 37  /* RD10 = AN18 */

/* SPI pin definitions (SPI1 via PPS). RC4/RC5/RC8/RC9 = J3 pins 6/5/14/13. */
#define PIN_SPI_SS          30  /* RC9  */
#define PIN_SPI_MOSI        26  /* RC5  */
#define PIN_SPI_MISO        29  /* RC8  */
#define PIN_SPI_SCK         25  /* RC4  */

static const uint8_t SS   = PIN_SPI_SS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

/* SPI PPS mapping macros */
#define SPI_SCK_RP_REG      _RP52R      /* PPS output register for RC4  (SCK1) */
#define SPI_MOSI_RP_REG     _RP53R      /* PPS output register for RC5  (SDO1) */
#define SPI_MISO_RP_NUM     56          /* RP number for RC8 (SDI1 input)  */
#define SPI_SS_RP_REG       _RP57R      /* PPS output register for RC9  (SS1) */
#define SPI_SCK_TRIS        TRISCbits.TRISC4
#define SPI_MOSI_TRIS       TRISCbits.TRISC5
#define SPI_MISO_TRIS       TRISCbits.TRISC8
#define SPI_SS_TRIS         TRISCbits.TRISC9
/* RC8 has no analog function, so no SPI_MISO_ANSEL */

/* I2C pin definitions (dedicated I2C1 pins, no PPS needed).
 * ALTI2C1 must stay OFF (the default in cores/arduino/system_config.c). */
#define PIN_WIRE_SDA        14  /* RB9 = SDA1 */
#define PIN_WIRE_SCL        13  /* RB8 = SCL1 */
#define WIRE_SDA_TRIS       TRISBbits.TRISB9
#define WIRE_SCL_TRIS       TRISBbits.TRISB8

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;

/* Serial pin definitions - UART header J5 (use a 3.3 V USB-serial adapter) */
#define PIN_SERIAL_TX       31  /* RC10 */
#define PIN_SERIAL_RX       32  /* RC11 */
#define SERIAL_TX_RP_REG    _RP58R  /* PPS output register for RC10 */
#define SERIAL_RX_RP_NUM    59      /* RP number for RC11 input */
#define SERIAL_TX_TRIS      TRISCbits.TRISC10
#define SERIAL_RX_TRIS      TRISCbits.TRISC11
/* #define SERIAL_RX_ANSEL  — RC11 has no analog function, always digital */

/* ============================================================
 * Internal pin table structure
 * Each Arduino pin maps to a PORT register and bit position
 * ============================================================ */

typedef struct {
    volatile uint16_t *port_reg;   /* PORTx register */
    volatile uint16_t *tris_reg;   /* TRISx register */
    volatile uint16_t *lat_reg;    /* LATx register */
    volatile uint16_t *ansel_reg;  /* ANSELx register (NULL if no analog) */
    uint8_t bit;                   /* bit position 0-15 */
    int8_t  adc_channel;           /* ADC channel or -1 if none */
} pin_map_t;

/* Port register addresses (defined in variant.c) */
#ifdef __cplusplus
extern "C" {
#endif
extern const pin_map_t g_pin_map[];
#ifdef __cplusplus
}
#endif

/* PWM capable pins */
#define digitalPinHasPWM(p) ((p) >= 5 && (p) <= 8)

/* Every pin can carry an interrupt (Change Notification covers all ports), so
 * this is the identity -- it exists only so sketches written for AVR, where the
 * mapping is real, compile unchanged. attachInterrupt() takes the pin number. */
#define digitalPinToInterrupt(p) (((p) < NUM_DIGITAL_PINS) ? (int)(p) : -1)

/* Analog channel for a pin (-1 if not analog) */
#define analogPinToChannel(p) (g_pin_map[p].adc_channel)

/* PPS output function codes (from device header _RPOUT_ defines) */
#define PPS_OUT_U1TX    1
#define PPS_OUT_U1RTS   2
#define PPS_OUT_U2TX    3
#define PPS_OUT_SDO1    5
#define PPS_OUT_SCK1OUT 6
#define PPS_OUT_SS1OUT  7
#define PPS_OUT_OCM1    15  /* SCCP1 Output Compare */
#define PPS_OUT_OCM2    16  /* SCCP2 Output Compare */
#define PPS_OUT_OCM3    17  /* SCCP3 Output Compare */
#define PPS_OUT_OCM4    18  /* SCCP4 Output Compare */
/* No OCM5/OCM6 — dsPIC33CK256MC005 has only SCCP1-4 */

/* PWM pin definitions: Arduino pin, RP number, SCCP module */
#define PWM1_PIN        5   /* D5 = RB0 */
#define PWM1_RP         32  /* RP32 */
#define PWM2_PIN        6   /* D6 = RB1 */
#define PWM2_RP         33  /* RP33 */
#define PWM3_PIN        7   /* D7 = RB2 */
#define PWM3_RP         34  /* RP34 */
#define PWM4_PIN        8   /* D8 = RB3 */
#define PWM4_RP         35  /* RP35 */

#endif /* PINS_ARDUINO_H */
