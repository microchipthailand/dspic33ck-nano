/*
 * SPI.c - SPI1 implementation for dsPIC33CK Arduino core
 *
 * Uses SPI1 module with PPS auto-configured from variant pins_arduino.h:
 *   SCK  -> SPI_SCK_RP_REG  (PPS output function code 6 = SCK1OUT)
 *   MOSI -> SPI_MOSI_RP_REG (PPS output function code 5 = SDO1)
 *   MISO -> SPI_MISO_RP_NUM (PPS input to SDI1R)
 *
 * Master mode only. 8-bit default.
 */

#include "Arduino.h"
#include "SPI.h"

#ifdef __cplusplus
extern "C" {
#endif

static void _spi_begin(void)
{
    /* Pin directions */
    SPI_SCK_TRIS  = 0;  /* SCK output */
    SPI_MOSI_TRIS = 0;  /* MOSI output */
    SPI_MISO_TRIS = 1;  /* MISO input */
    SPI_SS_TRIS   = 0;  /* SS output (software controlled) */

#ifdef SPI_MISO_ANSEL
    SPI_MISO_ANSEL = 0; /* Digital mode for MISO pin */
#endif

    /* PPS mapping */
    __builtin_write_RPCON(0x0000);
    SPI_SCK_RP_REG  = PPS_OUT_SCK1OUT;  /* 6 */
    SPI_MOSI_RP_REG = PPS_OUT_SDO1;     /* 5 */
    _SDI1R = SPI_MISO_RP_NUM;
    __builtin_write_RPCON(0x0800);

    /* Disable SPI1 for configuration */
    SPI1CON1Lbits.SPIEN = 0;

    /* Master mode, 8-bit, CKE=1 (Mode 0 default), MSTEN=1 */
    SPI1CON1L = 0x0000;
    SPI1CON1Lbits.MSTEN = 1;   /* Master mode */
    SPI1CON1Lbits.CKP = 0;     /* Clock idle low (CPOL=0) */
    SPI1CON1Lbits.CKE = 1;     /* Data on leading edge (CPHA=0 → CKE=1 for dsPIC) */

    SPI1CON1H = 0x0000;

    /* Clock: default ~1 MHz (FCY / (2*(BRG+1)), BRG=1 → FCY/4) */
    SPI1BRGL = 1;

    /* Enable SPI1 */
    SPI1CON1Lbits.SPIEN = 1;
}

static void _spi_end(void)
{
    SPI1CON1Lbits.SPIEN = 0;
}

static void _spi_beginTransaction(SPISettings_t settings)
{
    SPI1CON1Lbits.SPIEN = 0;

    /* Clock divider from requested frequency */
    uint32_t brg = (FCY / (2UL * settings.clock)) - 1;
    if (brg > 8191) brg = 8191;
    SPI1BRGL = (uint16_t)brg;

    /* Data mode (CPOL/CPHA) */
    switch (settings.dataMode) {
        case SPI_MODE0:
            SPI1CON1Lbits.CKP = 0;
            SPI1CON1Lbits.CKE = 1;
            break;
        case SPI_MODE1:
            SPI1CON1Lbits.CKP = 0;
            SPI1CON1Lbits.CKE = 0;
            break;
        case SPI_MODE2:
            SPI1CON1Lbits.CKP = 1;
            SPI1CON1Lbits.CKE = 1;
            break;
        case SPI_MODE3:
            SPI1CON1Lbits.CKP = 1;
            SPI1CON1Lbits.CKE = 0;
            break;
    }

    SPI1CON1Lbits.SPIEN = 1;
}

static void _spi_endTransaction(void)
{
    /* Nothing to do in single-master, no-interrupt environment */
}

static uint8_t _spi_transfer(uint8_t data)
{
    SPI1BUFL = data;
    while (SPI1STATLbits.SPIRBE);   /* Wait until receive buffer not empty */
    return (uint8_t)SPI1BUFL;
}

static uint16_t _spi_transfer16(uint16_t data)
{
    uint16_t result;
    result = (uint16_t)_spi_transfer((uint8_t)(data >> 8)) << 8;
    result |= _spi_transfer((uint8_t)(data & 0xFF));
    return result;
}

static void _spi_setBitOrder(uint8_t bitOrder)
{
    /* dsPIC33CK SPI doesn't have a hardware bit-order bit for SPI1 enhanced mode,
       but we can handle it if needed. For now, no-op (MSB first is hardware default) */
    (void)bitOrder;
}

static void _spi_setClockDivider(uint8_t divider)
{
    SPI1CON1Lbits.SPIEN = 0;
    uint16_t brg = (divider / 2) - 1;
    SPI1BRGL = brg;
    SPI1CON1Lbits.SPIEN = 1;
}

static void _spi_setDataMode(uint8_t mode)
{
    SPI1CON1Lbits.SPIEN = 0;
    switch (mode) {
        case SPI_MODE0:
            SPI1CON1Lbits.CKP = 0;
            SPI1CON1Lbits.CKE = 1;
            break;
        case SPI_MODE1:
            SPI1CON1Lbits.CKP = 0;
            SPI1CON1Lbits.CKE = 0;
            break;
        case SPI_MODE2:
            SPI1CON1Lbits.CKP = 1;
            SPI1CON1Lbits.CKE = 1;
            break;
        case SPI_MODE3:
            SPI1CON1Lbits.CKP = 1;
            SPI1CON1Lbits.CKE = 0;
            break;
    }
    SPI1CON1Lbits.SPIEN = 1;
}

/*
 * Global SPI object.
 *
 * Positional, not designated: platform.txt sets compiler.c.cmd to the g++ driver,
 * and g++ compiles a .c file as C++, where designated initializers are a GNU
 * extension that -Wpedantic reports. The order below must stay in step with
 * SPIClass_t in SPI.h -- hence the trailing names.
 */
SPIClass_t SPI = {
    _spi_begin,             /* begin            */
    _spi_end,               /* end              */
    _spi_beginTransaction,  /* beginTransaction */
    _spi_endTransaction,    /* endTransaction   */
    _spi_transfer,          /* transfer         */
    _spi_transfer16,        /* transfer16       */
    _spi_setBitOrder,       /* setBitOrder      */
    _spi_setClockDivider,   /* setClockDivider  */
    _spi_setDataMode        /* setDataMode      */
};

#ifdef __cplusplus
}
#endif
