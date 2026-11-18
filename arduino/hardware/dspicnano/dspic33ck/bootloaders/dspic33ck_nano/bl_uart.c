/*
 * bl_uart.c - polled UART1, no interrupts, no buffers.
 *
 * The configuration is deliberately the same sequence cores/arduino/
 * HardwareSerial.c uses, including the two ordering rules that are easy to get
 * wrong on this module and produce a UART that looks configured and never
 * works:
 *
 *   - drive TX high BEFORE making the pin an output. An asynchronous line idles
 *     high, so a brief low looks like a start bit plus a break and the first
 *     character is framed wrong.
 *   - set UARTEN BEFORE UTXEN/URXEN. The transmitter and receiver enables are
 *     gated by UARTEN, so setting them while the module is off does not stick.
 */

#include <xc.h>
#include "bl_config.h"
#include "bl_time.h"
#include "bl_uart.h"

/* baud = FCY / BRG with the fractional baud generator; rounded, not truncated,
 * because at BRG = 35 one whole count is nearly 3% of the bit period. */
#define BL_BRG  ((BL_FCY + (BL_BAUD / 2UL)) / BL_BAUD)

void bl_uart_init(void)
{
    BL_TX_LAT  = 1;
    BL_TX_TRIS = 0;
    BL_RX_TRIS = 1;

    __builtin_write_RPCON(0x0000);      /* unlock PPS */
    BL_TX_RP_REG = BL_PPS_OUT_U1TX;
    _U1RXR = BL_RX_RP_NUM;
    __builtin_write_RPCON(0x0800);      /* lock PPS */

    U1MODE  = 0x0000;
    U1MODEH = 0x0000;
    U1MODEHbits.BCLKSEL = 0b00;         /* baud clock = FOSC/2 = FCY */
    U1MODEHbits.BCLKMOD = 1;            /* fractional baud generator */
    U1MODEbits.BRGH = 0;                /* must be 0 in fractional mode */
    U1BRG  = (uint16_t)(BL_BRG & 0xFFFFUL);
    U1BRGH = (uint16_t)((BL_BRG >> 16) & 0x000FUL);
    U1MODEbits.MOD = 0b0000;            /* 8-N-1 */
    U1MODEHbits.STSEL = 0b00;

    U1MODEbits.UARTEN = 1;
    U1MODEbits.UTXEN  = 1;
    U1MODEbits.URXEN  = 1;
}

void bl_uart_deinit(void)
{
    bl_uart_flush();

    U1MODEbits.UTXEN  = 0;
    U1MODEbits.URXEN  = 0;
    U1MODEbits.UARTEN = 0;
    U1MODE  = 0x0000;
    U1MODEH = 0x0000;
    U1BRG   = 0;
    U1BRGH  = 0;
    U1STA   = 0x0000;
    U1STAH  = 0x0000;

    /* Hand the pins back as inputs with no PPS mapping. The sketch's
     * Serial.begin() reconfigures all of this, but it is entitled to find the
     * hardware as a power-on reset would leave it. */
    __builtin_write_RPCON(0x0000);
    BL_TX_RP_REG = 0;
    _U1RXR = 0x3F;                      /* 0x3F = tied low, the reset default */
    __builtin_write_RPCON(0x0800);

    BL_TX_TRIS = 1;
}

void bl_uart_putc(uint8_t c)
{
    while (U1STAHbits.UTXBF) {
    }
    U1TXREG = c;
}

void bl_uart_write(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        bl_uart_putc(buf[i]);
    }
}

void bl_uart_flush(void)
{
    while (!U1STAbits.TRMT) {
    }
}

int bl_uart_getc(uint16_t timeout, uint8_t *c)
{
    uint16_t start = bl_ticks();

    for (;;) {
        /* An overrun latches OERR and stops reception dead until it is cleared.
         * Nothing in the core clears it; here it must be cleared, because a
         * single overrun would otherwise wedge the bootloader until power-off.
         * The bytes already in the FIFO are dropped with it: the frame is lost,
         * its CRC-16 would have failed anyway, and the host retries. */
        if (U1STAbits.OERR) {
            U1STAbits.OERR = 0;
        }

        if (!U1STAHbits.URXBE) {
            *c = (uint8_t)U1RXREG;
            return 1;
        }

        if (bl_ticks_since(start) >= timeout) {
            return 0;
        }
    }
}
